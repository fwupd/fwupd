/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif

#include "fu-dfu-device.h"
#include "fu-dfu-sector.h"

#include "fu-context-private.h"

typedef struct {
	GCancellable		*cancellable;
	GPtrArray		*cmd_array;
	gboolean		 force;
	gchar			*device_vid_pid;
	guint16			 transfer_size;
	FuContext		*ctx;
} FuDfuTool;

static void
fu_dfu_tool_free (FuDfuTool *self)
{
	if (self == NULL)
		return;
	g_free (self->device_vid_pid);
	if (self->cancellable != NULL)
		g_object_unref (self->cancellable);
	g_object_unref (self->ctx);
	if (self->cmd_array != NULL)
		g_ptr_array_unref (self->cmd_array);
	g_free (self);
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuDfuTool, fu_dfu_tool_free)
#pragma clang diagnostic pop

typedef gboolean (*FuUtilPrivateCb)	(FuDfuTool	*util,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar		*name;
	gchar		*arguments;
	gchar		*description;
	FuUtilPrivateCb	 callback;
} FuUtilItem;

static void
fu_dfu_tool_item_free (FuUtilItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

static gint
fu_dfu_tool_sort_command_name_cb (FuUtilItem **item1, FuUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

static void
fu_dfu_tool_add (GPtrArray *array,
	      const gchar *name,
	      const gchar *arguments,
	      const gchar *description,
	      FuUtilPrivateCb callback)
{
	g_auto(GStrv) names = NULL;

	g_return_if_fail (name != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (callback != NULL);

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuUtilItem *item = g_new0 (FuUtilItem, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf (_("Alias to %s"),
							     names[0]);
		}
		item->arguments = g_strdup (arguments);
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
}

static gchar *
fu_dfu_tool_get_descriptions (GPtrArray *array)
{
	gsize len;
	const gsize max_len = 31;
	FuUtilItem *item;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (guint i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append (string, " ");
			g_string_append (string, item->arguments);
			len += strlen (item->arguments) + 1;
		}
		if (len < max_len) {
			for (guint j = len; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		} else {
			g_string_append_c (string, '\n');
			for (guint j = 0; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

static gboolean
fu_dfu_tool_run (FuDfuTool *self,
	      const gchar *command,
	      gchar **values,
	      GError **error)
{
	/* find command */
	for (guint i = 0; i < self->cmd_array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (self->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (self, values, error);
	}

	/* not found */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

static FuDfuDevice *
fu_dfu_tool_get_default_device (FuDfuTool *self, GError **error)
{
	g_autoptr(GUsbContext) usb_context = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get all the DFU devices */
	usb_context = g_usb_context_new (error);
	if (usb_context == NULL)
		return NULL;
	g_usb_context_enumerate (usb_context);

	/* we specified it manually */
	if (self->device_vid_pid != NULL) {
		gchar *tmp;
		guint64 pid;
		guint64 vid;
		g_autoptr(FuDfuDevice) device = NULL;
		g_autoptr(GUsbDevice) usb_device = NULL;

		/* parse */
		vid = g_ascii_strtoull (self->device_vid_pid, &tmp, 16);
		if (vid == 0 || vid > G_MAXUINT16) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}
		if (tmp[0] != ':') {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}
		pid = g_ascii_strtoull (tmp + 1, NULL, 16);
		if (pid == 0 || pid > G_MAXUINT16) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Invalid format of VID:PID");
			return NULL;
		}

		/* find device */
		usb_device = g_usb_context_find_by_vid_pid (usb_context,
							    (guint16) vid,
							    (guint16) pid,
							    error);
		if (usb_device == NULL) {
			g_prefix_error (error,
					"no device matches for %04x:%04x: ",
					(guint) vid, (guint) pid);
			return NULL;
		}
		device = fu_dfu_device_new (usb_device);
		fu_device_set_context (FU_DEVICE (device), self->ctx);
		return g_steal_pointer (&device);
	}

	/* auto-detect first device */
	devices = g_usb_context_get_devices (usb_context);
	for (guint i = 0; i < devices->len; i++) {
		GUsbDevice *usb_device = g_ptr_array_index (devices, i);
		g_autoptr(FuDfuDevice) device = fu_dfu_device_new (usb_device);
		fu_device_set_context (FU_DEVICE (device), self->ctx);
		if (fu_device_probe (FU_DEVICE (device), NULL))
			return g_steal_pointer (&device);
	}

	/* failed */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "no DFU devices found");
	return NULL;
}

static gboolean
fu_dfu_device_wait_for_replug (FuDfuTool *self, FuDfuDevice *device, guint timeout, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GUsbDevice) usb_device2  = NULL;
	g_autoptr(GUsbContext) usb_context = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get all the DFU devices */
	usb_context = g_usb_context_new (error);
	if (usb_context == NULL)
		return FALSE;

	/* close */
	if (!fu_device_close (FU_DEVICE (device), &error_local))
		g_debug ("failed to close: %s", error_local->message);

	/* watch the device disappear and re-appear */
	usb_device2 = g_usb_context_wait_for_replug (usb_context,
						     usb_device,
						     timeout,
						     error);
	if (usb_device2 == NULL)
		return FALSE;

	/* re-open with new device set */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_IDLE);
	fu_usb_device_set_dev (FU_USB_DEVICE (device), usb_device2);
	if (!fu_device_open (FU_DEVICE (device), error))
		return FALSE;
	if (!fu_dfu_device_refresh_and_clear (device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GBytes *
fu_dfu_tool_parse_hex_string (const gchar *val, GError **error)
{
	gsize result_size;
	g_autofree guint8 *result = NULL;

	/* sanity check */
	if (val == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "nothing to parse");
		return NULL;
	}

	/* parse each hex byte */
	result_size = strlen (val) / 2;
	result = g_malloc (result_size);
	for (guint i = 0; i < result_size; i++) {
		gchar buf[3] = { "xx" };
		gchar *endptr = NULL;
		guint64 tmp;

		/* copy two bytes and parse as hex */
		memcpy (buf, val + (i * 2), 2);
		tmp = g_ascii_strtoull (buf, &endptr, 16);
		if (tmp > 0xff || endptr[0] != '\0') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to parse '%s'", val);
			return NULL;
		}
		result[i] = tmp;
	}
	return g_bytes_new (result, result_size);
}

static guint
fu_dfu_tool_bytes_replace (GBytes *data, GBytes *search, GBytes *replace)
{
	gsize data_sz;
	gsize replace_sz;
	gsize search_sz;
	guint8 *data_buf;
	guint8 *replace_buf;
	guint8 *search_buf;
	guint cnt = 0;

	data_buf = (gpointer) g_bytes_get_data (data, &data_sz);
	search_buf = (gpointer) g_bytes_get_data (search, &search_sz);
	replace_buf = (gpointer) g_bytes_get_data (replace, &replace_sz);

	g_return_val_if_fail (search_sz == replace_sz, 0);

	/* find and replace each one */
	for (gsize i = 0; i < data_sz - search_sz + 1; i++) {
		if (memcmp (data_buf + i, search_buf, search_sz) == 0) {
			g_print ("Replacing %" G_GSIZE_FORMAT " bytes @0x%04x\n",
				 replace_sz, (guint) i);
			memcpy (data_buf + i, replace_buf, replace_sz);
			i += replace_sz;
			cnt++;
		}
	}
	return cnt;
}

static gboolean
fu_dfu_tool_parse_firmware_from_file (FuFirmware *firmware, GFile *file,
				   FwupdInstallFlags flags,
				   GError **error)
{
	gchar *contents = NULL;
	gsize length = 0;
	g_autoptr(GBytes) bytes = NULL;
	if (!g_file_load_contents (file, NULL, &contents, &length, NULL, error))
		return FALSE;
	bytes = g_bytes_new_take (contents, length);
	return fu_firmware_parse (firmware, bytes, flags, error);
}

static gboolean
fu_dfu_tool_write_firmware_to_file (FuFirmware *firmware, GFile *file, GError **error)
{
	const guint8 *data;
	gsize length = 0;
	g_autoptr(GBytes) bytes = fu_firmware_write (firmware, error);
	if (bytes == NULL)
		return FALSE;
	data = g_bytes_get_data (bytes, &length);
	return g_file_replace_contents (file,
					(const gchar *) data,
					length,
					NULL,
					FALSE,
					G_FILE_CREATE_NONE,
					NULL,
					NULL, /* cancellable */
					error);
}

static gboolean
fu_dfu_tool_replace_data (FuDfuTool *self, gchar **values, GError **error)
{
	guint cnt = 0;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GBytes) data_search = NULL;
	g_autoptr(GBytes) data_replace = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* check args */
	if (g_strv_length (values) < 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE SEARCH REPLACE"
				     " -- e.g. `firmware.dfu deadbeef beefdead");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = fu_dfu_firmware_new ();
	if (!fu_dfu_tool_parse_firmware_from_file (firmware, file,
				      FWUPD_INSTALL_FLAG_NONE,
				      error)) {
		return FALSE;
	}

	/* parse hex values */
	data_search = fu_dfu_tool_parse_hex_string (values[1], error);
	if (data_search == NULL)
		return FALSE;
	data_replace = fu_dfu_tool_parse_hex_string (values[2], error);
	if (data_replace == NULL)
		return FALSE;
	if (g_bytes_get_size (data_search) != g_bytes_get_size (data_replace)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "search and replace were different sizes");
		return FALSE;
	}

	/* get each data segment */
	images = fu_firmware_get_images (firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *image = g_ptr_array_index (images, i);
		g_autoptr(GPtrArray) chunks = fu_firmware_get_chunks (image, error);
		if (chunks == NULL)
			return FALSE;
		for (guint j = 0; j < chunks->len; j++) {
			FuChunk *chk = g_ptr_array_index (chunks, j);
			g_autoptr(GBytes) contents = fu_chunk_get_bytes (chk);
			if (contents == NULL)
				continue;
			cnt += fu_dfu_tool_bytes_replace (contents, data_search, data_replace);
		}
	}

	/* nothing done */
	if (cnt == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "search string was not found");
		return FALSE;
	}

	/* write out new file */
	return fu_dfu_tool_write_firmware_to_file (firmware, file, error);
}

static void
fu_tool_action_changed_cb (FuDevice *device, GParamSpec *pspec, FuDfuTool *self)
{
	g_print("%s:\n", fwupd_status_to_string(fu_device_get_status(device)));
}

static void
fu_tool_percentage_changed_cb(FuProgress *progress, guint percentage, gpointer data)
{
	g_print("%u%%\n", percentage);
}

static gboolean
fu_dfu_tool_read_alt (FuDfuTool *self, gchar **values, GError **error)
{
	FuDfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(FuDfuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuDfuTarget) target = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new();
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID");
		return FALSE;
	}

	/* open correct device */
	device = fu_dfu_tool_get_default_device (self, error);
	if (device == NULL)
		return FALSE;
	if (self->transfer_size > 0)
		fu_dfu_device_set_transfer_size (device, self->transfer_size);
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_dfu_device_refresh (device, error))
		return FALSE;

	/* set up progress */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), self);
	g_signal_connect(progress,
			 "percentage-changed",
			 G_CALLBACK(fu_tool_percentage_changed_cb),
			 self);

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("detaching");
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!fu_dfu_device_wait_for_replug (self, device,
						    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						    error))
			return FALSE;
	}

	/* transfer */
	target = fu_dfu_device_get_target_by_alt_name (device,
						       values[1],
						       NULL);
	if (target == NULL) {
		gchar *endptr;
		guint64 tmp = g_ascii_strtoull (values[1], &endptr, 10);
		if (tmp > 0xff || endptr[0] != '\0') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse alt-setting '%s'",
				     values[1]);
			return FALSE;
		}
		target = fu_dfu_device_get_target_by_alt_setting (device,
							       (guint8) tmp,
							       error);
		if (target == NULL)
			return FALSE;
	}

	/* do transfer */
	firmware = fu_dfuse_firmware_new ();
	fu_dfu_firmware_set_vid (FU_DFU_FIRMWARE (firmware), fu_dfu_device_get_runtime_vid (device));
	fu_dfu_firmware_set_pid (FU_DFU_FIRMWARE (firmware), fu_dfu_device_get_runtime_pid (device));
	if (!fu_dfu_target_upload(target, firmware, progress, flags, error))
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;
	if (!fu_dfu_device_wait_for_replug (self, device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* save file */
	file = g_file_new_for_path (values[0]);
	if (!fu_dfu_tool_write_firmware_to_file (firmware, file, error))
		return FALSE;

	/* print the new object */
	str_debug = fu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* success */
	g_print ("Successfully uploaded from device\n");
	return TRUE;
}

static gboolean
fu_dfu_tool_read (FuDfuTool *self, gchar **values, GError **error)
{
	FuDfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(FuDfuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new();
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) != 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* open correct device */
	device = fu_dfu_tool_get_default_device (self, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_dfu_device_refresh (device, error))
		return FALSE;

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!fu_dfu_device_wait_for_replug (self, device,
						 FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						 error)) {
			return FALSE;
		}
	}

	/* transfer */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), self);
	g_signal_connect(progress,
			 "percentage-changed",
			 G_CALLBACK(fu_tool_percentage_changed_cb),
			 self);
	firmware = fu_dfu_device_upload(device, progress, flags, error);
	if (firmware == NULL)
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;
	if (!fu_dfu_device_wait_for_replug (self, device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* save file */
	file = g_file_new_for_path (values[0]);
	if (!fu_dfu_tool_write_firmware_to_file (firmware, file, error))
		return FALSE;

	/* print the new object */
	str_debug = fu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* success */
	g_print ("successfully uploaded from device\n");
	return TRUE;
}

static gboolean
fu_dfu_tool_write_alt (FuDfuTool *self, gchar **values, GError **error)
{
	FuDfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_VERIFY;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(FuDfuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) image = NULL;
	g_autoptr(FuDfuTarget) target = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new();
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID "
				     "[IMAGE-ALT-NAME|IMAGE-ALT-ID]");
		return FALSE;
	}

	/* open file */
	firmware = fu_dfuse_firmware_new ();
	file = g_file_new_for_path (values[0]);
	if (!fu_dfu_tool_parse_firmware_from_file (firmware, file,
				      FWUPD_INSTALL_FLAG_NONE,
				      error))
		return FALSE;

	/* open correct device */
	device = fu_dfu_tool_get_default_device (self, error);
	if (device == NULL)
		return FALSE;
	if (self->transfer_size > 0)
		fu_dfu_device_set_transfer_size (device, self->transfer_size);
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_dfu_device_refresh (device, error))
		return FALSE;

	/* set up progress */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), self);
	g_signal_connect(progress,
			 "percentage-changed",
			 G_CALLBACK(fu_tool_percentage_changed_cb),
			 self);

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("detaching");
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!fu_dfu_device_wait_for_replug (self, device, 5000, error))
			return FALSE;
	}

	/* print the new object */
	str_debug = fu_firmware_to_string (firmware);
	g_debug ("DFU: %s", str_debug);

	/* get correct target on device */
	target = fu_dfu_device_get_target_by_alt_name (device, values[1], NULL);
	if (target == NULL) {
		gchar *endptr;
		guint64 tmp = g_ascii_strtoull (values[1], &endptr, 10);
		if (tmp > 0xff || endptr[0] != '\0') {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to parse alt-setting '%s'",
				     values[1]);
			return FALSE;
		}
		target = fu_dfu_device_get_target_by_alt_setting (device,
								  (guint8) tmp,
								  error);
		if (target == NULL)
			return FALSE;
	}

	/* allow overriding the firmware alt-setting */
	if (g_strv_length (values) > 2) {
		image = fu_firmware_get_image_by_id (firmware, values[2], NULL);
		if (image == NULL) {
			gchar *endptr;
			guint64 tmp = g_ascii_strtoull (values[2], &endptr, 10);
			if (tmp > 0xff || endptr[0] != '\0') {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Failed to parse image alt-setting '%s'",
					     values[2]);
				return FALSE;
			}
			image = fu_firmware_get_image_by_idx (firmware, tmp, error);
			if (image == NULL)
				return FALSE;
		}
	} else {
		g_print ("WARNING: Using default firmware image\n");
		image = fu_firmware_get_image_by_id (firmware, NULL, error);
		if (image == NULL)
			return FALSE;
	}

	/* transfer */
	if (!fu_dfu_target_download(target, image, progress, flags, error))
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;
	if (!fu_dfu_device_wait_for_replug (self, device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* success */
	g_print ("Successfully downloaded to device\n");
	return TRUE;
}

static gboolean
fu_dfu_tool_write (FuDfuTool *self, gchar **values, GError **error)
{
	FwupdInstallFlags flags = FWUPD_INSTALL_FLAG_NONE;
	g_autoptr(FuDfuDevice) device = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* check args */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* open file */
	fw = fu_common_get_contents_bytes (values[0], error);
	if (fw == NULL)
		return FALSE;

	/* open correct device */
	device = fu_dfu_tool_get_default_device (self, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_dfu_device_refresh (device, error))
		return FALSE;

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!fu_dfu_device_wait_for_replug (self, device,
						    fu_device_get_remove_delay (FU_DEVICE (device)),
						    error))
			return FALSE;
	}

	/* allow wildcards */
	if (self->force) {
		flags |= FWUPD_INSTALL_FLAG_IGNORE_VID_PID;
		flags |= FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM;
	}

	/* transfer */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), self);
	g_signal_connect(progress,
			 "percentage-changed",
			 G_CALLBACK(fu_tool_percentage_changed_cb),
			 self);
	if (!fu_device_write_firmware(FU_DEVICE(device), fw, progress, flags, error))
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;

	if (fu_dfu_device_has_attribute (device, FU_DFU_DEVICE_ATTR_MANIFEST_TOL)) {
		if (!fu_dfu_device_wait_for_replug (self, device, fu_device_get_remove_delay (FU_DEVICE (device)), error))
			return FALSE;
	}

	/* success */
	g_print ("%u bytes successfully downloaded to device\n",
		 (guint) g_bytes_get_size (fw));
	return TRUE;
}

#ifdef HAVE_GIO_UNIX
static gboolean
fu_dfu_tool_sigint_cb (gpointer user_data)
{
	FuDfuTool *self = (FuDfuTool *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (self->cancellable);
	return FALSE;
}
#endif

int
main (int argc, char *argv[])
{
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean version = FALSE;
	g_autofree gchar *cmd_descriptions = NULL;
	g_autoptr(FuDfuTool) self = g_new0 (FuDfuTool, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
			_("Print the version number"), NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Print verbose debug statements"), NULL },
		{ "device", 'd', 0, G_OPTION_ARG_STRING, &self->device_vid_pid,
			_("Specify Vendor/Product ID(s) of DFU device"), _("VID:PID") },
		{ "transfer-size", 't', 0, G_OPTION_ARG_STRING, &self->transfer_size,
			_("Specify the number of bytes per USB transfer"), _("BYTES") },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &self->force,
			_("Force the action ignoring all warnings"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* add commands */
	self->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_dfu_tool_item_free);
	fu_dfu_tool_add (self->cmd_array,
		     "read",
		     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
		     _("FILENAME"),
		     /* TRANSLATORS: command description */
		     _("Read firmware from device into a file"),
		     fu_dfu_tool_read);
	fu_dfu_tool_add (self->cmd_array,
		     "read-alt",
		     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
		     _("FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID"),
		     /* TRANSLATORS: command description */
		     _("Read firmware from one partition into a file"),
		     fu_dfu_tool_read_alt);
	fu_dfu_tool_add (self->cmd_array,
		     "write",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into device"),
		     fu_dfu_tool_write);
	fu_dfu_tool_add (self->cmd_array,
		     "write-alt",
		     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
		     _("FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID [IMAGE-ALT-NAME|IMAGE-ALT-ID]"),
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into one partition"),
		     fu_dfu_tool_write_alt);
	fu_dfu_tool_add (self->cmd_array,
		     "replace-data",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Replace data in an existing firmware file"),
		     fu_dfu_tool_replace_data);

	/* use quirks */
	self->ctx = fu_context_new ();
	if (!fu_context_load_quirks (self->ctx, FU_QUIRKS_LOAD_FLAG_NONE, &error)) {
		/* TRANSLATORS: quirks are device-specific workarounds */
		g_print ("%s: %s\n", _("Failed to load quirks"), error->message);
		return EXIT_FAILURE;
	}

	/* do stuff on ctrl+c */
	self->cancellable = g_cancellable_new ();
#ifdef HAVE_GIO_UNIX
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				fu_dfu_tool_sigint_cb,
				self,
				NULL);
#endif

	/* sort by command name */
	g_ptr_array_sort (self->cmd_array,
			  (GCompareFunc) fu_dfu_tool_sort_command_name_cb);

	/* get a list of the commands */
	context = g_option_context_new (NULL);
	cmd_descriptions = fu_dfu_tool_get_descriptions (self->cmd_array);
	g_option_context_set_summary (context, cmd_descriptions);

	/* TRANSLATORS: DFU stands for device firmware update */
	g_set_application_name (_("DFU Utility"));
	g_option_context_add_main_entries (context, options, NULL);
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print ("%s: %s\n", _("Failed to parse arguments"), error->message);
		return EXIT_FAILURE;
	}

	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);

	/* version */
	if (version) {
		g_print ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
		return EXIT_SUCCESS;
	}

	/* run the specified command */
	ret = fu_dfu_tool_run (self, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
			g_autofree gchar *tmp = NULL;
			tmp = g_option_context_get_help (context, TRUE, NULL);
			g_print ("%s\n\n%s", error->message, tmp);
		} else {
			g_print ("%s\n", error->message);
		}
		return EXIT_FAILURE;
	}

	/* success/ */
	return EXIT_SUCCESS;
}

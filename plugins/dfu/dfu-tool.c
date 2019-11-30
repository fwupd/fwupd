/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif

#include "dfu-device.h"
#include "dfu-sector.h"

#include "fu-device-locker.h"

#include "fwupd-error.h"

typedef struct {
	GCancellable		*cancellable;
	GPtrArray		*cmd_array;
	gboolean		 force;
	gchar			*device_vid_pid;
	guint16			 transfer_size;
	FuQuirks		*quirks;
} DfuToolPrivate;

static void
dfu_tool_print_indent (const gchar *title, const gchar *message, guint indent)
{
	for (gsize i = 0; i < indent; i++)
		g_print (" ");
	g_print ("%s:", title);
	for (gsize i = strlen (title) + indent; i < 15; i++)
		g_print (" ");
	g_print ("%s\n", message);
}

static void
dfu_tool_private_free (DfuToolPrivate *priv)
{
	if (priv == NULL)
		return;
	g_free (priv->device_vid_pid);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->quirks);
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
	g_free (priv);
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(DfuToolPrivate, dfu_tool_private_free)
#pragma clang diagnostic pop

typedef gboolean (*FuUtilPrivateCb)	(DfuToolPrivate	*util,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar		*name;
	gchar		*arguments;
	gchar		*description;
	FuUtilPrivateCb	 callback;
} FuUtilItem;

static void
dfu_tool_item_free (FuUtilItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

static gint
dfu_tool_sort_command_name_cb (FuUtilItem **item1, FuUtilItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

static void
dfu_tool_add (GPtrArray *array,
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
dfu_tool_get_descriptions (GPtrArray *array)
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
dfu_tool_run (DfuToolPrivate *priv,
	      const gchar *command,
	      gchar **values,
	      GError **error)
{
	/* find command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuUtilItem *item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, error);
	}

	/* not found */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     /* TRANSLATORS: error message */
			     _("Command not found"));
	return FALSE;
}

static DfuDevice *
dfu_tool_get_default_device (DfuToolPrivate *priv, GError **error)
{
	g_autoptr(GUsbContext) usb_context = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get all the DFU devices */
	usb_context = g_usb_context_new (error);
	if (usb_context == NULL)
		return NULL;
	g_usb_context_enumerate (usb_context);

	/* we specified it manually */
	if (priv->device_vid_pid != NULL) {
		gchar *tmp;
		guint64 pid;
		guint64 vid;
		g_autoptr(DfuDevice) device = NULL;
		g_autoptr(GUsbDevice) usb_device = NULL;

		/* parse */
		vid = g_ascii_strtoull (priv->device_vid_pid, &tmp, 16);
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
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no device matches for %04x:%04x",
				     (guint) vid, (guint) pid);
			return NULL;
		}
		device = dfu_device_new (usb_device);
		fu_device_set_quirks (FU_DEVICE (device), priv->quirks);
		return device;
	}

	/* auto-detect first device */
	devices = g_usb_context_get_devices (usb_context);
	for (guint i = 0; i < devices->len; i++) {
		GUsbDevice *usb_device = g_ptr_array_index (devices, i);
		g_autoptr(DfuDevice) device = dfu_device_new (usb_device);
		fu_device_set_quirks (FU_DEVICE (device), priv->quirks);
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
dfu_device_wait_for_replug (DfuToolPrivate *priv, DfuDevice *device, guint timeout, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GUsbDevice) usb_device2  = NULL;
	g_autoptr(GUsbContext) usb_context = NULL;

	/* get all the DFU devices */
	usb_context = g_usb_context_new (error);
	if (usb_context == NULL)
		return FALSE;

	/* close */
	fu_device_close (FU_DEVICE (device), NULL);

	/* watch the device disappear and re-appear */
	usb_device2 = g_usb_context_wait_for_replug (usb_context,
						     usb_device,
						     FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						     error);
	if (usb_device2 == NULL)
		return FALSE;

	/* re-open with new device set */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_IDLE);
	fu_usb_device_set_dev (FU_USB_DEVICE (device), usb_device2);
	if (!fu_device_open (FU_DEVICE (device), error))
		return FALSE;
	if (!dfu_device_refresh_and_clear (device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
dfu_tool_set_vendor (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE VID"
				     " -- e.g. `firmware.dfu 273f");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      FWUPD_INSTALL_FLAG_NONE, error)) {
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 16);
	if (tmp == 0 || tmp > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse VID '%s'",
			     values[1]);
		return FALSE;
	}
	fu_dfu_firmware_set_vid (FU_DFU_FIRMWARE (firmware), (guint16) tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware, file, error);
}

static gboolean
dfu_tool_set_product (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE PID"
				     " -- e.g. `firmware.dfu 1004");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      FWUPD_INSTALL_FLAG_NONE,
				      error)) {
		return FALSE;
	}

	/* parse VID */
	tmp = g_ascii_strtoull (values[1], NULL, 16);
	if (tmp == 0 || tmp > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse PID '%s'", values[1]);
		return FALSE;
	}
	fu_dfu_firmware_set_pid (FU_DFU_FIRMWARE (firmware), (guint16) tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware, file, error);
}

static guint16
dfu_tool_parse_release_uint16 (const gchar *version, GError **error)
{
	gchar *endptr = NULL;
	guint64 tmp_lsb, tmp_msb;
	g_auto(GStrv) split = g_strsplit (version, ".", -1);

	/* check if valid */
	if (g_strv_length (split) != 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid format, expected 'major.minor'");
		return 0xffff;
	}

	/* parse MSB & LSB */
	tmp_msb = g_ascii_strtoull (split[0], &endptr, 10);
	if (tmp_msb > 0xff || endptr[0] != '\0') {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse version '%s'",
			     version);
		return 0xffff;
	}
	tmp_lsb = g_ascii_strtoull (split[1], &endptr, 10);
	if (tmp_lsb > 0xff || endptr[0] != '\0') {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to parse version '%s'",
			     version);
		return 0xffff;
	}
	return (tmp_msb << 8) + tmp_lsb;
}

static gboolean
dfu_tool_set_release (DfuToolPrivate *priv, gchar **values, GError **error)
{
	gchar *endptr = NULL;
	guint64 tmp;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) < 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILE RELEASE"
				     " -- e.g. `firmware.dfu ffff");
		return FALSE;
	}

	/* open */
	file = g_file_new_for_path (values[0]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      FWUPD_INSTALL_FLAG_NONE,
				      error)) {
		return FALSE;
	}

	/* parse release */
	tmp = g_ascii_strtoull (values[1], &endptr, 16);
	if (tmp > G_MAXUINT16 || endptr[0] != '\0') {
		tmp = dfu_tool_parse_release_uint16 (values[1], error);
		if (tmp == 0xffff)
			return FALSE;
	}
	fu_dfu_firmware_set_release (FU_DFU_FIRMWARE (firmware), (guint16) tmp);

	/* write out new file */
	return dfu_firmware_write_file (firmware, file, error);
}

static GBytes *
dfu_tool_parse_hex_string (const gchar *val, GError **error)
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
dfu_tool_bytes_replace (GBytes *data, GBytes *search, GBytes *replace)
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

	g_return_val_if_fail (search_sz == replace_sz, FALSE);

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
dfu_tool_replace_data (DfuToolPrivate *priv, gchar **values, GError **error)
{
	guint cnt = 0;
	g_autoptr(DfuFirmware) firmware = NULL;
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
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file,
				      FWUPD_INSTALL_FLAG_NONE,
				      error)) {
		return FALSE;
	}

	/* parse hex values */
	data_search = dfu_tool_parse_hex_string (values[1], error);
	if (data_search == NULL)
		return FALSE;
	data_replace = dfu_tool_parse_hex_string (values[2], error);
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
	images = fu_firmware_get_images (FU_FIRMWARE (firmware));
	for (guint i = 0; i < images->len; i++) {
		DfuImage *image = g_ptr_array_index (images, i);
		GPtrArray *elements = dfu_image_get_elements (image);
		for (guint j = 0; j < elements->len; j++) {
			DfuElement *element = g_ptr_array_index (elements, j);
			GBytes *contents = dfu_element_get_contents (element);
			if (contents == NULL)
				continue;
			cnt += dfu_tool_bytes_replace (contents, data_search, data_replace);
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
	return dfu_firmware_write_file (firmware, file, error);
}

static gboolean
dfu_tool_convert (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuFirmwareFormat format;
	guint argc = g_strv_length (values);
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(GFile) file_in = NULL;
	g_autoptr(GFile) file_out = NULL;

	/* check args */
	if (argc != 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected "
				     "FORMAT FILE-IN FILE-OUT"
				     " -- e.g. `dfu firmware.hex firmware.dfu`");
		return FALSE;
	}

	/* parse file */
	file_in = g_file_new_for_path (values[1]);
	file_out = g_file_new_for_path (values[2]);
	firmware = dfu_firmware_new ();
	if (!dfu_firmware_parse_file (firmware, file_in,
				      FWUPD_INSTALL_FLAG_NONE,
				      error)) {
		return FALSE;
	}

	/* set output format */
	format = dfu_firmware_format_from_string (values[0]);
	dfu_firmware_set_format (firmware, format);
	if (format == DFU_FIRMWARE_FORMAT_UNKNOWN) {
		g_autoptr(GString) tmp = g_string_new (NULL);
		for (guint i = 1; i < DFU_FIRMWARE_FORMAT_LAST; i++) {
			if (tmp->len > 0)
				g_string_append (tmp, "|");
			g_string_append (tmp, dfu_firmware_format_to_string (i));
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "unknown format '%s', expected [%s]",
			     values[0], tmp->str);
		return FALSE;
	}

	/* print the new object */
	str_debug = fu_firmware_to_string (FU_FIRMWARE (firmware));
	g_debug ("DFU: %s", str_debug);

	/* write out new file */
	return dfu_firmware_write_file (firmware, file_out, error);
}

static void
fu_tool_action_changed_cb (FuDevice *device, GParamSpec *pspec, DfuToolPrivate *priv)
{
	g_print ("%s:\t%u%%\n",
		 fwupd_status_to_string (fu_device_get_status (device)),
		 fu_device_get_progress (device));
}

static gboolean
dfu_tool_read_alt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
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
	device = dfu_tool_get_default_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (priv->transfer_size > 0)
		dfu_device_set_transfer_size (device, priv->transfer_size);
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh (device, error))
		return FALSE;

	/* set up progress */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "notify::progress",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("detaching");
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!dfu_device_wait_for_replug (priv, device,
						 FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						 error))
			return FALSE;
	}

	/* transfer */
	target = dfu_device_get_target_by_alt_name (device,
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
		target = dfu_device_get_target_by_alt_setting (device,
							       (guint8) tmp,
							       error);
		if (target == NULL)
			return FALSE;
	}

	/* do transfer */
	image = dfu_target_upload (target, flags, error);
	if (image == NULL)
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;
	if (!dfu_device_wait_for_replug (priv, device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* create new firmware object */
	firmware = dfu_firmware_new ();
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_DFU);
	fu_dfu_firmware_set_vid (FU_DFU_FIRMWARE (firmware), dfu_device_get_runtime_vid (device));
	fu_dfu_firmware_set_pid (FU_DFU_FIRMWARE (firmware), dfu_device_get_runtime_pid (device));
	fu_firmware_add_image (FU_FIRMWARE (firmware), FU_FIRMWARE_IMAGE (image));

	/* save file */
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_write_file (firmware, file, error))
		return FALSE;

	/* print the new object */
	str_debug = fu_firmware_to_string (FU_FIRMWARE (firmware));
	g_debug ("DFU: %s", str_debug);

	/* success */
	g_print ("%u bytes successfully uploaded from device\n",
		 dfu_image_get_size (image));
	return TRUE;
}

static gboolean
dfu_tool_read (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuFirmwareFormat format;
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_NONE;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GFile) file = NULL;

	/* check args */
	if (g_strv_length (values) == 1) {
		/* guess output format */
		if (g_str_has_suffix (values[0], ".dfu")) {
			format = DFU_FIRMWARE_FORMAT_DFU;
		} else if (g_str_has_suffix (values[0], ".bin") ||
			   g_str_has_suffix (values[0], ".rom")) {
			format = DFU_FIRMWARE_FORMAT_RAW;
		} else {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "Could not guess a file format");
			return FALSE;
		}
	} else if (g_strv_length (values) == 2) {
		format = dfu_firmware_format_from_string (values[1]);
		if (format == DFU_FIRMWARE_FORMAT_UNKNOWN) {
			g_autoptr(GString) tmp = g_string_new (NULL);
			for (guint i = 1; i < DFU_FIRMWARE_FORMAT_LAST; i++) {
				if (tmp->len > 0)
					g_string_append (tmp, "|");
				g_string_append (tmp, dfu_firmware_format_to_string (i));
			}
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "unknown format '%s', expected [%s]",
				     values[0], tmp->str);
			return FALSE;
		}
	} else {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME [FORMAT]");
		return FALSE;
	}

	/* open correct device */
	device = dfu_tool_get_default_device (priv, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh (device, error))
		return FALSE;

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!dfu_device_wait_for_replug (priv, device,
						 FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						 error)) {
			return FALSE;
		}
	}

	/* transfer */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "notify::progress",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	firmware = dfu_device_upload (device, flags, error);
	if (firmware == NULL)
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;
	if (!dfu_device_wait_for_replug (priv, device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* save file */
	file = g_file_new_for_path (values[0]);
	dfu_firmware_set_format (firmware, format);
	if (!dfu_firmware_write_file (firmware, file, error))
		return FALSE;

	/* print the new object */
	str_debug = fu_firmware_to_string (FU_FIRMWARE (firmware));
	g_debug ("DFU: %s", str_debug);

	/* success */
	g_print ("%u bytes successfully uploaded from device\n",
		 dfu_firmware_get_size (firmware));
	return TRUE;
}

static gchar *
dfu_tool_get_device_string (DfuToolPrivate *priv, DfuDevice *device)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(FuDeviceLocker) locker  = NULL;

	/* open if required, and get status */
	if (usb_device == NULL) {
		return g_strdup_printf ("%04x:%04x [%s]",
					dfu_device_get_runtime_vid (device),
					dfu_device_get_runtime_pid (device),
					"removed");
	}
	if (!fu_usb_device_is_open (FU_USB_DEVICE (device))) {
		g_autoptr(GError) error = NULL;
		locker = fu_device_locker_new (device, &error);
		if (locker == NULL) {
			return g_strdup_printf ("%04x:%04x [%s]",
						fu_usb_device_get_vid (FU_USB_DEVICE (device)),
						fu_usb_device_get_pid (FU_USB_DEVICE (device)),
						error->message);
		}
		if (!dfu_device_refresh (device, &error))
			return NULL;
	}
	return g_strdup_printf ("%04x:%04x [%s:%s]",
				fu_usb_device_get_vid (FU_USB_DEVICE (device)),
				fu_usb_device_get_pid (FU_USB_DEVICE (device)),
				dfu_state_to_string (dfu_device_get_state (device)),
				dfu_status_to_string (dfu_device_get_status (device)));
}

static void
dfu_tool_device_added_cb (GUsbContext *context,
			  DfuDevice *device,
			  gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_autofree gchar *tmp = dfu_tool_get_device_string (priv, device);
	/* TRANSLATORS: this is when a device is hotplugged */
	dfu_tool_print_indent (_("Added"), tmp, 0);
}

static void
dfu_tool_device_removed_cb (GUsbContext *context,
			    DfuDevice *device,
			    gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_autofree gchar *tmp = dfu_tool_get_device_string (priv, device);
	/* TRANSLATORS: this is when a device is hotplugged */
	dfu_tool_print_indent (_("Removed"), tmp, 0);
}

static void
dfu_tool_device_changed_cb (GUsbContext *context, DfuDevice *device, gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_autofree gchar *tmp = dfu_tool_get_device_string (priv, device);
	/* TRANSLATORS: this is when a device is hotplugged */
	dfu_tool_print_indent (_("Changed"), tmp, 0);
}

static void
dfu_tool_watch_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	/* TRANSLATORS: this is when a device ctrl+c's a watch */
	g_print ("%s\n", _("Cancelled"));
	g_main_loop_quit (loop);
}

static gboolean
dfu_tool_watch (DfuToolPrivate *priv, gchar **values, GError **error)
{
	g_autoptr(GUsbContext) usb_context = NULL;
	g_autoptr(GMainLoop) loop = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get all the DFU devices */
	usb_context = g_usb_context_new (error);
	if (usb_context == NULL)
		return FALSE;
	g_usb_context_enumerate (usb_context);

	/* print what's already attached */
	devices = g_usb_context_get_devices (usb_context);
	for (guint i = 0; i < devices->len; i++) {
		DfuDevice *device = g_ptr_array_index (devices, i);
		dfu_tool_device_added_cb (usb_context, device, priv);
	}

	/* watch for any hotplugged device */
	loop = g_main_loop_new (NULL, FALSE);
	g_signal_connect (usb_context, "device-added",
			  G_CALLBACK (dfu_tool_device_added_cb), priv);
	g_signal_connect (usb_context, "device-removed",
			  G_CALLBACK (dfu_tool_device_removed_cb), priv);
	g_signal_connect (usb_context, "device-changed",
			  G_CALLBACK (dfu_tool_device_changed_cb), priv);
	g_signal_connect (priv->cancellable, "cancelled",
			  G_CALLBACK (dfu_tool_watch_cancelled_cb), loop);
	g_main_loop_run (loop);
	return TRUE;
}

static gboolean
dfu_tool_dump (DfuToolPrivate *priv, gchar **values, GError **error)
{
	FwupdInstallFlags flags = FWUPD_INSTALL_FLAG_NONE;

	/* check args */
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* dump corrupt files */
	if (priv->force)
		flags |= FWUPD_INSTALL_FLAG_FORCE;

	/* open files */
	for (guint i = 0; values[i] != NULL; i++) {
		g_autofree gchar *tmp = NULL;
		g_autoptr(DfuFirmware) firmware = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GError) error_local = NULL;

		/* dump to screen */
		g_print ("Loading %s:\n", values[i]);
		firmware = dfu_firmware_new ();
		file = g_file_new_for_path (values[i]);
		if (!dfu_firmware_parse_file (firmware, file, flags, &error_local)) {
			g_print ("Failed to load firmware: %s\n",
				 error_local->message);
			continue;
		}
		tmp = fu_firmware_to_string (FU_FIRMWARE (firmware));
		g_print ("%s\n", tmp);
	}
	return TRUE;
}

static gboolean
dfu_tool_write_alt (DfuToolPrivate *priv, gchar **values, GError **error)
{
	DfuTargetTransferFlags flags = DFU_TARGET_TRANSFER_FLAG_VERIFY;
	g_autofree gchar *str_debug = NULL;
	g_autoptr(DfuDevice) device = NULL;
	g_autoptr(DfuFirmware) firmware = NULL;
	g_autoptr(DfuImage) image = NULL;
	g_autoptr(DfuTarget) target = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
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
	firmware = dfu_firmware_new ();
	file = g_file_new_for_path (values[0]);
	if (!dfu_firmware_parse_file (firmware, file,
				      FWUPD_INSTALL_FLAG_NONE,
				      error))
		return FALSE;

	/* open correct device */
	device = dfu_tool_get_default_device (priv, error);
	if (device == NULL)
		return FALSE;
	if (priv->transfer_size > 0)
		dfu_device_set_transfer_size (device, priv->transfer_size);
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh (device, error))
		return FALSE;

	/* set up progress */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "notify::progress",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("detaching");
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!dfu_device_wait_for_replug (priv, device, 5000, error))
			return FALSE;
	}

	/* print the new object */
	str_debug = fu_firmware_to_string (FU_FIRMWARE (firmware));
	g_debug ("DFU: %s", str_debug);

	/* get correct target on device */
	target = dfu_device_get_target_by_alt_name (device,
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
		target = dfu_device_get_target_by_alt_setting (device,
							       (guint8) tmp,
							       error);
		if (target == NULL)
			return FALSE;
	}

	/* allow overriding the firmware alt-setting */
	if (g_strv_length (values) > 2) {
		image = DFU_IMAGE (fu_firmware_get_image_by_id (FU_FIRMWARE (firmware), values[2], NULL));
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
			image = DFU_IMAGE (fu_firmware_get_image_by_idx (FU_FIRMWARE (firmware), tmp, error));
			if (image == NULL)
				return FALSE;
		}
	} else {
		g_print ("WARNING: Using default firmware image\n");
		image = DFU_IMAGE (fu_firmware_get_image_default (FU_FIRMWARE (firmware), error));
		if (image == NULL)
			return FALSE;
	}

	/* transfer */
	if (!dfu_target_download (target, image, flags, error))
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;
	if (!dfu_device_wait_for_replug (priv, device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* success */
	g_print ("%u bytes successfully downloaded to device\n",
		 dfu_image_get_size (image));
	return TRUE;
}

static gboolean
dfu_tool_write (DfuToolPrivate *priv, gchar **values, GError **error)
{
	FwupdInstallFlags flags = FWUPD_INSTALL_FLAG_NONE;
	g_autoptr(DfuDevice) device = NULL;
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
	device = dfu_tool_get_default_device (priv, error);
	if (device == NULL)
		return FALSE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!dfu_device_refresh (device, error))
		return FALSE;

	/* APP -> DFU */
	if (!fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_device_detach (FU_DEVICE (device), error))
			return FALSE;
		if (!dfu_device_wait_for_replug (priv, device,
						 FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
						 error)) {
			return FALSE;
		}
	}

	/* allow wildcards */
	if (priv->force)
		flags |= FWUPD_INSTALL_FLAG_FORCE;

	/* transfer */
	g_signal_connect (device, "notify::status",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	g_signal_connect (device, "notify::progress",
			  G_CALLBACK (fu_tool_action_changed_cb), priv);
	if (!fu_device_write_firmware (FU_DEVICE (device), fw, flags, error))
		return FALSE;

	/* do host reset */
	if (!fu_device_attach (FU_DEVICE (device), error))
		return FALSE;
	if (!dfu_device_wait_for_replug (priv, device, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE, error))
		return FALSE;

	/* success */
	g_print ("%u bytes successfully downloaded to device\n",
		 (guint) g_bytes_get_size (fw));
	return TRUE;
}

#ifdef HAVE_GIO_UNIX
static gboolean
dfu_tool_sigint_cb (gpointer user_data)
{
	DfuToolPrivate *priv = (DfuToolPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (priv->cancellable);
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
	g_autoptr(DfuToolPrivate) priv = g_new0 (DfuToolPrivate, 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
			_("Print the version number"), NULL },
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Print verbose debug statements"), NULL },
		{ "device", 'd', 0, G_OPTION_ARG_STRING, &priv->device_vid_pid,
			_("Specify Vendor/Product ID(s) of DFU device"), "VID:PID" },
		{ "transfer-size", 't', 0, G_OPTION_ARG_STRING, &priv->transfer_size,
			_("Specify the number of bytes per USB transfer"), "BYTES" },
		{ "force", '\0', 0, G_OPTION_ARG_NONE, &priv->force,
			_("Force the action ignoring all warnings"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, FWUPD_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) dfu_tool_item_free);
	dfu_tool_add (priv->cmd_array,
		     "convert",
		     "FORMAT FILE-IN FILE OUT [SIZE]",
		     /* TRANSLATORS: command description */
		     _("Convert firmware to DFU format"),
		     dfu_tool_convert);
	dfu_tool_add (priv->cmd_array,
		     "set-vendor",
		     "FILE VID",
		     /* TRANSLATORS: command description */
		     _("Set vendor ID on firmware file"),
		     dfu_tool_set_vendor);
	dfu_tool_add (priv->cmd_array,
		     "set-product",
		     "FILE PID",
		     /* TRANSLATORS: command description */
		     _("Set product ID on firmware file"),
		     dfu_tool_set_product);
	dfu_tool_add (priv->cmd_array,
		     "set-release",
		     "FILE RELEASE",
		     /* TRANSLATORS: command description */
		     _("Set release version on firmware file"),
		     dfu_tool_set_release);
	dfu_tool_add (priv->cmd_array,
		     "read",
		     "FILENAME",
		     /* TRANSLATORS: command description */
		     _("Read firmware from device into a file"),
		     dfu_tool_read);
	dfu_tool_add (priv->cmd_array,
		     "read-alt",
		     "FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID",
		     /* TRANSLATORS: command description */
		     _("Read firmware from one partition into a file"),
		     dfu_tool_read_alt);
	dfu_tool_add (priv->cmd_array,
		     "write",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into device"),
		     dfu_tool_write);
	dfu_tool_add (priv->cmd_array,
		     "write-alt",
		     "FILENAME DEVICE-ALT-NAME|DEVICE-ALT-ID [IMAGE-ALT-NAME|IMAGE-ALT-ID]",
		     /* TRANSLATORS: command description */
		     _("Write firmware from file into one partition"),
		     dfu_tool_write_alt);
	dfu_tool_add (priv->cmd_array,
		     "dump",
		     "FILENAME",
		     /* TRANSLATORS: command description */
		     _("Dump details about a firmware file"),
		     dfu_tool_dump);
	dfu_tool_add (priv->cmd_array,
		     "watch",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Watch DFU devices being hotplugged"),
		     dfu_tool_watch);
	dfu_tool_add (priv->cmd_array,
		     "replace-data",
		     NULL,
		     /* TRANSLATORS: command description */
		     _("Replace data in an existing firmware file"),
		     dfu_tool_replace_data);

	/* use quirks */
	priv->quirks = fu_quirks_new ();
	if (!fu_quirks_load (priv->quirks, FU_QUIRKS_LOAD_FLAG_NONE, &error)) {
		/* TRANSLATORS: quirks are device-specific workarounds */
		g_print ("%s: %s\n", _("Failed to load quirks"), error->message);
		return EXIT_FAILURE;
	}

	/* do stuff on ctrl+c */
	priv->cancellable = g_cancellable_new ();
#ifdef HAVE_GIO_UNIX
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				dfu_tool_sigint_cb,
				priv,
				NULL);
#endif

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) dfu_tool_sort_command_name_cb);

	/* get a list of the commands */
	context = g_option_context_new (NULL);
	cmd_descriptions = dfu_tool_get_descriptions (priv->cmd_array);
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
	ret = dfu_tool_run (priv, argv[1], (gchar**) &argv[2], &error);
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

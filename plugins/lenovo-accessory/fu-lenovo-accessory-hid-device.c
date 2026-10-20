/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <unistd.h>

#include "fu-lenovo-accessory-hid-common.h"
#include "fu-lenovo-accessory-hid-device.h"

struct _FuLenovoAccessoryHidDevice {
	FuHidDevice parent_instance;
	gint notify_fd;
	GIOChannel *notify_channel;
	guint notify_watch_id;
};

static void
fu_lenovo_accessory_hid_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuLenovoAccessoryHidDevice,
			fu_lenovo_accessory_hid_device,
			FU_TYPE_HID_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_LENOVO_ACCESSORY_IMPL,
					      fu_lenovo_accessory_hid_device_impl_iface_init))

/* Interface 2 carries keyboard input + wireless device notifications (Report ID 0x04) */
#define FU_LENOVO_ACCESSORY_IFACE_NOTIFY 0x02

static gchar *
fu_lenovo_accessory_hid_device_find_notify_hidraw(FuUsbDevice *usb_device, GError **error)
{
	const gchar *usb_syspath = NULL;
	g_autofree gchar *iface_path = NULL;
	g_autoptr(GDir) iface_dir = NULL;
	const gchar *hid_entry = NULL;
	g_autofree gchar *platform_id = NULL;

	/* get USB device sysfs path, e.g. /sys/.../3-6.2.3 */
	usb_syspath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(usb_device));

	/* extract platform ID (e.g. "3-6.2.3" from the path) */
	platform_id = g_path_get_basename(usb_syspath);

	/* construct Interface 2 path: /sys/.../3-6.2.3/3-6.2.3:1.2 */
	iface_path = g_strdup_printf("%s/%s:1.%d",
				     usb_syspath,
				     platform_id,
				     FU_LENOVO_ACCESSORY_IFACE_NOTIFY);

	/* open the interface directory */
	iface_dir = g_dir_open(iface_path, 0, error);
	if (iface_dir == NULL)
		return NULL;

	/* look for HID device subdirectory (0003:17EF:629D.*) */
	while ((hid_entry = g_dir_read_name(iface_dir)) != NULL) {
		g_autofree gchar *hid_device_path = NULL;
		g_autofree gchar *hidraw_dir_path = NULL;
		g_autoptr(GDir) hidraw_dir = NULL;
		const gchar *hidraw_entry = NULL;

		if (!g_str_has_prefix(hid_entry, "0003:"))
			continue;

		hid_device_path = g_build_filename(iface_path, hid_entry, NULL);
		hidraw_dir_path = g_build_filename(hid_device_path, "hidraw", NULL);
		hidraw_dir = g_dir_open(hidraw_dir_path, 0, NULL);
		if (hidraw_dir == NULL)
			continue;

		/* get the hidraw node */
		hidraw_entry = g_dir_read_name(hidraw_dir);
		if (hidraw_entry != NULL && g_str_has_prefix(hidraw_entry, "hidraw")) {
			return g_strdup_printf("/dev/%s", hidraw_entry);
		}
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "Interface 2 hidraw not found");
	return NULL;
}

#define FU_LENOVO_ACCESSORY_NOTIFY_ADD_RETRY_COUNT 3
#define FU_LENOVO_ACCESSORY_NOTIFY_ADD_RETRY_DELAY 100 /* ms */

static gboolean
fu_lenovo_accessory_hid_device_add_child_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint8 slot = *((guint8 *)user_data);
	g_autofree gchar *logical_id = g_strdup_printf("slot%u", slot);
	g_autoptr(FuDevice) child = NULL;

	/* a repeated connect notification must not create a duplicate child */
	child = fu_device_get_child_by_logical_id(device, logical_id, NULL);
	if (child != NULL)
		return TRUE;
	return fu_lenovo_accessory_hid_add_child(FU_LENOVO_ACCESSORY_IMPL(device), slot, error);
}

static gboolean
fu_lenovo_accessory_hid_device_notify_cb(GIOChannel *channel,
					 GIOCondition condition,
					 gpointer user_data)
{
	FuLenovoAccessoryHidDevice *self = FU_LENOVO_ACCESSORY_HID_DEVICE(user_data);
	guint8 buf[64] = {0};
	gsize bytes_read = 0;
	guint8 slot;
	guint8 status_val;
	GIOStatus status;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuStructLenovoAccessoryNotify) st = NULL;

	/* read hidraw data */
	status =
	    g_io_channel_read_chars(channel, (gchar *)buf, sizeof(buf), &bytes_read, &error_local);
	if (status != G_IO_STATUS_NORMAL) {
		if (status != G_IO_STATUS_AGAIN)
			g_warning("failed to read notify: %s", error_local->message);
		return G_SOURCE_CONTINUE;
	}

	/* filter non-notification reports */
	if (buf[0] != 0x04)
		return G_SOURCE_CONTINUE;

	/* parse notification structure */
	st = fu_struct_lenovo_accessory_notify_parse(buf, bytes_read, 0, &error_local);
	if (st == NULL) {
		g_debug("failed to parse notify: %s", error_local->message);
		return G_SOURCE_CONTINUE;
	}

	/* check event type */
	if (fu_struct_lenovo_accessory_notify_get_event(st) !=
	    FU_LENOVO_ACCESSORY_NOTIFY_EVENT_WIRELESS_CONNECT_STATUS_CHANGE) {
		return G_SOURCE_CONTINUE;
	}

	slot = fu_struct_lenovo_accessory_notify_get_slot(st);
	status_val = fu_struct_lenovo_accessory_notify_get_connect_status(st);

	g_warning("wireless slot %u status change: %s",
		  slot,
		  status_val == FU_LENOVO_ACCESSORY_NOTIFY_CONNECT_STATUS_CONNECTED
		      ? "connected"
		      : "disconnected");

	/* handle disconnection without querying device: the notification already
	 * tells us which slot disconnected, so we can remove the child directly */
	if (status_val == FU_LENOVO_ACCESSORY_NOTIFY_CONNECT_STATUS_DISCONNECTED) {
		g_autofree gchar *logical_id = g_strdup_printf("slot%u", slot);
		g_autoptr(FuDevice) child =
		    fu_device_get_child_by_logical_id(FU_DEVICE(self), logical_id, NULL);
		if (child != NULL) {
			g_debug("removing disconnected child at slot %u", slot);
			fu_device_remove_child(FU_DEVICE(self), child);
		}
		return G_SOURCE_CONTINUE;
	}

	/* the USB device is closed after setup, so reopen it to query PID and name */
	locker = fu_device_locker_new(FU_DEVICE(self), &error_local);
	if (locker == NULL) {
		g_warning("failed to open device after notify: %s", error_local->message);
		return G_SOURCE_CONTINUE;
	}

	/* the peripheral may not answer the slot query straight after connecting */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_lenovo_accessory_hid_device_add_child_cb,
				  FU_LENOVO_ACCESSORY_NOTIFY_ADD_RETRY_COUNT,
				  FU_LENOVO_ACCESSORY_NOTIFY_ADD_RETRY_DELAY,
				  &slot,
				  &error_local))
		g_warning("failed to add child for slot %u: %s", slot, error_local->message);

	return G_SOURCE_CONTINUE;
}

static gboolean
fu_lenovo_accessory_hid_device_setup(FuDevice *device, GError **error)
{
	FuLenovoAccessoryHidDevice *self = FU_LENOVO_ACCESSORY_HID_DEVICE(device);
	guint8 major = 0;
	guint8 minor = 0;
	guint8 micro = 0;
	g_autofree gchar *version = NULL;
	g_autofree gchar *hidraw_path = NULL;
	g_autoptr(GPtrArray) descriptors = NULL;
	g_autoptr(GError) error_notify = NULL;
	gboolean found_report = FALSE;

	/* the command interface is a vendor HID collection (UsagePage 0xFF00,
	 * Usage 0x02) carrying a 64-byte report; confirm it is present so we
	 * fail early on a device that does not speak this protocol */
	descriptors = fu_hid_device_parse_descriptors(FU_HID_DEVICE(self), error);
	if (descriptors == NULL)
		return FALSE;
	for (guint i = 0; i < descriptors->len; i++) {
		FuHidDescriptor *desc = g_ptr_array_index(descriptors, i);
		g_autoptr(FuHidReport) report = NULL;
		report = fu_hid_descriptor_find_report(desc,
						       NULL,
						       "usage-page",
						       0xFF00,
						       "usage",
						       0x02,
						       "report-size",
						       8,
						       "report-count",
						       0x40,
						       NULL);
		if (report != NULL) {
			found_report = TRUE;
			break;
		}
	}
	if (!found_report) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no vendor command report (0xFF00/0x02) found");
		return FALSE;
	}
	if (!fu_lenovo_accessory_impl_get_fwversion(FU_LENOVO_ACCESSORY_IMPL(device),
						    &major,
						    &minor,
						    &micro,
						    error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u", major, minor, micro);
	fu_device_set_version(device, version);
	if (!fu_lenovo_accessory_hid_add_children(FU_LENOVO_ACCESSORY_IMPL(device), error))
		return FALSE;

	/* try to open Interface 2 hidraw for wireless notifications (best-effort) */
	hidraw_path =
	    fu_lenovo_accessory_hid_device_find_notify_hidraw(FU_USB_DEVICE(device), &error_notify);
	if (hidraw_path == NULL) {
		g_debug("Interface 2 hidraw not found, hotplug detection disabled: %s",
			error_notify->message);
		return TRUE; /* not a fatal error */
	}

	/* open hidraw node in non-blocking mode */
	self->notify_fd = open(hidraw_path, O_RDONLY | O_NONBLOCK);
	if (self->notify_fd < 0) {
		g_debug("failed to open %s: %s", hidraw_path, fwupd_strerror(errno));
		return TRUE; /* not a fatal error */
	}

	/* create GIOChannel for async reading */
	self->notify_channel = g_io_channel_unix_new(self->notify_fd);
	g_io_channel_set_encoding(self->notify_channel, NULL, NULL);
	g_io_channel_set_buffered(self->notify_channel, FALSE);

	/* add watch for incoming data */
	self->notify_watch_id = g_io_add_watch(self->notify_channel,
					       G_IO_IN,
					       fu_lenovo_accessory_hid_device_notify_cb,
					       self);

	g_info("hotplug notification listening started on %s", hidraw_path);

	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLenovoAccessoryHidDevice *self = FU_LENOVO_ACCESSORY_HID_DEVICE(device);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_lenovo_accessory_impl_set_mode(FU_LENOVO_ACCESSORY_IMPL(self),
					       FU_LENOVO_ACCESSORY_DEVICE_MODE_DFU_MODE,
					       error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_lenovo_accessory_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 26, "reload");
}

static void
fu_lenovo_accessory_hid_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface)
{
	iface->read = fu_lenovo_accessory_hid_read;
	iface->write = fu_lenovo_accessory_hid_write;
	iface->process = fu_lenovo_accessory_hid_process;
}

static void
fu_lenovo_accessory_hid_device_finalize(GObject *object)
{
	FuLenovoAccessoryHidDevice *self = FU_LENOVO_ACCESSORY_HID_DEVICE(object);

	/* cleanup notification monitoring */
	if (self->notify_watch_id != 0) {
		g_source_remove(self->notify_watch_id);
		self->notify_watch_id = 0;
	}
	if (self->notify_channel != NULL) {
		/* g_io_channel_shutdown closes the underlying fd, so we must not
		 * close(notify_fd) again afterward to avoid a double-close */
		g_io_channel_shutdown(self->notify_channel, FALSE, NULL);
		g_io_channel_unref(self->notify_channel);
		self->notify_channel = NULL;
		self->notify_fd = -1;
	} else if (self->notify_fd >= 0) {
		/* only close directly if no channel was created */
		close(self->notify_fd);
		self->notify_fd = -1;
	}

	G_OBJECT_CLASS(fu_lenovo_accessory_hid_device_parent_class)->finalize(object);
}

static void
fu_lenovo_accessory_hid_device_class_init(FuLenovoAccessoryHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_lenovo_accessory_hid_device_finalize;
	device_class->setup = fu_lenovo_accessory_hid_device_setup;
	device_class->set_progress = fu_lenovo_accessory_hid_device_set_progress;
	device_class->detach = fu_lenovo_accessory_hid_device_detach;
}

static void
fu_lenovo_accessory_hid_device_init(FuLenovoAccessoryHidDevice *self)
{
	self->notify_fd = -1;
	self->notify_channel = NULL;
	self->notify_watch_id = 0;

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.accessory");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_install_duration(FU_DEVICE(self), 30);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_USB_RECEIVER);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_hid_device_set_interface(FU_HID_DEVICE(self), FU_LENOVO_ACCESSORY_IFACE_CMD);
}

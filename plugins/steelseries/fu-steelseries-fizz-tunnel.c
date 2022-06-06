/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-firmware.h"
#include "fu-steelseries-fizz-tunnel.h"

struct _FuSteelseriesFizzTunnel {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizzTunnel, fu_steelseries_fizz_tunnel, FU_TYPE_DEVICE)

static gboolean
fu_steelseries_fizz_tunnel_ping(FuDevice *device, gboolean *reached, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	guint8 status;
	guint8 level;
	g_autoptr(GError) error_local = NULL;

	if (!fu_steelseries_fizz_get_connection_status(parent, &status, error)) {
		g_prefix_error(error, "failed to get connection status: ");
		return FALSE;
	}
	g_debug("ConnectionStatus: %u", status);
	*reached = status != STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED;
	if (!*reached) {
		/* success */
		return TRUE;
	}

	/* ping device anyway */
	if (!fu_steelseries_fizz_get_battery_level(fu_device_get_parent(device),
						   TRUE,
						   &level,
						   &error_local)) {
		*reached = FALSE;

		if (!g_error_matches(error_local,
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_TIMED_OUT)) {
			/* failure */
			*error = g_steal_pointer(&error_local);
			return FALSE;
		}

		/* success */
		return TRUE;
	}
	g_debug("BatteryLevel: 0x%02x", level);
	/*
	 * CHARGING: Most significant bit. When bit is set to 1 it means battery is currently
	 * charging/plugged in
	 *
	 * LEVEL: 7 least significant bit value of the battery. Values are between 2-21, to get %
	 * you can do (LEVEL - 1) * 5
	 */
	fu_device_set_battery_level(device,
				    ((level & STEELSERIES_FIZZ_BATTERY_LEVEL_STATUS_BITS) - 1U) *
					5U);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_wait_for_reconnect_cb(FuDevice *device,
						 gpointer user_data,
						 GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	guint8 status;

	if (!fu_steelseries_fizz_get_connection_status(parent, &status, error)) {
		g_prefix_error(error, "failed to get connection status: ");
		return FALSE;
	}
	g_debug("ConnectionStatus: %u", status);
	if (status == STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "device is unreachable");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_wait_for_reconnect(FuDevice *device, guint delay, GError **error)
{
	return fu_device_retry_full(device,
				    fu_steelseries_fizz_tunnel_wait_for_reconnect_cb,
				    delay / 1000,
				    1000,
				    NULL,
				    error);
}

static gboolean
fu_steelseries_fizz_tunnel_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	guint remove_delay = fu_device_get_remove_delay(device);
	g_autoptr(GError) error_local = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 67, "sleep");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 33, NULL);

	if (!fu_steelseries_fizz_reset(parent,
				       TRUE,
				       STEELSERIES_FIZZ_RESET_MODE_NORMAL,
				       &error_local))
		g_warning("failed to reset: %s", error_local->message);
	fu_progress_step_done(progress);

	/* wait for receiver to reset the connection status to 0 */
	fu_progress_sleep(fu_progress_get_child(progress), 2000); /* 2 s */
	remove_delay -= 2000;
	fu_progress_step_done(progress);

	if (!fu_steelseries_fizz_tunnel_wait_for_reconnect(device, remove_delay, error)) {
		g_prefix_error(error, "device %s did not come back", fu_device_get_id(device));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_probe(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(parent));
	guint16 release;

	/* set the version if the release has been set */
	release = g_usb_device_get_release(usb_device);
	if (release != 0x0 &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		g_autofree gchar *version = NULL;
		version = fu_version_from_uint16(release, FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version(device, version);
	}

	/* add GUIDs in order of priority */
	fu_device_add_instance_str(device, "PROTOCOL", "FIZZ_TUNNEL");
	fu_device_add_instance_u16(device, "VID", g_usb_device_get_vid(usb_device));
	fu_device_add_instance_u16(device, "PID", g_usb_device_get_pid(usb_device));
	fu_device_add_instance_u16(device, "REV", release);
	fu_device_build_instance_id_quirk(device, NULL, "STEELSERIES", "VID", "PROTOCOL", NULL);
	fu_device_build_instance_id(device, NULL, "STEELSERIES", "VID", "PID", "PROTOCOL", NULL);
	fu_device_build_instance_id(device,
				    NULL,
				    "STEELSERIES",
				    "VID",
				    "PID",
				    "REV",
				    "PROTOCOL",
				    NULL);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_setup(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	guint32 calculated_crc;
	guint32 stored_crc;
	gboolean reached;
	guint8 fs = STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
	guint8 id = STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *version = NULL;

	/* ping */
	if (!fu_steelseries_fizz_tunnel_ping(device, &reached, &error_local)) {
		g_debug("ignoring error on ping: %s", error_local->message);

		/* success */
		return TRUE;
	}
	if (!reached) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNREACHABLE);

		/* success */
		return TRUE;
	}

	version = fu_steelseries_fizz_get_version(parent, TRUE, error);
	if (version == NULL) {
		g_prefix_error(error, "failed to get version: ");
		return FALSE;
	}
	fu_device_set_version(device, version);

	if (!fu_steelseries_fizz_get_crc32_fs(parent,
					      TRUE,
					      fs,
					      id,
					      &calculated_crc,
					      &stored_crc,
					      error)) {
		g_prefix_error(error,
			       "failed to get file CRC32 from FS 0x%02x ID 0x%02x: ",
			       fs,
			       id);
		return FALSE;
	}

	if (calculated_crc != stored_crc) {
		g_warning("%s: checksum mismatch, got 0x%08x, expected 0x%08x",
			  fu_device_get_name(device),
			  calculated_crc,
			  stored_crc);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_poll(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	guint32 calculated_crc;
	guint32 stored_crc;
	gboolean reached;
	guint8 fs = STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
	guint8 id = STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree gchar *version = NULL;

	/* open */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_steelseries_fizz_tunnel_ping(device, &reached, error)) {
		g_prefix_error(error, "failed to ping: ");
		return FALSE;
	}

	if (!reached) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNREACHABLE);

		/* success */
		return TRUE;
	}

	/* "deferred" setup */
	version = fu_steelseries_fizz_get_version(parent, TRUE, &error_local);
	if (version == NULL) {
		g_debug("ignoring error on version: %s", error_local->message);

		/* success */
		return TRUE;
	}
	fu_device_set_version(device, version);

	if (!fu_steelseries_fizz_get_crc32_fs(parent,
					      TRUE,
					      fs,
					      id,
					      &calculated_crc,
					      &stored_crc,
					      &error_local)) {
		g_debug("ignoring error on get file CRC32 from FS 0x%02x ID 0x%02x: %s",
			fs,
			id,
			error_local->message);

		/* success */
		return TRUE;
	}

	if (calculated_crc != stored_crc)
		g_warning("%s: checksum mismatch, got 0x%08x, expected 0x%08x",
			  fu_device_get_name(device),
			  calculated_crc,
			  stored_crc);

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_UNREACHABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	guint8 fs = STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
	guint8 id = STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, NULL);

	if (!fu_steelseries_fizz_write_firmware_fs(parent,
						   TRUE,
						   fs,
						   id,
						   firmware,
						   fu_progress_get_child(progress),
						   flags,
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_steelseries_fizz_tunnel_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	guint8 fs = STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
	guint8 id = STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;
	g_autoptr(FuFirmware) firmware = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 100, NULL);

	firmware = fu_steelseries_fizz_read_firmware_fs(parent,
							TRUE,
							fs,
							id,
							fu_device_get_firmware_size_max(device),
							fu_progress_get_child(progress),
							error);
	if (firmware == NULL)
		return NULL;
	fu_progress_step_done(progress);

	/* success */
	return g_steal_pointer(&firmware);
}

static void
fu_steelseries_fizz_tunnel_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 6, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_steelseries_fizz_tunnel_class_init(FuSteelseriesFizzTunnelClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->attach = fu_steelseries_fizz_tunnel_attach;
	klass_device->probe = fu_steelseries_fizz_tunnel_probe;
	klass_device->setup = fu_steelseries_fizz_tunnel_setup;
	klass_device->poll = fu_steelseries_fizz_tunnel_poll;
	klass_device->write_firmware = fu_steelseries_fizz_tunnel_write_firmware;
	klass_device->read_firmware = fu_steelseries_fizz_tunnel_read_firmware;
	klass_device->set_progress = fu_steelseries_fizz_tunnel_set_progress;
}

static void
fu_steelseries_fizz_tunnel_init(FuSteelseriesFizzTunnel *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_AUTO_PAUSE_POLLING);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.fizz");
	fu_device_set_logical_id(FU_DEVICE(self), "tunnel");
	fu_device_set_install_duration(FU_DEVICE(self), 38);				  /* 38 s */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE); /* 10 s */
	fu_device_set_poll_interval(FU_DEVICE(self), 60000); /* 1 min */
	fu_device_set_battery_threshold(FU_DEVICE(self), 20);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_STEELSERIES_FIRMWARE);
}

FuSteelseriesFizzTunnel *
fu_steelseries_fizz_tunnel_new(FuSteelseriesFizz *parent)
{
	return g_object_new(FU_TYPE_STEELSERIES_FIZZ_TUNNEL, "parent", FU_DEVICE(parent), NULL);
}

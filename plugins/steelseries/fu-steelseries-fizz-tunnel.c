/*
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-firmware.h"
#include "fu-steelseries-fizz-impl.h"
#include "fu-steelseries-fizz-tunnel.h"

#define STEELSERIES_FIZZ_TUNNEL_POLL_INTERVAL 20000 /* ms*/

struct _FuSteelseriesFizzTunnel {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizzTunnel, fu_steelseries_fizz_tunnel, FU_TYPE_DEVICE)

static gboolean
fu_steelseries_fizz_tunnel_ping(FuDevice *device, gboolean *reached, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	guint8 status;
	guint8 level;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *version = NULL;

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	if (!fu_steelseries_fizz_impl_get_connection_status(FU_STEELSERIES_FIZZ_IMPL(proxy),
							    &status,
							    error)) {
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
	if (!fu_steelseries_fizz_impl_get_battery_level(FU_STEELSERIES_FIZZ_IMPL(proxy),
							TRUE,
							&level,
							&error_local)) {
		*reached = FALSE;

		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			/* failure */
			*error = g_steal_pointer(&error_local);
			return FALSE;
		}

		/* success */
		return TRUE;
	}
	g_debug("BatteryLevel: 0x%02x", level);
	fu_device_set_battery_level(device, level);

	/* re-read version after reconnect/update */
	version =
	    fu_steelseries_fizz_impl_get_version(FU_STEELSERIES_FIZZ_IMPL(proxy), TRUE, error);
	if (version == NULL) {
		*reached = FALSE;
		g_prefix_error(error,
			       "unable to read version from device %s: ",
			       fu_device_get_id(device));
		return FALSE;
	}
	fu_device_set_version(device, version); /* nocheck:set-version */

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
	gboolean reached;

	if (!fu_steelseries_fizz_get_connection_status(parent, &status, error)) {
		g_prefix_error(error, "failed to get connection status: ");
		return FALSE;
	}
	g_debug("ConnectionStatus: %u", status);
	if (status == STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "device is unreachable");
		return FALSE;
	}

	/* ping */
	if (!fu_steelseries_fizz_tunnel_ping(device, &reached, error)) {
		g_prefix_error(error, "failed to ping on reconnect: ");
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
fu_steelseries_fizz_tunnel_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}
	return fu_steelseries_fizz_impl_is_updatable(FU_STEELSERIES_FIZZ_IMPL(proxy),
						     device,
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
	fu_device_sleep_full(device, 2000, fu_progress_get_child(progress)); /* ms */
	remove_delay -= 2000;
	fu_progress_step_done(progress);

	if (!fu_steelseries_fizz_tunnel_wait_for_reconnect(device, remove_delay, error)) {
		g_prefix_error(error, "device %s did not come back: ", fu_device_get_id(device));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_probe(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	guint16 release;

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	/* set the version if the release has been set */
	release = fu_usb_device_get_release(FU_USB_DEVICE(proxy));
	if (release != 0x0 &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version_raw(device, release);
	}

	/* add GUIDs in order of priority */
	fu_device_add_instance_str(device, "PROTOCOL", "FIZZ_TUNNEL");
	fu_device_add_instance_u16(device, "VID", fu_usb_device_get_vid(FU_USB_DEVICE(proxy)));
	fu_device_add_instance_u16(device, "PID", fu_usb_device_get_pid(FU_USB_DEVICE(proxy)));
	fu_device_add_instance_u16(device, "REV", release);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "STEELSERIES",
					 "VID",
					 "PROTOCOL",
					 NULL);
	fu_device_build_instance_id(device, NULL, "STEELSERIES", "VID", "PID", "PROTOCOL", NULL);
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV)) {
		fu_device_build_instance_id(device,
					    NULL,
					    "STEELSERIES",
					    "VID",
					    "PID",
					    "REV",
					    "PROTOCOL",
					    NULL);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_setup(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	gboolean reached;
	g_autofree gchar *serial = NULL;
	g_autoptr(GError) error_local = NULL;

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	/* ping */
	if (!fu_steelseries_fizz_tunnel_ping(device, &reached, &error_local)) {
		g_debug("ignoring error for %s on ping: %s",
			fu_device_get_id(device),
			error_local->message);

		/* success */
		return TRUE;
	}

	serial = fu_steelseries_fizz_impl_get_serial(FU_STEELSERIES_FIZZ_IMPL(proxy),
						     FALSE,
						     &error_local);
	if (serial != NULL) {
		fu_device_set_serial(device, serial);
		fu_device_set_equivalent_id(device, serial);
	} else {
		g_debug("ignoring error: %s", error_local->message);
	}

	if (!reached) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNREACHABLE);

		/* success */
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_tunnel_poll(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	guint32 calculated_crc;
	guint32 stored_crc;
	gboolean reached;
	guint8 fs =
	    fu_steelseries_fizz_impl_get_fs_id(FU_STEELSERIES_FIZZ_IMPL(proxy), FALSE, error);
	guint8 id =
	    fu_steelseries_fizz_impl_get_file_id(FU_STEELSERIES_FIZZ_IMPL(proxy), FALSE, error);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

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

	/* set reachable here since we ignore crc error while polling device */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_UNREACHABLE);

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
	FuDevice *proxy = fu_device_get_proxy(device);
	guint8 fs =
	    fu_steelseries_fizz_impl_get_fs_id(FU_STEELSERIES_FIZZ_IMPL(proxy), FALSE, error);
	guint8 id =
	    fu_steelseries_fizz_impl_get_file_id(FU_STEELSERIES_FIZZ_IMPL(proxy), FALSE, error);

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
	FuDevice *proxy = fu_device_get_proxy(device);
	guint8 fs =
	    fu_steelseries_fizz_impl_get_fs_id(FU_STEELSERIES_FIZZ_IMPL(proxy), FALSE, error);
	guint8 id =
	    fu_steelseries_fizz_impl_get_file_id(FU_STEELSERIES_FIZZ_IMPL(proxy), FALSE, error);
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

static gchar *
fu_steelseries_fizz_tunnel_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_steelseries_fizz_tunnel_class_init(FuSteelseriesFizzTunnelClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->detach = fu_steelseries_fizz_tunnel_detach;
	device_class->attach = fu_steelseries_fizz_tunnel_attach;
	device_class->probe = fu_steelseries_fizz_tunnel_probe;
	device_class->setup = fu_steelseries_fizz_tunnel_setup;
	device_class->poll = fu_steelseries_fizz_tunnel_poll;
	device_class->write_firmware = fu_steelseries_fizz_tunnel_write_firmware;
	device_class->read_firmware = fu_steelseries_fizz_tunnel_read_firmware;
	device_class->set_progress = fu_steelseries_fizz_tunnel_set_progress;
	device_class->convert_version = fu_steelseries_fizz_tunnel_convert_version;
}

static void
fu_steelseries_fizz_tunnel_init(FuSteelseriesFizzTunnel *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.fizz");
	fu_device_set_logical_id(FU_DEVICE(self), "tunnel");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE); /* 10 s */
	fu_device_set_poll_interval(FU_DEVICE(self), STEELSERIES_FIZZ_TUNNEL_POLL_INTERVAL);
	fu_device_set_battery_threshold(FU_DEVICE(self), 20);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_STEELSERIES_FIRMWARE);
}

FuSteelseriesFizzTunnel *
fu_steelseries_fizz_tunnel_new(FuSteelseriesFizz *parent)
{
	return g_object_new(FU_TYPE_STEELSERIES_FIZZ_TUNNEL, "parent", FU_DEVICE(parent), NULL);
}

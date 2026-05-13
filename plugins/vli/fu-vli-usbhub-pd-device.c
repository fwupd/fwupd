/*
 * Copyright 2017 VIA Corporation
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-vli-pd-common.h"
#include "fu-vli-pd-firmware.h"
#include "fu-vli-struct.h"
#include "fu-vli-usbhub-common.h"
#include "fu-vli-usbhub-device.h"
#include "fu-vli-usbhub-pd-device.h"

struct _FuVliUsbhubPdDevice {
	FuDevice parent_instance;
	FuVliDeviceKind device_kind;
	guint32 pd_offset;
};

G_DEFINE_TYPE(FuVliUsbhubPdDevice, fu_vli_usbhub_pd_device, FU_TYPE_DEVICE)

static void
fu_vli_usbhub_pd_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);
	fwupd_codec_string_append(str,
				  idt,
				  "DeviceKind",
				  fu_vli_device_kind_to_string(self->device_kind));
	fwupd_codec_string_append_hex(str, idt, "FwOffset", self->pd_offset);
}

static gboolean
fu_vli_usbhub_pd_device_setup(FuDevice *device, GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);
	FuDevice *proxy;
	const gchar *name;
	guint32 fwver;
	gsize bufsz = FU_STRUCT_VLI_PD_HDR_SIZE;
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(FuStructVliPdHdr) st = NULL;

	/* legacy location */
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(proxy),
					  FU_VLI_USBHUB_FLASHMAP_ADDR_PD_LEGACY +
					      VLI_USBHUB_PD_FLASHMAP_ADDR_LEGACY,
					  buf,
					  bufsz,
					  error)) {
		g_prefix_error_literal(error, "failed to read legacy PD header: ");
		return FALSE;
	}
	st = fu_struct_vli_pd_hdr_parse(buf, bufsz, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* new location */
	if (fu_struct_vli_pd_hdr_get_vid(st) != 0x2109) {
		g_debug("PD VID was 0x%04x trying new location", fu_struct_vli_pd_hdr_get_vid(st));
		if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(proxy),
						  FU_VLI_USBHUB_FLASHMAP_ADDR_PD +
						      VLI_USBHUB_PD_FLASHMAP_ADDR,
						  buf,
						  bufsz,
						  error)) {
			g_prefix_error_literal(error, "failed to read PD header: ");
			return FALSE;
		}
		fu_struct_vli_pd_hdr_unref(st);
		st = fu_struct_vli_pd_hdr_parse(buf, bufsz, 0x0, error);
		if (st == NULL)
			return FALSE;
	}

	/* just empty space */
	fwver = fu_struct_vli_pd_hdr_get_fwver(st);
	if (fwver == G_MAXUINT32) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no PD device header found");
		return FALSE;
	}

	/* get version */
	self->device_kind = fu_vli_pd_common_guess_device_kind(fwver);
	if (self->device_kind == FU_VLI_DEVICE_KIND_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "PD version invalid [0x%x]",
			    fwver);
		return FALSE;
	}

	name = fu_vli_device_kind_to_string(self->device_kind);
	fu_device_set_name(device, name);

	/* use header to populate device info */
	fu_device_set_version_raw(device, fwver);

	/* add standard GUIDs in order of priority */
	fu_device_add_instance_u16(device, "VID", fu_struct_vli_pd_hdr_get_vid(st));
	fu_device_add_instance_u16(device, "PID", fu_struct_vli_pd_hdr_get_pid(st));
	fu_device_add_instance_u8(device, "APP", fwver & 0xff);
	fu_device_add_instance_strup(device, "DEV", name);
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "USB",
					      "VID",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "VLI",
					      "DEV",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "APP", NULL))
		return FALSE;

	/* ensure the quirk was applied */
	if (self->pd_offset == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no VliPdOffset quirk defined for %s",
			    fu_vli_device_kind_to_string(self->device_kind));
		return FALSE;
	}

	/* these have a backup section */
	if (self->pd_offset == FU_VLI_USBHUB_FLASHMAP_ADDR_PD)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_pd_device_check_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);
	FuVliDeviceKind device_kind;

	/* check is compatible with firmware */
	device_kind = fu_vli_pd_firmware_get_kind(FU_VLI_PD_FIRMWARE(firmware));
	if (self->device_kind != device_kind) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware incompatible, got %s, expected %s",
			    fu_vli_device_kind_to_string(device_kind),
			    fu_vli_device_kind_to_string(self->device_kind));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_vli_usbhub_pd_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);

	/* read */
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return NULL;
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	return fu_vli_device_spi_read(FU_VLI_DEVICE(proxy),
				      self->pd_offset,
				      fu_device_get_firmware_size_max(device),
				      progress,
				      error);
}

static gboolean
fu_vli_usbhub_pd_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);
	FuDevice *proxy;
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 78, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 22, NULL);

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase */
	buf = g_bytes_get_data(fw, &bufsz);
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_vli_device_spi_erase(FU_VLI_DEVICE(proxy),
				     self->pd_offset,
				     bufsz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_vli_device_spi_write(FU_VLI_DEVICE(proxy),
				     self->pd_offset,
				     buf,
				     bufsz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

/* reboot the parent FuVliUsbhubDevice if we update the FuVliUsbhubPdDevice */
static gboolean
fu_vli_usbhub_pd_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	return fu_device_attach_full(proxy, progress, error);
}

static void
fu_vli_usbhub_pd_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gboolean
fu_vli_usbhub_pd_device_set_quirk_kv(FuDevice *device,
				     const gchar *key,
				     const gchar *value,
				     GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);

	if (g_strcmp0(key, "VliPdOffset") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->pd_offset = tmp;
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static gchar *
fu_vli_usbhub_pd_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_vli_usbhub_pd_device_init(FuVliUsbhubPdDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_USB_HUB);
	fu_device_add_protocol(FU_DEVICE(self), "com.vli.usbhub");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_install_duration(FU_DEVICE(self), 15); /* seconds */
	fu_device_set_logical_id(FU_DEVICE(self), "PD");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_VLI_PD_FIRMWARE);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_VLI_USBHUB_DEVICE);
	fu_device_set_summary(FU_DEVICE(self), "USB-C power delivery device");
}

static void
fu_vli_usbhub_pd_device_class_init(FuVliUsbhubPdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_vli_usbhub_pd_device_to_string;
	device_class->setup = fu_vli_usbhub_pd_device_setup;
	device_class->attach = fu_vli_usbhub_pd_device_attach;
	device_class->dump_firmware = fu_vli_usbhub_pd_device_dump_firmware;
	device_class->write_firmware = fu_vli_usbhub_pd_device_write_firmware;
	device_class->check_firmware = fu_vli_usbhub_pd_device_check_firmware;
	device_class->convert_version = fu_vli_usbhub_pd_device_convert_version;
	device_class->set_quirk_kv = fu_vli_usbhub_pd_device_set_quirk_kv;
	device_class->set_progress = fu_vli_usbhub_pd_device_set_progress;
}

FuVliUsbhubPdDevice *
fu_vli_usbhub_pd_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_VLI_USBHUB_PD_DEVICE, "proxy", proxy, NULL);
}

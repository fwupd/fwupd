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
#include "fu-vli-usbhub-pd-device.h"

struct _FuVliUsbhubPdDevice {
	FuDevice parent_instance;
	FuVliDeviceKind device_kind;
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
	fwupd_codec_string_append_hex(str,
				      idt,
				      "FwOffset",
				      fu_vli_common_device_kind_get_offset(self->device_kind));
	fwupd_codec_string_append_hex(str,
				      idt,
				      "FwSize",
				      fu_vli_common_device_kind_get_size(self->device_kind));
}

static gboolean
fu_vli_usbhub_pd_device_setup(FuDevice *device, GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	const gchar *name;
	guint32 fwver;
	gsize bufsz = FU_STRUCT_VLI_PD_HDR_SIZE;
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(GByteArray) st = NULL;

	/* legacy location */
	if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(parent),
					  VLI_USBHUB_FLASHMAP_ADDR_PD_LEGACY +
					      VLI_USBHUB_PD_FLASHMAP_ADDR_LEGACY,
					  buf,
					  bufsz,
					  error)) {
		g_prefix_error(error, "failed to read legacy PD header: ");
		return FALSE;
	}
	st = fu_struct_vli_pd_hdr_parse(buf, bufsz, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* new location */
	if (fu_struct_vli_pd_hdr_get_vid(st) != 0x2109) {
		g_debug("PD VID was 0x%04x trying new location", fu_struct_vli_pd_hdr_get_vid(st));
		if (!fu_vli_device_spi_read_block(FU_VLI_DEVICE(parent),
						  VLI_USBHUB_FLASHMAP_ADDR_PD +
						      VLI_USBHUB_PD_FLASHMAP_ADDR,
						  buf,
						  bufsz,
						  error)) {
			g_prefix_error(error, "failed to read PD header: ");
			return FALSE;
		}
		g_byte_array_unref(st);
		st = fu_struct_vli_pd_hdr_parse(buf, bufsz, 0x0, error);
		if (st == NULL)
			return FALSE;
	}

	/* just empty space */
	fwver = fu_struct_vli_pd_hdr_get_fwver(st);
	if (fwver == G_MAXUINT32) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no PD device header found");
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
	fu_device_add_instance_str(device, "DEV", name);
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "USB",
					      "VID",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "APP", NULL))
		return FALSE;

	/* these have a backup section */
	if (fu_vli_common_device_kind_get_offset(self->device_kind) == VLI_USBHUB_FLASHMAP_ADDR_PD)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);

	/* success */
	return TRUE;
}

static gboolean
fu_vli_usbhub_pd_device_reload(FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open parent device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_vli_usbhub_pd_device_setup(device, error);
}

static FuFirmware *
fu_vli_usbhub_pd_device_prepare_firmware(FuDevice *device,
					 GInputStream *stream,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);
	FuVliDeviceKind device_kind;
	g_autoptr(FuFirmware) firmware = fu_vli_pd_firmware_new();

	/* check is compatible with firmware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	device_kind = fu_vli_pd_firmware_get_kind(FU_VLI_PD_FIRMWARE(firmware));
	if (self->device_kind != device_kind) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware incompatible, got %s, expected %s",
			    fu_vli_device_kind_to_string(device_kind),
			    fu_vli_device_kind_to_string(self->device_kind));
		return NULL;
	}

	/* we could check this against flags */
	g_info("parsed version: %s", fu_firmware_get_version(firmware));
	return g_steal_pointer(&firmware);
}

static GBytes *
fu_vli_usbhub_pd_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	FuVliUsbhubPdDevice *self = FU_VLI_USBHUB_PD_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return NULL;

	/* read */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	return fu_vli_device_spi_read(FU_VLI_DEVICE(parent),
				      fu_vli_common_device_kind_get_offset(self->device_kind),
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
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 78, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 22, NULL);

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	/* erase */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_vli_device_spi_erase(FU_VLI_DEVICE(parent),
				     fu_vli_common_device_kind_get_offset(self->device_kind),
				     bufsz,
				     fu_progress_get_child(progress),
				     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_vli_device_spi_write(FU_VLI_DEVICE(parent),
				     fu_vli_common_device_kind_get_offset(self->device_kind),
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
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_attach_full(parent, progress, error);
}

static gboolean
fu_vli_usbhub_pd_device_probe(FuDevice *device, GError **error)
{
	FuVliUsbhubDevice *parent = FU_VLI_USBHUB_DEVICE(fu_device_get_parent(device));
	fu_device_incorporate(device, FU_DEVICE(parent), FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	return TRUE;
}

static void
fu_vli_usbhub_pd_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_vli_usbhub_pd_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_vli_usbhub_pd_device_init(FuVliUsbhubPdDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), "usb-hub");
	fu_device_add_protocol(FU_DEVICE(self), "com.vli.usbhub");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_install_duration(FU_DEVICE(self), 15); /* seconds */
	fu_device_set_logical_id(FU_DEVICE(self), "PD");
	fu_device_set_summary(FU_DEVICE(self), "USB-C power delivery device");
}

static void
fu_vli_usbhub_pd_device_class_init(FuVliUsbhubPdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_vli_usbhub_pd_device_to_string;
	device_class->probe = fu_vli_usbhub_pd_device_probe;
	device_class->setup = fu_vli_usbhub_pd_device_setup;
	device_class->reload = fu_vli_usbhub_pd_device_reload;
	device_class->attach = fu_vli_usbhub_pd_device_attach;
	device_class->dump_firmware = fu_vli_usbhub_pd_device_dump_firmware;
	device_class->write_firmware = fu_vli_usbhub_pd_device_write_firmware;
	device_class->prepare_firmware = fu_vli_usbhub_pd_device_prepare_firmware;
	device_class->convert_version = fu_vli_usbhub_pd_device_convert_version;
	device_class->set_progress = fu_vli_usbhub_pd_device_set_progress;
}

FuDevice *
fu_vli_usbhub_pd_device_new(FuVliUsbhubDevice *parent)
{
	FuVliUsbhubPdDevice *self =
	    g_object_new(FU_TYPE_VLI_USBHUB_PD_DEVICE, "parent", parent, NULL);
	return FU_DEVICE(self);
}

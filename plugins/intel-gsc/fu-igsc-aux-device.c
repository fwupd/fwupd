/*
 * Copyright 2022 Intel, Inc
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR Apache-2.0
 */

#include "config.h"

#include "fu-igsc-aux-device.h"
#include "fu-igsc-aux-firmware.h"
#include "fu-igsc-device.h"

struct _FuIgscAuxDevice {
	FuDevice parent_instance;
	guint32 oem_version;
	guint16 major_version;
	guint16 major_vcn;
};

G_DEFINE_TYPE(FuIgscAuxDevice, fu_igsc_aux_device, FU_TYPE_DEVICE)

static void
fu_igsc_aux_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIgscAuxDevice *self = FU_IGSC_AUX_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "OemManufDataVersion", self->oem_version);
	fwupd_codec_string_append_hex(str, idt, "MajorVersion", self->major_version);
	fwupd_codec_string_append_hex(str, idt, "MajorVcn", self->major_vcn);
}

static gboolean
fu_igsc_aux_device_probe(FuDevice *device, GError **error)
{
	FuDevice *parent;

	/* from the self tests */
	parent = fu_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* add extra instance IDs */
	fu_device_add_instance_str(device,
				   "PART",
				   fu_device_has_private_flag(parent, FU_IGSC_DEVICE_FLAG_IS_WEDGED)
				       ? "FWDATA_RECOVERY"
				       : "FWDATA");
	if (!fu_device_build_instance_id(device, error, "PCI", "VEN", "DEV", "PART", NULL))
		return FALSE;
	return fu_device_build_instance_id(device,
					   error,
					   "PCI",
					   "VEN",
					   "DEV",
					   "SUBSYS",
					   "PART",
					   NULL);
}

static gboolean
fu_igsc_aux_device_setup(FuDevice *device, GError **error)
{
	FuIgscAuxDevice *self = FU_IGSC_AUX_DEVICE(device);
	FuIgscDevice *parent;
	g_autofree gchar *version = NULL;

	/* get version */
	parent = FU_IGSC_DEVICE(fu_device_get_parent(device, error));
	if (parent == NULL)
		return FALSE;
	if (!fu_igsc_device_get_aux_version(parent,
					    &self->oem_version,
					    &self->major_version,
					    &self->major_vcn,
					    error)) {
		g_prefix_error_literal(error, "failed to get aux version: ");
		return FALSE;
	}
	if (fu_device_has_private_flag(FU_DEVICE(parent), FU_IGSC_DEVICE_FLAG_IS_WEDGED)) {
		version = g_strdup("0.0");
	} else {
		version = g_strdup_printf("%u.%u", self->major_version, self->oem_version);
	}
	fu_device_set_version(device, version);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_igsc_aux_device_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FuProgress *progress,
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	FuIgscAuxDevice *self = FU_IGSC_AUX_DEVICE(device);
	FuIgscDevice *parent;
	g_autoptr(FuIgscAuxFirmware) firmware = FU_IGSC_AUX_FIRMWARE(fu_igsc_aux_firmware_new());

	/* parse container */
	if (!fu_firmware_parse_stream(FU_FIRMWARE(firmware), stream, 0x0, flags, error))
		return NULL;

	/* search the device list for a match */
	parent = FU_IGSC_DEVICE(fu_device_get_parent(device, error));
	if (parent == NULL)
		return NULL;
	if (!fu_igsc_aux_firmware_match_device(firmware,
					       fu_device_get_vid(FU_DEVICE(parent)),
					       fu_device_get_pid(FU_DEVICE(parent)),
					       fu_igsc_device_get_ssvid(parent),
					       fu_igsc_device_get_ssdid(parent),
					       error))
		return NULL;

	/* verify is compatible */
	if (fu_igsc_aux_firmware_get_major_version(firmware) != self->major_version) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "image is not for this product, got 0x%x, expected 0x%x",
			    fu_igsc_aux_firmware_get_major_version(firmware),
			    self->major_version);
		return NULL;
	}
	if (fu_igsc_aux_firmware_get_major_vcn(firmware) > self->major_vcn) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "image VCN is not compatible, got 0x%x, expected 0x%x",
			    fu_igsc_aux_firmware_get_major_vcn(firmware),
			    self->major_vcn);
		return NULL;
	}
	if (fu_igsc_aux_firmware_get_oem_version(firmware) <= self->oem_version) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid OEM version, got 0x%x, expected higher than 0x%x",
			    fu_igsc_aux_firmware_get_oem_version(firmware),
			    self->oem_version);
		return NULL;
	}

	/* success, but return container, not CPD */
	return FU_FIRMWARE(g_steal_pointer(&firmware));
}

static gboolean
fu_igsc_aux_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuIgscDevice *parent;
	g_autoptr(GBytes) fw_info = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* get image */
	fw_info =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_IFWI_FPT_FIRMWARE_IDX_INFO, error);
	if (fw_info == NULL)
		return FALSE;
	stream_payload =
	    fu_firmware_get_image_by_idx_stream(firmware, FU_IFWI_FPT_FIRMWARE_IDX_SDTA, error);
	if (stream_payload == NULL)
		return FALSE;
	parent = FU_IGSC_DEVICE(fu_device_get_parent(device, error));
	if (parent == NULL)
		return FALSE;
	return fu_igsc_device_write_blob(parent,
					 FU_IGSC_FWU_HECI_PAYLOAD_TYPE_FWDATA,
					 fw_info,
					 stream_payload,
					 progress,
					 error);
}

static void
fu_igsc_aux_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_igsc_aux_device_init(FuIgscAuxDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_IGSC_DEVICE);
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.gsc");
	fu_device_set_logical_id(FU_DEVICE(self), "fw-data");
	fu_device_set_name(FU_DEVICE(self), "Data");
}

static void
fu_igsc_aux_device_class_init(FuIgscAuxDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->set_progress = fu_igsc_aux_device_set_progress;
	device_class->to_string = fu_igsc_aux_device_to_string;
	device_class->probe = fu_igsc_aux_device_probe;
	device_class->setup = fu_igsc_aux_device_setup;
	device_class->prepare_firmware = fu_igsc_aux_device_prepare_firmware;
	device_class->write_firmware = fu_igsc_aux_device_write_firmware;
}

FuIgscAuxDevice *
fu_igsc_aux_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_IGSC_AUX_DEVICE, "proxy", proxy, NULL);
}

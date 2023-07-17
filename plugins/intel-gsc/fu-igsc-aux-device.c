/*
 * Copyright (C) 2022 Intel, Inc
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR Apache-2.0
 */

#include "config.h"

#include "fu-igsc-aux-device.h"
#include "fu-igsc-aux-firmware.h"
#include "fu-igsc-device.h"
#include "fu-igsc-heci.h"

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
	fu_string_append_kx(str, idt, "OemManufDataVersion", self->oem_version);
	fu_string_append_kx(str, idt, "MajorVersion", self->major_version);
	fu_string_append_kx(str, idt, "MajorVcn", self->major_vcn);
}

static gboolean
fu_igsc_aux_device_probe(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);

	/* fix name */
	if (parent != NULL) {
		g_autofree gchar *name = NULL;
		name = g_strdup_printf("%s Data", fu_device_get_name(parent));
		fu_device_set_name(device, name);
	}

	/* add extra instance IDs */
	fu_device_add_instance_str(device, "PART", "FWDATA");
	if (!fu_device_build_instance_id(device, error, "MEI", "VEN", "DEV", "PART", NULL))
		return FALSE;
	return fu_device_build_instance_id(device,
					   error,
					   "MEI",
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
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device));
	g_autofree gchar *version = NULL;

	/* get version */
	if (!fu_igsc_device_get_aux_version(igsc_parent,
					    &self->oem_version,
					    &self->major_version,
					    &self->major_vcn,
					    error)) {
		g_prefix_error(error, "failed to get aux version: ");
		return FALSE;
	}
	version = g_strdup_printf("%u.%x", self->major_version, self->oem_version);
	fu_device_set_version(device, version);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_igsc_aux_device_prepare_firmware(FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuIgscAuxDevice *self = FU_IGSC_AUX_DEVICE(device);
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device));
	g_autoptr(FuIgscAuxFirmware) firmware = FU_IGSC_AUX_FIRMWARE(fu_igsc_aux_firmware_new());

	/* parse container */
	if (!fu_firmware_parse(FU_FIRMWARE(firmware), fw, flags, error))
		return NULL;

	/* search the device list for a match */
	if (!fu_igsc_aux_firmware_match_device(firmware,
					       self->major_version,
					       self->major_vcn,
					       fu_igsc_device_get_ssvid(igsc_parent),
					       fu_igsc_device_get_ssdid(igsc_parent),
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
			    "invalid manufacturer data version, got 0x%x, expected 0x%x",
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
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device));
	g_autoptr(GBytes) fw_info = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* get image */
	fw_info =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_IFWI_FPT_FIRMWARE_IDX_INFO, error);
	if (fw_info == NULL)
		return FALSE;
	fw_payload =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_IFWI_FPT_FIRMWARE_IDX_SDTA, error);
	if (fw_payload == NULL)
		return FALSE;
	return fu_igsc_device_write_blob(igsc_parent,
					 GSC_FWU_HECI_PAYLOAD_TYPE_FWDATA,
					 fw_info,
					 fw_payload,
					 progress,
					 error);
}

static gboolean
fu_igsc_aux_device_prepare(FuDevice *device,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	/* set PCI power policy */
	return fu_device_prepare(fu_device_get_parent(device), progress, flags, error);
}

static gboolean
fu_igsc_aux_device_cleanup(FuDevice *device,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	/* set PCI power policy */
	return fu_device_cleanup(fu_device_get_parent(device), progress, flags, error);
}

static void
fu_igsc_aux_device_init(FuIgscAuxDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_AABB_CCDD);
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.gsc");
	fu_device_set_logical_id(FU_DEVICE(self), "fw-data");
}

static void
fu_igsc_aux_device_class_init(FuIgscAuxDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_igsc_aux_device_to_string;
	klass_device->probe = fu_igsc_aux_device_probe;
	klass_device->setup = fu_igsc_aux_device_setup;
	klass_device->prepare_firmware = fu_igsc_aux_device_prepare_firmware;
	klass_device->write_firmware = fu_igsc_aux_device_write_firmware;
	klass_device->prepare = fu_igsc_aux_device_prepare;
	klass_device->cleanup = fu_igsc_aux_device_cleanup;
}

FuIgscAuxDevice *
fu_igsc_aux_device_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_IGSC_AUX_DEVICE, "context", ctx, NULL);
}

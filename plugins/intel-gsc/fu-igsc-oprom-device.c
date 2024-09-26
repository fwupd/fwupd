/*
 * Copyright 2022 Intel, Inc
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR Apache-2.0
 */

#include "config.h"

#include "fu-igsc-device.h"
#include "fu-igsc-oprom-device.h"
#include "fu-igsc-oprom-firmware.h"
#include "fu-igsc-struct.h"

struct _FuIgscOpromDevice {
	FuDevice parent_instance;
	enum gsc_fwu_heci_payload_type payload_type;
	enum gsc_fwu_heci_partition_version partition_version;
	guint16 major_version;
};

G_DEFINE_TYPE(FuIgscOpromDevice, fu_igsc_oprom_device, FU_TYPE_DEVICE)

static void
fu_igsc_oprom_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "PayloadType", self->payload_type);
	fwupd_codec_string_append_hex(str, idt, "PartitionVersion", self->partition_version);
}

static gboolean
fu_igsc_oprom_device_probe(FuDevice *device, GError **error)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	FuDevice *parent = fu_device_get_parent(device);
	g_autofree gchar *name = NULL;

	/* set strings now we know the type */
	if (self->payload_type == GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_CODE) {
		self->partition_version = GSC_FWU_HECI_PART_VERSION_OPROM_CODE;
		fu_device_add_instance_str(device, "PART", "OPROMCODE");
		fu_device_set_logical_id(FU_DEVICE(self), "oprom-code");
		if (parent != NULL) {
			name = g_strdup_printf("%s OptionROM Code", fu_device_get_name(parent));
			fu_device_set_name(FU_DEVICE(self), name);
		}
	} else if (self->payload_type == GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_DATA) {
		self->partition_version = GSC_FWU_HECI_PART_VERSION_OPROM_DATA;
		fu_device_add_instance_str(device, "PART", "OPROMDATA");
		fu_device_set_logical_id(FU_DEVICE(self), "oprom-data");
		if (parent != NULL) {
			name = g_strdup_printf("%s OptionROM Data", fu_device_get_name(parent));
			fu_device_set_name(FU_DEVICE(self), name);
		}
	}

	/* add extra instance IDs */
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
fu_igsc_oprom_device_setup(FuDevice *device, GError **error)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device));
	guint8 buf[8] = {0x0};
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* get version */
	if (!fu_igsc_device_get_version_raw(igsc_parent,
					    self->partition_version,
					    buf,
					    sizeof(buf),
					    error)) {
		g_prefix_error(error, "failed to get oprom version: ");
		return FALSE;
	}
	st = fu_struct_igsc_oprom_version_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	self->major_version = fu_struct_igsc_oprom_version_get_major(st);
	version = g_strdup_printf("%u.%u.%u.%u",
				  self->major_version,
				  fu_struct_igsc_oprom_version_get_minor(st),
				  fu_struct_igsc_oprom_version_get_hotfix(st),
				  fu_struct_igsc_oprom_version_get_build(st));
	fu_device_set_version(device, version);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_igsc_oprom_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device));
	guint16 vid = fu_device_get_vid(FU_DEVICE(igsc_parent));
	guint16 pid = fu_device_get_pid(FU_DEVICE(igsc_parent));
	guint16 subsys_vendor_id = fu_igsc_device_get_ssvid(igsc_parent);
	guint16 subsys_device_id = fu_igsc_device_get_ssvid(igsc_parent);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) fw_linear = fu_linear_firmware_new(FU_TYPE_IGSC_OPROM_FIRMWARE);

	/* parse container */
	if (!fu_firmware_parse_stream(fw_linear, stream, 0x0, flags, error))
		return NULL;

	/* get correct image */
	firmware = fu_firmware_get_image_by_idx(fw_linear, self->payload_type, error);
	if (firmware == NULL)
		return NULL;

	/* major numbers must be the same, unless the device's major is zero,
	 * because some platforms may come originally with 0 major number */
	if (fu_igsc_oprom_firmware_get_major_version(FU_IGSC_OPROM_FIRMWARE(firmware)) !=
		self->major_version &&
	    self->major_version != 0) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "image major version is not compatible, got 0x%x, expected 0x%x",
		    fu_igsc_oprom_firmware_get_major_version(FU_IGSC_OPROM_FIRMWARE(firmware)),
		    self->major_version);
		return NULL;
	}

	/* If oprom_code_devid_enforcement is set to True:
	 *    The update is accepted only if the update file contains a Device IDs allowlist
	 *    and the card's {VID, DID, SSVID, SSDID} is in the update file's Device IDs allowlist.
	 * If the flag doesn't exist or is False:
	 *    The update is accepted only if the update file does not contain a Device ID allowlist
	 */
	if (self->payload_type == GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_CODE) {
		if (fu_igsc_device_get_oprom_code_devid_enforcement(igsc_parent)) {
			if (!fu_igsc_oprom_firmware_match_device(FU_IGSC_OPROM_FIRMWARE(firmware),
								 vid,
								 pid,
								 subsys_vendor_id,
								 subsys_device_id,
								 error))
				return NULL;
		} else {
			if (fu_igsc_oprom_firmware_has_allowlist(
				FU_IGSC_OPROM_FIRMWARE(firmware))) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "device is not enforcing devid match, but "
						    "firmware provided allowlist");
				return NULL;
			}
		}
	}

	/* If the Device IDs allowlist (0x37) exists in the update image:
	 *    The update is accepted only if the card's {VID, DID, SSVID, SSDID}
	 *    is in the update image's Device IDs allowlist.
	 * If the Device IDs allowlist (0x37) doesn't exist in the update image:
	 *    The update is accepted only if the card's SSVID and SSDID are zero.
	 */
	if (self->payload_type == GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_DATA) {
		if (fu_igsc_oprom_firmware_has_allowlist(FU_IGSC_OPROM_FIRMWARE(firmware))) {
			if (!fu_igsc_oprom_firmware_match_device(FU_IGSC_OPROM_FIRMWARE(firmware),
								 vid,
								 pid,
								 subsys_vendor_id,
								 subsys_device_id,
								 error))
				return NULL;
		} else {
			if (subsys_vendor_id != 0x0 || subsys_device_id != 0x0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "firmware does not specify allowlist and SSVID "
						    "and SSDID are nonzero");
				return NULL;
			}
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_igsc_oprom_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device));
	g_autoptr(GInputStream) stream_payload = NULL;

	/* get image */
	stream_payload = fu_firmware_get_stream(firmware, error);
	if (stream_payload == NULL)
		return FALSE;

	/* OPROM image doesn't require metadata */
	return fu_igsc_device_write_blob(igsc_parent,
					 self->payload_type,
					 NULL,
					 stream_payload,
					 progress,
					 error);
}

static gboolean
fu_igsc_oprom_device_prepare(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	/* set PCI power policy */
	return fu_device_prepare(fu_device_get_parent(device), progress, flags, error);
}

static gboolean
fu_igsc_oprom_device_cleanup(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	/* set PCI power policy */
	return fu_device_cleanup(fu_device_get_parent(device), progress, flags, error);
}

static void
fu_igsc_oprom_device_init(FuIgscOpromDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.gsc");
}

static void
fu_igsc_oprom_device_class_init(FuIgscOpromDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_igsc_oprom_device_to_string;
	device_class->probe = fu_igsc_oprom_device_probe;
	device_class->setup = fu_igsc_oprom_device_setup;
	device_class->prepare_firmware = fu_igsc_oprom_device_prepare_firmware;
	device_class->write_firmware = fu_igsc_oprom_device_write_firmware;
	device_class->prepare = fu_igsc_oprom_device_prepare;
	device_class->cleanup = fu_igsc_oprom_device_cleanup;
}

FuIgscOpromDevice *
fu_igsc_oprom_device_new(FuContext *ctx, enum gsc_fwu_heci_payload_type payload_type)
{
	FuIgscOpromDevice *self = g_object_new(FU_TYPE_IGSC_OPROM_DEVICE, "context", ctx, NULL);
	self->payload_type = payload_type;
	return FU_IGSC_OPROM_DEVICE(self);
}

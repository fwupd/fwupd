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
	FuIgscFwuHeciPayloadType payload_type;
	FuIgscFwuHeciPartitionVersion partition_version;
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
	FuDevice *parent = fu_device_get_parent(device, error);

	/* from the self tests */
	if (parent == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no parent FuIgscDevice");
		return FALSE;
	}

	/* set strings now we know the type */
	if (self->payload_type == FU_IGSC_FWU_HECI_PAYLOAD_TYPE_OPROM_CODE) {
		self->partition_version = FU_IGSC_FWU_HECI_PARTITION_VERSION_OPROM_CODE;
		fu_device_add_instance_str(
		    device,
		    "PART",
		    fu_device_has_private_flag(parent, FU_IGSC_DEVICE_FLAG_IS_WEDGED)
			? "OPROMCODE_RECOVERY"
			: "OPROMCODE");
		fu_device_set_logical_id(FU_DEVICE(self), "oprom-code");
		fu_device_set_name(FU_DEVICE(self), "OptionROM Code");
	} else if (self->payload_type == FU_IGSC_FWU_HECI_PAYLOAD_TYPE_OPROM_DATA) {
		self->partition_version = FU_IGSC_FWU_HECI_PARTITION_VERSION_OPROM_DATA;
		fu_device_add_instance_str(
		    device,
		    "PART",
		    fu_device_has_private_flag(parent, FU_IGSC_DEVICE_FLAG_IS_WEDGED)
			? "OPROMDATA_RECOVERY"
			: "OPROMDATA");
		fu_device_set_logical_id(FU_DEVICE(self), "oprom-data");
		fu_device_set_name(FU_DEVICE(self), "OptionROM Data");
	}

	/* add extra instance IDs */
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
fu_igsc_oprom_device_setup(FuDevice *device, GError **error)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device, error));
	guint8 buf[FU_STRUCT_IGSC_OPROM_VERSION_SIZE] = {0x0};
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructIgscOpromVersion) st = NULL;

	/* get version */
	if (!fu_igsc_device_get_version_raw(igsc_parent,
					    self->partition_version,
					    buf,
					    sizeof(buf),
					    error)) {
		g_prefix_error_literal(error, "failed to get oprom version: ");
		return FALSE;
	}
	st = fu_struct_igsc_oprom_version_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	self->major_version = fu_struct_igsc_oprom_version_get_major(st);
	if (fu_device_has_private_flag(FU_DEVICE(igsc_parent), FU_IGSC_DEVICE_FLAG_IS_WEDGED)) {
		version = g_strdup("0.0");
	} else {
		version = g_strdup_printf("%u.%u.%u.%u",
					  self->major_version,
					  fu_struct_igsc_oprom_version_get_minor(st),
					  fu_struct_igsc_oprom_version_get_hotfix(st),
					  fu_struct_igsc_oprom_version_get_build(st));
	}
	fu_device_set_version(device, version);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_igsc_oprom_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	FuIgscDevice *igsc_parent = FU_IGSC_DEVICE(fu_device_get_parent(device, error));
	g_autoptr(GInputStream) stream_igsc = NULL;
	g_autoptr(FuFirmware) firmware_igsc = g_object_new(FU_TYPE_IGSC_OPROM_FIRMWARE, NULL);
	g_autoptr(FuFirmware) firmware_oprom = NULL;
	g_autoptr(FuFirmware) fw_linear = fu_linear_firmware_new(FU_TYPE_OPROM_FIRMWARE);

	/* sanity check */
	if (igsc_parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no IGSC parent");
		return NULL;
	}

	/* parse container */
	if (!fu_firmware_parse_stream(fw_linear, stream, 0x0, flags, error))
		return NULL;

	/* get correct image */
	firmware_oprom = fu_firmware_get_image_by_idx(
	    fw_linear,
	    self->payload_type == FU_IGSC_FWU_HECI_PAYLOAD_TYPE_OPROM_CODE ? FU_IGSC_OPROM_IDX_CODE
									   : FU_IGSC_OPROM_IDX_DATA,
	    error);
	if (firmware_oprom == NULL)
		return NULL;

	/* reparse with more specific requirements */
	stream_igsc = fu_firmware_get_stream(firmware_oprom, error);
	if (stream_igsc == NULL)
		return NULL;
	if (!fu_firmware_parse_stream(firmware_igsc, stream_igsc, 0x0, flags, error))
		return NULL;

	/* major numbers must be the same, unless the device's major is zero,
	 * because some platforms may come originally with 0 major number */
	if (fu_igsc_oprom_firmware_get_major_version(FU_IGSC_OPROM_FIRMWARE(firmware_igsc)) !=
		self->major_version &&
	    self->major_version != 0) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "image major version is not compatible, got 0x%x, expected 0x%x",
		    fu_igsc_oprom_firmware_get_major_version(FU_IGSC_OPROM_FIRMWARE(firmware_igsc)),
		    self->major_version);
		return NULL;
	}

	/* If oprom_code_devid_enforcement is set to True:
	 *    The update is accepted only if the update file contains a Device IDs allowlist
	 *    and the card's {VID, DID, SSVID, SSDID} is in the update file's Device IDs allowlist.
	 * If the flag doesn't exist or is False:
	 *    The update is accepted only if the update file does not contain a Device ID allowlist
	 */
	if (self->payload_type == FU_IGSC_FWU_HECI_PAYLOAD_TYPE_OPROM_CODE) {
		if (fu_igsc_device_get_oprom_code_devid_enforcement(igsc_parent)) {
			if (!fu_igsc_oprom_firmware_match_device(
				FU_IGSC_OPROM_FIRMWARE(firmware_igsc),
				fu_device_get_vid(FU_DEVICE(igsc_parent)),
				fu_device_get_pid(FU_DEVICE(igsc_parent)),
				fu_igsc_device_get_ssvid(igsc_parent),
				fu_igsc_device_get_ssdid(igsc_parent),
				error))
				return NULL;
		} else {
			if (fu_igsc_oprom_firmware_has_allowlist(
				FU_IGSC_OPROM_FIRMWARE(firmware_igsc))) {
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
	if (self->payload_type == FU_IGSC_FWU_HECI_PAYLOAD_TYPE_OPROM_DATA) {
		if (fu_igsc_oprom_firmware_has_allowlist(FU_IGSC_OPROM_FIRMWARE(firmware_igsc))) {
			if (!fu_igsc_oprom_firmware_match_device(
				FU_IGSC_OPROM_FIRMWARE(firmware_igsc),
				fu_device_get_vid(FU_DEVICE(igsc_parent)),
				fu_device_get_pid(FU_DEVICE(igsc_parent)),
				fu_igsc_device_get_ssvid(igsc_parent),
				fu_igsc_device_get_ssdid(igsc_parent),
				error))
				return NULL;
		} else {
			if (fu_igsc_device_get_ssvid(igsc_parent) != 0x0 ||
			    fu_igsc_device_get_ssdid(igsc_parent) != 0x0) {
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
	return g_steal_pointer(&fw_linear);
}

static gboolean
fu_igsc_oprom_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuIgscOpromDevice *self = FU_IGSC_OPROM_DEVICE(device);
	FuIgscDevice *parent;
	g_autoptr(FuStructIgscFwuHeciImageMetadata) st_md = NULL;
	g_autoptr(GBytes) fw_info = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* get image, with no padding bytes */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	partial_stream =
	    fu_partial_input_stream_new(stream, 0x0, fu_firmware_get_size(firmware), error);
	if (partial_stream == NULL)
		return FALSE;

	/* weirdly, this is just empty data */
	st_md = fu_struct_igsc_fwu_heci_image_metadata_new();
	fu_struct_igsc_fwu_heci_image_metadata_set_version_format(st_md, 0x0);
	fw_info = fu_struct_igsc_fwu_heci_image_metadata_to_bytes(st_md);

	/* OPROM image doesn't require metadata */
	parent = FU_IGSC_DEVICE(fu_device_get_parent(device, error));
	if (parent == NULL)
		return FALSE;
	return fu_igsc_device_write_blob(parent,
					 self->payload_type,
					 fw_info,
					 partial_stream,
					 progress,
					 error);
}

static void
fu_igsc_oprom_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_igsc_oprom_device_init(FuIgscOpromDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_IGSC_DEVICE);
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.gsc");
}

static void
fu_igsc_oprom_device_class_init(FuIgscOpromDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->set_progress = fu_igsc_oprom_device_set_progress;
	device_class->to_string = fu_igsc_oprom_device_to_string;
	device_class->probe = fu_igsc_oprom_device_probe;
	device_class->setup = fu_igsc_oprom_device_setup;
	device_class->prepare_firmware = fu_igsc_oprom_device_prepare_firmware;
	device_class->write_firmware = fu_igsc_oprom_device_write_firmware;
}

FuIgscOpromDevice *
fu_igsc_oprom_device_new(FuDevice *proxy, FuIgscFwuHeciPayloadType payload_type)
{
	FuIgscOpromDevice *self = g_object_new(FU_TYPE_IGSC_OPROM_DEVICE, "proxy", proxy, NULL);
	self->payload_type = payload_type;
	return FU_IGSC_OPROM_DEVICE(self);
}

/*#
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-cfu-module.h"
#include "fu-cfu-struct.h"

struct _FuCfuModule {
	FuDevice parent_instance;
	guint8 component_id;
	guint8 bank;
};

G_DEFINE_TYPE(FuCfuModule, fu_cfu_module, FU_TYPE_DEVICE)

static void
fu_cfu_module_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCfuModule *self = FU_CFU_MODULE(device);
	fu_string_append_kx(str, idt, "ComponentId", self->component_id);
	fu_string_append_kx(str, idt, "Bank", self->bank);
}

guint8
fu_cfu_module_get_component_id(FuCfuModule *self)
{
	return self->component_id;
}

gboolean
fu_cfu_module_setup(FuCfuModule *self, const guint8 *buf, gsize bufsz, gsize offset, GError **error)
{
	FuDevice *device = FU_DEVICE(self);
	FuDevice *parent = fu_device_get_proxy(device);
	g_autofree gchar *logical_id = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_cfu_get_version_rsp_component_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;

	/* these GUIDs may cause the name or version-format to be overwritten */
	self->component_id = fu_struct_cfu_get_version_rsp_component_get_component_id(st);
	fu_device_add_instance_u8(device, "CID", self->component_id);
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "CID", NULL))
		return FALSE;

	/* bank */
	self->bank = fu_struct_cfu_get_version_rsp_component_get_flags(st) & 0b11;
	fu_device_add_instance_u4(device, "BANK", self->bank);
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "CID", "BANK", NULL))
		return FALSE;

	/* set name, if not already set using a quirk */
	if (fu_device_get_name(device) == NULL) {
		g_autofree gchar *name = NULL;
		name = g_strdup_printf("%s (0x%02X:0x%02x)",
				       fu_device_get_name(parent),
				       self->component_id,
				       self->bank);
		fu_device_set_name(device, name);
	}

	/* version */
	fu_device_set_version_u32(device,
				  fu_struct_cfu_get_version_rsp_component_get_fw_version(st));

	/* logical ID */
	logical_id = g_strdup_printf("CID:0x%02x,BANK:0x%02x", self->component_id, self->bank);
	fu_device_set_logical_id(device, logical_id);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_cfu_module_prepare_firmware(FuDevice *device,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) firmware_archive = fu_archive_firmware_new();
	g_autoptr(FuFirmware) fw_offer = NULL;
	g_autoptr(FuFirmware) fw_payload = NULL;
	g_autoptr(FuFirmware) offer = fu_cfu_offer_new();
	g_autoptr(FuFirmware) payload = fu_cfu_payload_new();
	g_autoptr(GBytes) blob_offer = NULL;
	g_autoptr(GBytes) blob_payload = NULL;

	/* parse archive */
	if (!fu_firmware_parse(firmware_archive, fw, flags, error))
		return NULL;

	/* offer */
	fw_offer = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware_archive),
							 "*.offer.bin",
							 error);
	if (fw_offer == NULL)
		return NULL;
	blob_offer = fu_firmware_get_bytes(fw_offer, NULL);
	if (blob_offer == NULL)
		return NULL;
	if (!fu_firmware_parse(offer, blob_offer, flags, error))
		return NULL;
	fu_firmware_set_id(offer, FU_FIRMWARE_ID_HEADER);
	fu_firmware_add_image(firmware, offer);

	/* payload */
	fw_payload = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware_archive),
							   "*.payload.bin",
							   error);
	if (fw_payload == NULL)
		return NULL;
	blob_payload = fu_firmware_get_bytes(fw_payload, NULL);
	if (blob_payload == NULL)
		return NULL;
	if (!fu_firmware_parse(payload, blob_payload, flags, error))
		return NULL;
	fu_firmware_set_id(payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, payload);

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_cfu_module_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDevice *proxy;
	FuDeviceClass *klass_proxy;

	/* process by the parent */
	proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no proxy device assigned");
		return FALSE;
	}
	klass_proxy = FU_DEVICE_GET_CLASS(proxy);
	return klass_proxy->write_firmware(proxy, firmware, progress, flags, error);
}

static void
fu_cfu_module_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_cfu_module_init(FuCfuModule *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.cfu");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_SURFACE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ARCHIVE_FIRMWARE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
}

static void
fu_cfu_module_class_init(FuCfuModuleClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_cfu_module_to_string;
	klass_device->prepare_firmware = fu_cfu_module_prepare_firmware;
	klass_device->write_firmware = fu_cfu_module_write_firmware;
	klass_device->set_progress = fu_cfu_module_set_progress;
}

FuCfuModule *
fu_cfu_module_new(FuDevice *parent)
{
	FuCfuModule *self;
	self = g_object_new(FU_TYPE_CFU_MODULE, "proxy", parent, "parent", parent, NULL);
	return self;
}

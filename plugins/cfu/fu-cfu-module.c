/*#
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cfu-module.h"

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
	fu_common_string_append_kx(str, idt, "ComponentId", self->component_id);
	fu_common_string_append_kx(str, idt, "Bank", self->bank);
}

guint8
fu_cfu_module_get_component_id(FuCfuModule *self)
{
	return self->component_id;
}

guint8
fu_cfu_module_get_bank(FuCfuModule *self)
{
	return self->bank;
}

gboolean
fu_cfu_module_setup(FuCfuModule *self, const guint8 *buf, gsize bufsz, gsize offset, GError **error)
{
	FuDevice *parent = fu_device_get_proxy(FU_DEVICE(self));
	guint32 version_raw = 0;
	guint8 tmp = 0;
	g_autofree gchar *instance_id0 = NULL;
	g_autofree gchar *instance_id1 = NULL;
	g_autofree gchar *instance_id2 = NULL;
	g_autofree gchar *logical_id = NULL;
	g_autofree gchar *version = NULL;

	/* component ID */
	if (!fu_common_read_uint8_safe(buf, bufsz, offset + 0x5, &self->component_id, error))
		return FALSE;

	/* these GUIDs may cause the name or version-format to be overwritten */
	instance_id0 = g_strdup_printf("HIDRAW\\VEN_%04X&DEV_%04X",
				       fu_udev_device_get_vendor(FU_UDEV_DEVICE(parent)),
				       fu_udev_device_get_model(FU_UDEV_DEVICE(parent)));
	fu_device_add_instance_id(FU_DEVICE(self), instance_id0);
	instance_id1 = g_strdup_printf("HIDRAW\\VEN_%04X&DEV_%04X&CID_%02X",
				       fu_udev_device_get_vendor(FU_UDEV_DEVICE(parent)),
				       fu_udev_device_get_model(FU_UDEV_DEVICE(parent)),
				       self->component_id);
	fu_device_add_instance_id(FU_DEVICE(self), instance_id1);

	/* bank */
	if (!fu_common_read_uint8_safe(buf, bufsz, offset + 0x4, &tmp, error))
		return FALSE;
	self->bank = tmp & 0b11;
	instance_id2 = g_strdup_printf("HIDRAW\\VEN_%04X&DEV_%04X&CID_%02X&BANK_%01X",
				       fu_udev_device_get_vendor(FU_UDEV_DEVICE(parent)),
				       fu_udev_device_get_model(FU_UDEV_DEVICE(parent)),
				       self->component_id,
				       self->bank);
	fu_device_add_instance_id(FU_DEVICE(self), instance_id2);

	/* set name, if not already set using a quirk */
	if (fu_device_get_name(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *name = NULL;
		name = g_strdup_printf("%s (0x%02X:0x%02x)",
				       fu_device_get_name(parent),
				       self->component_id,
				       self->bank);
		fu_device_set_name(FU_DEVICE(self), name);
	}

	/* version */
	if (!fu_common_read_uint32_safe(buf, bufsz, offset, &version_raw, G_LITTLE_ENDIAN, error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	version = fu_common_version_from_uint32(version_raw,
						fu_device_get_version_format(FU_DEVICE(self)));

	/* logical ID */
	logical_id = g_strdup_printf("CID:0x%02x,BANK:0x%02x", self->component_id, self->bank);
	fu_device_set_logical_id(FU_DEVICE(self), logical_id);

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
	g_autoptr(FuFirmware) offer = fu_cfu_offer_new();
	g_autoptr(FuFirmware) payload = fu_cfu_payload_new();
	g_autoptr(GBytes) fw_offset = NULL;

	/* offer */
	if (!fu_firmware_parse(offer, fw, flags, error))
		return NULL;
	fu_firmware_set_id(offer, FU_FIRMWARE_ID_HEADER);
	fu_firmware_add_image(firmware, offer);

	/* payload */
	fw_offset = fu_common_bytes_new_offset(fw, 0x10, g_bytes_get_size(fw) - 0x10, error);
	if (fw_offset == NULL)
		return NULL;
	if (!fu_firmware_parse(payload, fw_offset, flags, error))
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
	g_autoptr(GBytes) fw = NULL;

	/* get the whole image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* process by the parent */
	proxy = fu_device_get_proxy(device);
	if (proxy == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no proxy device assigned");
		return FALSE;
	}
	return fu_device_write_firmware(proxy, fw, progress, flags, error);
}

static void
fu_cfu_module_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2);	/* reload */
}

static void
fu_cfu_module_init(FuCfuModule *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.cfu");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
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
	self = g_object_new(FU_TYPE_CFU_MODULE,
			    "ctx",
			    fu_device_get_context(parent),
			    "proxy",
			    parent,
			    NULL);
	return self;
}

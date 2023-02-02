/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ccgx-dmc-devx-device.h"

#define DMC_FW_WRITE_STATUS_RETRY_COUNT	   3
#define DMC_FW_WRITE_STATUS_RETRY_DELAY_MS 30

struct _FuCcgxDmcDevxDevice {
	FuDevice parent_instance;
	DmcDevxStatus status;
};

G_DEFINE_TYPE(FuCcgxDmcDevxDevice, fu_ccgx_dmc_devx_device, FU_TYPE_DEVICE)

static gchar *
fu_ccgx_dmc_devx_status_version_dmc_bfw(DmcDevxStatus *status, gsize offset)
{
	return g_strdup_printf("%u.%u.%u.%u",
			       status->fw_version[offset + 3] >> 4,
			       status->fw_version[offset + 3] & 0xFu,
			       status->fw_version[offset + 2],
			       fu_memread_uint16(status->fw_version + offset, G_LITTLE_ENDIAN));
}

static gchar *
fu_ccgx_dmc_devx_status_version_dmc_app(DmcDevxStatus *status, gsize offset)
{
	return g_strdup_printf("%u.%u.%u",
			       status->fw_version[offset + 4 + 3] >> 4,
			       status->fw_version[offset + 4 + 3] & 0xFu,
			       status->fw_version[offset + 4 + 2]);
}

static gchar *
fu_ccgx_dmc_devx_status_version_hx3(DmcDevxStatus *status, gsize offset)
{
	return g_strdup_printf("%u.%u.%u",
			       status->fw_version[offset + 4 + 3],
			       status->fw_version[offset + 4 + 2],
			       status->fw_version[offset + 4 + 1]);
}

static void
fu_ccgx_dmc_devx_device_hexver_to_string(FuCcgxDmcDevxDevice *self,
					 const gchar *kind,
					 gsize offset,
					 guint idt,
					 GString *str)
{
	DmcDevxStatus *status = &self->status;
	g_autofree gchar *key = g_strdup_printf("FwVersion[%s]", kind);
	g_autofree gchar *val =
	    fu_version_from_uint64(fu_memread_uint64(status->fw_version + offset, G_LITTLE_ENDIAN),
				   FWUPD_VERSION_FORMAT_HEX);
	fu_string_append(str, idt, key, val);
}

static void
fu_ccgx_dmc_devx_device_hx3ver_to_string(FuCcgxDmcDevxDevice *self,
					 const gchar *kind,
					 gsize offset,
					 guint idt,
					 GString *str)
{
	DmcDevxStatus *status = &self->status;
	g_autofree gchar *key = g_strdup_printf("FwVersion[%s]", kind);
	g_autofree gchar *val = fu_ccgx_dmc_devx_status_version_hx3(status, offset);
	fu_string_append(str, idt, key, val);
}

static void
fu_ccgx_dmc_devx_device_dmcver_to_string(FuCcgxDmcDevxDevice *self,
					 const gchar *kind,
					 gsize offset,
					 guint idt,
					 GString *str)
{
	DmcDevxStatus *status = &self->status;
	g_autofree gchar *key = g_strdup_printf("FwVersion[%s]", kind);
	g_autofree gchar *bfw_val = fu_ccgx_dmc_devx_status_version_dmc_bfw(status, offset);
	g_autofree gchar *app_val = fu_ccgx_dmc_devx_status_version_dmc_app(status, offset);
	g_autofree gchar *tmp = g_strdup_printf("base:%s\tapp:%s", bfw_val, app_val);
	fu_string_append(str, idt, key, tmp);
}

static DmcDevxDeviceType
fu_ccgx_dmc_devx_device_version_type(FuCcgxDmcDevxDevice *self)
{
	DmcDevxStatus *status = &self->status;
	if (status->device_type == DMC_DEVX_DEVICE_TYPE_DMC ||
	    status->device_type == DMC_DEVX_DEVICE_TYPE_CCG3 ||
	    status->device_type == DMC_DEVX_DEVICE_TYPE_CCG4 ||
	    status->device_type == DMC_DEVX_DEVICE_TYPE_CCG5 || status->device_type == 0x0B)
		return DMC_DEVX_DEVICE_TYPE_DMC;
	if (status->device_type == DMC_DEVX_DEVICE_TYPE_HX3)
		return DMC_DEVX_DEVICE_TYPE_HX3;
	return DMC_DEVX_DEVICE_TYPE_INVALID;
}

static void
fu_ccgx_dmc_devx_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCcgxDmcDevxDevice *self = FU_CCGX_DMC_DEVX_DEVICE(device);
	DmcDevxStatus *status = &self->status;
	DmcDevxDeviceType device_version_type = fu_ccgx_dmc_devx_device_version_type(self);
	const gchar *device_type = fu_ccgx_dmc_devx_device_type_to_string(status->device_type);

	if (device_type != NULL) {
		g_autofree gchar *tmp =
		    g_strdup_printf("0x%x [%s]", status->device_type, device_type);
		fu_string_append(str, idt, "DeviceType", tmp);
	} else {
		fu_string_append_kx(str, idt, "DeviceType", status->device_type);
	}
	if (status->image_mode < DMC_IMG_MODE_LAST) {
		g_autofree gchar *tmp =
		    g_strdup_printf("0x%x [%s]",
				    status->image_mode,
				    fu_ccgx_dmc_img_mode_to_string(status->image_mode));
		fu_string_append(str, idt, "ImageMode", tmp);
	} else {
		fu_string_append_kx(str, idt, "ImageMode", status->image_mode);
	}

	fu_string_append_kx(str, idt, "CurrentImage", status->current_image);
	fu_string_append(str,
			 idt,
			 "ImgStatus1",
			 fu_ccgx_dmc_img_status_to_string(status->img_status & 0x0F));
	fu_string_append(str,
			 idt,
			 "ImgStatus2",
			 fu_ccgx_dmc_img_status_to_string((status->img_status >> 4) & 0x0F));

	/* versions */
	if (device_version_type == DMC_DEVX_DEVICE_TYPE_DMC) {
		fu_ccgx_dmc_devx_device_dmcver_to_string(self, "boot", 0x00, idt, str);
		fu_ccgx_dmc_devx_device_dmcver_to_string(self, "img1", 0x08, idt, str);
		if (status->image_mode != DMC_IMG_MODE_SINGLE_IMG)
			fu_ccgx_dmc_devx_device_dmcver_to_string(self, "img2", 0x10, idt, str);
	} else if (device_version_type == DMC_DEVX_DEVICE_TYPE_HX3) {
		fu_ccgx_dmc_devx_device_hx3ver_to_string(self, "boot", 0x00, idt, str);
		fu_ccgx_dmc_devx_device_hx3ver_to_string(self, "img1", 0x08, idt, str);
		if (status->image_mode != DMC_IMG_MODE_SINGLE_IMG)
			fu_ccgx_dmc_devx_device_hx3ver_to_string(self, "img2", 0x10, idt, str);
	} else {
		fu_ccgx_dmc_devx_device_hexver_to_string(self, "boot", 0x00, idt, str);
		fu_ccgx_dmc_devx_device_hexver_to_string(self, "img1", 0x08, idt, str);
		if (status->image_mode != DMC_IMG_MODE_SINGLE_IMG)
			fu_ccgx_dmc_devx_device_hexver_to_string(self, "img2", 0x10, idt, str);
	}
}

static gboolean
fu_ccgx_dmc_devx_device_set_quirk_kv(FuDevice *device,
				     const gchar *key,
				     const gchar *value,
				     GError **error)
{
	if (g_strcmp0(key, "CcgxDmcCompositeVersion") == 0) {
		guint64 tmp = 0;
		FuDevice *proxy = fu_device_get_proxy(device);
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		if (fu_device_get_version_raw(proxy) != tmp) {
			g_debug("overriding composite version from %u to %u from %s",
				(guint)fu_device_get_version_raw(proxy),
				(guint)tmp,
				fu_device_get_id(device));
			fu_device_set_version_from_uint32(proxy, tmp);
		}
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static const gchar *
fu_ccgx_dmc_devx_device_type_to_name(DmcDevxDeviceType device_type)
{
	if (device_type == DMC_DEVX_DEVICE_TYPE_CCG3)
		return "CCG3";
	if (device_type == DMC_DEVX_DEVICE_TYPE_DMC)
		return "DMC";
	if (device_type == DMC_DEVX_DEVICE_TYPE_CCG4)
		return "CCG4";
	if (device_type == DMC_DEVX_DEVICE_TYPE_CCG5)
		return "CCG5";
	if (device_type == DMC_DEVX_DEVICE_TYPE_HX3)
		return "HX3";
	if (device_type == DMC_DEVX_DEVICE_TYPE_HX3_PD)
		return "HX3 PD";
	if (device_type == DMC_DEVX_DEVICE_TYPE_DMC_PD)
		return "DMC PD";
	if (device_type == DMC_DEVX_DEVICE_TYPE_SPI)
		return "SPI";
	return "Unknown";
}

static gboolean
fu_ccgx_dmc_devx_device_probe(FuDevice *device, GError **error)
{
	FuCcgxDmcDevxDevice *self = FU_CCGX_DMC_DEVX_DEVICE(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	DmcDevxStatus *status = &self->status;
	DmcDevxDeviceType device_version_type = fu_ccgx_dmc_devx_device_version_type(self);
	gsize offset = 0;
	g_autofree gchar *logical_id = g_strdup_printf("0x%02x", status->component_id);
	g_autofree gchar *version = NULL;

	fu_device_set_name(device, fu_ccgx_dmc_devx_device_type_to_name(status->device_type));
	fu_device_set_logical_id(device, logical_id);

	/* for the version number */
	if (status->current_image == 0x01)
		offset = 4;
	else if (status->current_image == 0x02)
		offset = 8;

	/* version, if possible */
	if (device_version_type == DMC_DEVX_DEVICE_TYPE_DMC) {
		version = fu_ccgx_dmc_devx_status_version_dmc_bfw(status, offset);
		fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	} else if (device_version_type == DMC_DEVX_DEVICE_TYPE_HX3) {
		version = fu_ccgx_dmc_devx_status_version_hx3(status, offset);
		fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
		fu_device_set_version(device, version);
	}
	if (version != NULL) {
		fu_device_set_version(device, version);
		fu_device_add_instance_strsafe(device, "VER", version);
	}

	/* add GUIDs */
	fu_device_add_instance_strup(device,
				     "TYPE",
				     fu_ccgx_dmc_devx_device_type_to_string(status->device_type));
	fu_device_add_instance_u8(device, "CID", status->component_id);
	fu_device_add_instance_u16(device, "VID", fu_usb_device_get_vid(FU_USB_DEVICE(proxy)));
	fu_device_add_instance_u16(device, "PID", fu_usb_device_get_pid(FU_USB_DEVICE(proxy)));
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "CID", NULL);
	fu_device_build_instance_id_quirk(device, NULL, "USB", "VID", "PID", "CID", "TYPE", NULL);
	fu_device_build_instance_id_quirk(device, NULL, "USB", "VID", "PID", "CID", "VER", NULL);

	/* success */
	return TRUE;
}

static void
fu_ccgx_dmc_devx_device_init(FuCcgxDmcDevxDevice *self)
{
}

static void
fu_ccgx_dmc_devx_device_class_init(FuCcgxDmcDevxDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_ccgx_dmc_devx_device_probe;
	klass_device->to_string = fu_ccgx_dmc_devx_device_to_string;
	klass_device->set_quirk_kv = fu_ccgx_dmc_devx_device_set_quirk_kv;
}

FuCcgxDmcDevxDevice *
fu_ccgx_dmc_devx_device_new(FuDevice *proxy, DmcDevxStatus *status)
{
	FuCcgxDmcDevxDevice *self = g_object_new(FU_TYPE_CCGX_DMC_DEVX_DEVICE,
						 "context",
						 fu_device_get_context(proxy),
						 "proxy",
						 proxy,
						 NULL);
	self->status = *status;
	return self;
}

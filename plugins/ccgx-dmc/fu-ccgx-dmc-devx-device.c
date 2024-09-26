/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ccgx-dmc-devx-device.h"

#define DMC_FW_WRITE_STATUS_RETRY_COUNT	   3
#define DMC_FW_WRITE_STATUS_RETRY_DELAY_MS 30

struct _FuCcgxDmcDevxDevice {
	FuDevice parent_instance;
	GByteArray *status; /* DmcDevxStatus */
};

G_DEFINE_TYPE(FuCcgxDmcDevxDevice, fu_ccgx_dmc_devx_device, FU_TYPE_DEVICE)

const guint8 *
fu_ccgx_dmc_devx_device_get_fw_version(FuCcgxDmcDevxDevice *self)
{
	return fu_struct_ccgx_dmc_devx_status_get_fw_version(self->status, NULL);
}

FuCcgxDmcDevxDeviceType
fu_ccgx_dmc_devx_device_get_device_type(FuCcgxDmcDevxDevice *self)
{
	return fu_struct_ccgx_dmc_devx_status_get_device_type(self->status);
}

static gchar *
fu_ccgx_dmc_devx_device_version_dmc_bfw(FuCcgxDmcDevxDevice *self, gsize offset)
{
	const guint8 *fw_version = fu_ccgx_dmc_devx_device_get_fw_version(self);
	return g_strdup_printf("%u.%u.%u.%u",
			       fw_version[offset + 3] >> 4,
			       fw_version[offset + 3] & 0xFu,
			       fw_version[offset + 2],
			       fu_memread_uint16(fw_version + offset, G_LITTLE_ENDIAN));
}

static gchar *
fu_ccgx_dmc_devx_device_version_dmc_app(FuCcgxDmcDevxDevice *self, gsize offset)
{
	const guint8 *fw_version = fu_ccgx_dmc_devx_device_get_fw_version(self);
	return g_strdup_printf("%u.%u.%u",
			       fw_version[offset + 4 + 3] >> 4,
			       fw_version[offset + 4 + 3] & 0xFu,
			       fw_version[offset + 4 + 2]);
}

static gchar *
fu_ccgx_dmc_devx_device_version_hx3(FuCcgxDmcDevxDevice *self, gsize offset)
{
	const guint8 *fw_version = fu_ccgx_dmc_devx_device_get_fw_version(self);
	return g_strdup_printf("%u.%u.%u",
			       fw_version[offset + 4 + 3],
			       fw_version[offset + 4 + 2],
			       fw_version[offset + 4 + 1]);
}

static void
fu_ccgx_dmc_devx_device_hexver_to_string(FuCcgxDmcDevxDevice *self,
					 const gchar *kind,
					 gsize offset,
					 guint idt,
					 GString *str)
{
	const guint8 *fw_version = fu_ccgx_dmc_devx_device_get_fw_version(self);
	g_autofree gchar *key = g_strdup_printf("FwVersion[%s]", kind);
	g_autofree gchar *val =
	    fu_version_from_uint64(fu_memread_uint64(fw_version + offset, G_LITTLE_ENDIAN),
				   FWUPD_VERSION_FORMAT_HEX);
	fwupd_codec_string_append(str, idt, key, val);
}

static void
fu_ccgx_dmc_devx_device_hx3ver_to_string(FuCcgxDmcDevxDevice *self,
					 const gchar *kind,
					 gsize offset,
					 guint idt,
					 GString *str)
{
	g_autofree gchar *key = g_strdup_printf("FwVersion[%s]", kind);
	g_autofree gchar *val = fu_ccgx_dmc_devx_device_version_hx3(self, offset);
	fwupd_codec_string_append(str, idt, key, val);
}

static void
fu_ccgx_dmc_devx_device_dmcver_to_string(FuCcgxDmcDevxDevice *self,
					 const gchar *kind,
					 gsize offset,
					 guint idt,
					 GString *str)
{
	g_autofree gchar *key = g_strdup_printf("FwVersion[%s]", kind);
	g_autofree gchar *bfw_val = fu_ccgx_dmc_devx_device_version_dmc_bfw(self, offset);
	g_autofree gchar *app_val = fu_ccgx_dmc_devx_device_version_dmc_app(self, offset);
	g_autofree gchar *tmp = g_strdup_printf("base:%s\tapp:%s", bfw_val, app_val);
	fwupd_codec_string_append(str, idt, key, tmp);
}

static FuCcgxDmcDevxDeviceType
fu_ccgx_dmc_devx_device_version_type(FuCcgxDmcDevxDevice *self)
{
	guint8 device_type = fu_struct_ccgx_dmc_devx_status_get_device_type(self->status);
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC ||
	    device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_CCG3 ||
	    device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_CCG4 ||
	    device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_CCG5 || device_type == 0x0B)
		return FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC;
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_HX3)
		return FU_CCGX_DMC_DEVX_DEVICE_TYPE_HX3;
	return FU_CCGX_DMC_DEVX_DEVICE_TYPE_INVALID;
}

static void
fu_ccgx_dmc_devx_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCcgxDmcDevxDevice *self = FU_CCGX_DMC_DEVX_DEVICE(device);
	FuCcgxDmcDevxDeviceType device_version_type = fu_ccgx_dmc_devx_device_version_type(self);
	guint8 device_type = fu_struct_ccgx_dmc_devx_status_get_device_type(self->status);
	guint8 image_mode = fu_struct_ccgx_dmc_devx_status_get_image_mode(self->status);
	guint8 img_status = fu_struct_ccgx_dmc_devx_status_get_img_status(self->status);
	const gchar *device_type_str = fu_ccgx_dmc_devx_device_type_to_string(device_type);

	if (device_type_str != NULL) {
		g_autofree gchar *tmp = g_strdup_printf("0x%x [%s]", device_type, device_type_str);
		fwupd_codec_string_append(str, idt, "DeviceType", tmp);
	} else {
		fwupd_codec_string_append_hex(str, idt, "DeviceType", device_type);
	}
	if (image_mode < FU_CCGX_DMC_IMG_MODE_LAST) {
		g_autofree gchar *tmp = g_strdup_printf("0x%x [%s]",
							image_mode,
							fu_ccgx_dmc_img_mode_to_string(image_mode));
		fwupd_codec_string_append(str, idt, "ImageMode", tmp);
	} else {
		fwupd_codec_string_append_hex(str, idt, "ImageMode", image_mode);
	}

	fwupd_codec_string_append_hex(
	    str,
	    idt,
	    "CurrentImage",
	    fu_struct_ccgx_dmc_devx_status_get_current_image(self->status));
	fwupd_codec_string_append(str,
				  idt,
				  "ImgStatus1",
				  fu_ccgx_dmc_img_status_to_string(img_status & 0x0F));
	fwupd_codec_string_append(str,
				  idt,
				  "ImgStatus2",
				  fu_ccgx_dmc_img_status_to_string((img_status >> 4) & 0x0F));

	/* versions */
	if (device_version_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC) {
		fu_ccgx_dmc_devx_device_dmcver_to_string(self, "boot", 0x00, idt, str);
		fu_ccgx_dmc_devx_device_dmcver_to_string(self, "img1", 0x08, idt, str);
		if (image_mode != FU_CCGX_DMC_IMG_MODE_SINGLE_IMG)
			fu_ccgx_dmc_devx_device_dmcver_to_string(self, "img2", 0x10, idt, str);
	} else if (device_version_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_HX3) {
		fu_ccgx_dmc_devx_device_hx3ver_to_string(self, "boot", 0x00, idt, str);
		fu_ccgx_dmc_devx_device_hx3ver_to_string(self, "img1", 0x08, idt, str);
		if (image_mode != FU_CCGX_DMC_IMG_MODE_SINGLE_IMG)
			fu_ccgx_dmc_devx_device_hx3ver_to_string(self, "img2", 0x10, idt, str);
	} else {
		fu_ccgx_dmc_devx_device_hexver_to_string(self, "boot", 0x00, idt, str);
		fu_ccgx_dmc_devx_device_hexver_to_string(self, "img1", 0x08, idt, str);
		if (image_mode != FU_CCGX_DMC_IMG_MODE_SINGLE_IMG)
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
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		if (fu_device_get_version_raw(proxy) != tmp) {
			g_debug("overriding composite version from %u to %u from %s",
				(guint)fu_device_get_version_raw(proxy),
				(guint)tmp,
				fu_device_get_id(device));
			fu_device_set_version_raw(proxy, tmp);
		}
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static const gchar *
fu_ccgx_dmc_devx_device_type_to_name(FuCcgxDmcDevxDeviceType device_type)
{
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_CCG3)
		return "CCG3";
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC)
		return "DMC";
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_CCG4)
		return "CCG4";
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_CCG5)
		return "CCG5";
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_HX3)
		return "HX3";
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_HX3_PD)
		return "HX3 PD";
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC_PD)
		return "DMC PD";
	if (device_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_SPI)
		return "SPI";
	return "Unknown";
}

guint
fu_ccgx_dmc_devx_device_get_remove_delay(FuCcgxDmcDevxDevice *self)
{
	guint remove_delay = 0;

	g_return_val_if_fail(FU_IS_CCGX_DMC_DEVX_DEVICE(self), G_MAXUINT);

	switch (fu_struct_ccgx_dmc_devx_status_get_device_type(self->status)) {
	case FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC:
		remove_delay = 40 * 1000;
		break;
	default:
		remove_delay = 30 * 1000;
		break;
	}
	return remove_delay;
}

static gboolean
fu_ccgx_dmc_devx_device_probe(FuDevice *device, GError **error)
{
	FuCcgxDmcDevxDevice *self = FU_CCGX_DMC_DEVX_DEVICE(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	FuCcgxDmcDevxDeviceType device_version_type = fu_ccgx_dmc_devx_device_version_type(self);
	gsize offset = 0;
	guint8 device_type = fu_struct_ccgx_dmc_devx_status_get_device_type(self->status);
	g_autofree gchar *logical_id =
	    g_strdup_printf("0x%02x",
			    fu_struct_ccgx_dmc_devx_status_get_component_id(self->status));
	g_autofree gchar *version = NULL;

	fu_device_set_name(device, fu_ccgx_dmc_devx_device_type_to_name(device_type));
	fu_device_set_logical_id(device, logical_id);

	/* for the version number */
	if (fu_struct_ccgx_dmc_devx_status_get_current_image(self->status) == 0x01)
		offset = 4;
	else if (fu_struct_ccgx_dmc_devx_status_get_current_image(self->status) == 0x02)
		offset = 8;

	/* version, if possible */
	if (device_version_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_DMC) {
		version = fu_ccgx_dmc_devx_device_version_dmc_bfw(self, offset);
		fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	} else if (device_version_type == FU_CCGX_DMC_DEVX_DEVICE_TYPE_HX3) {
		version = fu_ccgx_dmc_devx_device_version_hx3(self, offset);
		fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	}
	if (version != NULL) {
		fu_device_set_version(device, version); /* nocheck:set-version */
		fu_device_add_instance_strsafe(device, "VER", version);
	}

	/* add GUIDs */
	fu_device_add_instance_strup(device,
				     "TYPE",
				     fu_ccgx_dmc_devx_device_type_to_string(device_type));
	fu_device_add_instance_u8(device,
				  "CID",
				  fu_struct_ccgx_dmc_devx_status_get_component_id(self->status));
	fu_device_add_instance_u16(device, "VID", fu_device_get_vid(proxy));
	fu_device_add_instance_u16(device, "PID", fu_device_get_pid(proxy));
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "CID", NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "USB",
					 "VID",
					 "PID",
					 "CID",
					 "TYPE",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "USB",
					 "VID",
					 "PID",
					 "CID",
					 "VER",
					 NULL);

	/* success */
	return TRUE;
}

static gchar *
fu_ccgx_dmc_devx_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_ccgx_dmc_devx_device_init(FuCcgxDmcDevxDevice *self)
{
}

static void
fu_ccgx_dmc_devx_device_finalize(GObject *object)
{
	FuCcgxDmcDevxDevice *self = FU_CCGX_DMC_DEVX_DEVICE(object);
	if (self->status != NULL)
		g_byte_array_unref(self->status);
	G_OBJECT_CLASS(fu_ccgx_dmc_devx_device_parent_class)->finalize(object);
}

static void
fu_ccgx_dmc_devx_device_class_init(FuCcgxDmcDevxDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_ccgx_dmc_devx_device_finalize;
	device_class->probe = fu_ccgx_dmc_devx_device_probe;
	device_class->to_string = fu_ccgx_dmc_devx_device_to_string;
	device_class->set_quirk_kv = fu_ccgx_dmc_devx_device_set_quirk_kv;
	device_class->convert_version = fu_ccgx_dmc_devx_device_convert_version;
}

FuCcgxDmcDevxDevice *
fu_ccgx_dmc_devx_device_new(FuDevice *proxy,
			    const guint8 *buf,
			    gsize bufsz,
			    gsize offset,
			    GError **error)
{
	g_autoptr(FuCcgxDmcDevxDevice) self = g_object_new(FU_TYPE_CCGX_DMC_DEVX_DEVICE,
							   "context",
							   fu_device_get_context(proxy),
							   "proxy",
							   proxy,
							   NULL);
	self->status = fu_struct_ccgx_dmc_devx_status_parse(buf, bufsz, offset, error);
	if (self->status == NULL)
		return NULL;
	return g_steal_pointer(&self);
}

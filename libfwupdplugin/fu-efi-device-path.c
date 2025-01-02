/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiDevicePath"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-device-path.h"
#include "fu-efi-struct.h"
#include "fu-input-stream.h"

/**
 * FuEfiDevicePath:
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 subtype;
} FuEfiDevicePathPrivate;

static void
fu_efi_device_path_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuEfiDevicePath,
		       fu_efi_device_path,
		       FU_TYPE_FIRMWARE,
		       0,
		       G_ADD_PRIVATE(FuEfiDevicePath)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
						 fu_efi_device_path_codec_iface_init))
#define GET_PRIVATE(o) (fu_efi_device_path_get_instance_private(o))

static void
fu_efi_device_path_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiDevicePath *self = FU_EFI_DEVICE_PATH(firmware);
	FuEfiDevicePathPrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "subtype", priv->subtype);
}

static void
fu_efi_device_path_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuEfiDevicePath *self = FU_EFI_DEVICE_PATH(codec);
	FuEfiDevicePathPrivate *priv = GET_PRIVATE(self);
	fwupd_codec_json_append_int(builder, "Subtype", priv->subtype);
}

/**
 * fu_efi_device_path_get_subtype:
 * @self: a #FuEfiDevicePath
 *
 * Gets the DEVICE_PATH subtype.
 *
 * Returns: integer
 *
 * Since: 1.9.3
 **/
guint8
fu_efi_device_path_get_subtype(FuEfiDevicePath *self)
{
	FuEfiDevicePathPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_EFI_DEVICE_PATH(self), 0x0);
	return priv->subtype;
}

/**
 * fu_efi_device_path_set_subtype:
 * @self: a #FuEfiDevicePath
 * @subtype: integer
 *
 * Sets the DEVICE_PATH subtype.
 *
 * Since: 1.9.3
 **/
void
fu_efi_device_path_set_subtype(FuEfiDevicePath *self, guint8 subtype)
{
	FuEfiDevicePathPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_EFI_DEVICE_PATH(self));
	priv->subtype = subtype;
}

static gboolean
fu_efi_device_path_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuEfiDevicePath *self = FU_EFI_DEVICE_PATH(firmware);
	FuEfiDevicePathPrivate *priv = GET_PRIVATE(self);
	gsize dp_length;
	gsize streamsz = 0;
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_efi_device_path_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_efi_device_path_get_length(st) < FU_STRUCT_EFI_DEVICE_PATH_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "EFI DEVICE_PATH length invalid: 0x%x",
			    fu_struct_efi_device_path_get_length(st));
		return FALSE;
	}
	fu_firmware_set_idx(firmware, fu_struct_efi_device_path_get_type(st));
	priv->subtype = fu_struct_efi_device_path_get_subtype(st);

	/* work around a efiboot bug */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	dp_length = fu_struct_efi_device_path_get_length(st);
	if (streamsz > 4 && dp_length > streamsz) {
		dp_length = streamsz - 0x4;
		g_debug("fixing up DP length from 0x%x to 0x%x, because of a bug in efiboot",
			fu_struct_efi_device_path_get_length(st),
			(guint)dp_length);
	}
	if (dp_length > st->len) {
		g_autoptr(GBytes) payload =
		    fu_input_stream_read_bytes(stream, st->len, dp_length - st->len, NULL, error);
		if (payload == NULL)
			return FALSE;
		fu_firmware_set_bytes(firmware, payload);
	}
	fu_firmware_set_size(firmware, dp_length);

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_device_path_write(FuFirmware *firmware, GError **error)
{
	FuEfiDevicePath *self = FU_EFI_DEVICE_PATH(firmware);
	FuEfiDevicePathPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) st = fu_struct_efi_device_path_new();
	g_autoptr(GBytes) payload = NULL;

	/* required */
	payload = fu_firmware_get_bytes(firmware, error);
	if (payload == NULL)
		return NULL;
	fu_struct_efi_device_path_set_type(st, fu_firmware_get_idx(firmware));
	fu_struct_efi_device_path_set_subtype(st, priv->subtype);
	fu_struct_efi_device_path_set_length(st, st->len + g_bytes_get_size(payload));
	fu_byte_array_append_bytes(st, payload);

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_efi_device_path_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiDevicePath *self = FU_EFI_DEVICE_PATH(firmware);
	FuEfiDevicePathPrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "subtype", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->subtype = tmp;

	/* success */
	return TRUE;
}

static void
fu_efi_device_path_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_efi_device_path_add_json;
}

static void
fu_efi_device_path_init(FuEfiDevicePath *self)
{
}

static void
fu_efi_device_path_class_init(FuEfiDevicePathClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->export = fu_efi_device_path_export;
	firmware_class->parse = fu_efi_device_path_parse;
	firmware_class->write = fu_efi_device_path_write;
	firmware_class->build = fu_efi_device_path_build;
}

/**
 * fu_efi_device_path_new:
 *
 * Creates a new EFI DEVICE_PATH
 *
 * Since: 1.9.3
 **/
FuEfiDevicePath *
fu_efi_device_path_new(void)
{
	return g_object_new(FU_TYPE_EFI_DEVICE_PATH, NULL);
}

/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiLoadOption"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-device-path-list.h"
#include "fu-efi-file-path-device-path.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efi-load-option.h"
#include "fu-efi-struct.h"
#include "fu-input-stream.h"
#include "fu-mem.h"
#include "fu-string.h"

struct _FuEfiLoadOption {
	FuFirmware parent_instance;
	guint32 attrs;
	GBytes *optional_data;
};

static void
fu_efi_load_option_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuEfiLoadOption,
		       fu_efi_load_option,
		       FU_TYPE_FIRMWARE,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_efi_load_option_codec_iface_init))

#define FU_EFI_LOAD_OPTION_DESCRIPTION_SIZE_MAX 0x1000u /* bytes */

static void
fu_efi_load_option_set_optional_data(FuEfiLoadOption *self, GBytes *optional_data)
{
	g_return_if_fail(FU_IS_EFI_LOAD_OPTION(self));
	if (self->optional_data != NULL) {
		g_bytes_unref(self->optional_data);
		self->optional_data = NULL;
	}
	if (optional_data != NULL)
		self->optional_data = g_bytes_ref(optional_data);
}

/**
 * fu_efi_load_option_get_optional_path:
 * @self: a #FuEfiLoadOption
 * @error: (nullable): optional return location for an error
 *
 * Gets a path from the optional UTF-16 data.
 *
 * Returns: (transfer full): UTF-8 path, or %NULL
 *
 * Since: 2.0.0
 **/
gchar *
fu_efi_load_option_get_optional_path(FuEfiLoadOption *self, GError **error)
{
	g_autofree gchar *optional_path = NULL;

	g_return_val_if_fail(FU_IS_EFI_LOAD_OPTION(self), NULL);

	if (self->optional_data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no optional data");
		return NULL;
	}

	/* convert to UTF-8 */
	optional_path = fu_utf16_to_utf8_bytes(self->optional_data, G_LITTLE_ENDIAN, error);
	if (optional_path == NULL)
		return NULL;

	/* check is ASCII */
	if (!g_str_is_ascii(optional_path)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not ASCII data");
		return NULL;
	}

	/* remove leading slash if provided */
	if (g_str_has_prefix(optional_path, "\\"))
		return g_strdup(optional_path + 1);
	return g_steal_pointer(&optional_path);
}

/**
 * fu_efi_load_option_set_optional_path:
 * @self: a #FuEfiLoadOption
 * @optional_path: UTF-8 path
 * @error: (nullable): optional return location for an error
 *
 * Sets UTF-16 optional data from a path. If required, a leading backslash will be added.
 *
 * Since: 1.9.3
 **/
gboolean
fu_efi_load_option_set_optional_path(FuEfiLoadOption *self,
				     const gchar *optional_path,
				     GError **error)
{
	g_autoptr(GString) str = g_string_new(optional_path);
	g_autoptr(GBytes) opt_blob = NULL;

	g_return_val_if_fail(FU_IS_EFI_LOAD_OPTION(self), FALSE);
	g_return_val_if_fail(optional_path != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* is required if a path */
	if (!g_str_has_prefix(str->str, "\\"))
		g_string_prepend(str, "\\");
	opt_blob = fu_utf8_to_utf16_bytes(str->str,
					  G_LITTLE_ENDIAN,
					  FU_UTF_CONVERT_FLAG_APPEND_NUL,
					  error);
	if (opt_blob == NULL)
		return FALSE;
	fu_efi_load_option_set_optional_data(self, opt_blob);

	/* success */
	return TRUE;
}

static gboolean
fu_efi_load_option_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);
	gsize streamsz = 0;
	g_autofree gchar *id = NULL;
	g_autoptr(FuEfiDevicePathList) device_path_list = fu_efi_device_path_list_new();
	g_autoptr(GByteArray) buf_utf16 = g_byte_array_new();
	g_autoptr(GByteArray) st = NULL;

	/* parse header */
	st = fu_struct_efi_load_option_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	self->attrs = fu_struct_efi_load_option_get_attrs(st);
	offset += st->len;

	/* parse UTF-16 description */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (; offset < streamsz; offset += 2) {
		guint16 tmp = 0;
		if (buf_utf16->len > FU_EFI_LOAD_OPTION_DESCRIPTION_SIZE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "description was too long, limit is 0x%x chars",
				    FU_EFI_LOAD_OPTION_DESCRIPTION_SIZE_MAX / 2);
			return FALSE;
		}
		if (!fu_input_stream_read_u16(stream, offset, &tmp, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (tmp == 0)
			break;
		fu_byte_array_append_uint16(buf_utf16, tmp, G_LITTLE_ENDIAN);
	}
	id = fu_utf16_to_utf8_byte_array(buf_utf16, G_LITTLE_ENDIAN, error);
	if (id == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, id);
	offset += 2;

	/* parse dp blob */
	if (!fu_firmware_parse_stream(FU_FIRMWARE(device_path_list), stream, offset, flags, error))
		return FALSE;
	if (!fu_firmware_add_image_full(firmware, FU_FIRMWARE(device_path_list), error))
		return FALSE;
	offset += fu_struct_efi_load_option_get_dp_size(st);

	/* optional data */
	if (offset < streamsz) {
		g_autoptr(GBytes) opt_blob = NULL;
		opt_blob = fu_input_stream_read_bytes(stream, offset, streamsz - offset, error);
		if (opt_blob == NULL)
			return FALSE;
		fu_efi_load_option_set_optional_data(self, opt_blob);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_load_option_write(FuFirmware *firmware, GError **error)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);
	g_autoptr(GByteArray) buf_utf16 = NULL;
	g_autoptr(GByteArray) st = fu_struct_efi_load_option_new();
	g_autoptr(GBytes) dpbuf = NULL;

	/* header */
	fu_struct_efi_load_option_set_attrs(st, self->attrs);

	/* label */
	if (fu_firmware_get_id(firmware) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware ID required");
		return NULL;
	}
	buf_utf16 = fu_utf8_to_utf16_byte_array(fu_firmware_get_id(firmware),
						G_LITTLE_ENDIAN,
						FU_UTF_CONVERT_FLAG_APPEND_NUL,
						error);
	if (buf_utf16 == NULL)
		return NULL;
	g_byte_array_append(st, buf_utf16->data, buf_utf16->len);

	/* dpbuf */
	dpbuf = fu_firmware_get_image_by_gtype_bytes(firmware, FU_TYPE_EFI_DEVICE_PATH_LIST, error);
	if (dpbuf == NULL)
		return NULL;
	fu_struct_efi_load_option_set_dp_size(st, g_bytes_get_size(dpbuf));
	fu_byte_array_append_bytes(st, dpbuf);

	/* optional content */
	if (self->optional_data != NULL)
		fu_byte_array_append_bytes(st, self->optional_data);

	return g_steal_pointer(&st);
}

static gboolean
fu_efi_load_option_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);
	guint64 tmp;
	g_autoptr(XbNode) optional_data = NULL;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "attrs", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		self->attrs = tmp;

	/* optional data */
	optional_data = xb_node_query_first(n, "optional_data", NULL);
	if (optional_data != NULL) {
		g_autoptr(GBytes) blob = NULL;
		if (xb_node_get_text(optional_data) != NULL) {
			gsize bufsz = 0;
			g_autofree guchar *buf = NULL;
			buf = g_base64_decode(xb_node_get_text(optional_data), &bufsz);
			blob = g_bytes_new(buf, bufsz);
		} else {
			blob = g_bytes_new(NULL, 0);
		}
		fu_efi_load_option_set_optional_data(self, blob);
	}

	/* success */
	return TRUE;
}

static void
fu_efi_load_option_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);
	fu_xmlb_builder_insert_kx(bn, "attrs", self->attrs);
	if (self->optional_data != NULL) {
		gsize bufsz = 0;
		const guint8 *buf = g_bytes_get_data(self->optional_data, &bufsz);
		g_autofree gchar *datastr = g_base64_encode(buf, bufsz);
		xb_builder_node_insert_text(bn, "optional_data", datastr, NULL);
	}
}

static void
fu_efi_load_option_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(codec);
	g_autofree gchar *optional_path = fu_efi_load_option_get_optional_path(self, NULL);
	g_autoptr(FuFirmware) dp_list = NULL;

	fwupd_codec_json_append(builder, "Name", fu_firmware_get_id(FU_FIRMWARE(self)));
	fwupd_codec_json_append(builder, "OptionalPath", optional_path);
	dp_list =
	    fu_firmware_get_image_by_gtype(FU_FIRMWARE(self), FU_TYPE_EFI_DEVICE_PATH_LIST, NULL);
	if (dp_list != NULL)
		fwupd_codec_to_json(FWUPD_CODEC(dp_list), builder, flags);
}

static void
fu_efi_load_option_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_efi_load_option_add_json;
}

static void
fu_efi_load_option_finalize(GObject *obj)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(obj);
	if (self->optional_data != NULL)
		g_bytes_unref(self->optional_data);
	G_OBJECT_CLASS(fu_efi_load_option_parent_class)->finalize(obj);
}

static void
fu_efi_load_option_class_init(FuEfiLoadOptionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_efi_load_option_finalize;
	firmware_class->parse = fu_efi_load_option_parse;
	firmware_class->write = fu_efi_load_option_write;
	firmware_class->build = fu_efi_load_option_build;
	firmware_class->export = fu_efi_load_option_export;
}

static void
fu_efi_load_option_init(FuEfiLoadOption *self)
{
	self->attrs = FU_EFI_LOAD_OPTION_ATTRS_ACTIVE;
	g_type_ensure(FU_TYPE_EFI_DEVICE_PATH_LIST);
}

/**
 * fu_efi_load_option_new:
 *
 * Returns: (transfer full): a #FuEfiLoadOption
 *
 * Since: 1.9.3
 **/
FuEfiLoadOption *
fu_efi_load_option_new(void)
{
	return g_object_new(FU_TYPE_EFI_LOAD_OPTION, NULL);
}

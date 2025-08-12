/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiVariableAuthentication2"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-efi-struct.h"
#include "fu-efi-variable-authentication2.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"
#include "fu-pkcs7.h"

/**
 * FuEfiVariableAuthentication2:
 *
 * A UEFI signature list typically found in the `KEKUpdate.bin` and `DBXUpdate.bin` files.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiVariableAuthentication2 {
	FuEfiSignatureList parent_instance;
	GPtrArray *signers;
};

G_DEFINE_TYPE(FuEfiVariableAuthentication2,
	      fu_efi_variable_authentication2,
	      FU_TYPE_EFI_SIGNATURE_LIST)

/**
 * fu_efi_variable_authentication2_get_signers:
 * @self: A #FuEfiVariableAuthentication2
 *
 * Returns the certificates that signed the variable.
 *
 * Returns: (transfer full) (element-type FuX509Certificate): certificates
 *
 * Since: 2.0.9
 **/
GPtrArray *
fu_efi_variable_authentication2_get_signers(FuEfiVariableAuthentication2 *self)
{
	g_return_val_if_fail(FU_IS_EFI_VARIABLE_AUTHENTICATION2(self), NULL);
	return g_ptr_array_ref(self->signers);
}

static void
fu_efi_variable_authentication2_export(FuFirmware *firmware,
				       FuFirmwareExportFlags flags,
				       XbBuilderNode *bn)
{
	g_autoptr(XbBuilderNode) bn_signers = NULL;
	FuEfiVariableAuthentication2 *self = FU_EFI_VARIABLE_AUTHENTICATION2(firmware);

	bn_signers = xb_builder_node_insert(bn, "signers", NULL);
	for (guint i = 0; i < self->signers->len; i++) {
		FuFirmware *img = g_ptr_array_index(self->signers, i);
		g_autoptr(XbBuilderNode) bn_firmware = NULL;
		bn_firmware = xb_builder_node_insert(bn_signers, "firmware", NULL);
		fu_firmware_export(img, flags, bn_firmware);
	}
}

static gboolean
fu_efi_variable_authentication2_validate(FuFirmware *firmware,
					 GInputStream *stream,
					 gsize offset,
					 GError **error)
{
	return fu_struct_efi_variable_authentication2_validate_stream(stream, offset, error);
}

/*
 * with ContentInfo:
 *    30 82 05 90 -- SEQUENCE (1424 BYTES) -- ContentInfo
 *       06 09 -- OBJECT-IDENTIFIER (9 BYTES) -- ContentType
 *          2a 86 48 86 f7 0d 01 07 02 -- signedData [1.2.840.113549.1.7.2]
 *       a0 82 05 81 -- CONTEXT-SPECIFIC CONSTRUCTED TAG 0 (1409 BYTES) -- content
 *
 * without ContentInfo:
 *          30 82 05 7d -- SEQUENCE (1405 BYTES) -- SignedData
 *             02 01 01 -- INTEGER 1 -- Version
 *             31 0f -- SET (1 element) (15 BYTES) -- DigestAlgorithmIdentifiers
 *                30 0d -- SEQUENCE (13 BYTES) -- AlgorithmIdentifier
 *                   06 09 -- OBJECT-IDENTIFIER (9 BYTES) -- algorithm
 *                      60 86 48 01 65 03 04 02 01 -- sha256 [2.16.840.1.101.3.4.2.1]
 *                   05 00 -- NULL (0 BYTES) -- parameters
 */
static gboolean
fu_efi_variable_authentication2_add_content_info_prefix(GByteArray *buf, GError **error)
{
	guint16 sz = 0;
	const guint8 buf_algorithm[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02};
	g_autoptr(GByteArray) buf_prefix = g_byte_array_new();

	/* check is ASN.1 SEQUENCE */
	if (!fu_memread_uint16_safe(buf->data, buf->len, 0x0, &sz, G_BIG_ENDIAN, error)) {
		g_prefix_error_literal(error, "not ASN.1 SEQUENCE: ");
		return FALSE;
	}
	if (sz != 0x3082) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "not ASN.1 SEQUENCE, got 0x%x",
			    sz);
		return FALSE;
	}

	/* get size of SignedData */
	if (!fu_memread_uint16_safe(buf->data, buf->len, 0x2, &sz, G_BIG_ENDIAN, error))
		return FALSE;

	/* add SEQUENCE */
	fu_byte_array_append_uint16(buf_prefix, 0x3082, G_BIG_ENDIAN);
	fu_byte_array_append_uint16(buf_prefix, sz + 19, G_BIG_ENDIAN);

	/* add OBJECT-IDENTIFIER */
	fu_byte_array_append_uint16(buf_prefix, 0x0609, G_BIG_ENDIAN);
	g_byte_array_append(buf_prefix, buf_algorithm, sizeof(buf_algorithm));

	/* add CONTEXT-SPECIFIC CONSTRUCTED TAG */
	fu_byte_array_append_uint16(buf_prefix, 0xA082, G_BIG_ENDIAN);
	fu_byte_array_append_uint16(buf_prefix, sz + 4, G_BIG_ENDIAN);

	/* fix this up */
	g_byte_array_prepend(buf, buf_prefix->data, buf_prefix->len);
	return TRUE;
}

static gboolean
fu_efi_variable_authentication2_parse_pkcs7_certs(FuEfiVariableAuthentication2 *self,
						  GByteArray *buf,
						  GError **error)
{
	g_autoptr(FuPkcs7) pkcs7 = fu_pkcs7_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) imgs = NULL;

	/* parse PKCS#7 blob */
	blob = g_bytes_new(buf->data, buf->len);
	if (!fu_firmware_parse_bytes(FU_FIRMWARE(pkcs7),
				     blob,
				     0x0,
				     FU_FIRMWARE_PARSE_FLAG_NONE,
				     error))
		return FALSE;

	/* add certificates that signed this variable */
	imgs = fu_firmware_get_images(FU_FIRMWARE(pkcs7));
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_ptr_array_add(self->signers, g_object_ref(img));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_efi_variable_authentication2_parse(FuFirmware *firmware,
				      GInputStream *stream,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	FuEfiVariableAuthentication2 *self = FU_EFI_VARIABLE_AUTHENTICATION2(firmware);
	gsize offset = FU_STRUCT_EFI_TIME_SIZE;
	g_autoptr(FuStructEfiVariableAuthentication2) st = NULL;
	g_autoptr(FuStructEfiWinCertificate) st_wincert = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;
	gboolean offset_tmp = 0;

	st = fu_struct_efi_variable_authentication2_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* parse the EFI_SIGNATURE_LIST blob past the EFI_TIME + WIN_CERTIFICATE */
	st_wincert = fu_struct_efi_variable_authentication2_get_auth_info(st);
	if (fu_struct_efi_win_certificate_get_length(st_wincert) > st_wincert->len) {
		g_autoptr(GByteArray) buf = NULL;
		buf = fu_input_stream_read_byte_array(
		    stream,
		    offset + st_wincert->len + offset_tmp,
		    fu_struct_efi_win_certificate_get_length(st_wincert) - st_wincert->len,
		    NULL,
		    error);
		if (buf == NULL)
			return FALSE;
		if (!fu_efi_variable_authentication2_add_content_info_prefix(buf, error))
			return FALSE;
		if (!fu_efi_variable_authentication2_parse_pkcs7_certs(self, buf, error))
			return FALSE;
	}

	offset += fu_struct_efi_win_certificate_get_length(st_wincert);
	partial_stream = fu_partial_input_stream_new(stream, offset, G_MAXSIZE, error);
	if (partial_stream == NULL)
		return FALSE;
	return FU_FIRMWARE_CLASS(fu_efi_variable_authentication2_parent_class)
	    ->parse(firmware, partial_stream, flags, error);
}

static GByteArray *
fu_efi_variable_authentication2_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(FuStructEfiVariableAuthentication2) st =
	    fu_struct_efi_variable_authentication2_new();
	g_autoptr(GByteArray) st_parent = NULL;

	/* append EFI_SIGNATURE_LIST */
	st_parent =
	    FU_FIRMWARE_CLASS(fu_efi_variable_authentication2_parent_class)->write(firmware, error);
	if (st_parent == NULL)
		return NULL;
	g_byte_array_append(st, st_parent->data, st_parent->len);

	/* success */
	return g_steal_pointer(&st);
}

static void
fu_efi_variable_authentication2_init(FuEfiVariableAuthentication2 *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALWAYS_SEARCH);
	g_type_ensure(FU_TYPE_EFI_SIGNATURE_LIST);
	self->signers = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_efi_variable_authentication2_finalize(GObject *obj)
{
	FuEfiVariableAuthentication2 *self = FU_EFI_VARIABLE_AUTHENTICATION2(obj);
	g_ptr_array_unref(self->signers);
	G_OBJECT_CLASS(fu_efi_variable_authentication2_parent_class)->finalize(obj);
}

static void
fu_efi_variable_authentication2_class_init(FuEfiVariableAuthentication2Class *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_efi_variable_authentication2_finalize;
	firmware_class->validate = fu_efi_variable_authentication2_validate;
	firmware_class->parse = fu_efi_variable_authentication2_parse;
	firmware_class->export = fu_efi_variable_authentication2_export;
	firmware_class->write = fu_efi_variable_authentication2_write;
}

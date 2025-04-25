/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiVariableAuthentication2"

#include "config.h"

#include "fu-efi-struct.h"
#include "fu-efi-variable-authentication2.h"
#include "fu-partial-input-stream.h"

/**
 * FuEfiVariableAuthentication2:
 *
 * A UEFI signature list typically found in the `KEKUpdate.bin` and `DBXUpdate.bin` files.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiVariableAuthentication2 {
	FuEfiSignatureList parent_instance;
};

G_DEFINE_TYPE(FuEfiVariableAuthentication2,
	      fu_efi_variable_authentication2,
	      FU_TYPE_EFI_SIGNATURE_LIST)

static gboolean
fu_efi_variable_authentication2_validate(FuFirmware *firmware,
					 GInputStream *stream,
					 gsize offset,
					 GError **error)
{
	return fu_struct_efi_variable_authentication2_validate_stream(stream, offset, error);
}

static gboolean
fu_efi_variable_authentication2_parse(FuFirmware *firmware,
				      GInputStream *stream,
				      FwupdInstallFlags flags,
				      GError **error)
{
	gsize offset = FU_STRUCT_EFI_TIME_SIZE;
	g_autoptr(FuStructEfiVariableAuthentication2) st = NULL;
	g_autoptr(FuStructEfiWinCertificate) st_wincert = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;

	st = fu_struct_efi_variable_authentication2_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* parse the EFI_SIGNATURE_LIST blob past the EFI_TIME + WIN_CERTIFICATE */
	st_wincert = fu_struct_efi_variable_authentication2_get_auth_info(st);
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
fu_efi_variable_authentication2_class_init(FuEfiVariableAuthentication2Class *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_efi_variable_authentication2_validate;
	firmware_class->parse = fu_efi_variable_authentication2_parse;
	firmware_class->write = fu_efi_variable_authentication2_write;
}

static void
fu_efi_variable_authentication2_init(FuEfiVariableAuthentication2 *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALWAYS_SEARCH);
	g_type_ensure(FU_TYPE_EFI_SIGNATURE_LIST);
}

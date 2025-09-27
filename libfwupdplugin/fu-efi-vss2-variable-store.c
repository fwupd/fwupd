/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiVss2VariableStore"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-struct.h"
#include "fu-efi-vss-auth-variable.h"
#include "fu-efi-vss2-variable-store.h"
#include "fu-string.h"

/**
 * FuEfiVss2VariableStore:
 *
 * A NVRAM variable store.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiVss2VariableStore {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuEfiVss2VariableStore, fu_efi_vss2_variable_store, FU_TYPE_FIRMWARE)

static gboolean
fu_efi_vss2_variable_store_validate(FuFirmware *firmware,
				    GInputStream *stream,
				    gsize offset,
				    GError **error)
{
	return fu_struct_efi_vss2_variable_store_header_validate_stream(stream, offset, error);
}

static gboolean
fu_efi_vss2_variable_store_parse(FuFirmware *firmware,
				 GInputStream *stream,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	gsize offset = 0x0;
	g_autoptr(FuStructEfiVss2VariableStoreHeader) st = NULL;

	st = fu_struct_efi_vss2_variable_store_header_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	if (fu_struct_efi_vss2_variable_store_header_get_size(st) >
	    fu_firmware_get_size_max(firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "VSS store larger than max size: 0x%x > 0x%x",
			    (guint)fu_struct_efi_vss2_variable_store_header_get_size(st),
			    (guint)fu_firmware_get_size_max(firmware));
		return FALSE;
	}

	/* parse each attr */
	offset += st->len;
	while (offset < fu_struct_efi_vss2_variable_store_header_get_size(st)) {
		g_autoptr(FuFirmware) img = fu_efi_vss_auth_variable_new();

		if (!fu_firmware_parse_stream(img, stream, offset, flags, error)) {
			g_prefix_error(error, "offset @0x%x: ", (guint)offset);
			return FALSE;
		}
		if (fu_firmware_has_flag(img, FU_FIRMWARE_FLAG_IS_LAST_IMAGE))
			break;
		if (fu_firmware_get_size(img) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "VSS2 store entry has zero size");
			return FALSE;
		}
		if (fu_efi_vss_auth_variable_get_state(FU_EFI_VSS_AUTH_VARIABLE(img)) ==
		    FU_EFI_VARIABLE_STATE_VARIABLE_ADDED) {
			fu_firmware_set_offset(img, offset);
			fu_firmware_add_image(firmware, img);
		}
		offset += fu_firmware_get_size(img);
		offset = fu_common_align_up(offset, FU_FIRMWARE_ALIGNMENT_4);
	}

	/* success */
	fu_firmware_set_size(firmware, fu_struct_efi_vss2_variable_store_header_get_size(st));
	return TRUE;
}

static GByteArray *
fu_efi_vss2_variable_store_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(FuStructEfiVss2VariableStoreHeader) st =
	    fu_struct_efi_vss2_variable_store_header_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* sanity check */
	if (fu_firmware_get_size(firmware) < st->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "VSS2 variable store has zero size");
		return NULL;
	}

	/* each attr */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) fw = NULL;

		fw = fu_firmware_write(img, error);
		if (fw == NULL)
			return NULL;
		fu_byte_array_append_bytes(st, fw);
		fu_byte_array_align_up(st, FU_FIRMWARE_ALIGNMENT_4, 0xFF);
	}

	/* sanity check */
	if (st->len > fu_firmware_get_size(firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "VSS2 store is too small, needed 0x%x but defined as 0x%x",
			    st->len,
			    (guint)(fu_firmware_get_size(firmware)));
		return NULL;
	}

	/* fix up header and attrs */
	fu_byte_array_set_size(st, fu_firmware_get_size(firmware), 0xFF);
	fu_struct_efi_vss2_variable_store_header_set_size(st, fu_firmware_get_size(firmware));

	/* success */
	return g_steal_pointer(&st);
}

static void
fu_efi_vss2_variable_store_init(FuEfiVss2VariableStore *self)
{
	g_type_ensure(FU_TYPE_EFI_VSS_AUTH_VARIABLE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 10000);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 0x1000000); /* 16MB */
}

static void
fu_efi_vss2_variable_store_class_init(FuEfiVss2VariableStoreClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_efi_vss2_variable_store_validate;
	firmware_class->parse = fu_efi_vss2_variable_store_parse;
	firmware_class->write = fu_efi_vss2_variable_store_write;
}

/**
 * fu_efi_vss2_variable_store_new:
 *
 * Creates an empty VSS variable store.
 *
 * Returns: a #FuFirmware
 *
 * Since: 2.0.17
 **/
FuFirmware *
fu_efi_vss2_variable_store_new(void)
{
	return g_object_new(FU_TYPE_EFI_VSS2_VARIABLE_STORE, NULL);
}

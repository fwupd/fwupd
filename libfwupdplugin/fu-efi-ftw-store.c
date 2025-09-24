/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiFtwStore"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-ftw-store.h"
#include "fu-efi-struct.h"

/**
 * FuEfiFtwStore:
 *
 * A fault tolerant working block store, as found in EDK2 NVRAM blocks.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiFtwStore {
	FuFirmware parent_instance;
	FuEfiVariableStoreState state;
};

G_DEFINE_TYPE(FuEfiFtwStore, fu_efi_ftw_store, FU_TYPE_FIRMWARE)

static void
fu_efi_ftw_store_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiFtwStore *self = FU_EFI_FTW_STORE(firmware);
	if (self->state != FU_EFI_VARIABLE_STORE_STATE_UNSET) {
		const gchar *str = fu_efi_variable_store_state_to_string(self->state);
		fu_xmlb_builder_insert_kv(bn, "state", str);
	}
}

static gboolean
fu_efi_ftw_store_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	return fu_struct_efi_fault_tolerant_working_block_header64_validate_stream(stream,
										   offset,
										   error);
}

static gboolean
fu_efi_ftw_store_parse(FuFirmware *firmware,
		       GInputStream *stream,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	FuEfiFtwStore *self = FU_EFI_FTW_STORE(firmware);
	g_autoptr(FuStructEfiFaultTolerantWorkingBlockHeader64) st = NULL;

	st = fu_struct_efi_fault_tolerant_working_block_header64_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* attributes we care about */
	self->state = fu_struct_efi_fault_tolerant_working_block_header64_get_state(st);
	fu_firmware_set_size(
	    firmware,
	    st->len + fu_struct_efi_fault_tolerant_working_block_header64_get_write_queue_size(st));

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_ftw_store_write(FuFirmware *firmware, GError **error)
{
	FuEfiFtwStore *self = FU_EFI_FTW_STORE(firmware);
	g_autoptr(FuStructEfiFaultTolerantWorkingBlockHeader64) st =
	    fu_struct_efi_fault_tolerant_working_block_header64_new();

	/* sanity check */
	if (fu_firmware_get_size(firmware) < st->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "FTW store has zero size");
		return NULL;
	}

	/* CRC32 */
	fu_struct_efi_fault_tolerant_working_block_header64_set_write_queue_size(
	    st,
	    fu_firmware_get_size(firmware) - st->len);
	fu_struct_efi_fault_tolerant_working_block_header64_set_crc(
	    st,
	    fu_crc32(FU_CRC_KIND_B32_STANDARD, st->data, st->len));

	/* attrs */
	fu_struct_efi_fault_tolerant_working_block_header64_set_state(st, self->state);

	/* data area */
	fu_byte_array_set_size(st, fu_firmware_get_size(firmware), 0xFF);

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_efi_ftw_store_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiFtwStore *self = FU_EFI_FTW_STORE(firmware);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "state", NULL);
	if (tmp != NULL)
		self->state = fu_efi_variable_store_state_from_string(tmp);

	/* success */
	return TRUE;
}

static void
fu_efi_ftw_store_init(FuEfiFtwStore *self)
{
}

static void
fu_efi_ftw_store_class_init(FuEfiFtwStoreClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_efi_ftw_store_validate;
	firmware_class->parse = fu_efi_ftw_store_parse;
	firmware_class->export = fu_efi_ftw_store_export;
	firmware_class->write = fu_efi_ftw_store_write;
	firmware_class->build = fu_efi_ftw_store_build;
}

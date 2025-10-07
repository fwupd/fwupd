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
	g_autoptr(GBytes) blob = NULL;

	st = fu_struct_efi_fault_tolerant_working_block_header64_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	if (fu_struct_efi_fault_tolerant_working_block_header64_get_write_queue_size(st) >
	    fu_firmware_get_size_max(firmware)) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "FTW store larger than max size: 0x%x > 0x%x",
		    (guint)fu_struct_efi_fault_tolerant_working_block_header64_get_write_queue_size(
			st),
		    (guint)fu_firmware_get_size_max(firmware));
		return FALSE;
	}

	/* attributes we care about */
	self->state = fu_struct_efi_fault_tolerant_working_block_header64_get_state(st);

	/* data area */
	blob = fu_input_stream_read_bytes(
	    stream,
	    st->buf->len,
	    fu_struct_efi_fault_tolerant_working_block_header64_get_write_queue_size(st),
	    NULL,
	    error);
	if (blob == NULL)
		return FALSE;
	fu_firmware_set_bytes(firmware, blob);
	fu_firmware_set_size(firmware, st->buf->len + g_bytes_get_size(blob));

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_ftw_store_write(FuFirmware *firmware, GError **error)
{
	FuEfiFtwStore *self = FU_EFI_FTW_STORE(firmware);
	g_autoptr(FuStructEfiFaultTolerantWorkingBlockHeader64) st =
	    fu_struct_efi_fault_tolerant_working_block_header64_new();
	g_autoptr(GBytes) blob = NULL;

	/* get blob */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return NULL;

	/* CRC32 */
	fu_struct_efi_fault_tolerant_working_block_header64_set_write_queue_size(
	    st,
	    g_bytes_get_size(blob));
	fu_struct_efi_fault_tolerant_working_block_header64_set_crc(
	    st,
	    fu_crc32(FU_CRC_KIND_B32_STANDARD, st->buf->data, st->buf->len));

	/* attrs + data area */
	fu_struct_efi_fault_tolerant_working_block_header64_set_state(st, self->state);
	fu_byte_array_append_bytes(st->buf, blob);

	/* success */
	return g_steal_pointer(&st->buf);
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
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
#ifdef HAVE_FUZZER
	fu_firmware_set_size_max(FU_FIRMWARE(self), 0x1000); /* 4KB */
#else
	fu_firmware_set_size_max(FU_FIRMWARE(self), 0x1000000); /* 16MB */
#endif
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

/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2018 LongSoft
 * Copyright 2008 Apple Inc
 * Copyright 2006 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-2-Clause or LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-input-stream.h"
#include "fu-string.h"

struct _FuEfiLz77Decompressor {
	FuFirmware parent_instance;
};

/**
 * FuEfiLz77Decompressor:
 *
 * Funky LZ77 decompressor as specified by EFI. The compression design [and code] was designed for
 * a different era, and much better compression can be achieved using LZMA or zlib.
 *
 * My advice would be to not use this compression method in new designs.
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuEfiLz77Decompressor, fu_efi_lz77_decompressor, FU_TYPE_FIRMWARE)

#define BITBUFSIZ 32
#define MAXMATCH  256
#define THRESHOLD 3
#define CODE_BIT  16

/* c: char&len set; p: position set; t: extra set */
#define NC	(0xff + MAXMATCH + 2 - THRESHOLD)
#define CBIT	9
#define MAXPBIT 5
#define TBIT	5
#define MAXNP	((1U << MAXPBIT) - 1)
#define NT	(CODE_BIT + 3)
#if NT > MAXNP
#define NPT NT
#else
#define NPT MAXNP
#endif

typedef struct {
	GInputStream *stream; /* no-ref */
	GByteArray *dst;      /* no-ref */

	guint16 bit_count;
	guint32 bit_buf;
	guint32 sub_bit_buf;
	guint16 block_size;

	guint16 left[2 * NC - 1];
	guint16 right[2 * NC - 1];
	guint8 c_len[NC];
	guint8 pt_len[NPT];
	guint16 c_table[4096];
	guint16 pt_table[256];

	guint8 p_bit; /* 'position set code length array size' in block header */
} FuEfiLz77DecompressHelper;

static void
fu_efi_lz77_decompressor_memset16(guint16 *buf, gsize length, guint16 value)
{
	g_return_if_fail(length % 2 == 0);
	length /= sizeof(guint16);
	for (gsize i = 0; i < length; i++)
		buf[i] = value;
}

static gboolean
fu_efi_lz77_decompressor_read_source_bits(FuEfiLz77DecompressHelper *helper,
					  guint16 number_of_bits,
					  GError **error)
{
	/* left shift number_of_bits of bits in advance */
	helper->bit_buf = (guint32)(((guint64)helper->bit_buf) << number_of_bits);

	/* copy data needed in bytes into sub_bit_buf */
	while (number_of_bits > helper->bit_count) {
		gssize rc;
		guint8 sub_bit_buf = 0;

		number_of_bits = (guint16)(number_of_bits - helper->bit_count);
		helper->bit_buf |= (guint32)(((guint64)helper->sub_bit_buf) << number_of_bits);

		/* get 1 byte into sub_bit_buf */
		rc = g_input_stream_read(helper->stream,
					 &sub_bit_buf,
					 sizeof(sub_bit_buf),
					 NULL,
					 error);
		if (rc < 0)
			return FALSE;
		if (rc == 0) {
			/* no more bits from the source, just pad zero bit */
			helper->sub_bit_buf = 0;
		} else {
			helper->sub_bit_buf = sub_bit_buf;
		}
		helper->bit_count = 8;
	}

	/* calculate additional bit count read to update bit_count */
	helper->bit_count = (guint16)(helper->bit_count - number_of_bits);

	/* copy number_of_bits of bits from sub_bit_buf into bit_buf */
	helper->bit_buf |= helper->sub_bit_buf >> helper->bit_count;
	return TRUE;
}

static gboolean
fu_efi_lz77_decompressor_get_bits(FuEfiLz77DecompressHelper *helper,
				  guint16 number_of_bits,
				  guint16 *value,
				  GError **error)
{
	/* pop number_of_bits of bits from left */
	*value = (guint16)(helper->bit_buf >> (BITBUFSIZ - number_of_bits));

	/* fill up bit_buf from source */
	return fu_efi_lz77_decompressor_read_source_bits(helper, number_of_bits, error);
}

/* creates huffman code mapping table for extra set, char&len set and position set according to
 * code length array */
static gboolean
fu_efi_lz77_decompressor_make_huffman_table(FuEfiLz77DecompressHelper *helper,
					    guint16 number_of_symbols,
					    guint8 *code_length_array,
					    guint16 mapping_table_bits,
					    guint16 *table,
					    GError **error)
{
	guint16 count[17] = {0};
	guint16 weight[17] = {0};
	guint16 start[18] = {0};
	guint16 *pointer;
	guint16 index;
	guint16 c_char;
	guint16 ju_bits;
	guint16 avail_symbols;
	guint16 mask;
	guint16 max_table_length;

	/* the maximum mapping table width supported by this internal working function is 16 */
	if (mapping_table_bits >= (sizeof(count) / sizeof(guint16))) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "bad table");
		return FALSE;
	}

	for (index = 0; index < number_of_symbols; index++) {
		if (code_length_array[index] > 16) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "bad table");
			return FALSE;
		}
		count[code_length_array[index]]++;
	}

	for (index = 1; index <= 16; index++) {
		guint16 WordOfStart = start[index];
		guint16 WordOfCount = count[index];
		start[index + 1] = (guint16)(WordOfStart + (WordOfCount << (16 - index)));
	}

	if (start[17] != 0) {
		/*(1U << 16)*/
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "bad table");
		return FALSE;
	}

	ju_bits = (guint16)(16 - mapping_table_bits);
	for (index = 1; index <= mapping_table_bits; index++) {
		start[index] >>= ju_bits;
		weight[index] = (guint16)(1U << (mapping_table_bits - index));
	}

	while (index <= 16) {
		weight[index] = (guint16)(1U << (16 - index));
		index++;
	}

	index = (guint16)(start[mapping_table_bits + 1] >> ju_bits);
	if (index != 0) {
		guint16 index3 = (guint16)(1U << mapping_table_bits);
		if (index < index3) {
			fu_efi_lz77_decompressor_memset16(table + index,
							  (index3 - index) * sizeof(*table),
							  0);
		}
	}

	avail_symbols = number_of_symbols;
	mask = (guint16)(1U << (15 - mapping_table_bits));
	max_table_length = (guint16)(1U << mapping_table_bits);

	for (c_char = 0; c_char < number_of_symbols; c_char++) {
		guint16 len = code_length_array[c_char];
		guint16 next_code;

		if (len == 0 || len >= 17)
			continue;

		next_code = (guint16)(start[len] + weight[len]);
		if (len <= mapping_table_bits) {
			for (index = start[len]; index < next_code; index++) {
				if (index >= max_table_length) {
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_INVALID_DATA,
							    "bad table");
					return FALSE;
				}
				table[index] = c_char;
			}

		} else {
			guint16 index3 = start[len];
			pointer = &table[index3 >> ju_bits];
			index = (guint16)(len - mapping_table_bits);

			while (index != 0) {
				if (*pointer == 0 && avail_symbols < (2 * NC - 1)) {
					helper->right[avail_symbols] = helper->left[avail_symbols] =
					    0;
					*pointer = avail_symbols++;
				}
				if (*pointer < (2 * NC - 1)) {
					if ((index3 & mask) != 0)
						pointer = &helper->right[*pointer];
					else
						pointer = &helper->left[*pointer];
				}
				index3 <<= 1;
				index--;
			}
			*pointer = c_char;
		}
		start[len] = next_code;
	}
	/* success */
	return TRUE;
}

/* get a position value according to Position Huffman table */
static gboolean
fu_efi_lz77_decompressor_decode_p(FuEfiLz77DecompressHelper *helper, guint32 *value, GError **error)
{
	guint16 val;

	val = helper->pt_table[helper->bit_buf >> (BITBUFSIZ - 8)];
	if (val >= MAXNP) {
		guint32 mask = 1U << (BITBUFSIZ - 1 - 8);
		do {
			if ((helper->bit_buf & mask) != 0) {
				val = helper->right[val];
			} else {
				val = helper->left[val];
			}
			mask >>= 1;
		} while (val >= MAXNP);
	}

	/* advance what we have read */
	if (!fu_efi_lz77_decompressor_read_source_bits(helper, helper->pt_len[val], error))
		return FALSE;

	if (val > 1) {
		guint16 char_c = 0;
		if (!fu_efi_lz77_decompressor_get_bits(helper, (guint16)(val - 1), &char_c, error))
			return FALSE;
		*value = (guint32)((1U << (val - 1)) + char_c);
		return TRUE;
	}
	*value = val;
	return TRUE;
}

/* read in the extra set or position set length array, then generate the code mapping for them */
static gboolean
fu_efi_lz77_decompressor_read_pt_len(FuEfiLz77DecompressHelper *helper,
				     guint16 number_of_symbols,
				     guint16 number_of_bits,
				     guint16 special_symbol,
				     GError **error)
{
	guint16 number = 0;
	guint16 index = 0;

	/* read Extra Set Code Length Array size */
	if (!fu_efi_lz77_decompressor_get_bits(helper, number_of_bits, &number, error))
		return FALSE;

	/* fail if number or number_of_symbols is greater than size of pt_len */
	if ((number > sizeof(helper->pt_len)) || (number_of_symbols > sizeof(helper->pt_len))) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "bad table");
		return FALSE;
	}
	if (number == 0) {
		/* this represents only Huffman code used */
		guint16 char_c = 0;
		if (!fu_efi_lz77_decompressor_get_bits(helper, number_of_bits, &char_c, error))
			return FALSE;
		fu_efi_lz77_decompressor_memset16(&helper->pt_table[0],
						  sizeof(helper->pt_table),
						  (guint16)char_c);
		memset(helper->pt_len, 0, number_of_symbols);
		return TRUE;
	}

	while (index < number && index < NPT) {
		guint16 char_c = helper->bit_buf >> (BITBUFSIZ - 3);

		/* if a code length is less than 7, then it is encoded as a 3-bit value.
		 * Or it is encoded as a series of "1"s followed by a terminating "0".
		 * The number of "1"s = Code length - 4 */
		if (char_c == 7) {
			guint32 mask = 1U << (BITBUFSIZ - 1 - 3);
			while (mask & helper->bit_buf) {
				mask >>= 1;
				char_c += 1;
			}
		}

		if (!fu_efi_lz77_decompressor_read_source_bits(
			helper,
			(guint16)((char_c < 7) ? 3 : char_c - 3),
			error))
			return FALSE;

		helper->pt_len[index++] = (guint8)char_c;

		/* for code&len set, after the third length of the code length concatenation,
		 * a 2-bit value is used to indicated the number of consecutive zero lengths after
		 * the third length */
		if (index == special_symbol) {
			if (!fu_efi_lz77_decompressor_get_bits(helper, 2, &char_c, error))
				return FALSE;
			while ((gint16)(--char_c) >= 0 && index < NPT) {
				helper->pt_len[index++] = 0;
			}
		}
	}
	while (index < number_of_symbols && index < NPT)
		helper->pt_len[index++] = 0;
	return fu_efi_lz77_decompressor_make_huffman_table(helper,
							   number_of_symbols,
							   helper->pt_len,
							   8,
							   helper->pt_table,
							   error);
}

/* read in and decode the Char&len Set Code Length Array, then generate the Huffman Code mapping
 * table for the char&len set */
static gboolean
fu_efi_lz77_decompressor_read_c_len(FuEfiLz77DecompressHelper *helper, GError **error)
{
	guint16 number = 0;
	guint16 index = 0;

	if (!fu_efi_lz77_decompressor_get_bits(helper, CBIT, &number, error))
		return FALSE;
	if (number == 0) {
		/* this represents only Huffman code used */
		guint16 char_c = 0;
		if (!fu_efi_lz77_decompressor_get_bits(helper, CBIT, &char_c, error))
			return FALSE;
		memset(helper->c_len, 0, sizeof(helper->c_len));
		fu_efi_lz77_decompressor_memset16(&helper->c_table[0],
						  sizeof(helper->c_table),
						  char_c);
		return TRUE;
	}

	while (index < number && index < NC) {
		guint16 char_c = helper->pt_table[helper->bit_buf >> (BITBUFSIZ - 8)];
		if (char_c >= NT) {
			guint32 mask = 1U << (BITBUFSIZ - 1 - 8);
			do {
				if (mask & helper->bit_buf) {
					char_c = helper->right[char_c];
				} else {
					char_c = helper->left[char_c];
				}
				mask >>= 1;

			} while (char_c >= NT);
		}

		/* advance what we have read */
		if (!fu_efi_lz77_decompressor_read_source_bits(helper,
							       helper->pt_len[char_c],
							       error))
			return FALSE;

		if (char_c <= 2) {
			if (char_c == 0) {
				char_c = 1;
			} else if (char_c == 1) {
				if (!fu_efi_lz77_decompressor_get_bits(helper, 4, &char_c, error))
					return FALSE;
				char_c += 3;
			} else if (char_c == 2) {
				if (!fu_efi_lz77_decompressor_get_bits(helper,
								       CBIT,
								       &char_c,
								       error))
					return FALSE;
				char_c += 20;
			}
			while ((gint16)(--char_c) >= 0 && index < NC)
				helper->c_len[index++] = 0;
		} else {
			helper->c_len[index++] = (guint8)(char_c - 2);
		}
	}
	memset(helper->c_len + index, 0, sizeof(helper->c_len) - index);
	return fu_efi_lz77_decompressor_make_huffman_table(helper,
							   NC,
							   helper->c_len,
							   12,
							   helper->c_table,
							   error);
}

/* get one code. if it is at block boundary, generate huffman code mapping table for extra set,
 * code&len set and position set */
static gboolean
fu_efi_lz77_decompressor_decode_c(FuEfiLz77DecompressHelper *helper, guint16 *value, GError **error)
{
	guint16 index2;
	guint32 mask;

	if (helper->block_size == 0) {
		/* starting a new block, so read blocksize from block header */
		if (!fu_efi_lz77_decompressor_get_bits(helper, 16, &helper->block_size, error))
			return FALSE;

		/* read in the extra set code length array */
		if (!fu_efi_lz77_decompressor_read_pt_len(helper, NT, TBIT, 3, error)) {
			g_prefix_error(
			    error,
			    "failed to generate the Huffman code mapping table for extra set: ");
			return FALSE;
		}

		/* read in and decode the char&len set code length array */
		if (!fu_efi_lz77_decompressor_read_c_len(helper, error)) {
			g_prefix_error(error,
				       "failed to generate the code mapping table for char&len: ");
			return FALSE;
		}

		/* read in the position set code length array */
		if (!fu_efi_lz77_decompressor_read_pt_len(helper,
							  MAXNP,
							  helper->p_bit,
							  (guint16)(-1),
							  error)) {
			g_prefix_error(error,
				       "failed to generate the Huffman code mapping table for the "
				       "position set: ");
			return FALSE;
		}
	}

	/* get one code according to code&set huffman table */
	if (helper->block_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no blocks remained");
		return FALSE;
	}
	helper->block_size--;
	index2 = helper->c_table[helper->bit_buf >> (BITBUFSIZ - 12)];
	if (index2 >= NC) {
		mask = 1U << (BITBUFSIZ - 1 - 12);
		do {
			if ((helper->bit_buf & mask) != 0) {
				index2 = helper->right[index2];
			} else {
				index2 = helper->left[index2];
			}
			mask >>= 1;
		} while (index2 >= NC);
	}

	/* advance what we have read */
	if (!fu_efi_lz77_decompressor_read_source_bits(helper, helper->c_len[index2], error))
		return FALSE;
	*value = index2;
	return TRUE;
}

static gboolean
fu_efi_lz77_decompressor_internal(FuEfiLz77DecompressHelper *helper,
				  FuEfiLz77DecompressorVersion version,
				  GError **error)
{
	gsize dst_offset = 0;

	/* position set code length array size in the block header */
	switch (version) {
	case FU_EFI_LZ77_DECOMPRESSOR_VERSION_LEGACY:
		helper->p_bit = 4;
		break;
	case FU_EFI_LZ77_DECOMPRESSOR_VERSION_TIANO:
		helper->p_bit = 5;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unknown version 0x%x",
			    version);
		return FALSE;
	}

	/* fill the first BITBUFSIZ bits */
	if (!fu_efi_lz77_decompressor_read_source_bits(helper, BITBUFSIZ, error))
		return FALSE;

	/* decode each char */
	while (dst_offset < helper->dst->len) {
		guint16 char_c = 0;

		/* get one code */
		if (!fu_efi_lz77_decompressor_decode_c(helper, &char_c, error))
			return FALSE;
		if (char_c < 256) {
			/* write original character into dst_buf */
			helper->dst->data[dst_offset++] = (guint8)char_c;
		} else {
			guint16 bytes_remaining;
			guint32 data_offset;
			guint32 tmp = 0;

			/* process a pointer, so get string length */
			bytes_remaining = (guint16)(char_c - (0x00000100U - THRESHOLD));
			if (!fu_efi_lz77_decompressor_decode_p(helper, &tmp, error))
				return FALSE;
			data_offset = dst_offset - tmp - 1;

			/* write bytes_remaining of bytes into dst_buf */
			bytes_remaining--;
			while ((gint16)(bytes_remaining) >= 0) {
				if (dst_offset >= helper->dst->len) {
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_INVALID_DATA,
							    "bad pointer offset");
					return FALSE;
				}
				if (data_offset >= helper->dst->len) {
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_INVALID_DATA,
							    "bad table");
					return FALSE;
				}
				helper->dst->data[dst_offset++] = helper->dst->data[data_offset++];
				bytes_remaining--;
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_efi_lz77_decompressor_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       FwupdInstallFlags flags,
			       GError **error)
{
	gsize streamsz = 0;
	guint32 dst_bufsz;
	guint32 src_bufsz;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GError) error_all = NULL;
	g_autoptr(GByteArray) dst = g_byte_array_new();
	FuEfiLz77DecompressorVersion decompressor_versions[] = {
	    FU_EFI_LZ77_DECOMPRESSOR_VERSION_TIANO,
	    FU_EFI_LZ77_DECOMPRESSOR_VERSION_LEGACY,
	};

	/* parse header */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	st = fu_struct_efi_lz77_decompressor_header_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	src_bufsz = fu_struct_efi_lz77_decompressor_header_get_src_size(st);
	if (streamsz < src_bufsz + st->len) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "source buffer is truncated");
		return FALSE;
	}
	dst_bufsz = fu_struct_efi_lz77_decompressor_header_get_dst_size(st);
	if (dst_bufsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "destination size is zero");
		return FALSE;
	}
	if (dst_bufsz > fu_firmware_get_size_max(firmware)) {
		g_autofree gchar *sz_val = g_format_size(dst_bufsz);
		g_autofree gchar *sz_max = g_format_size(fu_firmware_get_size_max(firmware));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "destination size is too large (%s, limit %s)",
			    sz_val,
			    sz_max);
		return FALSE;
	}
	fu_byte_array_set_size(dst, dst_bufsz, 0x0);

	/* try both position */
	for (guint i = 0; i < G_N_ELEMENTS(decompressor_versions); i++) {
		FuEfiLz77DecompressHelper helper = {
		    .dst = dst,
		    .stream = stream,
		};
		g_autoptr(GError) error_local = NULL;

		if (!g_seekable_seek(G_SEEKABLE(stream), st->len, G_SEEK_SET, NULL, error))
			return FALSE;
		if (fu_efi_lz77_decompressor_internal(&helper,
						      decompressor_versions[i],
						      &error_local)) {
			g_autoptr(GBytes) blob =
			    g_byte_array_free_to_bytes(g_steal_pointer(&dst)); /* nocheck:blocked */
			if (!fu_firmware_set_stream(firmware, NULL, error))
				return FALSE;
			fu_firmware_set_bytes(firmware, blob);
			fu_firmware_set_version_raw(firmware, decompressor_versions[i]);
			return TRUE;
		}
		if (error_all == NULL) {
			g_propagate_prefixed_error(
			    &error_all,
			    g_steal_pointer(&error_local),
			    "failed to parse %s: ",
			    fu_efi_lz77_decompressor_version_to_string(decompressor_versions[i]));
			continue;
		}
		g_prefix_error(&error_all,
			       "failed to parse %s: %s: ",
			       fu_efi_lz77_decompressor_version_to_string(decompressor_versions[i]),
			       error_local->message);
	}

	/* success */
	g_propagate_error(error, g_steal_pointer(&error_all));
	return FALSE;
}

static void
fu_efi_lz77_decompressor_init(FuEfiLz77Decompressor *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 64 * 1024 * 1024);
}

static void
fu_efi_lz77_decompressor_class_init(FuEfiLz77DecompressorClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_efi_lz77_decompressor_parse;
}

/**
 * fu_efi_lz77_decompressor_new:
 *
 * Creates a new #FuFirmware that can be used to decompress LZ77.
 *
 * Since: 2.0.0
 **/
FuFirmware *
fu_efi_lz77_decompressor_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EFI_LZ77_DECOMPRESSOR, NULL));
}

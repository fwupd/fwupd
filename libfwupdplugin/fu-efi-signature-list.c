/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <string.h>

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-signature-list.h"
#include "fu-efi-signature-private.h"
#include "fu-mem.h"

/**
 * FuEfiSignatureList:
 *
 * A UEFI signature list typically found in the `PK` and `KEK` keys.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiSignatureList {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuEfiSignatureList, fu_efi_signature_list, FU_TYPE_FIRMWARE)

const guint8 FU_EFI_SIGLIST_HEADER_MAGIC[] = {0x26, 0x16, 0xC4, 0xC1, 0x4C};

static gboolean
fu_efi_signature_list_parse_item(FuEfiSignatureList *self,
				 FuEfiSignatureKind sig_kind,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize offset,
				 guint32 sig_size,
				 GError **error)
{
	fwupd_guid_t guid;
	gsize sig_datasz;
	g_autofree gchar *sig_owner = NULL;
	g_autofree guint8 *sig_data = NULL;
	g_autoptr(FuEfiSignature) sig = NULL;
	g_autoptr(GBytes) data = NULL;

	/* allocate data buf */
	if (sig_size <= sizeof(fwupd_guid_t)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureSize invalid: 0x%x",
			    (guint)sig_size);
		return FALSE;
	}
	sig_datasz = sig_size - sizeof(fwupd_guid_t);
	sig_data = g_malloc0(sig_datasz);

	/* read both blocks of data */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(guid),
			    error)) {
		g_prefix_error(error, "failed to read signature GUID: ");
		return FALSE;
	}
	if (!fu_memcpy_safe(sig_data,
			    sig_datasz,
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + sizeof(fwupd_guid_t), /* src */
			    sig_datasz,
			    error)) {
		g_prefix_error(error, "failed to read signature data: ");
		return FALSE;
	}

	/* create item */
	sig_owner = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	data = g_bytes_new(sig_data, sig_datasz);
	sig = fu_efi_signature_new(sig_kind, sig_owner);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), data);
	fu_firmware_add_image(FU_FIRMWARE(self), FU_FIRMWARE(sig));
	return TRUE;
}

static gboolean
fu_efi_signature_list_parse_list(FuEfiSignatureList *self,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize *offset,
				 GError **error)
{
	FuEfiSignatureKind sig_kind = FU_EFI_SIGNATURE_KIND_UNKNOWN;
	fwupd_guid_t guid;
	gsize offset_tmp;
	guint32 sig_header_size = 0;
	guint32 sig_list_size = 0;
	guint32 sig_size = 0;
	g_autofree gchar *sig_type = NULL;

	/* read EFI_SIGNATURE_LIST */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    *offset, /* src */
			    sizeof(guid),
			    error)) {
		g_prefix_error(error, "failed to read GUID header: ");
		return FALSE;
	}
	sig_type = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0(sig_type, "c1c41626-504c-4092-aca9-41f936934328") == 0) {
		sig_kind = FU_EFI_SIGNATURE_KIND_SHA256;
	} else if (g_strcmp0(sig_type, "a5c059a1-94e4-4aa7-87b5-ab155c2bf072") == 0) {
		sig_kind = FU_EFI_SIGNATURE_KIND_X509;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    *offset + 0x10,
				    &sig_list_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (sig_list_size < 0x1c || sig_list_size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureListSize invalid: 0x%x",
			    sig_list_size);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    *offset + 0x14,
				    &sig_header_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (sig_header_size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureHeaderSize invalid: 0x%x",
			    sig_size);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf, bufsz, *offset + 0x18, &sig_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (sig_size < sizeof(fwupd_guid_t) || sig_size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureSize invalid: 0x%x",
			    sig_size);
		return FALSE;
	}

	/* header is typically unused */
	offset_tmp = *offset + 0x1c + sig_header_size;
	for (guint i = 0; i < (sig_list_size - 0x1c) / sig_size; i++) {
		if (!fu_efi_signature_list_parse_item(self,
						      sig_kind,
						      buf,
						      bufsz,
						      offset_tmp,
						      sig_size,
						      error))
			return FALSE;
		offset_tmp += sig_size;
	}
	*offset += sig_list_size;
	return TRUE;
}

static gchar *
fu_efi_signature_list_get_version(FuEfiSignatureList *self)
{
	guint csum_cnt = 0;
	const gchar *ignored_guids[] = {FU_EFI_SIGNATURE_GUID_OVMF,
					FU_EFI_SIGNATURE_GUID_OVMF_LEGACY,
					NULL};
	g_autofree gchar *checksum_last = NULL;
	g_autoptr(GPtrArray) sigs = NULL;
	struct {
		const gchar *checksum;
		guint version;
	} known_checksums[] = {
	    /* DBXUpdate-20100307.x64.bin */
	    {"5391c3a2fb112102a6aa1edc25ae77e19f5d6f09cd09eeb2509922bfcd5992ea", 9},
	    /* DBXUpdate-20140413.x64.bin */
	    {"90fbe70e69d633408d3e170c6832dbb2d209e0272527dfb63d49d29572a6f44c", 13},
	    /* DBXUpdate-20160809.x64.bin */
	    {"45c7c8ae750acfbb48fc37527d6412dd644daed8913ccd8a24c94d856967df8e", 77},
	    /* DBXUpdate-20200729.x64.bin */
	    {"540801dd345dc1c33ef431b35bf4c0e68bd319b577b9abe1a9cff1cbc39f548f", 190},
	    /* DBXUpdate-20200729.aa64.bin */
	    {"8c8183ad9b96fe1f3c74dedb8087469227b642afe2e80f8fd22e0137c11c7d90", 19},
	    /* DBXUpdate-20200729.ia32.bin */
	    {"a7dfcc3a8d6ab30f93f31748dbc8ea38415cf52bb9ad8085672cd9ab8938d5de", 41},
	    /* DBXUpdate-20210429.x64.bin */
	    {"af79b14064601bc0987d4747af1e914a228c05d622ceda03b7a4f67014fee767", 211},
	    /* DBXUpdate-20210429.aa64.bin */
	    {"b133de42a37376f5d91439af3d61d38201f10377c36dacd9c2610f52aa124a91", 21},
	    /* DBXUpdate-20210429.ia32.bin */
	    {"a8a3300e33a0a2692839ccba84803c5e742d12501b6d58c46eb87f32017f2cff", 55},
	    /* DBXUpdate-20220812.x64.bin */
	    {"90aec5c4995674a849c1d1384463f3b02b5aa625a5c320fc4fe7d9bb58a62398", 217},
	    /* DBXUpdate-20220812.aa64.bin - only X509 certificates removed */
	    /* DBXUpdate-20220812.ia32.bin - only X509 certificates removed */
	    {NULL, 0}};

	sigs = fu_firmware_get_images(FU_FIRMWARE(self));
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_SHA256)
			continue;
		if (g_strv_contains(ignored_guids, fu_efi_signature_get_owner(sig)))
			continue;

		/* save the last hash in the list */
		if (i == sigs->len - 1) {
			g_autoptr(GError) error_local = NULL;
			checksum_last = fu_firmware_get_checksum(FU_FIRMWARE(sig),
								 G_CHECKSUM_SHA256,
								 &error_local);
			if (checksum_last == NULL) {
				g_warning("failed to get checksum for signature %u: %s",
					  i,
					  error_local->message);
			}
		}

		csum_cnt++;
	}

	/*
	 * Microsoft seems to actually remove checksums in UEFI dbx updates, which I'm guessing is
	 * a result from OEM pressure about SPI usage -- but local dbx updates are append-only.
	 *
	 * That means that if you remove hashes then you can have a different set of dbx checksums
	 * on your machine depending on whether you went A->B->C->D or A->D...
	 *
	 * If we use the metric of "count the number of SHA256 checksums from MS" then we might
	 * overcount (due to the now-removed entries) -- in some cases enough to not actually apply
	 * the new update at all.
	 *
	 * In these cases look at the *last* dbx checksum and compare to the set we know to see if
	 * we need to artificially lower the reported version. This isn't helped by having *zero*
	 * visibility in the reason that entries were removed or added.
	 *
	 * This also fixes the DBX update 20200729 which added 4 duplicate entries, which should be
	 * rectified during the SetVariable(), so they're only really a problem for transactional
	 * size limits. But we all noticed that load-bearing *should* word there, didn't we.
	 */
	for (guint i = 0; checksum_last != NULL && known_checksums[i].checksum != NULL; i++) {
		if (g_strcmp0(checksum_last, known_checksums[i].checksum) == 0) {
			if (csum_cnt != known_checksums[i].version) {
				g_debug("fixing signature list version from %u to %u as "
					"last dbx checksum was %s",
					csum_cnt,
					known_checksums[i].version,
					checksum_last);
				csum_cnt = known_checksums[i].version;
			}
			break;
		}
	}

	return g_strdup_printf("%u", csum_cnt);
}

static gboolean
fu_efi_signature_list_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	fwupd_guid_t guid = {0x0};
	g_autofree gchar *sig_type = NULL;

	/* read EFI_SIGNATURE_LIST */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    offset, /* src */
			    sizeof(guid),
			    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	sig_type = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0(sig_type, "c1c41626-504c-4092-aca9-41f936934328") != 0 &&
	    g_strcmp0(sig_type, "a5c059a1-94e4-4aa7-87b5-ab155c2bf072") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid magic for file");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_efi_signature_list_parse(FuFirmware *firmware,
			    GBytes *fw,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuEfiSignatureList *self = FU_EFI_SIGNATURE_LIST(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *version_str = NULL;

	/* parse each EFI_SIGNATURE_LIST */
	while (offset < bufsz) {
		if (!fu_efi_signature_list_parse_list(self, buf, bufsz, &offset, error))
			return FALSE;
	}

	/* set version */
	version_str = fu_efi_signature_list_get_version(self);
	if (version_str != NULL)
		fu_firmware_set_version(firmware, version_str);

	/* success */
	return TRUE;
}

static GBytes *
fu_efi_signature_list_write(FuFirmware *firmware, GError **error)
{
	GByteArray *buf = g_byte_array_new();

	/* SignatureType */
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, 0x0);

	/* SignatureListSize */
	fu_byte_array_append_uint32(buf, 16 + 4 + 4 + 4 + 16 + 32, G_LITTLE_ENDIAN);

	/* SignatureHeaderSize */
	fu_byte_array_append_uint32(buf, 0, G_LITTLE_ENDIAN);

	/* SignatureSize */
	fu_byte_array_append_uint32(buf, 16 + 32, G_LITTLE_ENDIAN);

	/* SignatureOwner */
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, '1');

	/* SignatureData */
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, '2');

	return g_byte_array_free_to_bytes(buf);
}

/**
 * fu_efi_signature_list_new:
 *
 * Creates a new #FuFirmware that can parse an EFI_SIGNATURE_LIST
 *
 * Since: 1.5.5
 **/
FuFirmware *
fu_efi_signature_list_new(void)
{
	return g_object_new(FU_TYPE_EFI_SIGNATURE_LIST, NULL);
}

static void
fu_efi_signature_list_class_init(FuEfiSignatureListClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_efi_signature_list_check_magic;
	klass_firmware->parse = fu_efi_signature_list_parse;
	klass_firmware->write = fu_efi_signature_list_write;
}

static void
fu_efi_signature_list_init(FuEfiSignatureList *self)
{
}

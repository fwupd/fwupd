/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-sbat-firmware.h"

struct _FuUefiSbatFirmware {
	FuCsvFirmware parent_instance;
};

G_DEFINE_TYPE(FuUefiSbatFirmware, fu_uefi_sbat_firmware, FU_TYPE_CSV_FIRMWARE)

static gboolean
fu_uefi_sbat_firmware_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	guint semver[] = {0, 0, 0};
	g_autofree gchar *debug_str = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GPtrArray) debug = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) imgs = NULL;

	/* FuCsvFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_uefi_sbat_firmware_parent_class)
		 ->parse(firmware, stream, offset, flags, error))
		return FALSE;

	imgs = fu_firmware_get_images(firmware);
	for (guint i = 0; i < imgs->len; i++) {
		FuCsvEntry *entry = g_ptr_array_index(imgs, i);
		const gchar *name = fu_firmware_get_id(FU_FIRMWARE(entry));
		guint64 component_version = fu_firmware_get_version_raw(FU_FIRMWARE(entry));
		guint semver_index = 2;

		/* sanity check */
		if (name == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "entry has no name");
			return FALSE;
		}

		/* parse generation */
		g_ptr_array_add(debug, g_strdup_printf("%s:%u", name, (guint)component_version));
		if (g_strcmp0(name, "sbat") == 0) {
			semver_index = 0;
		} else if (g_strstr_len(name, -1, ".") == NULL) {
			semver_index = 1;
		}
		semver[semver_index] += component_version;
	}

	/* success */
	version = g_strdup_printf("%u.%u.%u", semver[0], semver[1], semver[2]);
	fu_firmware_set_version(firmware, version);

	/* for debugging */
	debug_str = fu_strjoin(", ", debug);
	g_debug("%s -> %s", debug_str, version);
	return TRUE;
}

static gboolean
fu_uefi_sbat_firmware_check_compatible(FuFirmware *firmware,
				       FuFirmware *other,
				       FwupdInstallFlags flags,
				       GError **error)
{
	g_autoptr(FuFirmware) esp_sbat = NULL;
	g_autoptr(GPtrArray) revocation_entries = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_UEFI_SBAT_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(FU_IS_PEFILE_FIRMWARE(other), FALSE);

	/* find the .sbat section in the PE file */
	esp_sbat = fu_firmware_get_image_by_id(other, ".sbat", &error_local);
	if (esp_sbat == NULL) {
		g_debug("%s was ignored: %s",
			fu_firmware_get_filename(other),
			error_local->message);
		return TRUE;
	}

	/* the revocation data */
	revocation_entries = fu_firmware_get_images(firmware);
	for (guint i = 0; i < revocation_entries->len; i++) {
		FuFirmware *revocation_entry = g_ptr_array_index(revocation_entries, i);
		g_autoptr(FuFirmware) esp_entry = NULL;

		esp_entry = fu_firmware_get_image_by_id(esp_sbat,
							fu_firmware_get_id(revocation_entry),
							NULL);
		if (esp_entry == NULL) {
			g_debug("no %s SBAT entry in %s",
				fu_firmware_get_id(revocation_entry),
				fu_firmware_get_filename(other));
			continue;
		}
		g_debug("%s has SBAT entry %s v%u, revocation has v%u",
			fu_firmware_get_filename(other),
			fu_firmware_get_id(revocation_entry),
			(guint)fu_firmware_get_version_raw(esp_entry),
			(guint)fu_firmware_get_version_raw(revocation_entry));
		if (fu_firmware_get_version_raw(revocation_entry) >
		    fu_firmware_get_version_raw(esp_entry)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_SIGNATURE_INVALID,
				    "ESP file %s has SBAT entry %s v%u, but revocation has v%u",
				    fu_firmware_get_filename(other),
				    fu_firmware_get_id(revocation_entry),
				    (guint)fu_firmware_get_version_raw(esp_entry),
				    (guint)fu_firmware_get_version_raw(revocation_entry));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_sbat_firmware_init(FuUefiSbatFirmware *self)
{
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(self), "$id");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(self), "$version_raw");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(self), "timestamp");
}

static void
fu_uefi_sbat_firmware_class_init(FuUefiSbatFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_uefi_sbat_firmware_parse;
	firmware_class->check_compatible = fu_uefi_sbat_firmware_check_compatible;
}

FuFirmware *
fu_uefi_sbat_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_UEFI_SBAT_FIRMWARE, NULL));
}

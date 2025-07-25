/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-firmware.h"

#define MAXSECTIONS 2

struct _FuCrosEcFirmware {
	FuFmapFirmware parent_instance;
	GPtrArray *sections;
};

G_DEFINE_TYPE(FuCrosEcFirmware, fu_cros_ec_firmware, FU_TYPE_FMAP_FIRMWARE)

gboolean
fu_cros_ec_firmware_pick_sections(FuCrosEcFirmware *self, guint32 writeable_offset, GError **error)
{
	gboolean found = FALSE;

	for (gsize i = 0; i < self->sections->len; i++) {
		FuCrosEcFirmwareSection *section = g_ptr_array_index(self->sections, i);
		guint32 offset = section->offset;

		if (offset != writeable_offset)
			continue;

		section->ustatus = FU_CROS_EC_FIRMWARE_UPGRADE_STATUS_NEEDED;
		found = TRUE;
	}

	if (!found) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "no writable section found with offset: 0x%x",
			    writeable_offset);
		return FALSE;
	}

	/* success */
	return TRUE;
}

GPtrArray *
fu_cros_ec_firmware_get_needed_sections(FuCrosEcFirmware *self, GError **error)
{
	g_autoptr(GPtrArray) needed_sections = g_ptr_array_new();

	for (guint i = 0; i < self->sections->len; i++) {
		FuCrosEcFirmwareSection *section = g_ptr_array_index(self->sections, i);
		if (section->ustatus != FU_CROS_EC_FIRMWARE_UPGRADE_STATUS_NEEDED)
			continue;
		g_ptr_array_add(needed_sections, section);
	}
	if (needed_sections->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no needed sections");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&needed_sections);
}

gboolean
fu_cros_ec_firmware_ensure_version(FuCrosEcFirmware *self, GError **error)
{
	for (gsize i = 0; i < self->sections->len; i++) {
		gboolean rw = FALSE;
		FuCrosEcFirmwareSection *section = g_ptr_array_index(self->sections, i);
		const gchar *fmap_name;
		const gchar *fmap_fwid_name;
		g_autoptr(FuCrosEcVersion) version = NULL;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(FuFirmware) fwid_img = NULL;
		g_autoptr(GBytes) payload_bytes = NULL;
		g_autoptr(GBytes) fwid_bytes = NULL;

		if (g_strcmp0(section->name, "RO") == 0) {
			fmap_name = "EC_RO";
			fmap_fwid_name = "RO_FRID";
		} else if (g_strcmp0(section->name, "RW") == 0) {
			rw = TRUE;
			fmap_name = "EC_RW";
			fmap_fwid_name = "RW_FWID";
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "incorrect section name");
			return FALSE;
		}

		img = fu_firmware_get_image_by_id(FU_FIRMWARE(self), fmap_name, error);
		if (img == NULL) {
			g_prefix_error(error, "%s image not found: ", fmap_name);
			return FALSE;
		}

		fwid_img = fu_firmware_get_image_by_id(FU_FIRMWARE(self), fmap_fwid_name, error);
		if (fwid_img == NULL) {
			g_prefix_error(error, "%s image not found: ", fmap_fwid_name);
			return FALSE;
		}
		fwid_bytes = fu_firmware_write(fwid_img, error);
		if (fwid_bytes == NULL) {
			g_prefix_error(error, "unable to get bytes from %s: ", fmap_fwid_name);
			return FALSE;
		}
		if (!fu_memcpy_safe((guint8 *)section->raw_version,
				    FU_FMAP_FIRMWARE_STRLEN,
				    0x0,
				    g_bytes_get_data(fwid_bytes, NULL),
				    g_bytes_get_size(fwid_bytes),
				    0x0,
				    g_bytes_get_size(fwid_bytes),
				    error))
			return FALSE;

		payload_bytes = fu_firmware_write(img, error);
		if (payload_bytes == NULL) {
			g_prefix_error(error, "unable to get bytes from %s: ", fmap_name);
			return FALSE;
		}
		section->offset = fu_firmware_get_addr(img);
		section->size = g_bytes_get_size(payload_bytes);
		fu_firmware_set_version(img, section->raw_version);
		section->image_idx = fu_firmware_get_idx(img);

		version = fu_cros_ec_version_parse(section->raw_version, error);
		if (version == NULL) {
			g_prefix_error(error,
				       "failed parsing firmware's version: %32s: ",
				       section->raw_version);
			return FALSE;
		}

		if (rw) {
			g_autoptr(FuCrosEcVersion) version_rw = NULL;
			version_rw = fu_cros_ec_version_parse(section->raw_version, error);
			if (version_rw == NULL) {
				g_prefix_error(error,
					       "failed parsing firmware's version: %32s: ",
					       section->raw_version);
				return FALSE;
			}
			fu_firmware_set_version(FU_FIRMWARE(self), version_rw->triplet);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_cros_ec_firmware_init(FuCrosEcFirmware *self)
{
	FuCrosEcFirmwareSection *section;

	self->sections = g_ptr_array_new_with_free_func(g_free);
	section = g_new0(FuCrosEcFirmwareSection, 1);
	section->name = "RO";
	g_ptr_array_add(self->sections, section);
	section = g_new0(FuCrosEcFirmwareSection, 1);
	section->name = "RW";
	g_ptr_array_add(self->sections, section);
}

static void
fu_cros_ec_firmware_finalize(GObject *object)
{
	FuCrosEcFirmware *self = FU_CROS_EC_FIRMWARE(object);
	g_ptr_array_free(self->sections, TRUE);
	G_OBJECT_CLASS(fu_cros_ec_firmware_parent_class)->finalize(object);
}

static void
fu_cros_ec_firmware_class_init(FuCrosEcFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cros_ec_firmware_finalize;
}

FuFirmware *
fu_cros_ec_firmware_new(void)
{
	return g_object_new(FU_TYPE_CROS_EC_FIRMWARE, NULL);
}

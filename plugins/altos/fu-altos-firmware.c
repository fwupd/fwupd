/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gelf.h>
#include <libelf.h>

#include "fu-altos-firmware.h"

struct _FuAltosFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuAltosFirmware, fu_altos_firmware, FU_TYPE_FIRMWARE)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Elf, elf_end);
#pragma clang diagnostic pop

static gboolean
fu_altos_firmware_parse (FuFirmware *firmware,
			 GBytes *blob,
			 guint64 addr_start,
			 guint64 addr_end,
			 FwupdInstallFlags flags,
			 GError **error)
{
	const gchar *name;
	Elf_Scn *scn = NULL;
	GElf_Shdr shdr;
	size_t shstrndx;
	g_autoptr(Elf) e = NULL;

	/* load library */
	if (elf_version (EV_CURRENT) == EV_NONE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "ELF library init failed: %s",
			     elf_errmsg (-1));
		return FALSE;
	}

	/* parse data */
	e = elf_memory ((gchar *) g_bytes_get_data (blob, NULL),
			g_bytes_get_size (blob));
	if (e == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load data as ELF: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	if (elf_kind (e) != ELF_K_ELF) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "not a supported ELF format: %s",
			     elf_errmsg (-1));
		return FALSE;
	}

	/* add interesting section */
	if (elf_getshdrstrndx (e, &shstrndx) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid ELF file: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	while ((scn = elf_nextscn (e, scn)) != NULL ) {
		if (gelf_getshdr (scn, &shdr) != & shdr)
			continue;

		/* not program data with the same section name */
		if (shdr.sh_type != SHT_PROGBITS)
			continue;
		if ((name = elf_strptr (e, shstrndx, shdr.sh_name)) == NULL)
			continue;

		if (g_strcmp0 (name, ".text") == 0) {
			Elf_Data *data = elf_getdata (scn, NULL);
			if (data != NULL && data->d_buf != NULL) {
				g_autoptr(FuFirmwareImage) img = NULL;
				g_autoptr(GBytes) bytes = NULL;
				bytes = g_bytes_new (data->d_buf, data->d_size);
				img = fu_firmware_image_new (bytes);
				fu_firmware_image_set_addr (img, shdr.sh_addr);
				fu_firmware_add_image (firmware, img);
			}
			return TRUE;
		}
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "no firmware found in ELF file");
	return FALSE;
}

static void
fu_altos_firmware_class_init (FuAltosFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_altos_firmware_parse;
}

static void
fu_altos_firmware_init (FuAltosFirmware *self)
{
}

FuFirmware *
fu_altos_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_ALTOS_FIRMWARE, NULL));
}

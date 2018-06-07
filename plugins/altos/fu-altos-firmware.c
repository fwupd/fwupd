/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>
#include <gelf.h>
#include <libelf.h>

#include "fu-altos-firmware.h"
#include "fwupd-error.h"

struct _FuAltosFirmware {
	GObject			 parent_instance;
	GBytes			*data;
	guint64			 address;
};

G_DEFINE_TYPE (FuAltosFirmware, fu_altos_firmware, G_TYPE_OBJECT)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Elf, elf_end);
#pragma clang diagnostic pop

GBytes *
fu_altos_firmware_get_data (FuAltosFirmware *self)
{
	return self->data;
}

guint64
fu_altos_firmware_get_address (FuAltosFirmware *self)
{
	return self->address;
}

gboolean
fu_altos_firmware_parse (FuAltosFirmware *self, GBytes *blob, GError **error)
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
				self->data = g_bytes_new (data->d_buf, data->d_size);
				self->address = shdr.sh_addr;
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
fu_altos_firmware_finalize (GObject *object)
{
	FuAltosFirmware *self = FU_ALTOS_FIRMWARE (object);

	if (self->data != NULL)
		g_bytes_unref (self->data);

	G_OBJECT_CLASS (fu_altos_firmware_parent_class)->finalize (object);
}

static void
fu_altos_firmware_class_init (FuAltosFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_altos_firmware_finalize;
}

static void
fu_altos_firmware_init (FuAltosFirmware *self)
{
}

FuAltosFirmware *
fu_altos_firmware_new (void)
{
	FuAltosFirmware *self;
	self = g_object_new (FU_TYPE_ALTOS_FIRMWARE, NULL);
	return FU_ALTOS_FIRMWARE (self);
}

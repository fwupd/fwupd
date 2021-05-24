/*
 * Copyright (C) 2021 3mdeb Embedded Systems Consulting
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-uefi-common.h"
#include "fu-uefi-esrt.h"

struct _FuUefiEsrtEntry {
	GObject		 parent_instance;
	gchar		*path;
};

G_DEFINE_TYPE (FuUefiEsrtEntry, fu_uefi_esrt_entry, G_TYPE_OBJECT)

struct _FuUefiEsrt {
	GObject		 parent_instance;
	GPtrArray	*entries;		/* of FuUefiEsrtEntry */
};

G_DEFINE_TYPE (FuUefiEsrt, fu_uefi_esrt, G_TYPE_OBJECT)

static FuUefiEsrtEntry *
fu_uefi_esrt_entry_new (gchar *path)
{
	FuUefiEsrtEntry *self = g_object_new (FU_TYPE_UEFI_ESRT_ENTRY, NULL);
	self->path = path;
	return FU_UEFI_ESRT_ENTRY (self);
}

static void
fu_uefi_esrt_entry_init (FuUefiEsrtEntry *self)
{
}

static void
fu_uefi_esrt_entry_finalize (GObject *object)
{
	FuUefiEsrtEntry *self = FU_UEFI_ESRT_ENTRY (object);
	g_free (self->path);
	G_OBJECT_CLASS (fu_uefi_esrt_parent_class)->finalize (object);
}

static void
fu_uefi_esrt_entry_class_init (FuUefiEsrtEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_uefi_esrt_entry_finalize;
}

static gint
fu_uefi_entry_sort_cb (gconstpointer a, gconstpointer b)
{
	const FuUefiEsrtEntry *obja = *((const FuUefiEsrtEntry **) a);
	const FuUefiEsrtEntry *objb = *((const FuUefiEsrtEntry **) b);
	return g_strcmp0 (obja->path, objb->path);
}

gboolean
fu_uefi_esrt_setup (FuUefiEsrt *self, GError **error)
{
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrt_path = NULL;
	g_autofree gchar *esrt_entries = NULL;
	const gchar *fn = NULL;
	g_autoptr(GDir) dir = NULL;

	g_return_val_if_fail (FU_IS_UEFI_ESRT (self), FALSE);

	/* get the directory of ESRT entries */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	esrt_path = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);

	/* search ESRT */
	esrt_entries = g_build_filename (esrt_path, "entries", NULL);
	dir = g_dir_open (esrt_entries, 0, error);
	g_return_val_if_fail (dir != NULL, FALSE);
	while ((fn = g_dir_read_name (dir)) != NULL) {
		gchar *path = g_build_filename (esrt_entries, fn, NULL);
		g_ptr_array_add (self->entries, fu_uefi_esrt_entry_new (path));
	}

	/* sort by name */
	g_ptr_array_sort (self->entries, fu_uefi_entry_sort_cb);

	/* success */
	return TRUE;
}

static void
fu_uefi_esrt_finalize (GObject *object)
{
	FuUefiEsrt *self = FU_UEFI_ESRT (object);
	g_ptr_array_unref (self->entries);
	G_OBJECT_CLASS (fu_uefi_esrt_parent_class)->finalize (object);
}

static void
fu_uefi_esrt_class_init (FuUefiEsrtClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_uefi_esrt_finalize;
}

static void
fu_uefi_esrt_init (FuUefiEsrt *self)
{
	self->entries = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

FuUefiEsrt *
fu_uefi_esrt_new (void)
{
	FuUefiEsrt *self = g_object_new (FU_TYPE_UEFI_ESRT, NULL);
	return FU_UEFI_ESRT (self);
}

GPtrArray *
fu_uefi_esrt_get_entries (FuUefiEsrt *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT (self), NULL);

	return self->entries;
}

const gchar *
fu_uefi_esrt_entry_get_id (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), NULL);
	return self->path;
}

gchar *
fu_uefi_esrt_entry_get_class (FuUefiEsrtEntry *self)
{
	g_autofree gchar *fw_class_fn = NULL;
	gchar *fw_class = NULL;

	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), NULL);

	fw_class_fn = g_build_filename (self->path, "fw_class", NULL);
	if (g_file_get_contents (fw_class_fn, &fw_class, NULL, NULL))
		g_strdelimit (fw_class, "\n", '\0');

	return fw_class;
}

guint32
fu_uefi_esrt_entry_get_kind (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_read_file_as_uint64 (self->path, "fw_type");
}

guint32
fu_uefi_esrt_entry_get_capsule_flags (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_read_file_as_uint64 (self->path, "capsule_flags");
}

guint32
fu_uefi_esrt_entry_get_version (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_read_file_as_uint64 (self->path, "fw_version");
}

guint32
fu_uefi_esrt_entry_get_version_lowest (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_read_file_as_uint64 (self->path,
                                            "lowest_supported_fw_version");
}

guint32
fu_uefi_esrt_entry_get_status (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_read_file_as_uint64 (self->path, "last_attempt_status");
}

guint32
fu_uefi_esrt_entry_get_version_error (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_read_file_as_uint64 (self->path, "last_attempt_version");
}

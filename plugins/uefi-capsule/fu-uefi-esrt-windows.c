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
};

G_DEFINE_TYPE (FuUefiEsrtEntry, fu_uefi_esrt_entry, G_TYPE_OBJECT)

struct _FuUefiEsrt {
	GObject		 parent_instance;
	GPtrArray	*entries;		/* of FuUefiEsrtEntry */
};

G_DEFINE_TYPE (FuUefiEsrt, fu_uefi_esrt, G_TYPE_OBJECT)

static void
fu_uefi_esrt_entry_init (FuUefiEsrtEntry *self)
{
}

static void
fu_uefi_esrt_entry_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_uefi_esrt_parent_class)->finalize (object);
}

static void
fu_uefi_esrt_entry_class_init (FuUefiEsrtEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_uefi_esrt_entry_finalize;
}

gboolean
fu_uefi_esrt_setup (FuUefiEsrt *self, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "ESRT access wasn't implemented for Windows");
	return FALSE;
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
	return "";
}

gchar *
fu_uefi_esrt_entry_get_class (FuUefiEsrtEntry *self)
{
	return NULL;
}

guint32
fu_uefi_esrt_entry_get_kind (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return 0x0;
}

guint32
fu_uefi_esrt_entry_get_capsule_flags (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return 0x0;
}

guint32
fu_uefi_esrt_entry_get_version (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return 0x0;
}

guint32
fu_uefi_esrt_entry_get_version_lowest (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return 0x0;
}

guint32
fu_uefi_esrt_entry_get_status (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return 0x0;
}

guint32
fu_uefi_esrt_entry_get_version_error (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return 0x0;
}

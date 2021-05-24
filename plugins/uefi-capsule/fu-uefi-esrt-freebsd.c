/*
 * Copyright (C) 2021 3mdeb Embedded Systems Consulting
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/types.h>
#include <sys/sysctl.h>

#include "fu-common.h"
#include "fu-uefi-common.h"
#include "fu-uefi-esrt.h"

struct _FuUefiEsrtEntry {
	GObject		 parent_instance;
	gchar		*sysctl_name;
};

G_DEFINE_TYPE (FuUefiEsrtEntry, fu_uefi_esrt_entry, G_TYPE_OBJECT)

struct _FuUefiEsrt {
	GObject		 parent_instance;
	GPtrArray	*entries;		/* of FuUefiEsrtEntry */
};

G_DEFINE_TYPE (FuUefiEsrt, fu_uefi_esrt, G_TYPE_OBJECT)

static FuUefiEsrtEntry *
fu_uefi_esrt_entry_new (gint idx)
{
	FuUefiEsrtEntry *self = g_object_new (FU_TYPE_UEFI_ESRT_ENTRY, NULL);
	self->sysctl_name = g_strdup_printf ("hw.efi.esrt.entry%d", idx);
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
	g_free (self->sysctl_name);
	G_OBJECT_CLASS (fu_uefi_esrt_parent_class)->finalize (object);
}

static void
fu_uefi_esrt_entry_class_init (FuUefiEsrtEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_uefi_esrt_entry_finalize;
}

static gchar *
fu_uefi_get_sysctl_string (const gchar *name)
{
	g_autofree gchar *value = NULL;
	size_t len = 0;

	if (sysctlbyname (name, NULL, &len, NULL, 0))
		return NULL;

	value = g_malloc (len);
	if (sysctlbyname (name, value, &len, NULL, 0))
		return NULL;

	return g_steal_pointer (&value);
}

static guint64
fu_uefi_get_sysctl_uint64 (const gchar *name)
{
	g_autofree gchar *value = fu_uefi_get_sysctl_string (name);
	return fu_common_strtoull (value);
}

gboolean
fu_uefi_esrt_setup (FuUefiEsrt *self, GError **error)
{
	guint entry_count = 0;

	g_return_val_if_fail (FU_IS_UEFI_ESRT (self), FALSE);

	entry_count = fu_uefi_get_sysctl_uint64 ("hw.efi.esrt.fw_resource_count");
	for (guint i = 0; i < entry_count; i++)
		g_ptr_array_add (self->entries, fu_uefi_esrt_entry_new (i));

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
	return self->sysctl_name;
}

gchar *
fu_uefi_esrt_entry_get_class (FuUefiEsrtEntry *self)
{
	g_autofree gchar *name = NULL;

	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), NULL);

	name = g_build_path (".", self->sysctl_name, "fw_class", NULL);
	return fu_uefi_get_sysctl_string (name);
}

static guint64
fu_uefi_get_entry_field (FuUefiEsrtEntry *entry, const gchar *field_name)
{
	g_autofree gchar *name = g_build_path (".", entry->sysctl_name,
                                               field_name, NULL);
	return fu_uefi_get_sysctl_uint64 (name);
}

guint32
fu_uefi_esrt_entry_get_kind (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_get_entry_field (self, "fw_type");
}

guint32
fu_uefi_esrt_entry_get_capsule_flags (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_get_entry_field (self, "capsule_flags");
}

guint32
fu_uefi_esrt_entry_get_version (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_get_entry_field (self, "fw_version");
}

guint32
fu_uefi_esrt_entry_get_version_lowest (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_get_entry_field (self, "lowest_supported_fw_version");
}

guint32
fu_uefi_esrt_entry_get_status (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_get_entry_field (self, "last_attempt_status");
}

guint32
fu_uefi_esrt_entry_get_version_error (FuUefiEsrtEntry *self)
{
	g_return_val_if_fail (FU_IS_UEFI_ESRT_ENTRY (self), 0x0);
	return fu_uefi_get_entry_field (self, "last_attempt_version");
}

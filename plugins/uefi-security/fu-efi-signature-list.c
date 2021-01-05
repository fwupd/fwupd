/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-signature-list.h"

struct _FuEfiSignatureList {
	GObject			 parent_instance;
	FuEfiSignatureKind	 kind;
	GPtrArray		*items;		/* element-type: FuEfiSignature */
};

G_DEFINE_TYPE (FuEfiSignatureList, fu_efi_signature_list, G_TYPE_OBJECT)

FuEfiSignatureList *
fu_efi_signature_list_new (FuEfiSignatureKind kind)
{
	g_autoptr(FuEfiSignatureList) self = g_object_new (FU_TYPE_EFI_SIGNATURE_LIST, NULL);
	self->kind = kind;
	return g_steal_pointer (&self);
}

FuEfiSignatureKind
fu_efi_signature_list_get_kind (FuEfiSignatureList *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE_LIST (self), 0);
	return self->kind;
}

GPtrArray *
fu_efi_signature_list_get_all (FuEfiSignatureList *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE_LIST (self), NULL);
	return self->items;
}

void
fu_efi_signature_list_add (FuEfiSignatureList *self, FuEfiSignature *signature)
{
	g_return_if_fail (FU_IS_EFI_SIGNATURE_LIST (self));
	g_ptr_array_add (self->items, g_object_ref (signature));
}

gboolean
fu_efi_signature_list_has_checksum (FuEfiSignatureList *self, const gchar *checksum)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE_LIST (self), FALSE);
	for (guint i = 0; i < self->items->len; i++) {
		FuEfiSignature *item = g_ptr_array_index (self->items, i);
		if (g_strcmp0 (fu_efi_signature_get_checksum (item), checksum) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
fu_efi_signature_list_finalize (GObject *obj)
{
	FuEfiSignatureList *self = FU_EFI_SIGNATURE_LIST (obj);
	g_ptr_array_unref (self->items);
	G_OBJECT_CLASS (fu_efi_signature_list_parent_class)->finalize (obj);
}

static void
fu_efi_signature_list_class_init (FuEfiSignatureListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_efi_signature_list_finalize;
}

static void
fu_efi_signature_list_init (FuEfiSignatureList *self)
{
	self->items = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

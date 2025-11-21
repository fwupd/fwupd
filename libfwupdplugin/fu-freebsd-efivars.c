/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Norbert Kamiński <norbert.kaminski@3mdeb.com>
 * Copyright 2021 Michał Kopeć <michal.kopec@3mdeb.com>
 * Copyright 2021 Sergii Dmytruk <sergii.dmytruk@3mdeb.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFreebsdEfivars"

#include "config.h"

#include <efivar.h>

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-freebsd-efivars.h"

struct _FuFreebsdEfivars {
	FuEfivars parent_instance;
};

G_DEFINE_TYPE(FuFreebsdEfivars, fu_freebsd_efivars, FU_TYPE_EFIVARS)

static gboolean
fu_freebsd_efivars_supported(FuEfivars *efivars, GError **error)
{
	if (efi_variables_supported() == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "kernel efivars support missing");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_freebsd_efivars_delete(FuEfivars *efivars, const gchar *guid, const gchar *name, GError **error)
{
	efi_guid_t guidt;
	efi_str_to_guid(guid, &guidt);

	if (efi_del_variable(guidt, name) == 0)
		return TRUE;

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "failed to delete efivars %s",
		    name);
	return FALSE;
}

static gboolean
fu_freebsd_efivars_delete_with_glob(FuEfivars *efivars,
				    const gchar *guid,
				    const gchar *name_glob,
				    GError **error)
{
	efi_guid_t *guidt = NULL;
	gchar *name = NULL;
	gboolean rv = FALSE;
	efi_guid_t guid_to_delete;

	efi_str_to_guid(guid, &guid_to_delete);

	while (efi_get_next_variable_name(&guidt, &name)) {
		if (memcmp(&guid_to_delete, guidt, sizeof(guid_to_delete)) != 0)
			continue;
		if (!g_pattern_match_simple(name, name_glob))
			continue;
		rv = fu_freebsd_efivars_delete(efivars, guid, name, error);
		if (!rv)
			break;
	}
	return rv;
}

static gboolean
fu_freebsd_efivars_exists_guid(const gchar *guid)
{
	efi_guid_t *guidt = NULL;
	gchar *name = NULL;
	efi_guid_t test;

	efi_str_to_guid(guid, &test);
	while (efi_get_next_variable_name(&guidt, &name)) {
		if (memcmp(&test, guidt, sizeof(test)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
fu_freebsd_efivars_exists(FuEfivars *efivars, const gchar *guid, const gchar *name)
{
	/* any name */
	if (name == NULL)
		return fu_freebsd_efivars_exists_guid(guid);

	return fu_freebsd_efivars_get_data(efivars, guid, name, NULL, NULL, NULL, NULL);
}

static gboolean
fu_freebsd_efivars_get_data(FuEfivars *efivars,
			    const gchar *guid,
			    const gchar *name,
			    guint8 **data,
			    gsize *data_sz,
			    guint32 *attr,
			    GError **error)
{
	efi_guid_t guidt;
	efi_str_to_guid(guid, &guidt);
	return (efi_get_variable(guidt, name, data, data_sz, attr) != 0);
}

static GPtrArray *
fu_freebsd_efivars_get_names(FuEfivars *efivars, const gchar *guid, GError **error)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
	efi_guid_t *guidt = NULL;
	gchar *name = NULL;
	efi_guid_t test;
	efi_str_to_guid(guid, &test);

	/* find names with matching GUID */
	while (efi_get_next_variable_name(&guidt, &name)) {
		if (memcmp(&test, guidt, sizeof(test)) == 0) {
			g_ptr_array_add(names, g_strdup(name));
		}
	}

	/* nothing found */
	if (names->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no names for GUID %s",
				    guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&names);
}

static guint64
fu_freebsd_efivars_space_used(FuEfivars *efivars, GError **error)
{
	guint64 total = 0;
	efi_guid_t *guidt = NULL;
	char *name = NULL;

	while (efi_get_next_variable_name(&guidt, &name)) {
		size_t size = 0;
		if (efi_get_variable_size(*guidt, name, &size) < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to get efivars size");
			return G_MAXUINT64;
		}
		total += size;
	}

	/* success */
	return total;
}

static gboolean
fu_freebsd_efivars_set_data(FuEfivars *efivars,
			    const gchar *guid,
			    const gchar *name,
			    const guint8 *data,
			    gsize sz,
			    guint32 attr,
			    GError **error)
{
	efi_guid_t guidt;
	efi_str_to_guid(guid, &guidt);

	if (efi_set_variable(guidt, name, (guint8 *)data, sz, attr) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to write data to efivars %s",
			    name);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_freebsd_efivars_init(FuFreebsdEfivars *self)
{
}

static void
fu_freebsd_efivars_class_init(FuFreebsdEfivarsClass *klass)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_CLASS(klass);
	efivars_class->supported = fu_freebsd_efivars_supported;
	efivars_class->space_used = fu_freebsd_efivars_space_used;
	efivars_class->exists = fu_freebsd_efivars_exists;
	efivars_class->get_monitor = fu_freebsd_efivars_get_monitor;
	efivars_class->get_data = fu_freebsd_efivars_get_data;
	efivars_class->set_data = fu_freebsd_efivars_set_data;
	efivars_class->delete = fu_freebsd_efivars_delete;
	efivars_class->delete_with_glob = fu_freebsd_efivars_delete_with_glob;
	efivars_class->get_names = fu_freebsd_efivars_get_names;
}

FuEfivars *
fu_efivars_new(void)
{
	return FU_EFIVARS(g_object_new(FU_TYPE_FREEBSD_EFIVARS, NULL));
}

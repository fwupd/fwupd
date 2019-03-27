/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-uefi-pcrs.h"

typedef struct {
	guint		 idx;
	gchar		*checksum;
} FuUefiPcrItem;

struct _FuUefiPcrs {
	GObject		 parent_instance;
	GPtrArray	*items;		/* of FuUefiPcrItem */
};

G_DEFINE_TYPE (FuUefiPcrs, fu_uefi_pcrs, G_TYPE_OBJECT)

static gboolean
_g_string_isxdigit (GString *str)
{
	for (gsize i = 0; i < str->len; i++) {
		if (!g_ascii_isxdigit (str->str[i]))
			return FALSE;
	}
	return TRUE;
}

static void
fu_uefi_pcrs_parse_line (const gchar *line, gpointer user_data)
{
	FuUefiPcrs *self = FU_UEFI_PCRS (user_data);
	FuUefiPcrItem *item;
	guint64 idx;
	g_autofree gchar *idxstr = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GString) str = NULL;

	/* split into index:hash */
	if (line == NULL || line[0] == '\0')
		return;
	split = g_strsplit (line, ":", -1);
	if (g_strv_length (split) != 2) {
		g_debug ("unexpected format, skipping: %s", line);
		return;
	}

	/* get index */
	idxstr = fu_common_strstrip (split[0]);
	idx = fu_common_strtoull (idxstr);
	if (idx > 64) {
		g_debug ("unexpected index, skipping: %s", idxstr);
		return;
	}

	/* parse hash */
	str = g_string_new (split[1]);
	fu_common_string_replace (str, " ", "");
	if ((str->len != 40 && str->len != 64) || !_g_string_isxdigit (str)) {
		g_debug ("not SHA-1 or SHA-256, skipping: %s", split[1]);
		return;
	}
	g_string_ascii_down (str);
	item = g_new0 (FuUefiPcrItem, 1);
	item->idx = idx;
	item->checksum = g_string_free (g_steal_pointer (&str), FALSE);
	g_ptr_array_add (self->items, item);
	g_debug ("added PCR-%02u=%s", item->idx, item->checksum);
}

static gboolean
fu_uefi_pcrs_setup_dummy (FuUefiPcrs *self, const gchar *test_yaml, GError **error)
{
	g_auto(GStrv) lines = g_strsplit (test_yaml, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++)
		fu_uefi_pcrs_parse_line (lines[i], self);
	return TRUE;
}

static gboolean
fu_uefi_pcrs_setup_tpm12 (FuUefiPcrs *self, const gchar *fn_pcrs, GError **error)
{
	g_auto(GStrv) lines = NULL;
	g_autofree gchar *buf_pcrs = NULL;

	/* get entire contents */
	if (!g_file_get_contents (fn_pcrs, &buf_pcrs, NULL, error))
		return FALSE;

	/* find PCR lines */
	lines = g_strsplit (buf_pcrs, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix (lines[i], "PCR-"))
			fu_uefi_pcrs_parse_line (lines[i] + 4, self);
	}
	return TRUE;
}

static gboolean
fu_uefi_pcrs_setup_tpm20 (FuUefiPcrs *self, const gchar *argv0, GError **error)
{
	const gchar *argv[] = { argv0, NULL };
	return fu_common_spawn_sync (argv, fu_uefi_pcrs_parse_line, self, 1500, NULL, error);
}

gboolean
fu_uefi_pcrs_setup (FuUefiPcrs *self, GError **error)
{
	g_autofree gchar *devpath = NULL;
	g_autofree gchar *sysfstpmdir = NULL;
	g_autofree gchar *fn_pcrs = NULL;
	const gchar *test_yaml = g_getenv ("FWUPD_UEFI_TPM2_YAML_DATA");

	g_return_val_if_fail (FU_IS_UEFI_PCRS (self), FALSE);

	/* check the TPM device exists at all */
	sysfstpmdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_TPM);
	devpath = g_build_filename (sysfstpmdir, "tpm0", NULL);
	if (!g_file_test (devpath, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "no TPM device found");
		return FALSE;
	}
	fn_pcrs = g_build_filename (devpath, "pcrs", NULL);

	/* fake device */
	if (test_yaml != NULL) {
		if (!fu_uefi_pcrs_setup_dummy (self, test_yaml, error))
			return FALSE;

	/* look for TPM 1.2 */
	} else if (g_file_test (fn_pcrs, G_FILE_TEST_EXISTS)) {
		if (!fu_uefi_pcrs_setup_tpm12 (self, fn_pcrs, error))
			return FALSE;

	/* assume TPM 2.0 */
	} else {
		g_autofree gchar *argv0 = NULL;

		/* old name, then new name */
		argv0 = fu_common_find_program_in_path ("tpm2_listpcrs", NULL);
		if (argv0 == NULL)
			argv0 = fu_common_find_program_in_path ("tpm2_pcrlist", error);
		if (argv0 == NULL)
			return FALSE;
		if (!fu_uefi_pcrs_setup_tpm20 (self, argv0, error))
			return FALSE;
	}

	/* check we got anything */
	if (self->items->len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "no TPMxx measurements found");
		return FALSE;
	}

	/* success */
	return TRUE;
}

GPtrArray *
fu_uefi_pcrs_get_checksums (FuUefiPcrs *self, guint idx)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func (g_free);
	g_return_val_if_fail (FU_IS_UEFI_PCRS (self), NULL);
	for (guint i = 0; i < self->items->len; i++) {
		FuUefiPcrItem *item = g_ptr_array_index (self->items, i);
		if (item->idx == idx)
			g_ptr_array_add (array, g_strdup (item->checksum));
	}
	return g_steal_pointer (&array);
}

static void
fu_uefi_pcrs_item_free (FuUefiPcrItem *item)
{
	g_free (item->checksum);
	g_free (item);
}

static void
fu_uefi_pcrs_finalize (GObject *object)
{
	FuUefiPcrs *self = FU_UEFI_PCRS (object);
	g_ptr_array_unref (self->items);
	G_OBJECT_CLASS (fu_uefi_pcrs_parent_class)->finalize (object);
}

static void
fu_uefi_pcrs_class_init (FuUefiPcrsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_uefi_pcrs_finalize;
}

static void
fu_uefi_pcrs_init (FuUefiPcrs *self)
{
	self->items = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_uefi_pcrs_item_free);
}

FuUefiPcrs *
fu_uefi_pcrs_new (void)
{
	FuUefiPcrs *self;
	self = g_object_new (FU_TYPE_UEFI_PCRS, NULL);
	return FU_UEFI_PCRS (self);
}

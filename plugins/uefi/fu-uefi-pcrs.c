/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <tss2/tss2_esys.h>

#include "fu-common.h"
#include "fu-uefi-pcrs.h"
#include "fwupd-error.h"

typedef struct {
	guint		 idx;
	gchar		*checksum;
} FuUefiPcrItem;

struct _FuUefiPcrs {
	GObject		 parent_instance;
	GPtrArray	*items;		/* of FuUefiPcrItem */
};

G_DEFINE_TYPE (FuUefiPcrs, fu_uefi_pcrs, G_TYPE_OBJECT)

static void Esys_Finalize_autoptr_cleanup (ESYS_CONTEXT *esys_context)
{
	Esys_Finalize (&esys_context);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ESYS_CONTEXT, Esys_Finalize_autoptr_cleanup)

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
fu_uefi_pcrs_setup_tpm20 (FuUefiPcrs *self, GError **error)
{
	TSS2_RC rc;
	g_autoptr(ESYS_CONTEXT) ctx = NULL;
	g_autofree TPMS_CAPABILITY_DATA *capability_data = NULL;
	TPML_PCR_SELECTION pcr_selection_in = { 0, };
	g_autofree TPML_DIGEST *pcr_values = NULL;

	/* suppress warning messages about missing TCTI libraries for tpm2-tss <2.3 */
	if (g_getenv ("FWUPD_UEFI_VERBOSE") == NULL) {
		g_setenv ("TSS2_LOG", "esys+error,tcti+none", FALSE);
	}

	rc = Esys_Initialize (&ctx, NULL, NULL);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
		                     "failed to initialize TPM library");
		return FALSE;
	}
	rc = Esys_Startup (ctx, TPM2_SU_CLEAR);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "failed to initialize TPM");
		return FALSE;
	}

	/* get hash algorithms supported by the TPM */
	rc = Esys_GetCapability (ctx, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
	                         TPM2_CAP_PCRS, 0, 1, NULL, &capability_data);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "failed to get hash algorithms supported by TPM");
		return FALSE;
	}

	/* fetch PCR 0 for every supported hash algorithm */
	pcr_selection_in.count = capability_data->data.assignedPCR.count;
	for (guint i = 0; i < pcr_selection_in.count; i++) {
		pcr_selection_in.pcrSelections[i].hash =
			capability_data->data.assignedPCR.pcrSelections[i].hash;
		pcr_selection_in.pcrSelections[i].sizeofSelect =
			capability_data->data.assignedPCR.pcrSelections[i].sizeofSelect;
		pcr_selection_in.pcrSelections[i].pcrSelect[0] = 0b00000001;
	}

	rc = Esys_PCR_Read (ctx, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
	                    &pcr_selection_in, NULL, NULL, &pcr_values);
	if (rc != TSS2_RC_SUCCESS) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "failed to read PCR values from TPM");
		return FALSE;
	}

	for (guint i = 0; i < pcr_values->count; i++) {
		FuUefiPcrItem *item;
		g_autoptr(GString) str = NULL;

		str = g_string_new (NULL);
		for (guint j = 0; j < pcr_values->digests[i].size; j++) {
			gint64 val = pcr_values->digests[i].buffer[j];
			if (val > 0)
				g_string_append_printf (str, "%02x", pcr_values->digests[i].buffer[j]);
		}
		if (str->len > 0) {
			item = g_new0 (FuUefiPcrItem, 1);
			item->idx = 0; /* constant PCR index 0, since we only read this single PCR */
			item->checksum = g_string_free (g_steal_pointer (&str), FALSE);
			g_ptr_array_add (self->items, item);
			g_debug ("added PCR-%02u=%s", item->idx, item->checksum);
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_uefi_pcrs_setup (FuUefiPcrs *self, GError **error)
{
	g_autofree gchar *devpath = NULL;
	g_autofree gchar *sysfstpmdir = NULL;
	g_autofree gchar *fn_pcrs = NULL;

	g_return_val_if_fail (FU_IS_UEFI_PCRS (self), FALSE);

	/* look for TPM 1.2 */
	sysfstpmdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_TPM);
	devpath = g_build_filename (sysfstpmdir, "tpm0", NULL);
	fn_pcrs = g_build_filename (devpath, "pcrs", NULL);
	if (g_file_test (fn_pcrs, G_FILE_TEST_EXISTS) &&
	    g_getenv ("FWUPD_FORCE_TPM2") == NULL) {
		if (!fu_uefi_pcrs_setup_tpm12 (self, fn_pcrs, error))
			return FALSE;

	/* assume TPM 2.0 */
	} else {
		if (!fu_uefi_pcrs_setup_tpm20 (self, error))
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

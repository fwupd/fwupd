/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuTpmEventlog"

#include "config.h"

#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-tpm-eventlog-item.h"

const FuTpmAlg algs[] = {
    FU_TPM_ALG_SHA1,
    FU_TPM_ALG_SHA256,
    FU_TPM_ALG_SHA384,
};

struct _FuTpmEventlogItem {
	FuFirmware parent_instance;
	FuTpmEventlogItemKind kind;
	guint8 pcr;
	GBytes *checksums[G_N_ELEMENTS(algs)];
};

G_DEFINE_TYPE(FuTpmEventlogItem, fu_tpm_eventlog_item, FU_TYPE_FIRMWARE)

static guint
fu_tpm_eventlog_item_alg_to_idx(FuTpmAlg alg)
{
	for (guint i = 0; i < G_N_ELEMENTS(algs); i++) {
		if (algs[i] == alg)
			return i;
	}
	return G_MAXUINT;
}

/**
 * fu_tpm_eventlog_item_add_checksum:
 * @self: a #FuTpmEventlogItem
 * @alg: a #FuTpmAlg, e.g. %FU_TPM_ALG_SHA1
 * @checksum: a #GBytes of the raw checksum
 *
 * Adds the checksum of a specific type.
 *
 * Since: 2.1.1
 **/
void
fu_tpm_eventlog_item_add_checksum(FuTpmEventlogItem *self, FuTpmAlg alg, GBytes *checksum)
{
	guint csum_idx = fu_tpm_eventlog_item_alg_to_idx(alg);
	g_return_if_fail(FU_IS_TPM_EVENTLOG_ITEM(self));
	g_return_if_fail(csum_idx != G_MAXUINT);
	g_return_if_fail(checksum != NULL);

	if (self->checksums[csum_idx] != NULL)
		g_bytes_unref(self->checksums[csum_idx]);
	self->checksums[csum_idx] = g_bytes_ref(checksum);
}

/**
 * fu_tpm_eventlog_item_get_checksum:
 * @self: a #FuTpmEventlogItem
 * @alg: a #FuTpmAlg, e.g. %FU_TPM_ALG_SHA1
 * @error: (nullable): optional return location for an error
 *
 * Gets the raw checksum of a specific type.
 *
 * Returns: a #GBytes, or %NULL on error
 *
 * Since: 2.1.1
 **/
GBytes *
fu_tpm_eventlog_item_get_checksum(FuTpmEventlogItem *self, FuTpmAlg alg, GError **error)
{
	guint csum_idx = fu_tpm_eventlog_item_alg_to_idx(alg);
	g_return_val_if_fail(FU_IS_TPM_EVENTLOG_ITEM(self), NULL);
	g_return_val_if_fail(csum_idx != G_MAXUINT, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (self->checksums[csum_idx] == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "checksum %s not set",
			    fu_tpm_alg_to_string(alg));
		return NULL;
	}
	return g_bytes_ref(self->checksums[csum_idx]);
}

/**
 * fu_tpm_eventlog_item_get_kind:
 * @self: a #FuTpmEventlogItem
 *
 * Gets the item kind.
 *
 * Returns: a #FuTpmEventlogItemKind, e.g. %FU_TPM_EVENTLOG_ITEM_KIND_EFI_PLATFORM_FIRMWARE_BLOB
 *
 * Since: 2.1.1
 **/
FuTpmEventlogItemKind
fu_tpm_eventlog_item_get_kind(FuTpmEventlogItem *self)
{
	g_return_val_if_fail(FU_IS_TPM_EVENTLOG_ITEM(self), G_MAXUINT);
	return self->kind;
}

/**
 * fu_tpm_eventlog_item_set_kind:
 * @self: a #FuTpmEventlogItem
 * @kind: a #FuTpmEventlogItemKind, e.g. %FU_TPM_EVENTLOG_ITEM_KIND_EFI_PLATFORM_FIRMWARE_BLOB
 *
 * Sets item kind.
 *
 * Since: 2.1.1
 **/
void
fu_tpm_eventlog_item_set_kind(FuTpmEventlogItem *self, FuTpmEventlogItemKind kind)
{
	g_return_if_fail(FU_IS_TPM_EVENTLOG_ITEM(self));
	self->kind = kind;
}

/**
 * fu_tpm_eventlog_item_get_pcr:
 * @self: a #FuTpmEventlogItem
 *
 * Gets the PCR register.
 *
 * Returns: value
 *
 * Since: 2.1.1
 **/
guint8
fu_tpm_eventlog_item_get_pcr(FuTpmEventlogItem *self)
{
	g_return_val_if_fail(FU_IS_TPM_EVENTLOG_ITEM(self), G_MAXUINT8);
	return self->pcr;
}

/**
 * fu_tpm_eventlog_item_set_pcr:
 * @self: a #FuTpmEventlogItem
 * @pcr: a value
 *
 * Sets the PCR register.
 *
 * Since: 2.1.1
 **/
void
fu_tpm_eventlog_item_set_pcr(FuTpmEventlogItem *self, guint8 pcr)
{
	g_return_if_fail(FU_IS_TPM_EVENTLOG_ITEM(self));
	self->pcr = pcr;
}

static guint
fu_tpm_eventlog_item_csum_kind_to_idx(GChecksumType csum_kind)
{
	if (csum_kind == G_CHECKSUM_SHA1)
		return fu_tpm_eventlog_item_alg_to_idx(FU_TPM_ALG_SHA1);
	if (csum_kind == G_CHECKSUM_SHA256)
		return fu_tpm_eventlog_item_alg_to_idx(FU_TPM_ALG_SHA256);
	if (csum_kind == G_CHECKSUM_SHA384)
		return fu_tpm_eventlog_item_alg_to_idx(FU_TPM_ALG_SHA384);
	return G_MAXUINT;
}

static gchar *
fu_tpm_eventlog_item_get_checksum_string(FuFirmware *firmware,
					 GChecksumType csum_kind,
					 GError **error)
{
	FuTpmEventlogItem *self = FU_TPM_EVENTLOG_ITEM(firmware);
	guint csum_idx = fu_tpm_eventlog_item_csum_kind_to_idx(csum_kind);

	/* unset */
	if (csum_idx == G_MAXUINT) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return NULL;
	}
	if (self->checksums[csum_idx] == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not set");
		return NULL;
	}
	return fu_bytes_to_string(self->checksums[csum_idx]);
}

static gboolean
fu_tpm_eventlog_item_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuTpmEventlogItem *self = FU_TPM_EVENTLOG_ITEM(firmware);
	const gchar *tmp;
	guint64 tmp64;

	/* simple properties */
	tmp = xb_node_query_text(n, "kind", NULL);
	if (tmp != NULL)
		fu_tpm_eventlog_item_set_kind(self, fu_tpm_eventlog_item_kind_from_string(tmp));
	tmp64 = xb_node_query_text_as_uint(n, "pcr", NULL);
	if (tmp64 != G_MAXUINT64)
		fu_tpm_eventlog_item_set_pcr(self, tmp64);

	/* checksums */
	for (guint i = 0; i < G_N_ELEMENTS(algs); i++) {
		tmp = xb_node_query_text(n, fu_tpm_alg_to_string(algs[i]), NULL);
		if (tmp != NULL) {
			g_autoptr(GBytes) blob = fu_bytes_from_string(tmp, error);
			if (blob == NULL)
				return FALSE;
			fu_tpm_eventlog_item_add_checksum(self, algs[i], blob);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_tpm_eventlog_item_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuTpmEventlogItem *self = FU_TPM_EVENTLOG_ITEM(firmware);
	fu_xmlb_builder_insert_kv(bn, "kind", fu_tpm_eventlog_item_kind_to_string(self->kind));
	fu_xmlb_builder_insert_kx(bn, "pcr", self->pcr);
	for (guint i = 0; i < G_N_ELEMENTS(algs); i++) {
		if (self->checksums[i] != NULL) {
			g_autofree gchar *value = fu_bytes_to_string(self->checksums[i]);
			fu_xmlb_builder_insert_kv(bn, fu_tpm_alg_to_string(algs[i]), value);
		}
	}
}

static void
fu_tpm_eventlog_item_finalize(GObject *object)
{
	FuTpmEventlogItem *self = FU_TPM_EVENTLOG_ITEM(object);

	for (guint i = 0; i < G_N_ELEMENTS(self->checksums); i++) {
		if (self->checksums[i] != NULL)
			g_bytes_unref(self->checksums[i]);
	}

	G_OBJECT_CLASS(fu_tpm_eventlog_item_parent_class)->finalize(object);
}

static void
fu_tpm_eventlog_item_class_init(FuTpmEventlogItemClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_tpm_eventlog_item_finalize;
	firmware_class->get_checksum = fu_tpm_eventlog_item_get_checksum_string;
	firmware_class->build = fu_tpm_eventlog_item_build;
	firmware_class->export = fu_tpm_eventlog_item_export;
}

static void
fu_tpm_eventlog_item_init(FuTpmEventlogItem *self)
{
}

/**
 * fu_tpm_eventlog_item_new:
 *
 * Returns: (transfer full): a #FuTpmEventlogItem
 *
 * Since: 2.1.1
 **/
FuTpmEventlogItem *
fu_tpm_eventlog_item_new(void)
{
	return g_object_new(FU_TYPE_TPM_EVENTLOG_ITEM, NULL);
}

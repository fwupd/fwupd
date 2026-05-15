/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-sha512-engine.h"

struct _FuJcatSha512Engine {
	FuJcatEngine parent_instance;
};

G_DEFINE_TYPE(FuJcatSha512Engine, fu_jcat_sha512_engine, FU_TYPE_JCAT_ENGINE)

static FwupdJcatBlob *
fu_jcat_sha512_engine_self_sign(FuJcatEngine *engine,
				GBytes *data,
				FuJcatSignFlags flags,
				GError **error)
{
	g_autofree gchar *tmp = g_compute_checksum_for_bytes(G_CHECKSUM_SHA512, data);
	return fwupd_jcat_blob_new_utf8(FWUPD_JCAT_BLOB_KIND_SHA512, tmp);
}

static FuJcatResult *
fu_jcat_sha512_engine_self_verify(FuJcatEngine *engine,
				  GBytes *data,
				  GBytes *blob_signature,
				  FuJcatVerifyFlags flags,
				  GError **error)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(GBytes) data_tmp = NULL;

	tmp = g_compute_checksum_for_bytes(G_CHECKSUM_SHA512, data);
	data_tmp = g_bytes_new(tmp, strlen(tmp));
	if (!fu_bytes_compare(data_tmp, blob_signature, error)) {
		g_autofree gchar *sig = fu_strsafe_bytes(blob_signature, 1024);
		if (sig != NULL)
			g_prefix_error(error, "expected %s and got %s: ", sig, tmp);
		return NULL;
	}
	return FU_JCAT_RESULT(g_object_new(FU_TYPE_JCAT_RESULT, "engine", engine, NULL));
}

static void
fu_jcat_sha512_engine_class_init(FuJcatSha512EngineClass *klass)
{
	FuJcatEngineClass *engine_class = FU_JCAT_ENGINE_CLASS(klass);
	engine_class->self_sign = fu_jcat_sha512_engine_self_sign;
	engine_class->self_verify = fu_jcat_sha512_engine_self_verify;
}

static void
fu_jcat_sha512_engine_init(FuJcatSha512Engine *self)
{
}

FuJcatEngine *
fu_jcat_sha512_engine_new(FuJcatContext *context)
{
	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(context), NULL);
	return FU_JCAT_ENGINE(g_object_new(FU_TYPE_JCAT_SHA512_ENGINE,
					   "context",
					   context,
					   "kind",
					   FWUPD_JCAT_BLOB_KIND_SHA512,
					   "method",
					   FWUPD_JCAT_BLOB_METHOD_CHECKSUM,
					   NULL));
}

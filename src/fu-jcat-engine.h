/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-jcat-blob.h"

#include "fu-engine-struct.h"
#include "fu-jcat-result.h"

#define FU_TYPE_JCAT_ENGINE (fu_jcat_engine_get_type())
G_DECLARE_DERIVABLE_TYPE(FuJcatEngine, fu_jcat_engine, FU, JCAT_ENGINE, GObject)

struct _FuJcatEngineClass {
	GObjectClass parent_class;
	gboolean (*setup)(FuJcatEngine *self, GError **error);
	gboolean (*add_public_key)(FuJcatEngine *self, const gchar *filename, GError **error);
	FuJcatResult *(*pubkey_verify)(FuJcatEngine *self,
				       GBytes *blob,
				       GBytes *blob_signature,
				       FuJcatVerifyFlags flags,
				       GError **error);
	FwupdJcatBlob *(*pubkey_sign)(FuJcatEngine *self,
				      GBytes *blob,
				      GBytes *cert,
				      GBytes *privkey,
				      FuJcatSignFlags flags,
				      GError **error);
	FuJcatResult *(*self_verify)(FuJcatEngine *self,
				     GBytes *blob,
				     GBytes *blob_signature,
				     FuJcatVerifyFlags flags,
				     GError **error);
	FwupdJcatBlob *(*self_sign)(FuJcatEngine *self,
				    GBytes *blob,
				    FuJcatSignFlags flags,
				    GError **error);
	gboolean (*add_public_key_raw)(FuJcatEngine *self, GBytes *blob, GError **error);
};

FwupdJcatBlobKind
fu_jcat_engine_get_kind(FuJcatEngine *self) G_GNUC_NON_NULL(1);
FwupdJcatBlobMethod
fu_jcat_engine_get_method(FuJcatEngine *self) G_GNUC_NON_NULL(1);
FuJcatResult *
fu_jcat_engine_pubkey_verify(FuJcatEngine *self,
			     GBytes *blob,
			     GBytes *blob_signature,
			     FuJcatVerifyFlags flags,
			     GError **error) G_GNUC_NON_NULL(1, 2, 3);
FwupdJcatBlob *
fu_jcat_engine_pubkey_sign(FuJcatEngine *self,
			   GBytes *blob,
			   GBytes *cert,
			   GBytes *privkey,
			   FuJcatSignFlags flags,
			   GError **error) G_GNUC_NON_NULL(1, 2, 3, 4);
FuJcatResult *
fu_jcat_engine_self_verify(FuJcatEngine *self,
			   GBytes *blob,
			   GBytes *blob_signature,
			   FuJcatVerifyFlags flags,
			   GError **error) G_GNUC_NON_NULL(1, 2, 3);
FwupdJcatBlob *
fu_jcat_engine_self_sign(FuJcatEngine *self, GBytes *blob, FuJcatSignFlags flags, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_jcat_engine_add_public_key_raw(FuJcatEngine *self, GBytes *blob, GError **error)
    G_GNUC_NON_NULL(1, 2);
const gchar *
fu_jcat_engine_get_keyring_path(FuJcatEngine *self) G_GNUC_NON_NULL(1);

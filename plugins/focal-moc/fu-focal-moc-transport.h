/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-focal-moc-transport-impl.h"

typedef struct _FuFocalMocTransport FuFocalMocTransport;

gboolean
fu_focal_moc_transport_is_supported(void);

FuFocalMocTransport *
fu_focal_moc_transport_new(FuFocalMocTransportImpl *impl);

void
fu_focal_moc_transport_free(FuFocalMocTransport *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuFocalMocTransport, fu_focal_moc_transport_free)

gboolean
fu_focal_moc_transport_is_active(FuFocalMocTransport *self);

gboolean
fu_focal_moc_transport_handshake(FuFocalMocTransport *self, GError **error);

GByteArray *
fu_focal_moc_transport_command(FuFocalMocTransport *self,
			       guint8 command,
			       const guint8 *payload,
			       gsize payload_sz,
			       guint timeout_ms,
			       guint8 *status,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT;

void
fu_focal_moc_transport_teardown(FuFocalMocTransport *self);

gboolean
fu_focal_moc_transport_kdf_expand(const guint8 *prk,
				  gsize prk_sz,
				  const gchar *label,
				  const guint8 *context,
				  gsize context_sz,
				  guint8 *key,
				  gsize key_sz,
				  GError **error);

gboolean
fu_focal_moc_transport_derive_iv(const guint8 *key, guint32 sequence, guint8 *iv, GError **error);

gboolean
fu_focal_moc_transport_aes_ctr(const guint8 *key,
			       const guint8 *iv,
			       const guint8 *input,
			       gsize input_sz,
			       guint8 *output,
			       GError **error);

GByteArray *
fu_focal_moc_transport_cipher_frame_new(const guint8 *key_aes,
					const guint8 *key_mac,
					const guint8 *key_iv,
					guint32 sequence,
					const guint8 *plaintext,
					gsize plaintext_sz,
					GError **error);

GByteArray *
fu_focal_moc_transport_cipher_frame_parse(const guint8 *key_aes,
					  const guint8 *key_mac,
					  const guint8 *key_iv,
					  guint32 expected_sequence,
					  GByteArray *frame,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT;

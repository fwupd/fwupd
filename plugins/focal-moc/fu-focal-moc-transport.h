/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-focal-moc-transport-impl.h"

/* an ephemeral P-256 key packed as x||y||scalar, 32 bytes each */
#define FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE 96

#define FU_TYPE_FOCAL_MOC_TRANSPORT (fu_focal_moc_transport_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocTransport, fu_focal_moc_transport, FU, FOCAL_MOC_TRANSPORT, GObject)

gboolean
fu_focal_moc_transport_is_supported(void);

FuFocalMocTransport *
fu_focal_moc_transport_new(FuFocalMocTransportImpl *impl);

void
fu_focal_moc_transport_set_fixed_host_key(FuFocalMocTransport *self, GBytes *host_key);

void
fu_focal_moc_transport_clear_fixed_host_key(FuFocalMocTransport *self);

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
fu_focal_moc_transport_kdf_expand(FuSecureBytes *prk,
				  const gchar *label,
				  const guint8 *context,
				  gsize context_sz,
				  FuSecureBytes *key,
				  GError **error);

gboolean
fu_focal_moc_transport_derive_iv(FuSecureBytes *key, guint32 sequence, guint8 *iv, GError **error);

gboolean
fu_focal_moc_transport_aes_ctr(FuSecureBytes *key,
			       FuSecureBytes *iv,
			       FuSecureBytes *input,
			       guint8 *output,
			       GError **error);

GByteArray *
fu_focal_moc_transport_cipher_frame_new(FuSecureBytes *key_aes,
					FuSecureBytes *key_mac,
					FuSecureBytes *key_iv,
					guint32 sequence,
					FuSecureBytes *plaintext,
					GError **error);

FuSecureBytes *
fu_focal_moc_transport_cipher_frame_parse(FuSecureBytes *key_aes,
					  FuSecureBytes *key_mac,
					  FuSecureBytes *key_iv,
					  guint32 expected_sequence,
					  GByteArray *frame,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT;

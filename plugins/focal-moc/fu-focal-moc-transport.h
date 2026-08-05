/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef struct _FuFocalMocTransport FuFocalMocTransport;

typedef gboolean (*FuFocalMocTransportSendFunc)(gpointer user_data,
						const guint8 *buf,
						gsize bufsz,
						guint timeout_ms,
						GError **error);
typedef GByteArray *(*FuFocalMocTransportReceiveFunc)(gpointer user_data,
						      guint timeout_ms,
						      GError **error);

/* the host ephemeral key packed as x||y||scalar, 32 bytes each */
#define FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE 96

typedef gboolean (*FuFocalMocTransportKeyLoadFunc)(gpointer user_data,
						   guint8 *host_key,
						   gboolean *found,
						   GError **error);
typedef gboolean (*FuFocalMocTransportKeySaveFunc)(gpointer user_data,
						   const guint8 *host_key,
						   GError **error);

gboolean
fu_focal_moc_transport_is_supported(void);

FuFocalMocTransport *
fu_focal_moc_transport_new(FuFocalMocTransportSendFunc send_cb,
			   FuFocalMocTransportReceiveFunc receive_cb,
			   gpointer user_data);

void
fu_focal_moc_transport_free(FuFocalMocTransport *self);

void
fu_focal_moc_transport_set_key_journal(FuFocalMocTransport *self,
				       FuFocalMocTransportKeyLoadFunc load_cb,
				       FuFocalMocTransportKeySaveFunc save_cb);

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

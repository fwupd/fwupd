/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_GNUTLS
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#endif

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-struct.h"
#include "fu-focal-moc-transport.h"

#if defined(HAVE_GNUTLS) && GNUTLS_VERSION_NUMBER >= 0x030802
#define FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
#endif

#define FU_FOCAL_MOC_TRANSPORT_PUBLIC_KEY_SIZE	 65
#define FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE	 32
#define FU_FOCAL_MOC_TRANSPORT_KEY_SIZE		 32
#define FU_FOCAL_MOC_TRANSPORT_MAC_SIZE		 16
#define FU_FOCAL_MOC_TRANSPORT_KC_SIZE		 16
#define FU_FOCAL_MOC_TRANSPORT_MAX_DATA_SIZE	 1013
#define FU_FOCAL_MOC_TRANSPORT_REKEY_THRESHOLD	 0xFFFF0000
#define FU_FOCAL_MOC_TRANSPORT_MAC_FAILURE_LIMIT 16
#define FU_FOCAL_MOC_TRANSPORT_KEX_TIMEOUT_MS	 2000
#define FU_FOCAL_MOC_TRANSPORT_KC_TIMEOUT_MS	 3000
#define FU_FOCAL_MOC_TRANSPORT_DRAIN_TIMEOUT_MS	 50
#define FU_FOCAL_MOC_TRANSPORT_DRAIN_LIMIT	 8

#define FU_FOCAL_MOC_TRANSPORT_EXTRACT_INFO "SDCPv4-Transport-v1"

struct _FuFocalMocTransport {
	FuFocalMocTransportImpl *impl; /* no ref; the impl owns the transport */
	FuFocalMocTransportState state;
	FuSecureBytes *fixed_host_key;
	guint32 tx_sequence;
	guint32 rx_sequence;
	guint8 tx_message_id;
	guint8 mac_failures;
	guint8 public_key_host[FU_FOCAL_MOC_TRANSPORT_PUBLIC_KEY_SIZE];
	guint8 public_key_device[FU_FOCAL_MOC_TRANSPORT_PUBLIC_KEY_SIZE];
	FuSecureBytes *key_aes_d2m;
	FuSecureBytes *key_mac_d2m;
	FuSecureBytes *key_iv_d2m;
	FuSecureBytes *key_aes_m2d;
	FuSecureBytes *key_mac_m2d;
	FuSecureBytes *key_iv_m2d;
};

static gboolean
fu_focal_moc_transport_hmac(const guint8 *key,
			    gsize key_sz,
			    const guint8 *buf,
			    gsize bufsz,
			    guint8 *digest,
			    gsize digest_sz,
			    GError **error)
{
	gsize actual_sz = 32;
	guint8 actual[32] = {0};
	g_autoptr(FuSecureBytes) actual_secure = NULL;
	g_autoptr(GHmac) hmac = NULL;

	if (digest_sz > sizeof(actual)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "HMAC output too large: 0x%zx",
			    digest_sz);
		return FALSE;
	}
	hmac = g_hmac_new(G_CHECKSUM_SHA256, key, key_sz);
	g_hmac_update(hmac, buf, bufsz);
	g_hmac_get_digest(hmac, actual, &actual_sz);
	actual_secure = fu_secure_bytes_new(actual, sizeof(actual), NULL);
	if (actual_sz != sizeof(actual)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "HMAC output invalid: 0x%zx",
			    actual_sz);
		return FALSE;
	}
	return fu_memcpy_safe(digest, digest_sz, 0, actual, sizeof(actual), 0, digest_sz, error);
}

gboolean
fu_focal_moc_transport_kdf_expand(const guint8 *prk,
				  gsize prk_sz,
				  const gchar *label,
				  const guint8 *context,
				  gsize context_sz,
				  guint8 *key,
				  gsize key_sz,
				  GError **error)
{
	const guint8 counter = 0x01;
	const guint8 separator = 0x00;
	const guint8 output_bits[] = {0x00, 0x00, 0x01, 0x00};
	g_autoptr(GByteArray) input = g_byte_array_new();

	g_return_val_if_fail(prk != NULL, FALSE);
	g_return_val_if_fail(label != NULL, FALSE);
	g_return_val_if_fail(context != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);

	if (key_sz != FU_FOCAL_MOC_TRANSPORT_KEY_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "KDF output size invalid: 0x%zx",
			    key_sz);
		return FALSE;
	}
	g_byte_array_append(input, &counter, sizeof(counter));
	g_byte_array_append(input, (const guint8 *)label, strlen(label));
	g_byte_array_append(input, &separator, sizeof(separator));
	g_byte_array_append(input, context, context_sz);
	g_byte_array_append(input, output_bits, sizeof(output_bits));
	return fu_focal_moc_transport_hmac(prk,
					   prk_sz,
					   input->data,
					   input->len,
					   key,
					   key_sz,
					   error);
}

#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
static gboolean
fu_focal_moc_transport_gnutls_error(gint rc, const gchar *context, GError **error)
{
	if (rc >= GNUTLS_E_SUCCESS)
		return TRUE;
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_AUTH_FAILED,
		    "%s: %s [%i]",
		    context,
		    gnutls_strerror(rc),
		    rc);
	return FALSE;
}

static void
fu_focal_moc_transport_datum_free(gnutls_datum_t *datum)
{
	gnutls_free(datum->data);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(gnutls_datum_t, fu_focal_moc_transport_datum_free)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_privkey_t, gnutls_privkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pubkey_t, gnutls_pubkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_cipher_hd_t, gnutls_cipher_deinit, NULL)

static FuSecureBytes *
fu_focal_moc_transport_aes_ecb(const guint8 *key, const guint8 *input, GError **error)
{
	gint rc;
	guint8 iv_buf[16] = {0};
	g_autofree guint8 *output = g_malloc0(16);
	g_auto(gnutls_cipher_hd_t) cipher = NULL;
	g_autoptr(FuSecureBytes) block = NULL;
	gnutls_datum_t iv = {.data = iv_buf, .size = sizeof(iv_buf)};
	gnutls_datum_t key_datum = {
	    .data = (guint8 *)key,
	    .size = FU_FOCAL_MOC_TRANSPORT_KEY_SIZE,
	};

	rc = gnutls_cipher_init(&cipher, GNUTLS_CIPHER_AES_256_CBC, &key_datum, &iv);
	if (!fu_focal_moc_transport_gnutls_error(rc, "failed to initialize AES", error))
		return NULL;
	rc = gnutls_cipher_encrypt2(cipher, input, 16, output, 16);
	block = fu_secure_bytes_new(g_steal_pointer(&output), 16, g_free);
	if (!fu_focal_moc_transport_gnutls_error(rc, "failed to encrypt AES block", error))
		return NULL;
	return g_steal_pointer(&block);
}

static FuSecureBytes *
fu_focal_moc_transport_derive_iv_internal(const guint8 *key, guint32 sequence, GError **error)
{
	guint8 input[16] = {0};

	fu_memwrite_uint32(input, sequence, G_BIG_ENDIAN);
	return fu_focal_moc_transport_aes_ecb(key, input, error);
}

static void
fu_focal_moc_transport_counter_increment(guint8 *counter)
{
	for (gint i = 15; i >= 0; i--) {
		counter[i]++;
		if (counter[i] != 0)
			break;
	}
}

static gboolean
fu_focal_moc_transport_aes_ctr_internal(const guint8 *key,
					const guint8 *iv,
					const guint8 *input,
					gsize input_sz,
					guint8 *output,
					GError **error)
{
	guint8 counter[16] = {0};

	/* the counter is a nonce, not a secret */
	if (!fu_memcpy_safe(counter,
			    sizeof(counter),
			    0,
			    iv,
			    sizeof(counter),
			    0,
			    sizeof(counter),
			    error))
		return FALSE;
	for (gsize offset = 0; offset < input_sz; offset += 16) {
		gsize block_sz = MIN((gsize)16, input_sz - offset);
		const guint8 *stream_buf;
		g_autoptr(FuSecureBytes) stream = NULL;

		stream = fu_focal_moc_transport_aes_ecb(key, counter, error);
		if (stream == NULL)
			return FALSE;
		stream_buf = fu_secure_bytes_get_data(stream);
		for (gsize i = 0; i < block_sz; i++)
			output[offset + i] = input[offset + i] ^ stream_buf[i];
		fu_focal_moc_transport_counter_increment(counter);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_transport_derive_keys(FuFocalMocTransport *self,
				   FuSecureBytes *shared_secret,
				   GError **error)
{
	const gchar *labels[] = {
	    "AES_KEY_D2M",
	    "MAC_KEY_D2M",
	    "IV__KEY_D2M",
	    "AES_KEY_M2D",
	    "MAC_KEY_M2D",
	    "IV__KEY_M2D",
	};
	FuSecureBytes **keys[] = {
	    &self->key_aes_d2m,
	    &self->key_mac_d2m,
	    &self->key_iv_d2m,
	    &self->key_aes_m2d,
	    &self->key_mac_m2d,
	    &self->key_iv_m2d,
	};
	guint8 transcript_secret[32] = {0};
	g_autoptr(FuSecureBytes) transcript_secure = NULL;
	g_autoptr(GByteArray) context = g_byte_array_new();

	g_byte_array_append(context, self->public_key_host, sizeof(self->public_key_host));
	g_byte_array_append(context, self->public_key_device, sizeof(self->public_key_device));
	if (!fu_focal_moc_transport_hmac(fu_secure_bytes_get_data(shared_secret),
					 fu_secure_bytes_get_size(shared_secret),
					 (const guint8 *)FU_FOCAL_MOC_TRANSPORT_EXTRACT_INFO,
					 strlen(FU_FOCAL_MOC_TRANSPORT_EXTRACT_INFO),
					 transcript_secret,
					 sizeof(transcript_secret),
					 error))
		return FALSE;
	transcript_secure = fu_secure_bytes_new(transcript_secret, sizeof(transcript_secret), NULL);
	for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
		g_autofree guint8 *key = g_malloc0(FU_FOCAL_MOC_TRANSPORT_KEY_SIZE);

		if (!fu_focal_moc_transport_kdf_expand(transcript_secret,
						       sizeof(transcript_secret),
						       labels[i],
						       context->data,
						       context->len,
						       key,
						       FU_FOCAL_MOC_TRANSPORT_KEY_SIZE,
						       error))
			return FALSE;
		*keys[i] = fu_secure_bytes_new(g_steal_pointer(&key),
					       FU_FOCAL_MOC_TRANSPORT_KEY_SIZE,
					       g_free);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_focal_moc_transport_cipher_frame_new_internal(const guint8 *key_aes,
						 const guint8 *key_mac,
						 const guint8 *key_iv,
						 guint32 sequence,
						 const guint8 *plaintext,
						 gsize plaintext_sz,
						 GError **error)
{
	guint16 length;
	guint8 bcc;
	guint8 mac[FU_FOCAL_MOC_TRANSPORT_MAC_SIZE] = {0};
	g_autofree guint8 *encrypted = NULL;
	g_autoptr(FuSecureBytes) iv = NULL;
	g_autoptr(FuStructFocalMocCipherHeader) st = NULL;

	if (plaintext_sz == 0 ||
	    plaintext_sz > G_MAXUINT16 - 4 - FU_FOCAL_MOC_TRANSPORT_MAC_SIZE - 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cipher plaintext size invalid: 0x%zx",
			    plaintext_sz);
		return NULL;
	}
	iv = fu_focal_moc_transport_derive_iv_internal(key_iv, sequence, error);
	if (iv == NULL)
		return NULL;
	encrypted = g_malloc0(plaintext_sz);
	if (!fu_focal_moc_transport_aes_ctr_internal(key_aes,
						     fu_secure_bytes_get_data(iv),
						     plaintext,
						     plaintext_sz,
						     encrypted,
						     error))
		return NULL;
	length = 4 + plaintext_sz + FU_FOCAL_MOC_TRANSPORT_MAC_SIZE + 1;
	st = fu_struct_focal_moc_cipher_header_new();
	fu_struct_focal_moc_cipher_header_set_length(st, length);
	fu_struct_focal_moc_cipher_header_set_sequence(st, sequence);
	g_byte_array_append(st->buf, encrypted, plaintext_sz);
	if (!fu_focal_moc_transport_hmac(key_mac,
					 FU_FOCAL_MOC_TRANSPORT_KEY_SIZE,
					 st->buf->data,
					 st->buf->len,
					 mac,
					 sizeof(mac),
					 error))
		return NULL;
	g_byte_array_append(st->buf, mac, sizeof(mac));
	bcc = fu_xor8(st->buf->data + 1, st->buf->len - 1);
	g_byte_array_append(st->buf, &bcc, sizeof(bcc));

	/* success */
	return g_byte_array_ref(st->buf);
}

typedef enum {
	FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_NONE,
	FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_AUTH,
	FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_REKEY,
} FuFocalMocTransportCipherFault;

static FuSecureBytes *
fu_focal_moc_transport_cipher_frame_decode(const guint8 *key_aes,
					   const guint8 *key_mac,
					   const guint8 *key_iv,
					   guint32 expected_sequence,
					   GByteArray *frame,
					   FuFocalMocTransportCipherFault *fault,
					   GError **error)
{
	gsize encrypted_sz;
	gsize mac_offset;
	guint16 length;
	guint32 sequence;
	gboolean ret;
	guint8 bcc = 0;
	guint8 bcc_actual = 0;
	guint8 mac[FU_FOCAL_MOC_TRANSPORT_MAC_SIZE] = {0};
	g_autofree guint8 *decrypted = NULL;
	g_autoptr(FuSecureBytes) iv = NULL;
	g_autoptr(FuSecureBytes) plaintext = NULL;
	g_autoptr(FuStructFocalMocCipherHeader) st = NULL;

	if (fault != NULL)
		*fault = FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_NONE;
	st = fu_struct_focal_moc_cipher_header_parse(frame->data, frame->len, 0, error);
	if (st == NULL)
		return NULL;
	length = fu_struct_focal_moc_cipher_header_get_length(st);
	if (length < 4 + 1 + FU_FOCAL_MOC_TRANSPORT_MAC_SIZE + 1 ||
	    frame->len != (gsize)length + 3) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cipher frame size invalid: got=0x%x length=0x%x",
			    frame->len,
			    length);
		return NULL;
	}
	if (!fu_xor8_safe(frame->data, frame->len, 0x1, frame->len - 2, &bcc, error))
		return NULL;
	if (!fu_memread_uint8_safe(frame->data, frame->len, frame->len - 1, &bcc_actual, error))
		return NULL;
	if (bcc != bcc_actual) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cipher BCC mismatch: got=0x%02x expected=0x%02x",
			    bcc_actual,
			    bcc);
		return NULL;
	}
	encrypted_sz = length - 4 - FU_FOCAL_MOC_TRANSPORT_MAC_SIZE - 1;
	mac_offset = FU_STRUCT_FOCAL_MOC_CIPHER_HEADER_SIZE + encrypted_sz;
	if (!fu_focal_moc_transport_hmac(key_mac,
					 FU_FOCAL_MOC_TRANSPORT_KEY_SIZE,
					 frame->data,
					 mac_offset,
					 mac,
					 sizeof(mac),
					 error))
		return NULL;
	if (gnutls_memcmp(mac, frame->data + mac_offset, sizeof(mac)) != 0) {
		if (fault != NULL)
			*fault = FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_AUTH;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "cipher MAC mismatch");
		return NULL;
	}
	sequence = fu_struct_focal_moc_cipher_header_get_sequence(st);
	if (sequence != expected_sequence) {
		if (fault != NULL)
			*fault = FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_AUTH;
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "cipher sequence invalid: got=%u expected=%u",
			    sequence,
			    expected_sequence);
		return NULL;
	}
	if (sequence >= FU_FOCAL_MOC_TRANSPORT_REKEY_THRESHOLD) {
		if (fault != NULL)
			*fault = FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_REKEY;
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "transport receive sequence reached rekey threshold: %u",
			    sequence);
		return NULL;
	}
	iv = fu_focal_moc_transport_derive_iv_internal(key_iv, sequence, error);
	if (iv == NULL)
		return NULL;
	decrypted = g_malloc0(encrypted_sz);
	ret = fu_focal_moc_transport_aes_ctr_internal(key_aes,
						      fu_secure_bytes_get_data(iv),
						      frame->data +
							  FU_STRUCT_FOCAL_MOC_CIPHER_HEADER_SIZE,
						      encrypted_sz,
						      decrypted,
						      error);
	/* wrapped even on failure so a partial decrypt is still wiped */
	plaintext = fu_secure_bytes_new(g_steal_pointer(&decrypted), encrypted_sz, g_free);
	if (!ret)
		return NULL;

	/* success */
	return g_steal_pointer(&plaintext);
}

static FuSecureBytes *
fu_focal_moc_transport_cipher_frame_parse_internal(FuFocalMocTransport *self,
						   GByteArray *frame,
						   GError **error)
{
	FuFocalMocTransportCipherFault fault = FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_NONE;
	g_autoptr(FuSecureBytes) plaintext = NULL;

	plaintext =
	    fu_focal_moc_transport_cipher_frame_decode(fu_secure_bytes_get_data(self->key_aes_m2d),
						       fu_secure_bytes_get_data(self->key_mac_m2d),
						       fu_secure_bytes_get_data(self->key_iv_m2d),
						       self->rx_sequence,
						       frame,
						       &fault,
						       error);
	if (plaintext == NULL) {
		if (fault == FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_AUTH) {
			self->mac_failures = MIN((guint)self->mac_failures + 1,
						 FU_FOCAL_MOC_TRANSPORT_MAC_FAILURE_LIMIT);
			if (self->mac_failures == FU_FOCAL_MOC_TRANSPORT_MAC_FAILURE_LIMIT)
				fu_focal_moc_transport_teardown(self);
		} else if (fault == FU_FOCAL_MOC_TRANSPORT_CIPHER_FAULT_REKEY) {
			fu_focal_moc_transport_teardown(self);
		}
		return NULL;
	}
	self->rx_sequence++;

	/* success */
	return g_steal_pointer(&plaintext);
}

static gboolean
fu_focal_moc_transport_key_exchange(FuFocalMocTransport *self, GError **error)
{
	gint rc;
	guint8 status = 0;
	guint8 shared_secret[FU_FOCAL_MOC_TRANSPORT_KEY_SIZE] = {0};
	gnutls_datum_t secret = {0};
	g_auto(gnutls_datum_t) x = {0};
	g_auto(gnutls_datum_t) y = {0};
	gnutls_datum_t peer_x = {
	    .data = self->public_key_device + 1,
	    .size = FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE,
	};
	gnutls_datum_t peer_y = {
	    .data = self->public_key_device + 1 + FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE,
	    .size = FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE,
	};
	gnutls_ecc_curve_t curve = GNUTLS_ECC_CURVE_INVALID;
	g_auto(gnutls_privkey_t) private_key = NULL;
	g_auto(gnutls_pubkey_t) public_key = NULL;
	g_auto(gnutls_pubkey_t) peer_key = NULL;
	g_autoptr(FuSecureBytes) secret_secure = NULL;
	g_autoptr(FuSecureBytes) shared_secret_secure = NULL;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GByteArray) request = NULL;
	g_autoptr(GByteArray) response = NULL;

	rc = gnutls_privkey_init(&private_key);
	if (!fu_focal_moc_transport_gnutls_error(rc, "failed to initialize private key", error))
		return FALSE;
	/* an emulation reuses a well-known key; a live device gets a fresh one */
	if (self->fixed_host_key != NULL) {
		const guint coord = FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE;
		guint8 *host_key = (guint8 *)fu_secure_bytes_get_data(self->fixed_host_key);
		gnutls_datum_t datum_x = {.data = host_key, .size = coord};
		gnutls_datum_t datum_y = {.data = host_key + coord, .size = coord};
		gnutls_datum_t datum_k = {.data = host_key + (2 * coord), .size = coord};

		rc = gnutls_privkey_import_ecc_raw(private_key,
						   GNUTLS_ECC_CURVE_SECP256R1,
						   &datum_x,
						   &datum_y,
						   &datum_k);
		if (!fu_focal_moc_transport_gnutls_error(rc,
							 "failed to import fixed host key",
							 error))
			return FALSE;
	} else {
		rc = gnutls_privkey_generate(private_key,
					     GNUTLS_PK_ECDSA,
					     GNUTLS_CURVE_TO_BITS(GNUTLS_ECC_CURVE_SECP256R1),
					     0);
		if (!fu_focal_moc_transport_gnutls_error(rc, "failed to generate P-256 key", error))
			return FALSE;
	}
	rc = gnutls_pubkey_init(&public_key);
	if (!fu_focal_moc_transport_gnutls_error(rc, "failed to initialize public key", error))
		return FALSE;
	rc = gnutls_pubkey_import_privkey(public_key, private_key, 0, 0);
	if (!fu_focal_moc_transport_gnutls_error(rc, "failed to import host public key", error))
		return FALSE;
	rc = gnutls_pubkey_export_ecc_raw2(public_key, &curve, &x, &y, GNUTLS_EXPORT_FLAG_NO_LZ);
	if (!fu_focal_moc_transport_gnutls_error(rc, "failed to export host public key", error))
		return FALSE;
	if (curve != GNUTLS_ECC_CURVE_SECP256R1 || x.size == 0 || y.size == 0 ||
	    x.size > FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE ||
	    y.size > FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "host public key format invalid");
		return FALSE;
	}
	/* a coordinate with leading zeros exports short, so the padding must start clean */
	memset(self->public_key_host, 0, sizeof(self->public_key_host));
	self->public_key_host[0] = 0x04;
	if (!fu_memcpy_safe(self->public_key_host,
			    sizeof(self->public_key_host),
			    1 + FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE - x.size,
			    x.data,
			    x.size,
			    0,
			    x.size,
			    error) ||
	    !fu_memcpy_safe(self->public_key_host,
			    sizeof(self->public_key_host),
			    1 + (2 * FU_FOCAL_MOC_TRANSPORT_COORDINATE_SIZE) - y.size,
			    y.data,
			    y.size,
			    0,
			    y.size,
			    error))
		return FALSE;
	request = fu_focal_moc_packet_new(FU_FOCAL_MOC_CMD_TRANSPORT_KEY_EXCHANGE,
					  self->public_key_host,
					  sizeof(self->public_key_host),
					  error);
	if (request == NULL ||
	    !fu_focal_moc_transport_impl_write(self->impl,
					       request->data,
					       request->len,
					       FU_FOCAL_MOC_TRANSPORT_KEX_TIMEOUT_MS,
					       error))
		return FALSE;
	response = fu_focal_moc_transport_impl_read(self->impl,
						    FU_FOCAL_MOC_TRANSPORT_KEX_TIMEOUT_MS,
						    error);
	if (response == NULL)
		return FALSE;
	/* the key exchange reply is plaintext, so it carries the same one byte of
	 * stale buffer past the declared frame that the runtime always appends */
	data = fu_focal_moc_packet_parse(response->data, response->len, TRUE, &status, error);
	if (data == NULL)
		return FALSE;
	if (status != FU_FOCAL_MOC_STATUS_OK ||
	    data->len != FU_FOCAL_MOC_TRANSPORT_PUBLIC_KEY_SIZE || data->data[0] != 0x04) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "key exchange response invalid: status=0x%02x size=0x%x",
			    status,
			    data->len);
		return FALSE;
	}
	if (!fu_memcpy_safe(self->public_key_device,
			    sizeof(self->public_key_device),
			    0,
			    data->data,
			    data->len,
			    0,
			    data->len,
			    error))
		return FALSE;
	rc = gnutls_pubkey_init(&peer_key);
	if (!fu_focal_moc_transport_gnutls_error(rc, "failed to initialize peer key", error))
		return FALSE;
	rc = gnutls_pubkey_import_ecc_raw(peer_key, GNUTLS_ECC_CURVE_SECP256R1, &peer_x, &peer_y);
	if (!fu_focal_moc_transport_gnutls_error(rc, "device public key invalid", error))
		return FALSE;
	rc = gnutls_privkey_derive_secret(private_key, peer_key, NULL, &secret, 0);
	if (!fu_focal_moc_transport_gnutls_error(rc, "ECDH failed", error))
		return FALSE;
	secret_secure = fu_secure_bytes_new(secret.data, secret.size, gnutls_free);
	if (secret.size == 0 || secret.size > sizeof(shared_secret)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "ECDH secret size invalid: 0x%x",
			    secret.size);
		return FALSE;
	}
	if (!fu_memcpy_safe(shared_secret,
			    sizeof(shared_secret),
			    sizeof(shared_secret) - secret.size,
			    secret.data,
			    secret.size,
			    0,
			    secret.size,
			    error))
		return FALSE;
	shared_secret_secure = fu_secure_bytes_new(shared_secret, sizeof(shared_secret), NULL);
	if (!fu_focal_moc_transport_derive_keys(self, shared_secret_secure, error))
		return FALSE;
	self->tx_sequence = 0;
	self->rx_sequence = 0;
	self->tx_message_id = 0;
	self->state = FU_FOCAL_MOC_TRANSPORT_STATE_KC_PENDING;

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_transport_key_confirm(FuFocalMocTransport *self, GError **error)
{
	const guint8 label_driver[] = "kc_driver";
	const guint8 label_device[] = "kc_mcu";
	const guint8 *response_buf;
	guint8 expected[FU_FOCAL_MOC_TRANSPORT_KC_SIZE] = {0};
	guint8 plaintext[1 + FU_FOCAL_MOC_TRANSPORT_KC_SIZE] = {0};
	g_autoptr(FuSecureBytes) expected_secure = NULL;
	g_autoptr(FuSecureBytes) plaintext_secure = NULL;
	g_autoptr(FuSecureBytes) response_plaintext = NULL;
	g_autoptr(GByteArray) input = g_byte_array_new();
	g_autoptr(GByteArray) frame = NULL;
	g_autoptr(GByteArray) response = NULL;

	g_byte_array_append(input, label_driver, sizeof(label_driver) - 1);
	g_byte_array_append(input, self->public_key_host, sizeof(self->public_key_host));
	g_byte_array_append(input, self->public_key_device, sizeof(self->public_key_device));
	plaintext[0] = FU_FOCAL_MOC_CMD_TRANSPORT_KEY_CONFIRM;
	if (!fu_focal_moc_transport_hmac(fu_secure_bytes_get_data(self->key_mac_d2m),
					 FU_FOCAL_MOC_TRANSPORT_KEY_SIZE,
					 input->data,
					 input->len,
					 plaintext + 1,
					 FU_FOCAL_MOC_TRANSPORT_KC_SIZE,
					 error))
		return FALSE;
	plaintext_secure = fu_secure_bytes_new(plaintext, sizeof(plaintext), NULL);
	frame = fu_focal_moc_transport_cipher_frame_new_internal(
	    fu_secure_bytes_get_data(self->key_aes_d2m),
	    fu_secure_bytes_get_data(self->key_mac_d2m),
	    fu_secure_bytes_get_data(self->key_iv_d2m),
	    self->tx_sequence,
	    plaintext,
	    sizeof(plaintext),
	    error);
	if (frame == NULL)
		return FALSE;
	if (!fu_focal_moc_transport_impl_write(self->impl,
					       frame->data,
					       frame->len,
					       FU_FOCAL_MOC_TRANSPORT_KC_TIMEOUT_MS,
					       error))
		return FALSE;
	self->tx_sequence++;
	response = fu_focal_moc_transport_impl_read(self->impl,
						    FU_FOCAL_MOC_TRANSPORT_KC_TIMEOUT_MS,
						    error);
	if (response == NULL)
		return FALSE;
	response_plaintext =
	    fu_focal_moc_transport_cipher_frame_parse_internal(self, response, error);
	if (response_plaintext == NULL)
		return FALSE;
	response_buf = fu_secure_bytes_get_data(response_plaintext);
	if (fu_secure_bytes_get_size(response_plaintext) != sizeof(plaintext) ||
	    response_buf[0] != FU_FOCAL_MOC_STATUS_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "key confirmation response invalid: size=0x%zx",
			    fu_secure_bytes_get_size(response_plaintext));
		return FALSE;
	}
	g_byte_array_set_size(input, 0);
	g_byte_array_append(input, label_device, sizeof(label_device) - 1);
	g_byte_array_append(input, self->public_key_host, sizeof(self->public_key_host));
	g_byte_array_append(input, self->public_key_device, sizeof(self->public_key_device));
	if (!fu_focal_moc_transport_hmac(fu_secure_bytes_get_data(self->key_mac_m2d),
					 FU_FOCAL_MOC_TRANSPORT_KEY_SIZE,
					 input->data,
					 input->len,
					 expected,
					 sizeof(expected),
					 error))
		return FALSE;
	expected_secure = fu_secure_bytes_new(expected, sizeof(expected), NULL);
	if (gnutls_memcmp(expected, response_buf + 1, sizeof(expected)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "device key confirmation mismatch");
		return FALSE;
	}
	self->state = FU_FOCAL_MOC_TRANSPORT_STATE_ACTIVE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_focal_moc_transport_receive_message(FuFocalMocTransport *self,
				       guint8 expected_message_id,
				       guint timeout_ms,
				       guint8 *status,
				       GError **error)
{
	guint8 message_id = 0;
	guint8 total = 0;
	g_autoptr(GByteArray) data = g_byte_array_new();

	for (guint chunk = 0;; chunk++) {
		const guint8 *buf;
		gsize bufsz;
		guint8 more;
		g_autoptr(FuStructFocalMocFragmentHeader) st_fragment = NULL;
		g_autoptr(GByteArray) frame = NULL;
		g_autoptr(FuSecureBytes) plaintext = NULL;

		frame = fu_focal_moc_transport_impl_read(self->impl, timeout_ms, error);
		if (frame == NULL)
			return NULL;
		plaintext = fu_focal_moc_transport_cipher_frame_parse_internal(self, frame, error);
		if (plaintext == NULL)
			return NULL;
		buf = fu_secure_bytes_get_data(plaintext);
		bufsz = fu_secure_bytes_get_size(plaintext);
		if (bufsz < FU_STRUCT_FOCAL_MOC_FRAGMENT_HEADER_SIZE + 1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cipher response payload too short: 0x%zx",
				    bufsz);
			return NULL;
		}
		st_fragment = fu_struct_focal_moc_fragment_header_parse(buf, bufsz, 0, error);
		if (st_fragment == NULL)
			return NULL;
		more = fu_struct_focal_moc_fragment_header_get_more(st_fragment);
		if (chunk == 0) {
			message_id =
			    fu_struct_focal_moc_fragment_header_get_message_id(st_fragment);
			total = fu_struct_focal_moc_fragment_header_get_total(st_fragment);
			*status = buf[FU_STRUCT_FOCAL_MOC_FRAGMENT_HEADER_SIZE];
			/* a late response to an earlier command must not be accepted as
			 * the reply to this one */
			if (message_id != expected_message_id || total == 0 ||
			    fu_struct_focal_moc_fragment_header_get_index(st_fragment) != 0 ||
			    more != (total > 1 ? 1 : 0)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "first cipher fragment metadata invalid: "
					    "message-id=%u expected=%u",
					    message_id,
					    expected_message_id);
				return NULL;
			}
		} else if (fu_struct_focal_moc_fragment_header_get_message_id(st_fragment) !=
			       message_id ||
			   fu_struct_focal_moc_fragment_header_get_total(st_fragment) != total ||
			   fu_struct_focal_moc_fragment_header_get_index(st_fragment) != chunk ||
			   more != (chunk + 1 < total ? 1 : 0) ||
			   buf[FU_STRUCT_FOCAL_MOC_FRAGMENT_HEADER_SIZE] != *status) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cipher fragment metadata invalid: chunk=%u",
				    chunk);
			return NULL;
		}
		g_byte_array_append(data,
				    buf + FU_STRUCT_FOCAL_MOC_FRAGMENT_HEADER_SIZE + 1,
				    bufsz - FU_STRUCT_FOCAL_MOC_FRAGMENT_HEADER_SIZE - 1);
		if (chunk + 1 == total)
			return g_steal_pointer(&data);
	}
}
#endif

gboolean
fu_focal_moc_transport_is_supported(void)
{
#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	return TRUE;
#else
	return FALSE;
#endif
}

gboolean
fu_focal_moc_transport_derive_iv(const guint8 *key, guint32 sequence, guint8 *iv, GError **error)
{
#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	g_autoptr(FuSecureBytes) iv_secure = NULL;
#endif

	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(iv != NULL, FALSE);

#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	iv_secure = fu_focal_moc_transport_derive_iv_internal(key, sequence, error);
	if (iv_secure == NULL)
		return FALSE;
	return fu_memcpy_safe(iv,
			      16,
			      0,
			      fu_secure_bytes_get_data(iv_secure),
			      fu_secure_bytes_get_size(iv_secure),
			      0,
			      16,
			      error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "TransportSec requires GnuTLS 3.8.2 or later");
	return FALSE;
#endif
}

gboolean
fu_focal_moc_transport_aes_ctr(const guint8 *key,
			       const guint8 *iv,
			       const guint8 *input,
			       gsize input_sz,
			       guint8 *output,
			       GError **error)
{
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(iv != NULL, FALSE);
	g_return_val_if_fail(input != NULL || input_sz == 0, FALSE);
	g_return_val_if_fail(output != NULL || input_sz == 0, FALSE);

#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	return fu_focal_moc_transport_aes_ctr_internal(key, iv, input, input_sz, output, error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "TransportSec requires GnuTLS 3.8.2 or later");
	return FALSE;
#endif
}

GByteArray *
fu_focal_moc_transport_cipher_frame_new(const guint8 *key_aes,
					const guint8 *key_mac,
					const guint8 *key_iv,
					guint32 sequence,
					const guint8 *plaintext,
					gsize plaintext_sz,
					GError **error)
{
	g_return_val_if_fail(key_aes != NULL, NULL);
	g_return_val_if_fail(key_mac != NULL, NULL);
	g_return_val_if_fail(key_iv != NULL, NULL);
	g_return_val_if_fail(plaintext != NULL || plaintext_sz == 0, NULL);

#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	return fu_focal_moc_transport_cipher_frame_new_internal(key_aes,
								key_mac,
								key_iv,
								sequence,
								plaintext,
								plaintext_sz,
								error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "TransportSec requires GnuTLS 3.8.2 or later");
	return NULL;
#endif
}

FuSecureBytes *
fu_focal_moc_transport_cipher_frame_parse(const guint8 *key_aes,
					  const guint8 *key_mac,
					  const guint8 *key_iv,
					  guint32 expected_sequence,
					  GByteArray *frame,
					  GError **error)
{
#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	g_return_val_if_fail(key_aes != NULL, NULL);
	g_return_val_if_fail(key_mac != NULL, NULL);
	g_return_val_if_fail(key_iv != NULL, NULL);
	g_return_val_if_fail(frame != NULL, NULL);

	return fu_focal_moc_transport_cipher_frame_decode(key_aes,
							  key_mac,
							  key_iv,
							  expected_sequence,
							  frame,
							  NULL,
							  error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "TransportSec requires GnuTLS 3.8.2 or later");
	return NULL;
#endif
}

FuFocalMocTransport *
fu_focal_moc_transport_new(FuFocalMocTransportImpl *impl)
{
	FuFocalMocTransport *self;

	g_return_val_if_fail(FU_IS_FOCAL_MOC_TRANSPORT_IMPL(impl), NULL);

	self = g_new0(FuFocalMocTransport, 1);
	self->impl = impl;
	return self;
}

void
fu_focal_moc_transport_set_fixed_host_key(FuFocalMocTransport *self,
					  const guint8 *host_key,
					  gsize host_keysz)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(host_key != NULL);
	g_return_if_fail(host_keysz == FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE);

	fu_focal_moc_transport_clear_fixed_host_key(self);
	self->fixed_host_key =
	    fu_secure_bytes_new(g_memdup2(host_key, host_keysz), host_keysz, g_free);
}

void
fu_focal_moc_transport_clear_fixed_host_key(FuFocalMocTransport *self)
{
	g_return_if_fail(self != NULL);
	g_clear_pointer(&self->fixed_host_key, fu_secure_bytes_free);
}

void
fu_focal_moc_transport_teardown(FuFocalMocTransport *self)
{
	g_return_if_fail(self != NULL);

	g_clear_pointer(&self->key_aes_d2m, fu_secure_bytes_free);
	g_clear_pointer(&self->key_mac_d2m, fu_secure_bytes_free);
	g_clear_pointer(&self->key_iv_d2m, fu_secure_bytes_free);
	g_clear_pointer(&self->key_aes_m2d, fu_secure_bytes_free);
	g_clear_pointer(&self->key_mac_m2d, fu_secure_bytes_free);
	g_clear_pointer(&self->key_iv_m2d, fu_secure_bytes_free);
	self->tx_sequence = 0;
	self->rx_sequence = 0;
	self->tx_message_id = 0;
	self->mac_failures = 0;
	self->state = FU_FOCAL_MOC_TRANSPORT_STATE_IDLE;
}

void
fu_focal_moc_transport_free(FuFocalMocTransport *self)
{
	if (self == NULL)
		return;
	fu_focal_moc_transport_teardown(self);
	fu_focal_moc_transport_clear_fixed_host_key(self);
	g_free(self);
}

gboolean
fu_focal_moc_transport_is_active(FuFocalMocTransport *self)
{
	g_return_val_if_fail(self != NULL, FALSE);
	return self->state == FU_FOCAL_MOC_TRANSPORT_STATE_ACTIVE;
}

gboolean
fu_focal_moc_transport_handshake(FuFocalMocTransport *self, GError **error)
{
	g_return_val_if_fail(self != NULL, FALSE);

#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	if (fu_focal_moc_transport_is_active(self))
		return TRUE;
	if (self->state != FU_FOCAL_MOC_TRANSPORT_STATE_IDLE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "transport handshake requires idle state");
		return FALSE;
	}
	for (guint i = 0; i < FU_FOCAL_MOC_TRANSPORT_DRAIN_LIMIT; i++) {
		g_autoptr(GByteArray) stale = NULL;
		g_autoptr(GError) error_local = NULL;

		stale = fu_focal_moc_transport_impl_read(self->impl,
							 FU_FOCAL_MOC_TRANSPORT_DRAIN_TIMEOUT_MS,
							 &error_local);
		if (stale == NULL)
			break;
	}
	if (!fu_focal_moc_transport_key_exchange(self, error) ||
	    !fu_focal_moc_transport_key_confirm(self, error)) {
		fu_focal_moc_transport_teardown(self);
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "TransportSec requires GnuTLS 3.8.2 or later");
	return FALSE;
#endif
}

GByteArray *
fu_focal_moc_transport_command(FuFocalMocTransport *self,
			       guint8 command,
			       const guint8 *payload,
			       gsize payload_sz,
			       guint timeout_ms,
			       guint8 *status,
			       GError **error)
{
#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	guint8 message_id = 0;
	GByteArray *response;
	g_autoptr(FuStructFocalMocFragmentHeader) st_fragment = NULL;
	g_autoptr(GByteArray) plaintext = g_byte_array_new();
	g_autoptr(GByteArray) frame = NULL;
#endif

	g_return_val_if_fail(self != NULL, NULL);
	g_return_val_if_fail(status != NULL, NULL);

#ifdef FU_FOCAL_MOC_TRANSPORT_HAVE_ECDH
	message_id = self->tx_message_id;
	st_fragment = fu_struct_focal_moc_fragment_header_new();
	fu_struct_focal_moc_fragment_header_set_message_id(st_fragment, message_id);
	fu_struct_focal_moc_fragment_header_set_total(st_fragment, 1);
	if (!fu_focal_moc_transport_is_active(self)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "encrypted command requires active transport");
		return NULL;
	}
	if ((payload_sz > 0 && payload == NULL) ||
	    payload_sz > FU_FOCAL_MOC_TRANSPORT_MAX_DATA_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "encrypted command payload invalid: 0x%zx",
			    payload_sz);
		return NULL;
	}
	if (self->tx_sequence >= FU_FOCAL_MOC_TRANSPORT_REKEY_THRESHOLD) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "transport sequence reached rekey threshold: %u",
			    self->tx_sequence);
		fu_focal_moc_transport_teardown(self);
		return NULL;
	}
	g_byte_array_append(plaintext, st_fragment->buf->data, st_fragment->buf->len);
	g_byte_array_append(plaintext, &command, sizeof(command));
	if (payload_sz > 0)
		g_byte_array_append(plaintext, payload, payload_sz);
	frame = fu_focal_moc_transport_cipher_frame_new_internal(
	    fu_secure_bytes_get_data(self->key_aes_d2m),
	    fu_secure_bytes_get_data(self->key_mac_d2m),
	    fu_secure_bytes_get_data(self->key_iv_d2m),
	    self->tx_sequence,
	    plaintext->data,
	    plaintext->len,
	    error);
	if (frame == NULL)
		return NULL;
	/* once the frame may have reached the wire the sequence is burned:
	 * reusing it would repeat the AES-CTR keystream, and a partial response
	 * leaves rx desynchronized -- on any failure below require a fresh
	 * handshake with new keys */
	if (!fu_focal_moc_transport_impl_write(self->impl,
					       frame->data,
					       frame->len,
					       timeout_ms,
					       error)) {
		fu_focal_moc_transport_teardown(self);
		return NULL;
	}
	self->tx_sequence++;
	self->tx_message_id++;
	response =
	    fu_focal_moc_transport_receive_message(self, message_id, timeout_ms, status, error);
	if (response == NULL)
		fu_focal_moc_transport_teardown(self);
	return response;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "TransportSec requires GnuTLS 3.8.2 or later");
	return NULL;
#endif
}

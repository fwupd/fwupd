/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-struct.h"
#include "fu-focal-moc-test-device.h"

#ifdef FU_FOCAL_MOC_TEST_HAVE_ECDH
static void
fu_focal_moc_test_device_hmac(const guint8 *key, GByteArray *input, guint8 *out, gsize out_sz)
{
	gsize digest_sz = 32;
	guint8 digest[32] = {0};
	g_autoptr(GHmac) hmac = g_hmac_new(G_CHECKSUM_SHA256, key, 32);

	g_hmac_update(hmac, input->data, input->len);
	g_hmac_get_digest(hmac, digest, &digest_sz);
	g_assert_cmpuint(digest_sz, ==, sizeof(digest));
	g_assert_cmpuint(out_sz, <=, sizeof(digest));
	g_assert_true(fu_memcpy_safe(out, out_sz, 0, digest, sizeof(digest), 0, out_sz, NULL));
}

static void
fu_focal_moc_test_device_kc_tag(FuFocalMocTestDevice *self,
				const gchar *label,
				const guint8 *key_mac,
				guint8 *tag)
{
	g_autoptr(GByteArray) input = g_byte_array_new();

	g_byte_array_append(input, (const guint8 *)label, strlen(label));
	g_byte_array_append(input, self->pk_host, sizeof(self->pk_host));
	g_byte_array_append(input, self->pk_device, sizeof(self->pk_device));
	fu_focal_moc_test_device_hmac(key_mac, input, tag, 16);
}

static void
fu_focal_moc_test_device_derive_keys(FuFocalMocTestDevice *self, const guint8 *shared_secret)
{
	const gchar *labels[] = {
	    "AES_KEY_D2M",
	    "MAC_KEY_D2M",
	    "IV__KEY_D2M",
	    "AES_KEY_M2D",
	    "MAC_KEY_M2D",
	    "IV__KEY_M2D",
	};
	guint8 *keys[] = {
	    self->key_aes_d2m,
	    self->key_mac_d2m,
	    self->key_iv_d2m,
	    self->key_aes_m2d,
	    self->key_mac_m2d,
	    self->key_iv_m2d,
	};
	guint8 context[130] = {0};
	guint8 transcript[32] = {0};
	g_autoptr(GByteArray) info = g_byte_array_new();

	g_byte_array_append(info,
			    (const guint8 *)"SDCPv4-Transport-v1",
			    strlen("SDCPv4-Transport-v1"));
	fu_focal_moc_test_device_hmac(shared_secret, info, transcript, sizeof(transcript));
	g_assert_true(fu_memcpy_safe(context,
				     sizeof(context),
				     0,
				     self->pk_host,
				     sizeof(self->pk_host),
				     0,
				     sizeof(self->pk_host),
				     NULL));
	g_assert_true(fu_memcpy_safe(context,
				     sizeof(context),
				     sizeof(self->pk_host),
				     self->pk_device,
				     sizeof(self->pk_device),
				     0,
				     sizeof(self->pk_device),
				     NULL));
	for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
		g_autoptr(GError) error = NULL;

		g_assert_true(fu_focal_moc_transport_kdf_expand(transcript,
								sizeof(transcript),
								labels[i],
								context,
								sizeof(context),
								keys[i],
								32,
								&error));
		g_assert_no_error(error);
	}
}

static void
fu_focal_moc_test_device_handle_key_exchange(FuFocalMocTestDevice *self, GByteArray *data)
{
	gnutls_datum_t secret = {0};
	gnutls_datum_t x = {0};
	gnutls_datum_t y = {0};
	gnutls_datum_t peer_x = {.data = self->pk_host + 1, .size = 32};
	gnutls_datum_t peer_y = {.data = self->pk_host + 33, .size = 32};
	gnutls_ecc_curve_t curve = GNUTLS_ECC_CURVE_INVALID;
	gnutls_privkey_t private_key = NULL;
	gnutls_pubkey_t public_key = NULL;
	gnutls_pubkey_t peer_key = NULL;
	guint8 shared_secret[32] = {0};
	g_autoptr(GByteArray) response = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_cmpuint(data->len, ==, sizeof(self->pk_host));
	g_assert_cmphex(data->data[0], ==, 0x04);
	g_assert_true(fu_memcpy_safe(self->pk_host,
				     sizeof(self->pk_host),
				     0,
				     data->data,
				     data->len,
				     0,
				     data->len,
				     NULL));
	g_assert_cmpint(gnutls_privkey_init(&private_key), ==, 0);
	g_assert_cmpint(gnutls_privkey_generate(private_key,
						GNUTLS_PK_ECDSA,
						GNUTLS_CURVE_TO_BITS(GNUTLS_ECC_CURVE_SECP256R1),
						0),
			==,
			0);
	g_assert_cmpint(gnutls_pubkey_init(&public_key), ==, 0);
	g_assert_cmpint(gnutls_pubkey_import_privkey(public_key, private_key, 0, 0), ==, 0);
	g_assert_cmpint(
	    gnutls_pubkey_export_ecc_raw2(public_key, &curve, &x, &y, GNUTLS_EXPORT_FLAG_NO_LZ),
	    ==,
	    0);
	g_assert_cmpint(curve, ==, GNUTLS_ECC_CURVE_SECP256R1);
	g_assert_cmpuint(x.size, <=, 32);
	g_assert_cmpuint(y.size, <=, 32);
	memset(self->pk_device, 0, sizeof(self->pk_device));
	self->pk_device[0] = 0x04;
	g_assert_true(fu_memcpy_safe(self->pk_device,
				     sizeof(self->pk_device),
				     1 + 32 - x.size,
				     x.data,
				     x.size,
				     0,
				     x.size,
				     NULL));
	g_assert_true(fu_memcpy_safe(self->pk_device,
				     sizeof(self->pk_device),
				     1 + 64 - y.size,
				     y.data,
				     y.size,
				     0,
				     y.size,
				     NULL));
	g_assert_cmpint(gnutls_pubkey_init(&peer_key), ==, 0);
	g_assert_cmpint(
	    gnutls_pubkey_import_ecc_raw(peer_key, GNUTLS_ECC_CURVE_SECP256R1, &peer_x, &peer_y),
	    ==,
	    0);
	g_assert_cmpint(gnutls_privkey_derive_secret(private_key, peer_key, NULL, &secret, 0),
			==,
			0);
	g_assert_cmpuint(secret.size, >, 0);
	g_assert_cmpuint(secret.size, <=, sizeof(shared_secret));
	g_assert_true(fu_memcpy_safe(shared_secret,
				     sizeof(shared_secret),
				     sizeof(shared_secret) - secret.size,
				     secret.data,
				     secret.size,
				     0,
				     secret.size,
				     NULL));
	fu_focal_moc_test_device_derive_keys(self, shared_secret);
	self->d2m_sequence = 0;
	self->m2d_sequence = 0;
	self->tx_message_id = 0;
	response = fu_focal_moc_packet_new(FU_FOCAL_MOC_STATUS_OK,
					   self->pk_device,
					   sizeof(self->pk_device),
					   &error);
	g_assert_no_error(error);
	g_assert_nonnull(response);
	g_ptr_array_add(self->rx_queue, g_steal_pointer(&response));
	gnutls_free(secret.data);
	gnutls_free(x.data);
	gnutls_free(y.data);
	gnutls_pubkey_deinit(peer_key);
	gnutls_pubkey_deinit(public_key);
	gnutls_privkey_deinit(private_key);
}

static void
fu_focal_moc_test_device_handle_cipher(FuFocalMocTestDevice *self, GByteArray *frame)
{
	const guint8 *buf;
	gsize bufsz;
	g_autoptr(FuSecureBytes) plaintext = NULL;
	g_autoptr(GError) error = NULL;

	plaintext = fu_focal_moc_transport_cipher_frame_parse(self->key_aes_d2m,
							      self->key_mac_d2m,
							      self->key_iv_d2m,
							      self->d2m_sequence,
							      frame,
							      &error);
	g_assert_no_error(error);
	g_assert_nonnull(plaintext);
	buf = fu_secure_bytes_get_data(plaintext);
	bufsz = fu_secure_bytes_get_size(plaintext);
	if (self->d2m_sequence == 0) {
		guint8 reply[17] = {0};
		guint8 tag[16] = {0};
		g_autoptr(GByteArray) response = NULL;

		g_assert_cmpuint(bufsz, ==, 17);
		g_assert_cmphex(buf[0], ==, FU_FOCAL_MOC_CMD_TRANSPORT_KEY_CONFIRM);
		fu_focal_moc_test_device_kc_tag(self, "kc_driver", self->key_mac_d2m, tag);
		g_assert_cmpmem(buf + 1, 16, tag, sizeof(tag));
		reply[0] = FU_FOCAL_MOC_STATUS_OK;
		fu_focal_moc_test_device_kc_tag(self, "kc_mcu", self->key_mac_m2d, reply + 1);
		response = fu_focal_moc_transport_cipher_frame_new(self->key_aes_m2d,
								   self->key_mac_m2d,
								   self->key_iv_m2d,
								   self->m2d_sequence++,
								   reply,
								   sizeof(reply),
								   &error);
		g_assert_no_error(error);
		g_assert_nonnull(response);
		g_ptr_array_add(self->rx_queue, g_steal_pointer(&response));
	} else if (self->fault == FU_FOCAL_MOC_TEST_FAULT_SHORT) {
		const guint8 short_reply[3] = {0};
		g_autoptr(GByteArray) response = NULL;

		response = fu_focal_moc_transport_cipher_frame_new(self->key_aes_m2d,
								   self->key_mac_m2d,
								   self->key_iv_m2d,
								   self->m2d_sequence++,
								   short_reply,
								   sizeof(short_reply),
								   &error);
		g_assert_no_error(error);
		g_assert_nonnull(response);
		g_ptr_array_add(self->rx_queue, g_steal_pointer(&response));
		self->tx_message_id++;
	} else {
		gsize offset = 0;
		guint total;

		g_assert_cmpuint(bufsz, >=, 5);
		g_byte_array_set_size(self->last_request, 0);
		g_byte_array_append(self->last_request, buf, bufsz);
		/* the firmware slices response data into 1013-byte fragments */
		total = MAX(1u, (guint)((self->response_payload->len + 1012) / 1013));
		for (guint i = 0; i < total; i++) {
			gsize chunk_sz = MIN((gsize)1013, self->response_payload->len - offset);
			guint8 header[5] = {i + 1 < total ? 1 : 0,
					    self->tx_message_id,
					    (guint8)i,
					    (guint8)total,
					    self->response_status};
			g_autoptr(GByteArray) reply = g_byte_array_new();
			g_autoptr(GByteArray) response = NULL;

			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_TRUNCATED && i == 1)
				break;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_FIRST_INDEX && i == 0)
				header[2] = 1;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_FIRST_TOTAL_ZERO && i == 0)
				header[3] = 0;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_FIRST_MORE && i == 0)
				header[0] ^= 1;
			/* every fragment carries the same wrong ID, so only the
			 * expected-ID check can reject it */
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_FIRST_MESSAGE_ID)
				header[1]++;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_MESSAGE_ID && i == 1)
				header[1]++;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_TOTAL && i == 1)
				header[3]++;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_INDEX && i == 1)
				header[2]++;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_MORE && i == 1)
				header[0] ^= 1;
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_STATUS && i == 1)
				header[4] ^= 1;
			g_byte_array_append(reply, header, sizeof(header));
			g_byte_array_append(reply, self->response_payload->data + offset, chunk_sz);
			offset += chunk_sz;
			response = fu_focal_moc_transport_cipher_frame_new(self->key_aes_m2d,
									   self->key_mac_m2d,
									   self->key_iv_m2d,
									   self->m2d_sequence++,
									   reply->data,
									   reply->len,
									   &error);
			g_assert_no_error(error);
			g_assert_nonnull(response);
			if (self->fault == FU_FOCAL_MOC_TEST_FAULT_CIPHER_BCC && i == 1)
				response->data[response->len - 1] ^= 1;
			g_ptr_array_add(self->rx_queue, g_steal_pointer(&response));
		}
		self->tx_message_id++;
	}
	self->d2m_sequence++;
}

static gboolean
fu_focal_moc_test_device_write(FuFocalMocTransportImpl *impl,
			       const guint8 *buf,
			       gsize bufsz,
			       guint timeout_ms,
			       GError **error)
{
	FuFocalMocTestDevice *self = FU_FOCAL_MOC_TEST_DEVICE(impl);
	guint8 command = 0;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GByteArray) frame = g_byte_array_new();

	g_assert_cmpuint(bufsz, >, 0);
	if (self->fault == FU_FOCAL_MOC_TEST_FAULT_SEND) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "send fault");
		return FALSE;
	}
	if (buf[0] == 0x02) {
		data = fu_focal_moc_packet_parse(buf, bufsz, FALSE, &command, error);
		g_assert_nonnull(data);
		g_assert_cmphex(command, ==, FU_FOCAL_MOC_CMD_TRANSPORT_KEY_EXCHANGE);
		fu_focal_moc_test_device_handle_key_exchange(self, data);
		return TRUE;
	}
	g_byte_array_append(frame, buf, bufsz);
	fu_focal_moc_test_device_handle_cipher(self, frame);
	return TRUE;
}

static GByteArray *
fu_focal_moc_test_device_read(FuFocalMocTransportImpl *impl, guint timeout_ms, GError **error)
{
	FuFocalMocTestDevice *self = FU_FOCAL_MOC_TEST_DEVICE(impl);
	GByteArray *frame;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (self->rx_queue->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "no response queued");
		return NULL;
	}
	frame = g_ptr_array_index(self->rx_queue, 0);
	g_byte_array_append(buf, frame->data, frame->len);
	g_ptr_array_remove_index(self->rx_queue, 0);
	return g_steal_pointer(&buf);
}

static void
fu_focal_moc_test_device_transport_impl_iface_init(FuFocalMocTransportImplInterface *iface)
{
	iface->write = fu_focal_moc_test_device_write;
	iface->read = fu_focal_moc_test_device_read;
}

G_DEFINE_TYPE_WITH_CODE(FuFocalMocTestDevice,
			fu_focal_moc_test_device,
			G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(FU_TYPE_FOCAL_MOC_TRANSPORT_IMPL,
					      fu_focal_moc_test_device_transport_impl_iface_init))

static void
fu_focal_moc_test_device_init(FuFocalMocTestDevice *self)
{
	self->rx_queue = g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);
	self->last_request = g_byte_array_new();
	self->response_payload = g_byte_array_new();
}

static void
fu_focal_moc_test_device_finalize(GObject *object)
{
	FuFocalMocTestDevice *self = FU_FOCAL_MOC_TEST_DEVICE(object);

	g_ptr_array_unref(self->rx_queue);
	g_byte_array_unref(self->last_request);
	g_byte_array_unref(self->response_payload);
	G_OBJECT_CLASS(fu_focal_moc_test_device_parent_class)->finalize(object);
}

static void
fu_focal_moc_test_device_class_init(FuFocalMocTestDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_focal_moc_test_device_finalize;
}
#endif

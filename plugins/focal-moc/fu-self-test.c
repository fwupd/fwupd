/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * nocheck:magic-inlines=230
 */

#include "config.h"

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-firmware.h"
#include "fu-focal-moc-struct.h"
#include "fu-focal-moc-test-device.h"
#include "fu-focal-moc-transport.h"

static void
fu_focal_moc_transport_kdf_func(void)
{
	const guint8 expected[] = {
	    0x6E, 0x3C, 0xBE, 0xB6, 0x45, 0xF7, 0xD2, 0x68, 0xF1, 0x5F, 0x5B,
	    0xE5, 0xBE, 0xA2, 0x5D, 0x35, 0x63, 0xD0, 0xA7, 0xC7, 0x66, 0x27,
	    0x50, 0x13, 0xF9, 0x9B, 0x3D, 0xCF, 0xB8, 0xE3, 0x53, 0x89,
	};
	guint8 context[130] = {0};
	guint8 key[32] = {0};
	guint8 prk[32] = {0};
	g_autoptr(GError) error = NULL;

	for (guint i = 0; i < sizeof(prk); i++)
		prk[i] = i;
	for (guint i = 0; i < sizeof(context); i++)
		context[i] = i;
	g_assert_true(fu_focal_moc_transport_kdf_expand(prk,
							sizeof(prk),
							"AES_KEY_D2M",
							context,
							sizeof(context),
							key,
							sizeof(key),
							&error));
	g_assert_no_error(error);
	g_assert_cmpmem(key, sizeof(key), expected, sizeof(expected));
}

static void
fu_focal_moc_transport_aes_func(void)
{
	const guint8 expected_iv[] = {
	    0x9D,
	    0xBA,
	    0x41,
	    0xA7,
	    0x77,
	    0xF3,
	    0xB4,
	    0x6A,
	    0x37,
	    0xB7,
	    0xAA,
	    0xAE,
	    0x49,
	    0xD6,
	    0xDF,
	    0x8D,
	};
	const guint8 expected_ciphertext[] = {
	    0xD4, 0x28, 0x15, 0x45, 0x00, 0x4F, 0xBE, 0xCB, 0x52, 0x0C, 0xA8, 0xA5, 0x35,
	    0xC2, 0x10, 0x7A, 0xD1, 0xEF, 0x8A, 0xFB, 0xA3, 0xC3, 0xC7, 0xE4, 0x73, 0x68,
	    0xE0, 0x47, 0xDB, 0x45, 0x5C, 0x93, 0x07, 0xA5, 0x04, 0xF9, 0x72,
	};
	guint8 ciphertext[37] = {0};
	guint8 iv[16] = {0};
	guint8 key[32] = {0};
	guint8 plaintext[37] = {0};
	g_autoptr(GError) error = NULL;

	if (!fu_focal_moc_transport_is_supported()) {
		g_test_skip("GnuTLS 3.8.2 is unavailable");
		return;
	}
	for (guint i = 0; i < sizeof(key); i++)
		key[i] = i;
	for (guint i = 0; i < sizeof(plaintext); i++)
		plaintext[i] = i;
	g_assert_true(fu_focal_moc_transport_derive_iv(key, 1, iv, &error));
	g_assert_no_error(error);
	g_assert_cmpmem(iv, sizeof(iv), expected_iv, sizeof(expected_iv));
	g_assert_true(fu_focal_moc_transport_aes_ctr(key,
						     iv,
						     plaintext,
						     sizeof(plaintext),
						     ciphertext,
						     &error));
	g_assert_no_error(error);
	g_assert_cmpmem(ciphertext,
			sizeof(ciphertext),
			expected_ciphertext,
			sizeof(expected_ciphertext));
}

static void
fu_focal_moc_transport_cipher_frame_func(void)
{
	const guint8 expected[] = {
	    0x03, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x01, 0xF0, 0xD6, 0xE3,
	    0xDB, 0x44, 0x20, 0xFA, 0x86, 0xB3, 0x2C, 0x02, 0xC1, 0x85,
	    0x26, 0x5D, 0x8E, 0x80, 0x78, 0xB6, 0x4A, 0xC6, 0xF3,
	};
	const guint8 plaintext[] = {0x00, 0x00, 0x00, 0x01, 0x30};
	guint8 key_aes[32] = {0};
	guint8 key_iv[32] = {0};
	guint8 key_mac[32] = {0};
	g_autoptr(GByteArray) frame = NULL;
	g_autoptr(GByteArray) plaintext_parsed = NULL;
	g_autoptr(GError) error = NULL;

	if (!fu_focal_moc_transport_is_supported()) {
		g_test_skip("GnuTLS 3.8.2 is unavailable");
		return;
	}
	for (guint i = 0; i < sizeof(key_aes); i++) {
		key_aes[i] = i;
		key_mac[i] = i + 32;
		key_iv[i] = i + 64;
	}
	frame = fu_focal_moc_transport_cipher_frame_new(key_aes,
							key_mac,
							key_iv,
							1,
							plaintext,
							sizeof(plaintext),
							&error);
	g_assert_no_error(error);
	g_assert_nonnull(frame);
	g_assert_cmpmem(frame->data, frame->len, expected, sizeof(expected));
	plaintext_parsed =
	    fu_focal_moc_transport_cipher_frame_parse(key_aes, key_mac, key_iv, 1, frame, &error);
	g_assert_no_error(error);
	g_assert_nonnull(plaintext_parsed);
	g_assert_cmpmem(plaintext_parsed->data,
			plaintext_parsed->len,
			plaintext,
			sizeof(plaintext));

	frame->data[frame->len - 1] ^= 0x01;
	g_assert_null(
	    fu_focal_moc_transport_cipher_frame_parse(key_aes, key_mac, key_iv, 1, frame, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	frame->data[frame->len - 1] ^= 0x01;

	frame->data[frame->len - 2] ^= 0x01;
	frame->data[frame->len - 1] ^= 0x01;
	g_assert_null(
	    fu_focal_moc_transport_cipher_frame_parse(key_aes, key_mac, key_iv, 1, frame, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED);
	g_clear_error(&error);
	frame->data[frame->len - 2] ^= 0x01;
	frame->data[frame->len - 1] ^= 0x01;

	g_assert_null(
	    fu_focal_moc_transport_cipher_frame_parse(key_aes, key_mac, key_iv, 2, frame, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED);
	g_clear_error(&error);

	g_clear_pointer(&frame, g_byte_array_unref);
	frame = fu_focal_moc_transport_cipher_frame_new(key_aes,
							key_mac,
							key_iv,
							0xFFFF0000,
							plaintext,
							sizeof(plaintext),
							&error);
	g_assert_no_error(error);
	g_assert_nonnull(frame);
	g_assert_null(fu_focal_moc_transport_cipher_frame_parse(key_aes,
								key_mac,
								key_iv,
								0xFFFF0000,
								frame,
								&error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_AUTH_FAILED);
}

#ifdef FU_FOCAL_MOC_TEST_HAVE_ECDH
#define FU_TYPE_FOCAL_MOC_TEST_STUB (fu_focal_moc_test_stub_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocTestStub, fu_focal_moc_test_stub, FU, FOCAL_MOC_TEST_STUB, GObject)

struct _FuFocalMocTestStub {
	GObject parent_instance;
};

#define FU_TYPE_FOCAL_MOC_TEST_JOURNAL_STUB (fu_focal_moc_test_journal_stub_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocTestJournalStub,
		     fu_focal_moc_test_journal_stub,
		     FU,
		     FOCAL_MOC_TEST_JOURNAL_STUB,
		     GObject)

struct _FuFocalMocTestJournalStub {
	GObject parent_instance;
};

static void
fu_focal_moc_test_stub_transport_impl_iface_init(FuFocalMocTransportImplInterface *iface)
{
}

G_DEFINE_TYPE_WITH_CODE(FuFocalMocTestStub,
			fu_focal_moc_test_stub,
			G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(FU_TYPE_FOCAL_MOC_TRANSPORT_IMPL,
					      fu_focal_moc_test_stub_transport_impl_iface_init))

static void
fu_focal_moc_test_stub_init(FuFocalMocTestStub *self)
{
}

static void
fu_focal_moc_test_stub_class_init(FuFocalMocTestStubClass *klass)
{
}
static gboolean
fu_focal_moc_test_journal_stub_load_host_key(FuFocalMocTransportImpl *impl,
					     guint8 *host_key,
					     gboolean *found,
					     GError **error)
{
	*found = FALSE;
	return TRUE;
}

static void
fu_focal_moc_test_journal_stub_transport_impl_iface_init(FuFocalMocTransportImplInterface *iface)
{
	/* deliberately no save_host_key, so the pair check must reject it */
	iface->load_host_key = fu_focal_moc_test_journal_stub_load_host_key;
}

G_DEFINE_TYPE_WITH_CODE(
    FuFocalMocTestJournalStub,
    fu_focal_moc_test_journal_stub,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(FU_TYPE_FOCAL_MOC_TRANSPORT_IMPL,
			  fu_focal_moc_test_journal_stub_transport_impl_iface_init))

static void
fu_focal_moc_test_journal_stub_init(FuFocalMocTestJournalStub *self)
{
}

static void
fu_focal_moc_test_journal_stub_class_init(FuFocalMocTestJournalStubClass *klass)
{
}
#endif

static void
fu_focal_moc_transport_handshake_func(void)
{
#ifdef FU_FOCAL_MOC_TEST_HAVE_ECDH
	const guint8 payload_alive[] = {0x55, 0xAA};
	const guint8 payload_version[] = "FT9349_APP_FT9001_AA7A_USB_DEC_SV0.1_0104";
	const guint8 request_alive[] = {0x00, 0x00, 0x00, 0x01, FU_FOCAL_MOC_CMD_WAKE_UP};
	const guint8 request_version[] = {0x00, 0x01, 0x00, 0x01, FU_FOCAL_MOC_CMD_GET_FW_VERSION};
	const guint8 request_sensor[] = {0x00, 0x02, 0x00, 0x01, FU_FOCAL_MOC_CMD_GET_FP_VERSION};
	gboolean ret;
	guint8 status = 0;
	g_autoptr(FuFocalMocTestDevice) device = g_object_new(FU_TYPE_FOCAL_MOC_TEST_DEVICE, NULL);
	g_autoptr(FuFocalMocTransport) transport = NULL;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	transport = fu_focal_moc_transport_new(FU_FOCAL_MOC_TRANSPORT_IMPL(device));

	/* encrypted commands require a confirmed session */
	g_assert_null(fu_focal_moc_transport_command(transport,
						     FU_FOCAL_MOC_CMD_WAKE_UP,
						     NULL,
						     0,
						     100,
						     &status,
						     &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_clear_error(&error);

	ret = fu_focal_moc_transport_handshake(transport, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_focal_moc_transport_is_active(transport));

	/* single-fragment response with the first message ID */
	device->response_status = FU_FOCAL_MOC_STATUS_OK;
	g_byte_array_append(device->response_payload, payload_alive, sizeof(payload_alive));
	g_clear_pointer(&data, g_byte_array_unref);
	data = fu_focal_moc_transport_command(transport,
					      FU_FOCAL_MOC_CMD_WAKE_UP,
					      NULL,
					      0,
					      100,
					      &status,
					      &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmphex(status, ==, FU_FOCAL_MOC_STATUS_OK);
	g_assert_cmpmem(data->data, data->len, payload_alive, sizeof(payload_alive));
	g_assert_cmpmem(device->last_request->data,
			device->last_request->len,
			request_alive,
			sizeof(request_alive));

	/* still a single fragment, with the next message ID */
	g_byte_array_set_size(device->response_payload, 0);
	g_byte_array_append(device->response_payload, payload_version, sizeof(payload_version) - 1);
	g_clear_pointer(&data, g_byte_array_unref);
	data = fu_focal_moc_transport_command(transport,
					      FU_FOCAL_MOC_CMD_GET_FW_VERSION,
					      NULL,
					      0,
					      100,
					      &status,
					      &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmphex(status, ==, FU_FOCAL_MOC_STATUS_OK);
	g_assert_cmpmem(data->data, data->len, payload_version, sizeof(payload_version) - 1);
	g_assert_cmpmem(device->last_request->data,
			device->last_request->len,
			request_version,
			sizeof(request_version));

	/* 2100-byte response reassembled from 1013+1013+74-byte fragments */
	g_byte_array_set_size(device->response_payload, 0);
	for (guint i = 0; i < 2100; i++)
		fu_byte_array_append_uint8(device->response_payload, (guint8)i);
	g_clear_pointer(&data, g_byte_array_unref);
	data = fu_focal_moc_transport_command(transport,
					      FU_FOCAL_MOC_CMD_GET_FP_VERSION,
					      NULL,
					      0,
					      100,
					      &status,
					      &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmphex(status, ==, FU_FOCAL_MOC_STATUS_OK);
	g_assert_cmpmem(data->data,
			data->len,
			device->response_payload->data,
			device->response_payload->len);
	g_assert_cmpmem(device->last_request->data,
			device->last_request->len,
			request_sensor,
			sizeof(request_sensor));

	/* one KC exchange plus three commands, three reply messages in six frames */
	g_assert_cmpuint(device->d2m_sequence, ==, 4);
	g_assert_cmpuint(device->m2d_sequence, ==, 6);
	g_assert_cmpuint(device->tx_message_id, ==, 3);

	/* teardown ends the session and rejects further commands */
	fu_focal_moc_transport_teardown(transport);
	g_assert_false(fu_focal_moc_transport_is_active(transport));
	g_assert_null(fu_focal_moc_transport_command(transport,
						     FU_FOCAL_MOC_CMD_WAKE_UP,
						     NULL,
						     0,
						     100,
						     &status,
						     &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
#else
	g_test_skip("GnuTLS 3.8.2 is unavailable");
#endif
}

static void
fu_focal_moc_transport_fragment_invalid_func(void)
{
#ifdef FU_FOCAL_MOC_TEST_HAVE_ECDH
	const struct {
		FuFocalMocTestFault fault;
		FwupdError code;
	} faults[] = {
	    {FU_FOCAL_MOC_TEST_FAULT_FIRST_INDEX, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_FIRST_TOTAL_ZERO, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_FIRST_MORE, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_FIRST_MESSAGE_ID, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_MESSAGE_ID, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_TOTAL, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_INDEX, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_MORE, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_STATUS, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_SHORT, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_TRUNCATED, FWUPD_ERROR_TIMED_OUT},
	    {FU_FOCAL_MOC_TEST_FAULT_CIPHER_BCC, FWUPD_ERROR_INVALID_DATA},
	    {FU_FOCAL_MOC_TEST_FAULT_SEND, FWUPD_ERROR_WRITE},
	};
	g_autoptr(FuFocalMocTestDevice) device = g_object_new(FU_TYPE_FOCAL_MOC_TEST_DEVICE, NULL);
	g_autoptr(FuFocalMocTransport) transport = NULL;

	device->response_status = FU_FOCAL_MOC_STATUS_OK;
	/* two 1013-byte-rule fragments so both first and follow-up checks trigger */
	for (guint i = 0; i < 1200; i++)
		fu_byte_array_append_uint8(device->response_payload, (guint8)i);
	transport = fu_focal_moc_transport_new(FU_FOCAL_MOC_TRANSPORT_IMPL(device));

	for (guint i = 0; i < G_N_ELEMENTS(faults); i++) {
		gboolean ret;
		guint8 status = 0;
		g_autoptr(GError) error = NULL;

		/* a fresh session per fault; the handshake drains stale fragments */
		fu_focal_moc_transport_teardown(transport);
		device->fault = FU_FOCAL_MOC_TEST_FAULT_NONE;
		ret = fu_focal_moc_transport_handshake(transport, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		device->fault = faults[i].fault;
		g_assert_null(fu_focal_moc_transport_command(transport,
							     FU_FOCAL_MOC_CMD_GET_FP_VERSION,
							     NULL,
							     0,
							     100,
							     &status,
							     &error));
		g_assert_error(error, FWUPD_ERROR, (gint)faults[i].code);
		/* a burned sequence must never be reused: any post-send failure
		 * has to end the session and force a fresh handshake */
		g_assert_false(fu_focal_moc_transport_is_active(transport));
	}
#else
	g_test_skip("GnuTLS 3.8.2 is unavailable");
#endif
}

static void
fu_focal_moc_transport_impl_vfuncs_func(void)
{
#ifdef FU_FOCAL_MOC_TEST_HAVE_ECDH
	g_autoptr(FuFocalMocTestStub) stub = g_object_new(FU_TYPE_FOCAL_MOC_TEST_STUB, NULL);
	g_autoptr(FuFocalMocTestJournalStub) journal_stub =
	    g_object_new(FU_TYPE_FOCAL_MOC_TEST_JOURNAL_STUB, NULL);
	g_autoptr(FuFocalMocTransport) transport = NULL;
	g_autoptr(GError) error = NULL;

	/* missing mandatory I/O vfuncs must fail cleanly rather than crash */
	transport = fu_focal_moc_transport_new(FU_FOCAL_MOC_TRANSPORT_IMPL(stub));
	g_assert_false(fu_focal_moc_transport_handshake(transport, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_nonnull(g_strstr_len(error->message, -1, "not implemented"));
	g_assert_false(fu_focal_moc_transport_is_active(transport));
	g_clear_error(&error);
	g_clear_pointer(&transport, fu_focal_moc_transport_free);

	/* a host key journal has to implement load and save as a pair */
	transport = fu_focal_moc_transport_new(FU_FOCAL_MOC_TRANSPORT_IMPL(journal_stub));
	g_assert_false(fu_focal_moc_transport_handshake(transport, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_nonnull(g_strstr_len(error->message, -1, "load and save"));
	g_assert_false(fu_focal_moc_transport_is_active(transport));
#else
	g_test_skip("GnuTLS 3.8.2 is unavailable");
#endif
}

static void
fu_focal_moc_packet_build_func(void)
{
	const guint8 expected_version[] = {0x02, 0x00, 0x01, 0x30, 0x31};
	const guint8 expected_probe[] = {0x02, 0x00, 0x01, 0x31, 0x30};
	const guint8 expected_detach[] = {0x02, 0x00, 0x02, 0x32, 0x01, 0x31};
	const guint8 mode = 0x01;
	g_autoptr(GByteArray) packet = NULL;
	g_autoptr(GError) error = NULL;

	/* the BCC inside each captured frame is fu_xor8() over [LN..payload] */
	g_assert_cmphex(fu_xor8(expected_version + 1, sizeof(expected_version) - 2),
			==,
			expected_version[sizeof(expected_version) - 1]);
	g_assert_cmphex(fu_xor8(expected_probe + 1, sizeof(expected_probe) - 2),
			==,
			expected_probe[sizeof(expected_probe) - 1]);
	g_assert_cmphex(fu_xor8(expected_detach + 1, sizeof(expected_detach) - 2),
			==,
			expected_detach[sizeof(expected_detach) - 1]);

	packet = fu_focal_moc_packet_new(0x30, NULL, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(packet);
	g_assert_cmpmem(packet->data, packet->len, expected_version, sizeof(expected_version));

	g_clear_pointer(&packet, g_byte_array_unref);
	packet = fu_focal_moc_packet_new(FU_FOCAL_MOC_CMD_GET_FP_VERSION, NULL, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(packet);
	g_assert_cmpmem(packet->data, packet->len, expected_probe, sizeof(expected_probe));

	g_clear_pointer(&packet, g_byte_array_unref);
	packet = fu_focal_moc_packet_new(0x32, &mode, sizeof(mode), &error);
	g_assert_no_error(error);
	g_assert_nonnull(packet);
	g_assert_cmpmem(packet->data, packet->len, expected_detach, sizeof(expected_detach));
}

static void
fu_focal_moc_packet_parse_func(void)
{
	const guint8 response[] = {0x02, 0x00, 0x05, 0x04, 0x37, 0x32, 0x32, 0x35, 0x03, 0x00};
	guint8 status = 0;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	/* the BCC inside the captured frame is fu_xor8() over [LN..payload] */
	g_assert_cmphex(fu_xor8(response + 1, 7), ==, response[8]);

	data = fu_focal_moc_packet_parse(response, sizeof(response), TRUE, &status, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmphex(status, ==, 0x04);
	g_assert_cmpmem(data->data, data->len, "7225", 4);

	/* a zero-length USB read must set an error, not a GLib critical */
	g_assert_null(fu_focal_moc_packet_parse(NULL, 0, TRUE, &status, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_focal_moc_packet_status_only_func(void)
{
	/* an error status carries no payload: the parse must return an empty
	 * array rather than NULL so the caller can decode the status */
	const guint8 response[] = {0x02, 0x00, 0x01, 0x09, 0x08};
	const guint8 response_zero_len[] = {0x02, 0x00, 0x00, 0x04, 0x04};
	guint8 status = 0;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_cmphex(fu_xor8(response + 1, 3), ==, response[4]);

	data = fu_focal_moc_packet_parse(response, sizeof(response), FALSE, &status, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmpuint(data->len, ==, 0);
	g_assert_cmphex(status, ==, FU_FOCAL_MOC_STATUS_INVALID_COMMAND);

	/* a declared length of zero cannot even carry the status byte */
	g_assert_null(fu_focal_moc_packet_parse(response_zero_len,
						sizeof(response_zero_len),
						FALSE,
						&status,
						&error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_focal_moc_packet_bad_bcc_func(void)
{
	const guint8 response[] = {0x02, 0x00, 0x01, 0x04, 0x00};
	guint8 status = 0;
	g_autoptr(GError) error = NULL;

	/* the trailing byte deliberately disagrees with the computed BCC */
	g_assert_cmphex(fu_xor8(response + 1, 3), !=, response[4]);

	g_assert_null(
	    fu_focal_moc_packet_parse(response, sizeof(response), FALSE, &status, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_focal_moc_packet_length_func(void)
{
	const guint8 response[] = {0x02, 0x00, 0x05, 0x04, 0x37, 0x32, 0x32, 0x35, 0x03};
	const guint8 response_trailing[] = {
	    0x02,
	    0x00,
	    0x05,
	    0x04,
	    0x37,
	    0x32,
	    0x32,
	    0x35,
	    0x03,
	    0x00,
	};
	const guint8 response_extra[] = {
	    0x02,
	    0x00,
	    0x05,
	    0x04,
	    0x37,
	    0x32,
	    0x32,
	    0x35,
	    0x03,
	    0x00,
	    0x00,
	};
	guint8 status = 0;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_cmphex(fu_xor8(response + 1, 7), ==, response[8]);

	data = fu_focal_moc_packet_parse(response, sizeof(response), FALSE, &status, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	g_assert_null(fu_focal_moc_packet_parse(response_trailing,
						sizeof(response_trailing),
						FALSE,
						&status,
						&error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);

	g_clear_pointer(&data, g_byte_array_unref);
	data = fu_focal_moc_packet_parse(response_trailing,
					 sizeof(response_trailing),
					 TRUE,
					 &status,
					 &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	g_assert_null(fu_focal_moc_packet_parse(response_extra,
						sizeof(response_extra),
						TRUE,
						&status,
						&error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);

	g_assert_null(
	    fu_focal_moc_packet_parse(response, sizeof(response) - 1, TRUE, &status, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_focal_moc_version_frame_func(void)
{
	/* the firmware writes the string by length, so the checksum sits hard
	 * against the last character, and one byte of stale buffer follows the
	 * declared frame on protocol v1 */
	const gchar *str = "FT9365_APP_FT9001_A27A_USB_DEC_SV0.1_7004";
	gsize strsz = strlen(str);
	gboolean is_bootloader = TRUE;
	guint8 buf[47] = {0x02, 0x00, 0x00, 0x04};
	guint8 status = 0;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_cmpuint(sizeof(buf), ==, 0x4 + strsz + 1 + 1);
	fu_memwrite_uint16(buf + 0x1, strsz + 1, G_BIG_ENDIAN);
	g_assert_true(
	    fu_memcpy_safe(buf, sizeof(buf), 0x4, (const guint8 *)str, strsz, 0x0, strsz, &error));
	buf[0x4 + strsz] = fu_xor8(buf + 0x1, 0x3 + strsz);

	/* matches the checksum captured from real hardware */
	g_assert_cmpuint(buf[0x4 + strsz], ==, 0x1b);

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), TRUE, &status, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmphex(status, ==, FU_FOCAL_MOC_STATUS_OK);
	version = fu_focal_moc_version_parse(data->data, data->len, &is_bootloader, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(version, ==, "7004");
	g_assert_false(is_bootloader);
}

static void
fu_focal_moc_ymodem_frame_func(void)
{
	const guint8 payload[] = {0xAA, 0xBB, 0xCC};
	guint32 value = 0;
	g_autoptr(GByteArray) frame = NULL;
	g_autoptr(GError) error = NULL;

	frame = fu_focal_moc_ymodem_build_soh(0x1234, 0x89ABCDEF, &error);
	g_assert_no_error(error);
	g_assert_nonnull(frame);
	g_assert_cmpuint(frame->len, ==, FU_STRUCT_FOCAL_MOC_SOH_V1_SIZE);
	g_assert_cmphex(frame->data[0], ==, FU_FOCAL_MOC_FRAME_SOH);
	g_assert_cmphex(frame->data[1], ==, 0x00);
	g_assert_cmpmem(frame->data + 2, 7, "app.bin", 7);
	g_assert_true(
	    fu_memread_uint32_safe(frame->data, frame->len, 66, &value, G_BIG_ENDIAN, &error));
	g_assert_no_error(error);
	g_assert_cmphex(value, ==, 0x1234);
	g_assert_true(
	    fu_memread_uint32_safe(frame->data, frame->len, 70, &value, G_BIG_ENDIAN, &error));
	g_assert_no_error(error);
	g_assert_cmphex(value, ==, 0x89ABCDEF);

	g_clear_pointer(&frame, g_byte_array_unref);
	frame = fu_focal_moc_ymodem_build_data(FU_FOCAL_MOC_FRAME_EOT,
					       0xFF,
					       payload,
					       sizeof(payload),
					       &error);
	g_assert_no_error(error);
	g_assert_nonnull(frame);
	g_assert_cmpuint(frame->len, ==, FU_STRUCT_FOCAL_MOC_DATA_V1_SIZE);
	g_assert_cmphex(frame->data[0], ==, FU_FOCAL_MOC_FRAME_EOT);
	g_assert_cmphex(frame->data[1], ==, 0xFF);
	g_assert_cmpmem(frame->data + 2, sizeof(payload), payload, sizeof(payload));
	g_assert_cmphex(frame->data[2 + sizeof(payload)], ==, 0x00);
	g_assert_cmphex(frame->data[frame->len - 1], ==, 0x00);

	g_clear_pointer(&frame, g_byte_array_unref);
	frame = fu_focal_moc_ymodem_build_data(FU_FOCAL_MOC_FRAME_STX,
					       0x100,
					       payload,
					       sizeof(payload),
					       &error);
	g_assert_null(frame);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_focal_moc_ymodem_frame_plan_func(void)
{
	const struct {
		gsize firmware_sz;
		guint n_frames;
		gsize last_sz;
	} plans[] = {
	    /* the sizes that bracket the decision: short, exact multiple, one
	     * past a multiple, two exact frames, and the largest protocol v1
	     * payload, whose last frame number must still fit its 8-bit field */
	    {500, 1, 500},
	    {FU_FOCAL_MOC_YMODEM_DATA_SIZE, 1, FU_FOCAL_MOC_YMODEM_DATA_SIZE},
	    {FU_FOCAL_MOC_YMODEM_DATA_SIZE + 1, 2, 1},
	    {FU_FOCAL_MOC_YMODEM_DATA_SIZE * 2, 2, FU_FOCAL_MOC_YMODEM_DATA_SIZE},
	    {FU_FOCAL_MOC_YMODEM_DATA_SIZE * G_MAXUINT8, G_MAXUINT8, FU_FOCAL_MOC_YMODEM_DATA_SIZE},
	};

	for (guint i = 0; i < G_N_ELEMENTS(plans); i++) {
		guint32 crc32 = G_MAXUINT32;
		g_autofree guint8 *buf = g_malloc(plans[i].firmware_sz);
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuChunkArray) chunks = NULL;
		g_autoptr(FuInputStream) stream = NULL;
		g_autoptr(GByteArray) frame = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error = NULL;

		for (gsize j = 0; j < plans[i].firmware_sz; j++)
			buf[j] = (guint8)(j * 7);
		blob = g_bytes_new_take(g_steal_pointer(&buf), plans[i].firmware_sz);
		stream = fu_memory_input_stream_new_from_bytes(blob);

		/* the SOH advertises the same CRC the device computes over the image,
		 * and the stream helper only matches fu_crc32() for this seed */
		g_assert_true(fu_input_stream_compute_crc32(stream,
							    FU_CRC_KIND_B32_STANDARD,
							    &crc32,
							    &error));
		g_assert_no_error(error);
		g_assert_cmphex(crc32,
				==,
				fu_crc32(FU_CRC_KIND_B32_STANDARD,
					 g_bytes_get_data(blob, NULL),
					 plans[i].firmware_sz));

		/* the last chunk is sent as EOT, so an exact multiple of the frame
		 * size must not gain an empty trailing frame */
		chunks = fu_chunk_array_new_from_stream(stream,
							FU_CHUNK_ADDR_OFFSET_NONE,
							FU_CHUNK_PAGESZ_NONE,
							FU_FOCAL_MOC_YMODEM_DATA_SIZE,
							&error);
		g_assert_no_error(error);
		g_assert_nonnull(chunks);
		g_assert_cmpuint(fu_chunk_array_length(chunks), ==, plans[i].n_frames);
		chk = fu_chunk_array_index(chunks, plans[i].n_frames - 1, &error);
		g_assert_no_error(error);
		g_assert_nonnull(chk);
		g_assert_cmpuint(fu_chunk_get_data_sz(chk), ==, plans[i].last_sz);

		/* the write loop numbers frames from one, so the last frame of the
		 * largest payload must still be a frame the protocol can address */
		g_clear_pointer(&frame, g_byte_array_unref);
		frame = fu_focal_moc_ymodem_build_data(FU_FOCAL_MOC_FRAME_EOT,
						       plans[i].n_frames,
						       fu_chunk_get_data(chk),
						       fu_chunk_get_data_sz(chk),
						       &error);
		g_assert_no_error(error);
		g_assert_nonnull(frame);
	}
}

static void
fu_focal_moc_version_func(void)
{
	const guint8 version_app[] = "FT9349_APP_FT9001_A97A_USB_ENC_SV0.1_7225";
	const guint8 version_iap[] = "FT9349_IAP_FT9001_A97A_USB_ENC_SV1.0_7049";
	const guint8 version_debug[] = "FT9349_APP_FT9001_A97A_USB_ENC_SV0.1_f132";
	const gchar *version_invalid[] = {
	    "",						/* empty */
	    "FT9349_FT9001_A97A_USB_ENC_SV0.1_7225",	/* no mode marker */
	    "FT9349_APP_IAP_FT9001_USB_ENC_SV0.1_7225", /* both mode markers */
	    "FT9349_APP_FT9001_A97A_USB_ENC_SV0.1_",	/* no build number */
	    "FT9349_APP_FT9001_\xff\xff_SV0.1_7225",	/* not utf-8 */
	};
	gboolean is_bootloader = FALSE;
	g_autofree gchar *version = NULL;
	g_autoptr(GError) error = NULL;

	/* is_bootloader gates the IAP capability probe: a runtime device must
	 * never be probed as its firmware always implements the command */
	version = fu_focal_moc_version_parse(version_app,
					     sizeof(version_app) - 1,
					     &is_bootloader,
					     &error);
	g_assert_no_error(error);
	g_assert_cmpstr(version, ==, "7225");
	g_assert_false(is_bootloader);

	g_clear_pointer(&version, g_free);
	version = fu_focal_moc_version_parse(version_iap,
					     sizeof(version_iap) - 1,
					     &is_bootloader,
					     &error);
	g_assert_no_error(error);
	g_assert_cmpstr(version, ==, "7049");
	g_assert_true(is_bootloader);

	/* internal debug builds prefix the build number with a letter */
	g_clear_pointer(&version, g_free);
	version = fu_focal_moc_version_parse(version_debug,
					     sizeof(version_debug) - 1,
					     &is_bootloader,
					     &error);
	g_assert_no_error(error);
	g_assert_cmpstr(version, ==, "f132");
	g_assert_false(is_bootloader);

	for (guint i = 0; i < G_N_ELEMENTS(version_invalid); i++) {
		gboolean is_bootloader_tmp = FALSE;
		g_autofree gchar *version_tmp = NULL;
		g_autoptr(GError) error_tmp = NULL;

		version_tmp = fu_focal_moc_version_parse((const guint8 *)version_invalid[i],
							 strlen(version_invalid[i]),
							 &is_bootloader_tmp,
							 &error_tmp);
		g_assert_error(error_tmp, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_null(version_tmp);
	}

	/* a zero-length USB read hands over a NULL buffer */
	g_assert_null(fu_focal_moc_version_parse(NULL, 0, &is_bootloader, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_focal_moc_iap_status_layout_func(void)
{
	const guint8 payload_valid[] = "FT9349_Coating_V1.0";
	const guint8 payload_binary[] = {0x46, 0x54, 0x01, 0x02};
	guint8 payload_long[65] = {0};
	FuFocalMocIapStatusLayout layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN;
	g_autoptr(GError) error = NULL;

	/* 0x04 with a valid sensor version: the unified status layout is confirmed */
	g_assert_true(fu_focal_moc_iap_status_layout_from_probe(FU_FOCAL_MOC_STATUS_OK,
								payload_valid,
								sizeof(payload_valid) - 1,
								&layout,
								&error));
	g_assert_no_error(error);
	g_assert_cmpint(layout, ==, FU_FOCAL_MOC_IAP_STATUS_LAYOUT_ALIGNED);

	/* 0x09: the command is not registered on older bootloaders, which stay
	 * updatable using the legacy layout */
	layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN;
	g_assert_true(fu_focal_moc_iap_status_layout_from_probe(FU_FOCAL_MOC_STATUS_INVALID_COMMAND,
								NULL,
								0,
								&layout,
								&error));
	g_assert_no_error(error);
	g_assert_cmpint(layout, ==, FU_FOCAL_MOC_IAP_STATUS_LAYOUT_LEGACY);

	/* 0x04 with an empty payload must not count as the new capability */
	layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN;
	g_assert_false(fu_focal_moc_iap_status_layout_from_probe(FU_FOCAL_MOC_STATUS_OK,
								 NULL,
								 0,
								 &layout,
								 &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_cmpint(layout, ==, FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN);
	g_clear_error(&error);

	/* 0x04 with a non-text payload must not count as the new capability */
	g_assert_false(fu_focal_moc_iap_status_layout_from_probe(FU_FOCAL_MOC_STATUS_OK,
								 payload_binary,
								 sizeof(payload_binary),
								 &layout,
								 &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_cmpint(layout, ==, FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN);
	g_clear_error(&error);

	/* 0x04 with an oversized payload must not count as the new capability */
	memset(payload_long, 'A', sizeof(payload_long));
	g_assert_false(fu_focal_moc_iap_status_layout_from_probe(FU_FOCAL_MOC_STATUS_OK,
								 payload_long,
								 sizeof(payload_long),
								 &layout,
								 &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_cmpint(layout, ==, FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN);
	g_clear_error(&error);

	/* any other status leaves the capability unknown: fail closed */
	g_assert_false(fu_focal_moc_iap_status_layout_from_probe(FU_FOCAL_MOC_STATUS_TIMEOUT,
								 NULL,
								 0,
								 &layout,
								 &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpint(layout, ==, FU_FOCAL_MOC_IAP_STATUS_LAYOUT_UNKNOWN);
}

static void
fu_focal_moc_fp_version_func(void)
{
	const guint8 payload_dotted[] = "SV0.1_0104";
	const guint8 payload_lead_space[] = "  7225";
	const guint8 payload_trail_space[] = "7225  ";
	const guint8 payload_space[] = "   ";
	const guint8 payload_nul[] = "72\0garbage";
	const guint8 payload_binary[] = "FT\x01\x02";
	const guint8 payload_binary_first[] = "\x01"
					      "7225";
	const guint8 payload_junk[] = "\x01\x02\x03";
	guint8 payload_max[64] = {0};
	guint8 payload_over[sizeof(payload_max) + 1] = {0};
	struct {
		const gchar *name;
		const guint8 *buf;
		gsize bufsz;
		const gchar *error_substr;
	} cases[] = {
	    {"dotted", payload_dotted, sizeof(payload_dotted) - 1, NULL},
	    {"leading space", payload_lead_space, sizeof(payload_lead_space) - 1, NULL},
	    {"trailing space", payload_trail_space, sizeof(payload_trail_space) - 1, NULL},
	    {"embedded NUL", payload_nul, sizeof(payload_nul) - 1, NULL},
	    {"maximum length", payload_max, sizeof(payload_max), NULL},
	    {"whitespace only", payload_space, sizeof(payload_space) - 1, "empty"},
	    {"all binary", payload_junk, sizeof(payload_junk) - 1, "empty"},
	    {"no payload", NULL, 0, "empty"},
	    {"too long", payload_over, sizeof(payload_over), "too long"},
	    {"binary tail", payload_binary, sizeof(payload_binary) - 1, "not printable"},
	    {"binary head",
	     payload_binary_first,
	     sizeof(payload_binary_first) - 1,
	     "not printable"},
	};

	memset(payload_max, 'A', sizeof(payload_max));
	memset(payload_over, 'A', sizeof(payload_over));
	for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
		gboolean ret;
		g_autoptr(GError) error = NULL;

		g_test_message("checking '%s'", cases[i].name);
		ret = fu_focal_moc_fp_version_validate(cases[i].buf, cases[i].bufsz, &error);
		if (cases[i].error_substr == NULL) {
			g_assert_no_error(error);
			g_assert_true(ret);
			continue;
		}
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_false(ret);
		g_assert_nonnull(g_strstr_len(error->message, -1, cases[i].error_substr));
	}
}

static void
fu_focal_moc_iap_probe_required_func(void)
{
	/* only a bootloader is probed */
	g_assert_true(fu_focal_moc_iap_probe_required(TRUE));

	/* the runtime firmware always implements the command: never probe */
	g_assert_false(fu_focal_moc_iap_probe_required(FALSE));
}

static void
fu_focal_moc_status_error_func(void)
{
	g_autoptr(GByteArray) data = g_byte_array_new();
	g_autoptr(GError) error = NULL;

	/* unified layout: 0x07 is out-of-memory */
	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_NO_MEMORY,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    TRUE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_nonnull(g_strstr_len(error->message, -1, "out of memory"));
	g_clear_error(&error);

	/* unified layout: 0x08 is a command check failure */
	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_CHECK_ERROR,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    TRUE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "check failed"));
	g_clear_error(&error);

	/* unified layout: 0x0a is an execution failure */
	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_EXECUTION_FAILURE,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    TRUE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_nonnull(g_strstr_len(error->message, -1, "execution failed"));
	g_clear_error(&error);

	/* legacy layout: 0x07/0x08/0x0a keep their raw code and context */
	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_NO_MEMORY,
						    "STX rejected",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    FALSE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_nonnull(g_strstr_len(error->message, -1, "STX rejected"));
	g_assert_nonnull(g_strstr_len(error->message, -1, "status=0x07"));
	g_assert_nonnull(g_strstr_len(error->message, -1, "legacy IAP status"));
	g_assert_null(g_strstr_len(error->message, -1, "memory"));
	g_clear_error(&error);

	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_CHECK_ERROR,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    FALSE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_nonnull(g_strstr_len(error->message, -1, "status=0x08"));
	g_assert_null(g_strstr_len(error->message, -1, "check failed"));
	g_clear_error(&error);

	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_EXECUTION_FAILURE,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    FALSE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_nonnull(g_strstr_len(error->message, -1, "status=0x0a"));
	g_assert_nonnull(g_strstr_len(error->message, -1, "legacy IAP status"));
	g_assert_null(g_strstr_len(error->message, -1, "execution"));
	g_clear_error(&error);

	/* unambiguous codes stay mapped even on the legacy layout */
	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_INVALID_PARAMETER,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    FALSE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "invalid parameter"));
	g_clear_error(&error);

	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_TIMEOUT,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    FALSE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT);
	g_clear_error(&error);

	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_INVALID_COMMAND,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    FALSE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "invalid command"));
	g_clear_error(&error);

	/* firmware-transfer statuses keep their existing mapping in both layouts */
	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_FIRMWARE_VERIFY,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    FALSE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_clear_error(&error);

	g_assert_false(fu_focal_moc_status_to_error(FU_FOCAL_MOC_STATUS_FIRMWARE_FLASH,
						    "ctx",
						    FWUPD_ERROR_NOT_SUPPORTED,
						    TRUE,
						    data,
						    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE);
	g_clear_error(&error);
}

static GBytes *
fu_focal_moc_build_firmware_sized(guint32 kind, guint32 body_sz)
{
	guint8 reserved1[28] = {0};
	guint8 digest[32] = {0};
	guint8 signature[64] = {0};
	guint8 reserved2[112] = {0};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* nonzero patterns prove the writer carries the fields, not the raw copy */
	for (guint i = 0; i < sizeof(digest); i++)
		digest[i] = (guint8)i;
	for (guint i = 0; i < sizeof(signature); i++)
		signature[i] = (guint8)(0x80 + i);
	fu_byte_array_append_uint32(buf, 0x46574844, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, kind, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0x0104, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, body_sz, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, 0, G_LITTLE_ENDIAN);
	g_byte_array_append(buf, reserved1, sizeof(reserved1));
	g_byte_array_append(buf, digest, sizeof(digest));
	g_byte_array_append(buf, signature, sizeof(signature));
	g_byte_array_append(buf, reserved2, sizeof(reserved2));
	fu_byte_array_set_size(buf, FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE + body_sz, 0x0);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static GBytes *
fu_focal_moc_build_firmware(guint32 kind)
{
	return fu_focal_moc_build_firmware_sized(kind, 4);
}

static void
fu_focal_moc_firmware_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_focal_moc_firmware_new();
	g_autoptr(GBytes) bytes = fu_focal_moc_build_firmware(FU_FOCAL_MOC_FIRMWARE_KIND_APP);
	g_autoptr(GBytes) bytes_body = NULL;
	g_autoptr(GBytes) bytes_written = NULL;
	g_autoptr(FuInputStream) stream = fu_memory_input_stream_new_from_bytes(bytes);
	g_autoptr(GError) error = NULL;

	g_assert_true(
	    fu_firmware_parse_stream(firmware, stream, 0, FU_FIRMWARE_PARSE_FLAG_NONE, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "0104");

	/* the payload is the body without the header */
	bytes_body = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes_body);
	g_assert_cmpuint(g_bytes_get_size(bytes_body), ==, 4);

	/* the writer rebuilds the header from the parsed fields */
	bytes_written = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes_written);
	g_assert_cmpmem(g_bytes_get_data(bytes_written, NULL),
			g_bytes_get_size(bytes_written),
			g_bytes_get_data(bytes, NULL),
			g_bytes_get_size(bytes));
}

static void
fu_focal_moc_firmware_xml_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "focal-moc.builder.xml", NULL);
	g_assert_true(
	    fu_firmware_roundtrip_from_filename(filename,
						"82d74338dcf189114871f15666e96b7471db3e82",
						FU_FIRMWARE_BUILDER_FLAG_NONE,
						&error));
	g_assert_no_error(error);
}

static void
fu_focal_moc_firmware_wrong_type_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_focal_moc_firmware_new();
	g_autoptr(GBytes) bytes = fu_focal_moc_build_firmware(0x01);
	g_autoptr(FuInputStream) stream = fu_memory_input_stream_new_from_bytes(bytes);
	g_autoptr(GError) error = NULL;

	g_assert_false(
	    fu_firmware_parse_stream(firmware, stream, 0, FU_FIRMWARE_PARSE_FLAG_NONE, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
}

static void
fu_focal_moc_firmware_invalid_func(void)
{
	const struct {
		gsize offset;
		guint32 value;
		FwupdError code;
	} tests[] = {
	    /* a bad magic is rejected by the struct parser, the rest by the field checks */
	    {0, 0x00000000, FWUPD_ERROR_INVALID_DATA},
	    {8, 0x00010000, FWUPD_ERROR_INVALID_FILE},
	    {12, 0x00000005, FWUPD_ERROR_INVALID_FILE},
	    {16, 0x00000004, FWUPD_ERROR_INVALID_FILE},
	};

	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		g_autoptr(FuFirmware) firmware = fu_focal_moc_firmware_new();
		g_autoptr(GBytes) bytes =
		    fu_focal_moc_build_firmware(FU_FOCAL_MOC_FIRMWARE_KIND_APP);
		g_autoptr(GByteArray) buf = g_bytes_unref_to_array(g_steal_pointer(&bytes));
		g_autoptr(FuInputStream) stream = NULL;
		g_autoptr(GError) error = NULL;

		fu_memwrite_uint32(buf->data + tests[i].offset, tests[i].value, G_LITTLE_ENDIAN);
		stream = fu_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
		g_assert_false(fu_firmware_parse_stream(firmware,
							stream,
							0,
							FU_FIRMWARE_PARSE_FLAG_NONE,
							&error));
		g_assert_error(error, FWUPD_ERROR, (gint)tests[i].code);
	}
}

static void
fu_focal_moc_firmware_size_max_func(void)
{
	const gsize size_max = FU_FOCAL_MOC_FIRMWARE_SIZE_MAX;
	const struct {
		gsize streamsz;
		gboolean valid;
	} tests[] = {
	    {size_max, TRUE},
	    {size_max + 1, FALSE},
	};

	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		gboolean ret;
		g_autoptr(FuFirmware) firmware = fu_focal_moc_firmware_new();
		g_autoptr(GBytes) bytes = fu_focal_moc_build_firmware_sized(
		    FU_FOCAL_MOC_FIRMWARE_KIND_APP,
		    (guint32)(tests[i].streamsz - FU_STRUCT_FOCAL_MOC_FIRMWARE_HEADER_SIZE));
		g_autoptr(FuInputStream) stream = fu_memory_input_stream_new_from_bytes(bytes);
		g_autoptr(GError) error = NULL;

		ret = fu_firmware_parse_stream(firmware,
					       stream,
					       0,
					       FU_FIRMWARE_PARSE_FLAG_NONE,
					       &error);
		g_assert_cmpint(ret, ==, tests[i].valid);
		if (tests[i].valid)
			g_assert_no_error(error);
		else
			g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	}
}

static void
fu_focal_moc_firmware_strict_func(void)
{
	const struct {
		gsize offset;
		guint8 value;
	} tests[] = {
	    /* reserved regions must stay zero or a parse-write cycle would not
	     * reproduce the file */
	    {0x14, 0xAA},
	    {0x95, 0x01},
	};

	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		g_autoptr(FuFirmware) firmware = fu_focal_moc_firmware_new();
		g_autoptr(GBytes) bytes =
		    fu_focal_moc_build_firmware(FU_FOCAL_MOC_FIRMWARE_KIND_APP);
		g_autoptr(GByteArray) buf = g_bytes_unref_to_array(g_steal_pointer(&bytes));
		g_autoptr(FuInputStream) stream = NULL;
		g_autoptr(GError) error = NULL;

		buf->data[tests[i].offset] = tests[i].value;
		stream = fu_memory_input_stream_new_from_data(buf->data, buf->len, NULL);
		g_assert_false(fu_firmware_parse_stream(firmware,
							stream,
							0,
							FU_FIRMWARE_PARSE_FLAG_NONE,
							&error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	}
}

static void
fu_focal_moc_firmware_truncated_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_focal_moc_firmware_new();
	g_autoptr(GBytes) bytes = fu_focal_moc_build_firmware(FU_FOCAL_MOC_FIRMWARE_KIND_APP);
	g_autoptr(FuInputStream) stream =
	    fu_memory_input_stream_new_from_data(g_bytes_get_data(bytes, NULL), 128, NULL);
	g_autoptr(GError) error = NULL;

	g_assert_false(
	    fu_firmware_parse_stream(firmware, stream, 0, FU_FIRMWARE_PARSE_FLAG_NONE, &error));
	g_assert_nonnull(error);
}

static void
fu_focal_moc_firmware_entry_offset_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_focal_moc_firmware_new();
	g_autoptr(GBytes) bytes = fu_focal_moc_build_firmware(FU_FOCAL_MOC_FIRMWARE_KIND_APP);
	g_autoptr(GByteArray) buf = g_bytes_unref_to_array(g_steal_pointer(&bytes));
	g_autoptr(GBytes) bytes_patched = NULL;
	g_autoptr(GBytes) bytes_written = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;

	/* a nonzero entry offset must survive the parse-write cycle */
	fu_memwrite_uint32(buf->data + 0x10, 0x2, G_LITTLE_ENDIAN);
	bytes_patched = g_byte_array_free_to_bytes(g_steal_pointer(&buf));
	stream = fu_memory_input_stream_new_from_bytes(bytes_patched);
	g_assert_true(
	    fu_firmware_parse_stream(firmware, stream, 0, FU_FIRMWARE_PARSE_FLAG_NONE, &error));
	g_assert_no_error(error);
	bytes_written = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes_written);
	g_assert_cmpmem(g_bytes_get_data(bytes_written, NULL),
			g_bytes_get_size(bytes_written),
			g_bytes_get_data(bytes_patched, NULL),
			g_bytes_get_size(bytes_patched));
}

static void
fu_focal_moc_firmware_xml_invalid_func(void)
{
	const gchar *xmls[] = {
	    "<firmware gtype=\"FuFocalMocFirmware\"><digest>zz</digest></firmware>",
	    "<firmware gtype=\"FuFocalMocFirmware\"><digest>aabb</digest></firmware>",
	    "<firmware gtype=\"FuFocalMocFirmware\"><signature>aabb</signature></firmware>",
	};

	for (guint i = 0; i < G_N_ELEMENTS(xmls); i++) {
		g_autoptr(FuFirmware) firmware = NULL;
		g_autoptr(GError) error = NULL;

		firmware = fu_firmware_new_from_xml(xmls[i], &error);
		g_assert_null(firmware);
		g_assert_nonnull(error);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_FOCAL_MOC_FIRMWARE);
	g_test_add_func("/focal-moc/packet/build", fu_focal_moc_packet_build_func);
	g_test_add_func("/focal-moc/transport/kdf", fu_focal_moc_transport_kdf_func);
	g_test_add_func("/focal-moc/transport/aes", fu_focal_moc_transport_aes_func);
	g_test_add_func("/focal-moc/transport/cipher-frame",
			fu_focal_moc_transport_cipher_frame_func);
	g_test_add_func("/focal-moc/transport/handshake", fu_focal_moc_transport_handshake_func);
	g_test_add_func("/focal-moc/transport/fragment-invalid",
			fu_focal_moc_transport_fragment_invalid_func);
	g_test_add_func("/focal-moc/transport/impl-vfuncs",
			fu_focal_moc_transport_impl_vfuncs_func);
	g_test_add_func("/focal-moc/packet/parse", fu_focal_moc_packet_parse_func);
	g_test_add_func("/focal-moc/packet/status-only", fu_focal_moc_packet_status_only_func);
	g_test_add_func("/focal-moc/packet/bad-bcc", fu_focal_moc_packet_bad_bcc_func);
	g_test_add_func("/focal-moc/packet/length", fu_focal_moc_packet_length_func);
	g_test_add_func("/focal-moc/ymodem/frame", fu_focal_moc_ymodem_frame_func);
	g_test_add_func("/focal-moc/ymodem/frame-plan", fu_focal_moc_ymodem_frame_plan_func);
	g_test_add_func("/focal-moc/version", fu_focal_moc_version_func);
	g_test_add_func("/focal-moc/version/frame", fu_focal_moc_version_frame_func);
	g_test_add_func("/focal-moc/fp-version", fu_focal_moc_fp_version_func);
	g_test_add_func("/focal-moc/iap-probe-required", fu_focal_moc_iap_probe_required_func);
	g_test_add_func("/focal-moc/iap-status-layout", fu_focal_moc_iap_status_layout_func);
	g_test_add_func("/focal-moc/status-error", fu_focal_moc_status_error_func);
	g_test_add_func("/focal-moc/firmware/parse", fu_focal_moc_firmware_func);
	g_test_add_func("/focal-moc/firmware/xml", fu_focal_moc_firmware_xml_func);
	g_test_add_func("/focal-moc/firmware/wrong-type", fu_focal_moc_firmware_wrong_type_func);
	g_test_add_func("/focal-moc/firmware/invalid", fu_focal_moc_firmware_invalid_func);
	g_test_add_func("/focal-moc/firmware/size-max", fu_focal_moc_firmware_size_max_func);
	g_test_add_func("/focal-moc/firmware/strict", fu_focal_moc_firmware_strict_func);
	g_test_add_func("/focal-moc/firmware/truncated", fu_focal_moc_firmware_truncated_func);
	g_test_add_func("/focal-moc/firmware/entry-offset",
			fu_focal_moc_firmware_entry_offset_func);
	g_test_add_func("/focal-moc/firmware/xml-invalid", fu_focal_moc_firmware_xml_invalid_func);
	return g_test_run();
}

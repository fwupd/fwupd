/*
 * Copyright (C) 2012 Andrew Duggan
 * Copyright (C) 2012 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_GNUTLS
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#endif

#include "fu-synaptics-rmi-common.h"

#define RMI_FUNCTION_QUERY_OFFSET	      0
#define RMI_FUNCTION_COMMAND_OFFSET	      1
#define RMI_FUNCTION_CONTROL_OFFSET	      2
#define RMI_FUNCTION_DATA_OFFSET	      3
#define RMI_FUNCTION_INTERRUPT_SOURCES_OFFSET 4
#define RMI_FUNCTION_NUMBER		      5

#define RMI_FUNCTION_VERSION_MASK	    0x60
#define RMI_FUNCTION_INTERRUPT_SOURCES_MASK 0x7

#ifdef HAVE_GNUTLS
typedef guchar gnutls_data_t;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_data_t, gnutls_free)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pubkey_t, gnutls_pubkey_deinit, NULL)
#pragma clang diagnostic pop
#endif

guint32
fu_synaptics_rmi_generate_checksum(const guint8 *data, gsize len)
{
	guint32 lsw = 0xffff;
	guint32 msw = 0xffff;
	for (gsize i = 0; i < len / 2; i++) {
		lsw += fu_memread_uint16(&data[i * 2], G_LITTLE_ENDIAN);
		msw += lsw;
		lsw = (lsw & 0xffff) + (lsw >> 16);
		msw = (msw & 0xffff) + (msw >> 16);
	}
	return msw << 16 | lsw;
}

FuSynapticsRmiFunction *
fu_synaptics_rmi_function_parse(GByteArray *buf,
				guint16 page_base,
				guint interrupt_count,
				GError **error)
{
	FuSynapticsRmiFunction *func;
	guint8 interrupt_offset;
	const guint8 *data = buf->data;

	/* not expected */
	if (buf->len != RMI_DEVICE_PDT_ENTRY_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "PDT entry buffer invalid size %u != %i",
			    buf->len,
			    RMI_DEVICE_PDT_ENTRY_SIZE);
		return NULL;
	}

	func = g_new0(FuSynapticsRmiFunction, 1);
	func->query_base = data[RMI_FUNCTION_QUERY_OFFSET] + page_base;
	func->command_base = data[RMI_FUNCTION_COMMAND_OFFSET] + page_base;
	func->control_base = data[RMI_FUNCTION_CONTROL_OFFSET] + page_base;
	func->data_base = data[RMI_FUNCTION_DATA_OFFSET] + page_base;
	func->interrupt_source_count =
	    data[RMI_FUNCTION_INTERRUPT_SOURCES_OFFSET] & RMI_FUNCTION_INTERRUPT_SOURCES_MASK;
	func->function_number = data[RMI_FUNCTION_NUMBER];
	func->function_version =
	    (data[RMI_FUNCTION_INTERRUPT_SOURCES_OFFSET] & RMI_FUNCTION_VERSION_MASK) >> 5;
	if (func->interrupt_source_count > 0) {
		func->interrupt_reg_num = (interrupt_count + 8) / 8 - 1;
		/* set an enable bit for each data source */
		interrupt_offset = interrupt_count % 8;
		func->interrupt_mask = 0;
		for (guint i = interrupt_offset;
		     i < (func->interrupt_source_count + interrupt_offset);
		     i++)
			func->interrupt_mask |= 1 << i;
	}
	return func;
}

gboolean
fu_synaptics_rmi_device_writeln(const gchar *fn, const gchar *buf, GError **error)
{
	int fd;
	g_autoptr(FuIOChannel) io = NULL;

	fd = open(fn, O_WRONLY);
	if (fd < 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "could not open %s", fn);
		return FALSE;
	}
	io = fu_io_channel_unix_new(fd);
	return fu_io_channel_write_raw(io,
				       (const guint8 *)buf,
				       strlen(buf),
				       1000,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

gboolean
fu_synaptics_verify_sha256_signature(GBytes *payload,
				     GBytes *pubkey,
				     GBytes *signature,
				     GError **error)
{
#ifdef HAVE_GNUTLS
	gnutls_datum_t hash;
	gnutls_datum_t m;
	gnutls_datum_t e;
	gnutls_datum_t sig;
	gnutls_hash_hd_t sha2;
	g_auto(gnutls_pubkey_t) pub = NULL;
	gint ec;
	guint8 exponent[] = {1, 0, 1};
	guint hash_length = gnutls_hash_get_len(GNUTLS_DIG_SHA256);
	g_autoptr(gnutls_data_t) hash_data = NULL;

	/* hash firmware data */
	hash_data = gnutls_malloc(hash_length);
	gnutls_hash_init(&sha2, GNUTLS_DIG_SHA256);
	gnutls_hash(sha2, g_bytes_get_data(payload, NULL), g_bytes_get_size(payload));
	gnutls_hash_deinit(sha2, hash_data);

	/* hash */
	hash.size = hash_length;
	hash.data = hash_data;

	/* modulus */
	m.size = g_bytes_get_size(pubkey);
	m.data = (guint8 *)g_bytes_get_data(pubkey, NULL);

	/* exponent */
	e.size = sizeof(exponent);
	e.data = exponent;

	/* signature */
	sig.size = g_bytes_get_size(signature);
	sig.data = (guint8 *)g_bytes_get_data(signature, NULL);

	gnutls_pubkey_init(&pub);
	ec = gnutls_pubkey_import_rsa_raw(pub, &m, &e);
	if (ec < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to import RSA key: %s",
			    gnutls_strerror(ec));
		return FALSE;
	}
	ec = gnutls_pubkey_verify_hash2(pub, GNUTLS_SIGN_RSA_SHA256, 0, &hash, &sig);
	if (ec < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to verify firmware: %s",
			    gnutls_strerror(ec));
		return FALSE;
	}
#endif
	/* success */
	return TRUE;
}

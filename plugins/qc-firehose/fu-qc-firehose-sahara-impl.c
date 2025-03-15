/*
 * Copyright 2021 Quectel Wireless Solutions Co., Ltd.
 *                    Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-sahara-impl.h"
#include "fu-qc-firehose-struct.h"

G_DEFINE_INTERFACE(FuQcFirehoseSaharaImpl, fu_qc_firehose_sahara_impl, G_TYPE_OBJECT)

#define FU_QC_FIREHOSE_USB_DEVICE_TIMEOUT_MS 500

static void
fu_qc_firehose_sahara_impl_default_init(FuQcFirehoseSaharaImplInterface *iface)
{
}

static GByteArray *
fu_qc_firehose_sahara_impl_read(FuQcFirehoseSaharaImpl *self, guint timeout_ms, GError **error)
{
	FuQcFirehoseSaharaImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_FIREHOSE_SAHARA_IMPL(self), NULL);

	iface = FU_QC_FIREHOSE_SAHARA_IMPL_GET_IFACE(self);
	if (iface->read == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->read not implemented");
		return NULL;
	}
	return (*iface->read)(self, timeout_ms, error);
}

static gboolean
fu_qc_firehose_sahara_impl_write(FuQcFirehoseSaharaImpl *self,
				 const guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	FuQcFirehoseSaharaImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_FIREHOSE_SAHARA_IMPL(self), FALSE);

	iface = FU_QC_FIREHOSE_SAHARA_IMPL_GET_IFACE(self);
	if (iface->write == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->write not implemented");
		return FALSE;
	}
	return (*iface->write)(self, buf, bufsz, error);
}

static gboolean
fu_qc_firehose_sahara_impl_hello(FuQcFirehoseSaharaImpl *self, GByteArray *buf, GError **error)
{
	g_autoptr(FuQcFirehoseSaharaPktHello) st = NULL;
	g_autoptr(FuQcFirehoseSaharaPktHelloResp) st_resp =
	    fu_qc_firehose_sahara_pkt_hello_resp_new();

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_hello_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	fu_qc_firehose_sahara_pkt_hello_resp_set_mode(st_resp,
						      fu_qc_firehose_sahara_pkt_hello_get_mode(st));
	return fu_qc_firehose_sahara_impl_write(self, st_resp->data, st_resp->len, error);
}

static gboolean
fu_qc_firehose_sahara_impl_read32(FuQcFirehoseSaharaImpl *self,
				  GByteArray *buf,
				  GBytes *blob,
				  GError **error)
{
	g_autoptr(FuQcFirehoseSaharaPktRead) st = NULL;
	g_autoptr(GBytes) blob_chunk = NULL;

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_read_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	blob_chunk = fu_bytes_new_offset(blob,
					 fu_qc_firehose_sahara_pkt_read_get_offset(st),
					 fu_qc_firehose_sahara_pkt_read_get_length(st),
					 error);
	if (blob_chunk == NULL) {
		g_prefix_error(error, "failed to get bootloader chunk: ");
		return FALSE;
	}
	return fu_qc_firehose_sahara_impl_write(self,
						g_bytes_get_data(blob_chunk, NULL),
						g_bytes_get_size(blob_chunk),
						error);
}

static gboolean
fu_qc_firehose_sahara_impl_read64(FuQcFirehoseSaharaImpl *self,
				  GByteArray *buf,
				  GBytes *blob,
				  GError **error)
{
	g_autoptr(FuQcFirehoseSaharaPktRead64) st = NULL;
	g_autoptr(GBytes) blob_chunk = NULL;

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_read64_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	blob_chunk = fu_bytes_new_offset(blob,
					 fu_qc_firehose_sahara_pkt_read64_get_offset(st),
					 fu_qc_firehose_sahara_pkt_read64_get_length(st),
					 error);
	if (blob_chunk == NULL) {
		g_prefix_error(error, "failed to get bootloader chunk: ");
		return FALSE;
	}
	return fu_qc_firehose_sahara_impl_write(self,
						g_bytes_get_data(blob_chunk, NULL),
						g_bytes_get_size(blob_chunk),
						error);
}

static gboolean
fu_qc_firehose_sahara_impl_eoi(FuQcFirehoseSaharaImpl *self, GByteArray *buf, GError **error)
{
	FuQcFirehoseSaharaStatus status;
	g_autoptr(FuQcFirehoseSaharaPktEndOfImage) st = NULL;
	g_autoptr(FuQcFirehoseSaharaPktDone) st_resp = fu_qc_firehose_sahara_pkt_done_new();

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_end_of_image_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	status = fu_qc_firehose_sahara_pkt_end_of_image_get_status(st);
	if (status != FU_QC_FIREHOSE_SAHARA_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid image status for EndOfImage 0x%x: %s",
			    status,
			    fu_qc_firehose_sahara_status_to_string(status));
		return FALSE;
	}
	return fu_qc_firehose_sahara_impl_write(self, st_resp->data, st_resp->len, error);
}

static gboolean
fu_qc_firehose_sahara_impl_done(FuQcFirehoseSaharaImpl *self, GByteArray *buf, GError **error)
{
	FuQcFirehoseSaharaStatus status;
	g_autoptr(FuQcFirehoseSaharaPktDoneResp) st = NULL;

	/* re-parse */
	st = fu_qc_firehose_sahara_pkt_done_resp_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	status = fu_qc_firehose_sahara_pkt_done_resp_get_status(st);
	if (status != FU_QC_FIREHOSE_SAHARA_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid image status for Done 0x%x: %s",
			    status,
			    fu_qc_firehose_sahara_status_to_string(status));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_qc_firehose_sahara_impl_write_firmware(FuQcFirehoseSaharaImpl *self,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  GError **error)
{
	const gchar *fnglob = "firehose-prog.mbn|prog_nand*.mbn|prog_firehose*";
	gboolean done = FALSE;
	g_autoptr(GBytes) blob = NULL;

	blob = fu_firmware_get_image_by_id_bytes(firmware, fnglob, error);
	if (blob == NULL) {
		g_prefix_error(error, "failed to find %s: ", fnglob);
		return FALSE;
	}
	for (guint i = 0; i < G_MAXUINT16 && !done; i++) {
		FuQcFirehoseSaharaCommandId cmd_id;
		g_autoptr(FuQcFirehoseSaharaPkt) pkt = NULL;
		g_autoptr(GByteArray) buf = NULL;

		buf = fu_qc_firehose_sahara_impl_read(self,
						      FU_QC_FIREHOSE_USB_DEVICE_TIMEOUT_MS,
						      error);
		if (buf == NULL) {
			g_prefix_error(error, "failed to get device response: ");
			return FALSE;
		}

		/* check if we're already loaded, perhaps from MHI-QCDM */
		if (i == 0) {
			g_autofree gchar *str = fu_strsafe((const gchar *)buf->data, buf->len);
			if (str != NULL && g_str_has_prefix(str, "<?xml version=")) {
				g_debug("already receiving firehose XML!");
				return TRUE;
			}
		}
		pkt = fu_qc_firehose_sahara_pkt_parse(buf->data, buf->len, 0x0, error);
		if (pkt == NULL)
			return FALSE;
		if (buf->len != fu_qc_firehose_sahara_pkt_get_hdr_length(pkt)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid packet header");
			return FALSE;
		}

		/* parse the response */
		cmd_id = fu_qc_firehose_sahara_pkt_get_command_id(pkt);
		switch (cmd_id) {
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_HELLO:
			if (!fu_qc_firehose_sahara_impl_hello(self, buf, error))
				return FALSE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_READ:
			if (!fu_qc_firehose_sahara_impl_read32(self, buf, blob, error))
				return FALSE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_END_OF_IMAGE:
			if (!fu_qc_firehose_sahara_impl_eoi(self, buf, error))
				return FALSE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_DONE_RESPONSE:
			if (!fu_qc_firehose_sahara_impl_done(self, buf, error))
				return FALSE;
			done = TRUE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_READ64:
			if (!fu_qc_firehose_sahara_impl_read64(self, buf, blob, error))
				return FALSE;
			break;
		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid command ID 0x%x: %s",
				    cmd_id,
				    fu_qc_firehose_sahara_command_id_to_string(cmd_id));
			return FALSE;
		}
	}
	if (!done) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "transferring sahara never completed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

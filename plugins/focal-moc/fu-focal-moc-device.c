/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-device.h"
#include "fu-focal-moc-struct.h"

/*
 * FocalTech MOC fingerprint sensor — USB firmware-update plugin.
 *
 * Protocol source: UpgradeTool/ff_update.c (ff_get_firmware_version,
 *                  ff_set_bootloader_mode, ff_download_firmware).
 *
 * ── Standard command packet (ff_cmd_buf_t) ──────────────────────────────────
 *
 *   [ 0x02 | LEN_HI | LEN_LO | CMD | data... | BCC ]
 *
 *   LEN (big-endian uint16) = len(data) + 1  (+1 counts the trailing BCC byte)
 *   BCC = XOR of every byte from LEN_HI through the last data byte (inclusive)
 *
 *   The device always replies with CMD = 0x04 (ACK).
 *   Success is indicated solely by CMD == 0x04 — there is NO in-band status byte.
 *
 * ── Firmware-download packet (ff_down_buf_t, CMD = 0x33) ────────────────────
 *
 *   [ 0x02 | LEN_HI | LEN_LO | 0x33 | MAGIC | SEQ | data(up to 1024) | BCC ]
 *
 * ── Firmware update sequence ─────────────────────────────────────────────────
 *
 *   1. CMD_GET_FW_VERSION (0x30) → read version string
 *   2. CMD_SET_BOOT_MODE  (0x32, mode=1) → enter bootloader (IAP) mode
 *      [device re-enumerates → wait for replug]
 *   3. CMD_FW_DOWNLOAD    (0x33) × (1 + N + 1):
 *        a. SHO packet (magic=0x01): filename(64B) + filesize(4B BE) + CRC32(4B BE) + BCC
 *        b. STX packets (magic=0x02): 1024-byte firmware blocks, one ACK per block
 *        c. EOT packet  (magic=0x04): remaining (<1024) bytes — NO per-packet ACK
 *        d. Final ACK received after EOT
 *   4. Device reboots automatically into new firmware after EOT ACK.
 *      [wait for replug]
 */

struct _FuFocalMocDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuFocalMocDevice, fu_focal_moc_device, FU_TYPE_USB_DEVICE)

/* USB bulk endpoints (UpgradeTool detects these at runtime; standard values) */
#define FU_FOCAL_MOC_USB_EP_IN     (1 | 0x80)
#define FU_FOCAL_MOC_USB_EP_OUT    (2 | 0x00)
#define FU_FOCAL_MOC_USB_INTERFACE 0

#define FU_FOCAL_MOC_USB_TIMEOUT	    1000 /* ms — send */
#define FU_FOCAL_MOC_RECV_TIMEOUT	    1000 /* ms — receive (1 s, matching UpgradeTool) */
#define FU_FOCAL_MOC_RECV_FINAL_TIMEOUT 2000 /* ms — longer wait after EOT */

/* MSG_MAGIC from ff_trans.h */
#define FU_FOCAL_MOC_MSG_MAGIC 0x02

/* Firmware-download packet constants (ff_update.c) */
#define FU_FOCAL_MOC_DL_BLOCK_SIZE 1024	    /* bytes per STX/EOT data block */
#define FU_FOCAL_MOC_FW_MAX_SIZE   (256 * 1024) /* 256 KiB hard limit */
#define FU_FOCAL_MOC_FILENAME_LEN  64	    /* filename field in SHO packet */

/* Delays matching UpgradeTool (ff_util_msleep calls) */
#define FU_FOCAL_MOC_DELAY_CMD_MS	5   /* after version/mode commands */
#define FU_FOCAL_MOC_DELAY_DL_MS	10  /* between download packets */
#define FU_FOCAL_MOC_DELAY_FINAL_MS 200 /* before reading final ACK */

/* Maximum receive buffer (covers any response packet) */
#define FU_FOCAL_MOC_MAX_RSP_SIZE 256

/* --------------------------------------------------------------------------
 * Low-level helpers
 * -------------------------------------------------------------------------- */

/*
 * fu_focal_moc_device_bcc:
 * BCC = XOR of every byte in buf[0..len-1].
 * Covers the slice starting at LEN_HI (i.e. skip the leading 0x02 magic).
 */
static guint8
fu_focal_moc_device_bcc(const guint8 *buf, gsize len)
{
	guint8 bcc = 0;
	for (gsize i = 0; i < len; i++)
		bcc ^= buf[i];
	return bcc;
}

/* --------------------------------------------------------------------------
 * Packet primitives
 * -------------------------------------------------------------------------- */

/*
 * fu_focal_moc_device_send:
 * Transmit @pkt over the bulk-OUT endpoint.
 */
static gboolean
fu_focal_moc_device_send(FuFocalMocDevice *self, GByteArray *pkt, GError **error)
{
	gsize actual = 0;

	fu_dump_full(G_LOG_DOMAIN, "SEND", pkt->data, pkt->len, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_FOCAL_MOC_USB_EP_OUT,
					 pkt->data,
					 pkt->len,
					 &actual,
					 FU_FOCAL_MOC_USB_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "send failed: ");
		return FALSE;
	}
	if (actual != pkt->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "short write: sent %zu of %u bytes",
			    actual,
			    pkt->len);
		return FALSE;
	}
	return TRUE;
}

/*
 * fu_focal_moc_device_recv_ack:
 * Read one response packet and verify it is an ACK (CMD == 0x04).
 * Uses @timeout_ms to allow a longer wait for the final EOT response.
 *
 * The device response is a plain ff_cmd_buf_t:
 *   [ 0x02 | LEN_HI | LEN_LO | 0x04 | data... | BCC ]
 * Success = CMD == 0x04.  No in-band status byte.
 */
static gboolean
fu_focal_moc_device_recv_ack(FuFocalMocDevice *self, guint timeout_ms, GError **error)
{
	gsize actual = 0;
	guint16 pkt_ln;
	guint8 bcc_calc;
	guint8 bcc_recv;
	guint8 buf[FU_FOCAL_MOC_MAX_RSP_SIZE] = {0};
	g_autoptr(FuStructFocalMocCmdRsp) st_res = NULL;

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_FOCAL_MOC_USB_EP_IN,
					 buf,
					 sizeof(buf),
					 &actual,
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "recv failed: ");
		return FALSE;
	}

	if (actual < 5) {
		/* minimum: magic(1)+len(2)+cmd(1)+bcc(1) */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "response too short: %zu bytes",
			    actual);
		return FALSE;
	}

	fu_dump_full(G_LOG_DOMAIN, "RECV", buf, actual, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);

	/* parse the 4-byte response header */
	st_res = fu_struct_focal_moc_cmd_rsp_parse(buf, sizeof(buf), 0, error);
	if (st_res == NULL)
		return FALSE;

	/* validate magic */
	if (fu_struct_focal_moc_cmd_rsp_get_head(st_res) != FU_FOCAL_MOC_MSG_MAGIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "bad magic: 0x%02x",
			    fu_struct_focal_moc_cmd_rsp_get_head(st_res));
		return FALSE;
	}

	/* Use LN field to locate BCC; actual may include USB padding bytes.
	 * Device sends [MAGIC|LN_HI|LN_LO|CMD...|BCC] where BCC is at buf[3+LN].
	 * actual-1 is wrong when USB pads the response. */
	pkt_ln = fu_struct_focal_moc_cmd_rsp_get_ln(st_res);
	if (pkt_ln < 1 || (gsize)(3 + pkt_ln + 1) > actual) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "response truncated: LN=%u but only %zu bytes",
			    (guint)pkt_ln,
			    actual);
		return FALSE;
	}
	bcc_calc = fu_focal_moc_device_bcc(buf + 1, 2 + pkt_ln);
	bcc_recv = buf[3 + pkt_ln];
	if (bcc_calc != bcc_recv) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "BCC mismatch: expected 0x%02x, got 0x%02x",
			    bcc_calc,
			    bcc_recv);
		return FALSE;
	}

	/* check ACK command byte */
	if (fu_struct_focal_moc_cmd_rsp_get_cmd(st_res) != FU_FOCAL_MOC_CMD_ACK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "expected ACK (0x04), got 0x%02x",
			    fu_struct_focal_moc_cmd_rsp_get_cmd(st_res));
		return FALSE;
	}

	return TRUE;
}

/*
 * fu_focal_moc_device_cmd_xfer:
 * Send a standard command packet and receive the ACK.
 *
 * Builds:  [ 0x02 | ln(BE) | cmd | payload... | BCC ]
 *   ln = len(payload) + 1  (+1 for the BCC byte)
 *
 * @delay_ms: sleep between send and recv (matches UpgradeTool's ff_util_msleep)
 */
static gboolean
fu_focal_moc_device_cmd_xfer(FuFocalMocDevice *self,
				 FuFocalMocCmd cmd,
				 const guint8 *payload,
				 gsize payload_len,
				 guint delay_ms,
				 GError **error)
{
	guint16 ln;
	guint8 bcc;
	g_autoptr(GByteArray) pkt = g_byte_array_new();
	g_autoptr(FuStructFocalMocCmdReq) st_req = NULL;

	/* LEN = payload bytes + 1 (for BCC) */
	ln = (guint16)(payload_len + 1);

	st_req = fu_struct_focal_moc_cmd_req_new();
	fu_struct_focal_moc_cmd_req_set_head(st_req, FU_FOCAL_MOC_MSG_MAGIC);
	fu_struct_focal_moc_cmd_req_set_ln(st_req, ln);
	fu_struct_focal_moc_cmd_req_set_cmd(st_req, cmd);
	g_byte_array_append(pkt, st_req->buf->data, st_req->buf->len);
	if (payload != NULL && payload_len > 0)
		g_byte_array_append(pkt, payload, payload_len);

	/* BCC covers pkt[1..] (skip magic byte) */
	bcc = fu_focal_moc_device_bcc(pkt->data + 1, pkt->len - 1);
	fu_byte_array_append_uint8(pkt, bcc);

	if (!fu_focal_moc_device_send(self, pkt, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), delay_ms);

	return fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_TIMEOUT, error);
}

/* --------------------------------------------------------------------------
 * Device operations
 * -------------------------------------------------------------------------- */

/*
 * fu_focal_moc_device_ensure_version:
 *
 * CMD 0x30 — ff_update.c:ff_get_firmware_version()
 *
 * Response: [ 0x02 | LEN_HI | LEN_LO | 0x04 | version_string... | BCC ]
 * The version string occupies data[0 .. LEN-2] (LEN-1 bytes, last byte is BCC).
 * No status byte precedes the string.
 */
static gboolean
fu_focal_moc_device_ensure_version(FuFocalMocDevice *self, GError **error)
{
	gsize actual = 0;
	guint8 bcc_calc;
	guint8 bcc_recv;
	guint8 rx_buf[64] = {0};
	g_autoptr(GByteArray) pkt = g_byte_array_new();
	g_autoptr(FuStructFocalMocVersionRsp) st_res = NULL;
	g_autoptr(FuStructFocalMocCmdReq) st_req = NULL;
	g_autoptr(FuStructFocalMocCmdRsp) st_hdr = NULL;
	g_autofree gchar *version = NULL;
	guint16 ln;
	guint16 pkt_ln;
	guint8 bcc;

	/* build: [ 0x02 | 0x00 0x01 | 0x30 | BCC ] */
	ln = 1; /* no data payload, just BCC */
	st_req = fu_struct_focal_moc_cmd_req_new();
	fu_struct_focal_moc_cmd_req_set_head(st_req, FU_FOCAL_MOC_MSG_MAGIC);
	fu_struct_focal_moc_cmd_req_set_ln(st_req, ln);
	fu_struct_focal_moc_cmd_req_set_cmd(st_req, FU_FOCAL_MOC_CMD_GET_FW_VERSION);
	g_byte_array_append(pkt, st_req->buf->data, st_req->buf->len);
	bcc = fu_focal_moc_device_bcc(pkt->data + 1, pkt->len - 1);
	fu_byte_array_append_uint8(pkt, bcc);

	if (!fu_focal_moc_device_send(self, pkt, error)) {
		g_prefix_error_literal(error, "version: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_CMD_MS);

	/* receive response */
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_FOCAL_MOC_USB_EP_IN,
					 rx_buf,
					 sizeof(rx_buf),
					 &actual,
					 FU_FOCAL_MOC_RECV_TIMEOUT,
					 NULL,
					 error)) {
		g_prefix_error_literal(error, "version recv: ");
		return FALSE;
	}

	fu_dump_full(G_LOG_DOMAIN, "VERSION-RSP", rx_buf, actual, 16, FU_DUMP_FLAG_SHOW_ADDRESSES);

	/* minimum: magic(1)+len(2)+cmd(1)+bcc(1) = 5 bytes */
	if (actual < 5) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "bad version response");
		return FALSE;
	}

	/* parse the 4-byte response header to obtain LN and validate magic */
	st_hdr = fu_struct_focal_moc_cmd_rsp_parse(rx_buf, sizeof(rx_buf), 0, error);
	if (st_hdr == NULL)
		return FALSE;
	if (fu_struct_focal_moc_cmd_rsp_get_head(st_hdr) != FU_FOCAL_MOC_MSG_MAGIC) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "bad version response");
		return FALSE;
	}

	/* use LN from the header struct to locate BCC accurately */
	pkt_ln = fu_struct_focal_moc_cmd_rsp_get_ln(st_hdr);
	if (pkt_ln < 1 || (gsize)(3 + pkt_ln + 1) > actual ||
	    (gsize)(3 + pkt_ln) >= sizeof(rx_buf)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "version response LN out of range");
		return FALSE;
	}
	bcc_calc = fu_focal_moc_device_bcc(rx_buf + 1, 2 + pkt_ln);
	bcc_recv = rx_buf[3 + pkt_ln];
	if (bcc_calc != bcc_recv) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "version BCC mismatch: expected 0x%02x, got 0x%02x",
			    bcc_calc,
			    bcc_recv);
		return FALSE;
	}

	/* NUL-terminate at the BCC position so fu_strsafe stops before it */
	rx_buf[3 + pkt_ln] = '\0';

	/* parse response: char[60] guarantees NUL-termination and safe printing */
	st_res = fu_struct_focal_moc_version_rsp_parse(rx_buf, sizeof(rx_buf), 0, error);
	if (st_res == NULL)
		return FALSE;
	if (fu_struct_focal_moc_version_rsp_get_cmd(st_res) != FU_FOCAL_MOC_CMD_ACK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "version: expected ACK, got 0x%02x",
			    fu_struct_focal_moc_version_rsp_get_cmd(st_res));
		return FALSE;
	}

	version = fu_struct_focal_moc_version_rsp_get_version(st_res);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

/*
 * fu_focal_moc_device_set_boot_mode:
 *
 * CMD 0x32 — ff_update.c:ff_set_bootloader_mode()
 *
 * Packet: [ 0x02 | 0x00 0x02 | 0x32 | mode | BCC ]
 *   LEN = 2  (1 data byte + 1 BCC)
 */
static gboolean
fu_focal_moc_device_set_boot_mode(FuFocalMocDevice *self,
				      FuFocalMocBootMode mode,
				      GError **error)
{
	guint8 payload = (guint8)mode;
	return fu_focal_moc_device_cmd_xfer(self,
						FU_FOCAL_MOC_CMD_SET_BOOT_MODE,
						&payload,
						1,
						FU_FOCAL_MOC_DELAY_CMD_MS,
						error);
}

/*
 * fu_focal_moc_device_dl_pkt:
 * Build and send one firmware-download packet (ff_down_buf_t layout).
 *
 * Wire format:
 *   [ 0x02 | LEN_HI | LEN_LO | 0x33 | MAGIC | SEQ | data(dlen) | BCC ]
 *   LN = 1 + 2 + dlen  (magic byte + seq byte + data, then BCC counted separately)
 *
 * Note: the original code uses  tx_buf->ln = u16_swap_endian(1 + 2 + dlen)
 * and   tx_len = sizeof(ff_down_buf_t) + dlen,
 * which means LN = magic(1) + seq(1) + data(dlen) — and BCC is appended after.
 * BCC covers pkt[1..tx_len-1].
 */
static gboolean
fu_focal_moc_device_dl_pkt(FuFocalMocDevice *self,
			       FuFocalMocMagic magic,
			       guint8 seq,
			       const guint8 *data,
			       gsize data_len,
			       GError **error)
{
	guint16 ln;
	guint8 bcc;
	g_autoptr(GByteArray) pkt = g_byte_array_new();
	g_autoptr(FuStructFocalMocDlHdr) st_req = NULL;

	/*
	 * LN field from ff_update.c: tx_buf->ln = u16_swap_endian(1 + 2 + dlen)
	 *   magic(1) + seq(1) + data(dlen) + BCC(1) = dlen + 3
	 * The original protocol counts the BCC byte inside LN for download packets.
	 */
	ln = (guint16)(3 + data_len);

	st_req = fu_struct_focal_moc_dl_hdr_new();
	fu_struct_focal_moc_dl_hdr_set_head(st_req, FU_FOCAL_MOC_MSG_MAGIC);
	fu_struct_focal_moc_dl_hdr_set_ln(st_req, ln);
	fu_struct_focal_moc_dl_hdr_set_cmd(st_req, FU_FOCAL_MOC_CMD_FW_DOWNLOAD);
	fu_struct_focal_moc_dl_hdr_set_magic(st_req, magic);
	fu_struct_focal_moc_dl_hdr_set_seq(st_req, seq);
	g_byte_array_append(pkt, st_req->buf->data, st_req->buf->len);
	if (data != NULL && data_len > 0)
		g_byte_array_append(pkt, data, data_len);

	/* BCC: covers pkt[1..] */
	bcc = fu_focal_moc_device_bcc(pkt->data + 1, pkt->len - 1);
	fu_byte_array_append_uint8(pkt, bcc);

	return fu_focal_moc_device_send(self, pkt, error);
}

/* --------------------------------------------------------------------------
 * FuDevice virtual method implementations
 * -------------------------------------------------------------------------- */

/*
 * fu_focal_moc_device_probe:
 *
 * Chain up to parent FIRST (sets VID/PID from USB descriptor),
 * then guard against non-FocalTech devices.
 */
static gboolean
fu_focal_moc_device_probe(FuDevice *device, GError **error)
{
	static const guint16 supported_pids[] = {
		0x9E48,
		0xD979,
		0xA27A,
		0xA959,
		0xA99A,
		0xA57A,
		0xA78A,
		0xA97A,
		0x1579,
		0x077A,
		0x079A,
		0x5158,
		0x6553,
	};
	guint16 pid;

	if (!FU_DEVICE_CLASS(fu_focal_moc_device_parent_class)->probe(device, error))
		return FALSE;

	if (fu_device_get_vid(device) != 0x2808) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not a FocalTech device (VID 0x%04x)",
			    fu_device_get_vid(device));
		return FALSE;
	}

	pid = fu_device_get_pid(device);
	for (gsize i = 0; i < G_N_ELEMENTS(supported_pids); i++) {
		if (pid == supported_pids[i])
			return TRUE;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "unsupported FocalTech PID 0x%04x",
		    pid);
	return FALSE;
}

static gboolean
fu_focal_moc_device_setup(FuDevice *device, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_focal_moc_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_focal_moc_device_ensure_version(self, error)) {
		g_prefix_error_literal(error, "failed to read firmware version: ");
		return FALSE;
	}

	return TRUE;
}

/*
 * fu_focal_moc_device_detach:
 * CMD 0x32(mode=1) — enter bootloader, then wait for device re-enumeration.
 */
static gboolean
fu_focal_moc_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);

	if (!fu_focal_moc_device_set_boot_mode(self,
						   FU_FOCAL_MOC_BOOT_MODE_ENTER_BOOT,
						   error)) {
		g_prefix_error_literal(error, "failed to enter bootloader mode: ");
		return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

/*
 * fu_focal_moc_device_write_firmware:
 *
 * CMD 0x33 — ff_update.c:ff_download_firmware()
 *
 * Three-phase transfer:
 *   Phase 1 (SHO): Send header packet with filename, file size, CRC32.
 *                  Wait for ACK.
 *   Phase 2 (STX): Send 1024-byte data blocks.  Wait for ACK after each.
 *   Phase 3 (EOT): Send remaining (<1024) bytes.  Wait for final ACK.
 *
 * If the firmware size is an exact multiple of 1024 bytes, the last STX
 * block uses magic=EOT instead of STX to signal end-of-data.
 */
static gboolean
fu_focal_moc_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	const guint8 *fw_data;
	gsize fw_size;
	guint total_blocks;
	guint remain;
	guint8 seq = 0;
	guint32 crc32;
	/*
	 * SHO data layout (from ff_update.c):
	 *   data[0..63]   = filename (FF_SC_FW_FILENAME_MAX_LEN = 64)
	 *   data[64..67]  = file size (4 B big-endian)
	 *   data[68..71]  = CRC32    (4 B big-endian)
	 *   data[72..127] = zero padding (56 B)
	 * Total = 128 bytes.  BCC is placed at data[128] → tx_len = header(6) + 128 + 1.
	 * This matches LN = 131 = magic(1)+seq(1)+data(128)+BCC(1).
	 */
	guint8 sho_data[128] = {0}; /* 64 (filename) + 4 (size) + 4 (crc) + 56 (padding) */
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, NULL);

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	fw_data = g_bytes_get_data(fw, &fw_size);
	if (fw_size == 0 || fw_size > FU_FOCAL_MOC_FW_MAX_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware size %zu out of valid range (1..%u)",
			    fw_size,
			    (guint)FU_FOCAL_MOC_FW_MAX_SIZE);
		return FALSE;
	}

	total_blocks = (guint)(fw_size / FU_FOCAL_MOC_DL_BLOCK_SIZE);
	remain = (guint)(fw_size % FU_FOCAL_MOC_DL_BLOCK_SIZE);

	/* the original tool rejects firmwares smaller than 2 full blocks */
	if (total_blocks < 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware too small — not a valid FocalTech image");
		return FALSE;
	}

	/* ── Phase 1: SHO packet ──────────────────────────────────────────── */

	/* CRC32 over the full firmware image (UpgradeTool polynomial 0xedb88320) */
	crc32 = fu_crc32(FU_CRC_KIND_B32_STANDARD, fw_data, fw_size);

	/* filename field: use a fixed placeholder (device ignores actual name) */
	g_strlcpy((gchar *)sho_data, "firmware.bin", FU_FOCAL_MOC_FILENAME_LEN);
	if (!fu_memwrite_uint32_safe(sho_data,
				     sizeof(sho_data),
				     FU_FOCAL_MOC_FILENAME_LEN,
				     (guint32)fw_size,
				     G_BIG_ENDIAN,
				     error))
		return FALSE;
	if (!fu_memwrite_uint32_safe(sho_data,
				     sizeof(sho_data),
				     FU_FOCAL_MOC_FILENAME_LEN + 4,
				     crc32,
				     G_BIG_ENDIAN,
				     error))
		return FALSE;

	if (!fu_focal_moc_device_dl_pkt(self,
					    FU_FOCAL_MOC_MAGIC_SHO,
					    seq++,
					    sho_data,
					    sizeof(sho_data),
					    error)) {
		g_prefix_error_literal(error, "SHO packet send failed: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_DL_MS);
	if (!fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_TIMEOUT, error)) {
		g_prefix_error_literal(error, "SHO ACK failed: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 15); /* short settle, matching UpgradeTool */

	/* ── Phase 2: STX packets (full 1024-byte blocks) ─────────────────── */
	/*
	 * Original ff_update.c: ALL STX blocks use magic=STX and each receives
	 * its own per-packet ACK.  The EOT phase is always separate (when remain>0).
	 * When remain==0, the last block is still sent as STX and receives an ACK,
	 * then the final ACK collection below handles the end-of-download response.
	 */
	for (guint i = 0; i < total_blocks; i++) {
		const guint8 *block = fw_data + (gsize)i * FU_FOCAL_MOC_DL_BLOCK_SIZE;

		if (!fu_focal_moc_device_dl_pkt(self,
						    FU_FOCAL_MOC_MAGIC_STX,
						    seq++,
						    block,
						    FU_FOCAL_MOC_DL_BLOCK_SIZE,
						    error)) {
			g_prefix_error(error, "block %u send failed: ", i);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_DL_MS);

		/* per-block ACK for every STX packet, matching ff_update.c */
		if (!fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_TIMEOUT, error)) {
			g_prefix_error(error, "block %u ACK failed: ", i);
			return FALSE;
		}

		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)total_blocks + (remain ? 1 : 0));
	}

	/* ── Phase 3: EOT packet (remainder, if any) ──────────────────────── */
	if (remain > 0) {
		/*
		 * Original tool sends a full-sized buffer (1024) even for the tail;
		 * the unused bytes are zero from memset.  Mirror that exactly.
		 */
		guint8 tail_buf[FU_FOCAL_MOC_DL_BLOCK_SIZE] = {0};
		if (!fu_memcpy_safe(tail_buf,
				    sizeof(tail_buf),
				    0,
				    fw_data,
				    fw_size,
				    (gsize)total_blocks * FU_FOCAL_MOC_DL_BLOCK_SIZE,
				    remain,
				    error))
			return FALSE;

		if (!fu_focal_moc_device_dl_pkt(self,
						    FU_FOCAL_MOC_MAGIC_EOT,
						    seq++,
						    tail_buf,
						    FU_FOCAL_MOC_DL_BLOCK_SIZE,
						    error)) {
			g_prefix_error_literal(error, "EOT packet send failed: ");
			return FALSE;
		}
	}

	/* ── Final ACK (covers the last block whether STX-promoted-to-EOT or a separate EOT) */
	fu_device_sleep(FU_DEVICE(self), FU_FOCAL_MOC_DELAY_FINAL_MS);
	if (!fu_focal_moc_device_recv_ack(self, FU_FOCAL_MOC_RECV_FINAL_TIMEOUT, error)) {
		g_prefix_error_literal(error, "final ACK failed: ");
		return FALSE;
	}

	fu_progress_step_done(progress);

	/*
	 * Device reboots automatically after accepting the EOT + final ACK.
	 * No explicit reset command is needed (ff_update.c line 321 shows
	 * ff_set_bootloader_mode(ENTER_APP_MODE) is commented out).
	 */
	return TRUE;
}

/*
 * fu_focal_moc_device_attach:
 * CMD 0x32(mode=0) — return to application mode, then wait for re-enumeration.
 *
 * Some firmware images trigger an automatic reboot after the EOT+ACK; others
 * (e.g. test payloads) do not.  Sending ENTER_APP explicitly covers both cases.
 * If the device has already rebooted, the transfer may fail — that is not fatal.
 */
static gboolean
fu_focal_moc_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFocalMocDevice *self = FU_FOCAL_MOC_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	if (!fu_focal_moc_device_set_boot_mode(self,
						   FU_FOCAL_MOC_BOOT_MODE_ENTER_APP,
						   &error_local)) {
		/* Device may have already started rebooting — treat as non-fatal */
		g_info("ENTER_APP command failed (device may have already rebooted): %s",
		       error_local->message);
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_focal_moc_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 19, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 24, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_focal_moc_device_init(FuFocalMocDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000);
	fu_device_add_protocol(FU_DEVICE(self), "com.focal.moc");
	fu_device_set_name(FU_DEVICE(self), "Fingerprint Sensor");
	fu_device_set_summary(FU_DEVICE(self), "Match-On-Chip fingerprint sensor");
	fu_device_set_install_duration(FU_DEVICE(self), 15);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 2 * FU_FOCAL_MOC_DL_BLOCK_SIZE);
	fu_device_set_firmware_size_max(FU_DEVICE(self), FU_FOCAL_MOC_FW_MAX_SIZE);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), FU_FOCAL_MOC_USB_INTERFACE);
}

static void
fu_focal_moc_device_class_init(FuFocalMocDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_focal_moc_device_probe;
	device_class->setup = fu_focal_moc_device_setup;
	device_class->detach = fu_focal_moc_device_detach;
	device_class->write_firmware = fu_focal_moc_device_write_firmware;
	device_class->attach = fu_focal_moc_device_attach;
	device_class->set_progress = fu_focal_moc_device_set_progress;
}

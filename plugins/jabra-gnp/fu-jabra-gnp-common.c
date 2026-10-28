/*
 * Copyright 2023 GN Audio
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-jabra-gnp-child-device.h"
#include "fu-jabra-gnp-common.h"
#include "fu-jabra-gnp-device.h"
#include "fu-jabra-gnp-firmware.h"
#include "fu-jabra-gnp-image.h"
#include "fu-jabra-gnp-struct.h"

/*
 * NOTE: DO NOT ALLOW ANY MORE MAGIC CONSTANTS IN THIS FILE
 * nocheck:magic-inlines=400
 */

static guint64
fu_jabra_gnp_update_crc(guint64 acc, guint64 delta)
{
	static guint64 crcLookupTable[] = {
	    0x00000000, 0xDB710641, 0x6D930AC3, 0xB6E20C82, 0xDB261586, 0x005713C7, 0xB6B51F45,
	    0x6DC41904, 0x6D3D2D4D, 0xB64C2B0C, 0x00AE278E, 0xDBDF21CF, 0xB61B38CB, 0x6D6A3E8A,
	    0xDB883208, 0x00F93449, 0xDA7A5A9A, 0x010B5CDB, 0xB7E95059, 0x6C985618, 0x015C4F1C,
	    0xDA2D495D, 0x6CCF45DF, 0xB7BE439E, 0xB74777D7, 0x6C367196, 0xDAD47D14, 0x01A57B55,
	    0x6C616251, 0xB7106410, 0x01F26892, 0xDA836ED3, 0x6F85B375, 0xB4F4B534, 0x0216B9B6,
	    0xD967BFF7, 0xB4A3A6F3, 0x6FD2A0B2, 0xD930AC30, 0x0241AA71, 0x02B89E38, 0xD9C99879,
	    0x6F2B94FB, 0xB45A92BA, 0xD99E8BBE, 0x02EF8DFF, 0xB40D817D, 0x6F7C873C, 0xB5FFE9EF,
	    0x6E8EEFAE, 0xD86CE32C, 0x031DE56D, 0x6ED9FC69, 0xB5A8FA28, 0x034AF6AA, 0xD83BF0EB,
	    0xD8C2C4A2, 0x03B3C2E3, 0xB551CE61, 0x6E20C820, 0x03E4D124, 0xD895D765, 0x6E77DBE7,
	    0xB506DDA6, 0xDF0B66EA, 0x047A60AB, 0xB2986C29, 0x69E96A68, 0x042D736C, 0xDF5C752D,
	    0x69BE79AF, 0xB2CF7FEE, 0xB2364BA7, 0x69474DE6, 0xDFA54164, 0x04D44725, 0x69105E21,
	    0xB2615860, 0x048354E2, 0xDFF252A3, 0x05713C70, 0xDE003A31, 0x68E236B3, 0xB39330F2,
	    0xDE5729F6, 0x05262FB7, 0xB3C42335, 0x68B52574, 0x684C113D, 0xB33D177C, 0x05DF1BFE,
	    0xDEAE1DBF, 0xB36A04BB, 0x681B02FA, 0xDEF90E78, 0x05880839, 0xB08ED59F, 0x6BFFD3DE,
	    0xDD1DDF5C, 0x066CD91D, 0x6BA8C019, 0xB0D9C658, 0x063BCADA, 0xDD4ACC9B, 0xDDB3F8D2,
	    0x06C2FE93, 0xB020F211, 0x6B51F450, 0x0695ED54, 0xDDE4EB15, 0x6B06E797, 0xB077E1D6,
	    0x6AF48F05, 0xB1858944, 0x076785C6, 0xDC168387, 0xB1D29A83, 0x6AA39CC2, 0xDC419040,
	    0x07309601, 0x07C9A248, 0xDCB8A409, 0x6A5AA88B, 0xB12BAECA, 0xDCEFB7CE, 0x079EB18F,
	    0xB17CBD0D, 0x6A0DBB4C, 0x6567CB95, 0xBE16CDD4, 0x08F4C156, 0xD385C717, 0xBE41DE13,
	    0x6530D852, 0xD3D2D4D0, 0x08A3D291, 0x085AE6D8, 0xD32BE099, 0x65C9EC1B, 0xBEB8EA5A,
	    0xD37CF35E, 0x080DF51F, 0xBEEFF99D, 0x659EFFDC, 0xBF1D910F, 0x646C974E, 0xD28E9BCC,
	    0x09FF9D8D, 0x643B8489, 0xBF4A82C8, 0x09A88E4A, 0xD2D9880B, 0xD220BC42, 0x0951BA03,
	    0xBFB3B681, 0x64C2B0C0, 0x0906A9C4, 0xD277AF85, 0x6495A307, 0xBFE4A546, 0x0AE278E0,
	    0xD1937EA1, 0x67717223, 0xBC007462, 0xD1C46D66, 0x0AB56B27, 0xBC5767A5, 0x672661E4,
	    0x67DF55AD, 0xBCAE53EC, 0x0A4C5F6E, 0xD13D592F, 0xBCF9402B, 0x6788466A, 0xD16A4AE8,
	    0x0A1B4CA9, 0xD098227A, 0x0BE9243B, 0xBD0B28B9, 0x667A2EF8, 0x0BBE37FC, 0xD0CF31BD,
	    0x662D3D3F, 0xBD5C3B7E, 0xBDA50F37, 0x66D40976, 0xD03605F4, 0x0B4703B5, 0x66831AB1,
	    0xBDF21CF0, 0x0B101072, 0xD0611633, 0xBA6CAD7F, 0x611DAB3E, 0xD7FFA7BC, 0x0C8EA1FD,
	    0x614AB8F9, 0xBA3BBEB8, 0x0CD9B23A, 0xD7A8B47B, 0xD7518032, 0x0C208673, 0xBAC28AF1,
	    0x61B38CB0, 0x0C7795B4, 0xD70693F5, 0x61E49F77, 0xBA959936, 0x6016F7E5, 0xBB67F1A4,
	    0x0D85FD26, 0xD6F4FB67, 0xBB30E263, 0x6041E422, 0xD6A3E8A0, 0x0DD2EEE1, 0x0D2BDAA8,
	    0xD65ADCE9, 0x60B8D06B, 0xBBC9D62A, 0xD60DCF2E, 0x0D7CC96F, 0xBB9EC5ED, 0x60EFC3AC,
	    0xD5E91E0A, 0x0E98184B, 0xB87A14C9, 0x630B1288, 0x0ECF0B8C, 0xD5BE0DCD, 0x635C014F,
	    0xB82D070E, 0xB8D43347, 0x63A53506, 0xD5473984, 0x0E363FC5, 0x63F226C1, 0xB8832080,
	    0x0E612C02, 0xD5102A43, 0x0F934490, 0xD4E242D1, 0x62004E53, 0xB9714812, 0xD4B55116,
	    0x0FC45757, 0xB9265BD5, 0x62575D94, 0x62AE69DD, 0xB9DF6F9C, 0x0F3D631E, 0xD44C655F,
	    0xB9887C5B, 0x62F97A1A, 0xD41B7698, 0x0F6A70D9};
	guint64 t = acc >> 24;
	guint32 lookup = (guint32)(t & 0xFF);
	acc = (acc << 8) ^ crcLookupTable[lookup] ^ delta;
	return acc & 0x00000000FFFFFFFF;
}

guint64
fu_jabra_gnp_calculate_crc(GBytes *bytes)
{
	guint64 crc = 0;
	guint8 tmp[] = {0xFF, 0xFF, 0xFF, 0xFF};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_byte_array_append(buf, tmp, sizeof(tmp));
	fu_byte_array_append_bytes(buf, bytes);

	for (gint i = buf->len; i > 0; i -= 2) {
		if (i > 1)
			crc = fu_jabra_gnp_update_crc(crc, buf->data[i - 2] & 0xFF);
		crc = fu_jabra_gnp_update_crc(crc, buf->data[i - 1] & 0xFF);
	}
	return crc;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_ensure_name(FuDevice *device, guint8 address, guint8 seq, GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	g_autofree gchar *name = NULL;

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_GET_NAME);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	name = fu_memstrsafe(rx_data.rxbuf,
			     sizeof(rx_data.rxbuf),
			     0x8,
			     sizeof(rx_data.rxbuf) - 8,
			     error);
	if (name == NULL)
		return FALSE;
	fu_device_set_name(device, name);
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_ensure_battery_level(FuDevice *device, guint8 address, guint8 seq, GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	    .allow_battery_reports = TRUE,
	};
	guint8 battery_level = 0;

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_GET_BATTERY_LEVEL);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (!fu_memread_uint8_safe(rx_data.rxbuf, FU_JABRA_GNP_BUF_SIZE, 8, &battery_level, error))
		return FALSE;
	if (battery_level == 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "battery level was 0");
		return FALSE;
	}
	fu_device_set_battery_level(device, battery_level);
	fu_device_set_battery_threshold(device, 30);
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_read_dfu_pid(FuDevice *device,
			  guint8 address,
			  guint8 seq,
			  guint16 *dfu_pid,
			  GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_GET_DFU_PID);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	*dfu_pid = fu_memread_uint16(rx_data.rxbuf + 7, G_LITTLE_ENDIAN);
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_ensure_version(FuDevice *device, guint8 address, guint8 seq, GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};
	g_autofree gchar *version = NULL;

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_GET_VERSION);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;

	version = fu_memstrsafe(rx_data.rxbuf,
				sizeof(rx_data.rxbuf),
				0x8,
				sizeof(rx_data.rxbuf) - 8,
				error);

	if (version == NULL)
		return FALSE;

	/* some devices append a few extra non number characters to the version; the devices set
	 * FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER so fu_device_set_version() strips that for us */
	fu_device_set_version(device, version);
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_read_fwu_protocol(FuDevice *device,
			       guint8 address,
			       guint8 seq,
			       guint8 *fwu_protocol,
			       GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_GET_FWU_PROTOCOL);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[7] != FU_JABRA_GNP_PROTOCOL_OTA &&
	    rx_data.rxbuf[7] != FU_JABRA_GNP_PROTOCOL_EXTENDED_OTA &&
	    rx_data.rxbuf[7] != FU_JABRA_GNP_PROTOCOL_SPLIT_OTA) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "unrecognized protocol: expected %d, %d or %d, got %d",
			    FU_JABRA_GNP_PROTOCOL_OTA,
			    FU_JABRA_GNP_PROTOCOL_EXTENDED_OTA,
			    FU_JABRA_GNP_PROTOCOL_SPLIT_OTA,
			    rx_data.rxbuf[7]);
		return FALSE;
	}
	*fwu_protocol = rx_data.rxbuf[7];
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_write_partition(FuDevice *device,
			     guint8 address,
			     guint8 seq,
			     guint8 part,
			     GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd_length(st, 0x87);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_WRITE_PARTITION);
	fu_byte_array_append_uint8(st->buf, part);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_start(FuDevice *device, guint8 address, guint8 seq, GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd_length(st, 0x86);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_START);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_flash_erase_done(FuDevice *device, guint8 address, GError **error)
{
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_EXTRA_LONG_RECEIVE_TIMEOUT,
	};
	g_autoptr(FuStructJabraGnpPacket) st_pkt = NULL;

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_cb
				      : fu_jabra_gnp_device_rx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;

	st_pkt = fu_struct_jabra_gnp_packet_parse(rx_data.rxbuf, sizeof(rx_data.rxbuf), 0x0, error);
	if (st_pkt == NULL)
		return FALSE;
	if (fu_struct_jabra_gnp_packet_get_cmd(st_pkt) !=
	    FU_JABRA_GNP_PACKET_CMD_FLASH_ERASE_DONE) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "cmd was %s, but expected FlashEraseDone",
		    fu_jabra_gnp_packet_cmd_to_string(fu_struct_jabra_gnp_packet_get_cmd(st_pkt)));
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_write_crc(FuDevice *device,
		       guint8 address,
		       guint8 seq,
		       guint32 crc,
		       guint total_chunks,
		       guint preload_count,
		       GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd_length(st, 0x8E);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_WRITE_CRC);

	fu_byte_array_append_uint32(st->buf, crc, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(st->buf, total_chunks, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(st->buf, preload_count, G_LITTLE_ENDIAN);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_write_extended_crc(FuDevice *device,
				guint8 address,
				guint8 seq,
				guint32 crc,
				guint total_chunks,
				guint preload_count,
				GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd_length(st, 0x92);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_WRITE_CRC);
	fu_byte_array_append_uint32(st->buf, crc, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(st->buf, 0x00, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(st->buf, preload_count, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(st->buf, total_chunks, G_LITTLE_ENDIAN);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
static gboolean
fu_jabra_gnp_write_chunk(FuDevice *device,
			 guint8 address,
			 guint32 chunk_number,
			 const guint8 *buf,
			 gsize bufsz,
			 GError **error)
{
	guint8 write_length = (guint8)(bufsz + 10);
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, 0x00);
	fu_struct_jabra_gnp_packet_set_cmd_length(st, write_length);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_WRITE_CHUNK);
	fu_byte_array_append_uint16(st->buf, chunk_number, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(st->buf, bufsz, G_LITTLE_ENDIAN);
	g_byte_array_append(st->buf, buf, bufsz);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	return fu_device_retry_full(device,
				    address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
					? fu_jabra_gnp_child_device_tx_cb
					: fu_jabra_gnp_device_tx_cb,
				    FU_JABRA_GNP_MAX_RETRIES,
				    FU_JABRA_GNP_RETRY_DELAY,
				    &tx_data,
				    error);
}

typedef struct {
	guint8 data[FU_JABRA_GNP_BUF_SIZE];
	gsize length;
	GError *error;
} FuJabraGnpReport;

typedef struct {
	FuUsbDevice *usb_device;
	GAsyncQueue *queue;
	guint8 endpoint;
	gint stop;
} FuJabraGnpReceiver;

static void
fu_jabra_gnp_report_free(FuJabraGnpReport *report)
{
	g_clear_error(&report->error);
	g_free(report);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuJabraGnpReport, fu_jabra_gnp_report_free)

typedef struct {
	FuJabraGnpReceiver *receiver;
	guint8 address;
	FuJabraGnpPacketCmd expected_cmd;
	guint32 chunk;
	GError *error;
} FuJabraGnpReceiverReadData;

static gpointer
fu_jabra_gnp_receiver_thread(gpointer user_data)
{
	FuJabraGnpReceiver *receiver = user_data;

	while (!g_atomic_int_get(&receiver->stop)) {
		g_autoptr(FuJabraGnpReport) report = g_new0(FuJabraGnpReport, 1);
		if (!fu_usb_device_interrupt_transfer(receiver->usb_device,
						      receiver->endpoint,
						      report->data,
						      sizeof(report->data),
						      &report->length,
						      FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
						      &report->error)) {
			if (g_error_matches(report->error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT))
				continue;

			g_async_queue_push(receiver->queue, g_steal_pointer(&report));
			break;
		}
		if (g_async_queue_length(receiver->queue) >=
		    FU_JABRA_GNP_RECEIVER_MAX_QUEUED_REPORTS) {
			g_debug("OTA receive queue full, dropping report of size %" G_GSIZE_FORMAT,
				report->length);
			continue;
		}
		g_async_queue_push(receiver->queue, g_steal_pointer(&report));
	}
	return NULL;
}

/* nocheck:name */
static gboolean
fu_jabra_gnp_receiver_read_report_cb(FuDevice *device G_GNUC_UNUSED,
				     gpointer user_data,
				     GError **error)
{
	FuJabraGnpReceiverReadData *data = user_data;
	g_autoptr(FuJabraGnpReport) report = NULL;
	g_autoptr(FuStructJabraGnpPacket) st_pkt = NULL;

	report = g_async_queue_timeout_pop(data->receiver->queue, G_TIME_SPAN_SECOND);
	if (report == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "timed out reading OTA response");
		return FALSE;
	}
	if (report->error != NULL) {
		data->error = g_steal_pointer(&report->error);
		return TRUE;
	}
	if (report->length < 7 || report->data[0] != FU_STRUCT_JABRA_GNP_PACKET_DEFAULT_IFACE) {
		g_debug("ignoring non-GNP report of size %" G_GSIZE_FORMAT, report->length);
	} else {
		st_pkt = fu_struct_jabra_gnp_packet_parse(report->data,
							  report->length,
							  0x0,
							  &data->error);
		if (st_pkt == NULL)
			return TRUE;
		if (fu_struct_jabra_gnp_packet_get_src(st_pkt) == data->address &&
		    (report->data[4] >> 6) == 0) {
			FuJabraGnpPacketCmd received_cmd =
			    fu_struct_jabra_gnp_packet_get_cmd(st_pkt);

			if (received_cmd == FU_JABRA_GNP_PACKET_CMD_WRITE_CHUNKS &&
			    data->expected_cmd == FU_JABRA_GNP_PACKET_CMD_WRITE_CHUNKS) {
				gboolean extended = (report->data[4] & 0x3F) >= 10;
				guint32 received_chunk;

				if (report->length < (extended ? 11 : 9)) {
					g_set_error_literal(&data->error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_INVALID_DATA,
							    "truncated OTA preload event");
					return TRUE;
				}
				received_chunk =
				    extended ? fu_memread_uint32(report->data + 7, G_LITTLE_ENDIAN)
					     : fu_memread_uint16(report->data + 7, G_LITTLE_ENDIAN);
				g_debug("RX preload=%u, expected=%u", received_chunk, data->chunk);
				if (received_chunk ==
				    (extended ? data->chunk : (data->chunk & 0xFFFF)))
					return TRUE;
			} else if (received_cmd == FU_JABRA_GNP_PACKET_CMD_READ_VERIFY_STATUS &&
				   data->expected_cmd ==
				       FU_JABRA_GNP_PACKET_CMD_READ_VERIFY_STATUS) {
				return TRUE;
			}
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "received unrelated OTA response");
	return FALSE;
}

/* nocheck:name */
static gboolean
fu_jabra_gnp_receiver_wait_event(FuDevice *device,
				 FuJabraGnpReceiver *receiver,
				 guint8 address,
				 FuJabraGnpPacketCmd expected_cmd,
				 guint32 chunk,
				 GError **error)
{
	FuJabraGnpReceiverReadData read_data = {
	    .receiver = receiver,
	    .address = address,
	    .expected_cmd = expected_cmd,
	    .chunk = chunk,
	};
	if (!fu_device_retry_full(device,
				  fu_jabra_gnp_receiver_read_report_cb,
				  30,
				  0,
				  &read_data,
				  error)) {
		g_prefix_error(error,
			       "waiting for %s at chunk %u: ",
			       expected_cmd == FU_JABRA_GNP_PACKET_CMD_WRITE_CHUNKS
				   ? "preload"
				   : "verify status",
			       chunk);
		return FALSE;
	}
	if (read_data.error != NULL) {
		g_propagate_error(error, g_steal_pointer(&read_data.error));
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name */
static gboolean
fu_jabra_gnp_receiver_wait_preload_event(FuDevice *device,
					 FuJabraGnpReceiver *receiver,
					 guint8 address,
					 guint32 chunk,
					 GError **error)
{
	return fu_jabra_gnp_receiver_wait_event(device,
						receiver,
						address,
						FU_JABRA_GNP_PACKET_CMD_WRITE_CHUNKS,
						chunk,
						error);
}

/* nocheck:name */
static gboolean
fu_jabra_gnp_receiver_wait_verify_status_event(FuDevice *device,
					       FuJabraGnpReceiver *receiver,
					       guint8 address,
					       guint32 chunk,
					       GError **error)
{
	return fu_jabra_gnp_receiver_wait_event(device,
						receiver,
						address,
						FU_JABRA_GNP_PACKET_CMD_READ_VERIFY_STATUS,
						chunk,
						error);
}

/* nocheck:name */
static gboolean
fu_jabra_gnp_receiver_write(FuDevice *device,
			    FuJabraGnpReceiver *receiver,
			    guint8 address,
			    FuChunkArray *chunks,
			    FuProgress *progress,
			    GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_jabra_gnp_write_chunk(device,
					      address,
					      i,
					      fu_chunk_get_data(chk),
					      fu_chunk_get_data_sz(chk),
					      error))
			return FALSE;
		if (i % FU_JABRA_GNP_PRELOAD_COUNT == 0 &&
		    !fu_jabra_gnp_receiver_wait_preload_event(device, receiver, address, i, error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

/* nocheck:name */
gboolean
fu_jabra_gnp_write_chunks(FuDevice *device,
			  guint8 address,
			  FuChunkArray *chunks,
			  FuProgress *progress,
			  GError **error)
{
	FuDevice *target = device;
	FuJabraGnpReceiver receiver = {0};
	GThread *thread;
	gboolean ret = FALSE;
	g_autoptr(GAsyncQueue) queue =
	    g_async_queue_new_full((GDestroyNotify)fu_jabra_gnp_report_free);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write-chunks");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "read-verify-status");

	if (address == FU_JABRA_GNP_ADDRESS_OTA_CHILD) {
		target = fu_device_get_proxy(device, error);
		if (target == NULL)
			return FALSE;
	}
	receiver.usb_device = FU_USB_DEVICE(target);
	receiver.endpoint = fu_jabra_gnp_device_get_epin(FU_JABRA_GNP_DEVICE(target));
	receiver.queue = queue;
	thread = g_thread_new("jabra-gnp-rx", fu_jabra_gnp_receiver_thread, &receiver);
	ret = fu_jabra_gnp_receiver_write(device,
					  &receiver,
					  address,
					  chunks,
					  fu_progress_get_child(progress),
					  error);
	if (ret) {
		fu_progress_step_done(progress);
		ret = fu_jabra_gnp_receiver_wait_verify_status_event(device,
								     &receiver,
								     address,
								     fu_chunk_array_length(chunks),
								     error);
		if (ret)
			fu_progress_step_done(progress);
	}
	g_atomic_int_set(&receiver.stop, 1);
	g_thread_join(thread);
	return ret;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_write_version(FuDevice *device,
			   guint8 address,
			   guint8 seq,
			   FuJabraGnpVersionData *version_data,
			   GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd_length(st, 0x89);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_WRITE_VERSION);
	fu_byte_array_append_uint8(st->buf, version_data->major);
	fu_byte_array_append_uint8(st->buf, version_data->minor);
	fu_byte_array_append_uint8(st->buf, version_data->micro);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name -- this should probably be implemented using an interface */
gboolean
fu_jabra_gnp_write_dfu_from_squif(FuDevice *device, guint8 address, guint8 seq, GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd_length(st, 0x86);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_WRITE_DFU_FROM_SQUIF);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "internal error: expected 0xFF, got 0x%02x 0x%02x",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name */
gboolean
fu_jabra_gnp_trigger_reboot(FuDevice *self, guint8 address, guint8 seq, GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* verify that config-mode exit is permitted */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_GET_CONFIG_MODE_PERMISSION);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);
	if (!fu_device_retry_full(self,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(self,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[7] != 0x01) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device denied config-mode exit, permission was 0x%02x",
			    rx_data.rxbuf[7]);
		return FALSE;
	}

	/* leaving config mode activates the staged image and restarts the device */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, (guint8)(seq + 1));
	fu_struct_jabra_gnp_packet_set_cmd_length(st, 0x86);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_END_CONFIG_MODE);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);
	if (!fu_device_retry_full(self,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return FALSE;
	if (!fu_device_retry_full(self,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return FALSE;
	if (rx_data.rxbuf[5] != 0xFF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "device rejected end-config-mode command 0x0D 0x12: "
			    "got 0x%02x 0x%02x, expected 0xFF",
			    rx_data.rxbuf[5],
			    rx_data.rxbuf[6]);
		return FALSE;
	}
	return TRUE;
}

/* nocheck:name */
gchar *
fu_jabra_gnp_get_tunes_pack_version(FuDevice *device, guint8 address, guint8 seq, GError **error)
{
	g_autoptr(FuStructJabraGnpPacket) st = fu_struct_jabra_gnp_packet_new();
	FuJabraGnpTxData tx_data = {
	    .buf = st->buf,
	    .timeout = FU_JABRA_GNP_STANDARD_SEND_TIMEOUT,
	};
	FuJabraGnpRxData rx_data = {
	    .rxbuf = {0x00},
	    .timeout = FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT,
	};

	/* set up request */
	fu_struct_jabra_gnp_packet_set_dst(st, address);
	fu_struct_jabra_gnp_packet_set_sequence_number(st, seq);
	fu_struct_jabra_gnp_packet_set_cmd(st, FU_JABRA_GNP_PACKET_CMD_GET_TUNES_PACK_VERSION);
	fu_byte_array_set_size(st->buf, FU_JABRA_GNP_BUF_SIZE, 0x0);

	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_tx_cb
				      : fu_jabra_gnp_device_tx_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &tx_data,
				  error))
		return NULL;
	if (!fu_device_retry_full(device,
				  address == FU_JABRA_GNP_ADDRESS_OTA_CHILD
				      ? fu_jabra_gnp_child_device_rx_with_sequence_cb
				      : fu_jabra_gnp_device_rx_with_sequence_cb,
				  FU_JABRA_GNP_MAX_RETRIES,
				  FU_JABRA_GNP_RETRY_DELAY,
				  &rx_data,
				  error))
		return NULL;

	return fu_memstrsafe(rx_data.rxbuf, sizeof(rx_data.rxbuf), 0x8, 5, error);
}

/* nocheck:name */
GPtrArray *
fu_jabra_gnp_get_split_images(FuDevice *device,
			      FuFirmware *firmware,
			      const gchar *tunes_pack_version,
			      gboolean *another_write_required,
			      GError **error)
{
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GPtrArray) language_images = g_ptr_array_new();
	g_autoptr(GPtrArray) firmware_images = g_ptr_array_new();
	g_autoptr(GPtrArray) selected_images = g_ptr_array_new();
	const gchar *current_version = fu_device_get_version(device);
	const gchar *target_version = fu_firmware_get_version(firmware);
	gboolean language_needs_update = FALSE;
	gboolean firmware_needs_update = FALSE;

	*another_write_required = FALSE;

	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		const gchar *content = fu_firmware_get_id(img);

		if (g_strcmp0(content, "tunepack") == 0) {
			const gchar *target_tunes_version = fu_firmware_get_version(img);

			g_ptr_array_add(language_images, img);
			if (target_tunes_version == NULL || tunes_pack_version == NULL) {
				language_needs_update = TRUE;
			} else if (fu_version_compare(tunes_pack_version,
						      target_tunes_version,
						      FWUPD_VERSION_FORMAT_TRIPLET) != 0) {
				language_needs_update = TRUE;
			}
		} else {
			g_ptr_array_add(firmware_images, img);
		}
	}

	if (firmware_images->len > 0) {
		if (current_version == NULL || target_version == NULL) {
			firmware_needs_update = TRUE;
		} else {
			firmware_needs_update =
			    fu_version_compare(current_version,
					       target_version,
					       FWUPD_VERSION_FORMAT_TRIPLET) != 0;
		}
	}

	if (language_needs_update && firmware_needs_update) {
		for (guint i = 0; i < language_images->len; i++)
			g_ptr_array_add(selected_images, g_ptr_array_index(language_images, i));
		*another_write_required = TRUE;
		g_debug("protocol %d: firmware also needs update, another write required",
			FU_JABRA_GNP_PROTOCOL_SPLIT_OTA);
	} else if (language_needs_update) {
		for (guint i = 0; i < language_images->len; i++)
			g_ptr_array_add(selected_images, g_ptr_array_index(language_images, i));
		g_debug("protocol %d: language pack needs update, another write may be required",
			FU_JABRA_GNP_PROTOCOL_SPLIT_OTA);
	} else if (firmware_needs_update) {
		for (guint i = 0; i < firmware_images->len; i++)
			g_ptr_array_add(selected_images, g_ptr_array_index(firmware_images, i));
		g_debug("protocol %d: firmware needs update, another write may be required",
			FU_JABRA_GNP_PROTOCOL_SPLIT_OTA);
	} else {
		for (guint i = 0; i < language_images->len; i++)
			g_ptr_array_add(selected_images, g_ptr_array_index(language_images, i));
		for (guint i = 0; i < firmware_images->len; i++)
			g_ptr_array_add(selected_images, g_ptr_array_index(firmware_images, i));
		g_debug("protocol %d: no version change detected, updating all images",
			FU_JABRA_GNP_PROTOCOL_SPLIT_OTA);
	}
	if (selected_images->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware contains no images for the device");
		return NULL;
	}
	return g_steal_pointer(&selected_images);
}

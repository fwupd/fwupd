/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-hid.h"
#include "fu-elan-ts-hidraw.h"

/* Helper structure to pass results back from the retry callback */
typedef struct {
	guint8 hello_packet;
	guint16 bc_version;
} FuElanTsHelloHelper;

/* Helper structure for info page retrieval via retry mechanism */
typedef struct {
	guint8 info_page_buf[ELAN_TS_MEMORY_PAGE_SIZE];
} FuElanTsInfoPageHelper;

/**
 * fu_elan_ts_hid_write_vendor_command:
 * @device: a #FuHidrawDevice
 * @p_vendor_cmd_buf: the raw vendor command payload (without Report ID)
 * @vendor_cmd_len: length of @p_vendor_cmd_buf
 * @error: (nullable): a #GError, or %NULL
 *
 * Wraps the vendor command payload with the HID Output Report ID (0x03)
 * and sends it as a fixed-length report (33 bytes) with retries.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
fu_elan_ts_hid_write_vendor_command(FuHidrawDevice *device,
				    const guint8 *p_vendor_cmd_buf,
				    gsize vendor_cmd_len,
				    GError **error)
{
	guint8 out_report_buf[ELAN_TS_OUTPUT_REPORT_SIZE] = {0x00};

	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_vendor_cmd_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* [byte 0] set Report ID */
	out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID;

	/*
	 * copy payload safely with offset 1.
	 * fu_memcpy_safe automatically validates if (1 + vendor_cmd_len)
	 * exceeds ELAN_TS_OUTPUT_REPORT_SIZE.
	 */
	if (!fu_memcpy_safe(out_report_buf,
			    sizeof(out_report_buf),
			    1, /* dst_offset */
			    p_vendor_cmd_buf,
			    vendor_cmd_len, /* src_sz */
			    0,		    /* src_offset */
			    vendor_cmd_len, /* n */
			    error)) {
		g_prefix_error_literal(error, "failed to copy vendor command: ");
		return FALSE;
	}

	/* internal retries are managed by write_with_retry */
	return fu_elan_ts_hidraw_write_with_retry(device,
						  out_report_buf,
						  sizeof(out_report_buf),
						  error);
}

/**
 * fu_elan_ts_hid_write_command:
 * @device: a #FuHidrawDevice
 * @p_cmd_buf: the raw command payload (e.g., 4 or 6 bytes)
 * @cmd_len: length of @p_cmd_buf
 * @error: (nullable): a #GError, or %NULL
 *
 * Wraps the TP command with the appropriate HID Report ID (based on PID)
 * and a 2-byte header, then sends it as a fixed-length HID Output Report with retries.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
fu_elan_ts_hid_write_command(FuHidrawDevice *device,
			     const guint8 *p_cmd_buf,
			     gsize cmd_len,
			     GError **error)
{
	guint8 out_report_buf[ELAN_TS_OUTPUT_REPORT_SIZE] = {0x00};
	guint16 hid_pid = fu_device_get_pid(FU_DEVICE(device));

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_cmd_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* [byte 0] select Report ID based on hardware PID */
	if (hid_pid == ELAN_TS_HID_PID_BRIDGE || hid_pid == ELAN_TS_HID_PID_BRIDGE_B)
		out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID_BRIDGE;
	else
		out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID;

	/* [byte 1] bridge command byte (Fixed 0x00) */
	out_report_buf[1] = 0x00;

	/* [byte 2] length of the raw command payload */
	out_report_buf[2] = (guint8)cmd_len;

	/*
	 * [byte 3..n] copy the raw command payload into the report safely.
	 * fu_memcpy_safe handles the boundary check (3 + cmd_len) internally.
	 */
	if (!fu_memcpy_safe(out_report_buf,
			    sizeof(out_report_buf),
			    3, /* dst_offset */
			    p_cmd_buf,
			    cmd_len, /* src_sz */
			    0,	     /* src_offset */
			    cmd_len, /* n */
			    error)) {
		g_prefix_error_literal(error, "failed to copy command payload: ");
		return FALSE;
	}

	/* internal retries are managed by write_with_retry */
	return fu_elan_ts_hidraw_write_with_retry(device,
						  out_report_buf,
						  sizeof(out_report_buf),
						  error);
}

/**
 * fu_elan_ts_hid_read_data:
 * @device: a #FuHidrawDevice
 * @p_data_buf: (out): destination buffer to store the read data
 * @data_len: length of data to copy into @p_data_buf
 * @filter: if %TRUE, strips the 2-byte hid header (report_id + data_length)
 * @error: (nullable): a #GError, or %NULL
 *
 * reads an input report from elan ts with retries and validates the report id.
 *
 * Returns: %TRUE for success, %FALSE for failure or invalid data pattern.
 */
gboolean
fu_elan_ts_hid_read_data(FuHidrawDevice *device,
			 guint8 *p_data_buf,
			 gsize data_len,
			 gboolean filter,
			 GError **error)
{
	guint8 in_report_buf[ELAN_TS_INPUT_REPORT_SIZE] = {0x00};
	guint16 hid_pid = 0;
	guint8 elan_ts_input_report_id = 0;
	guint attempt_index = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_data_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* select expected command response report id based on pid */
	hid_pid = fu_device_get_pid(FU_DEVICE(device));
	if ((hid_pid == ELAN_TS_HID_PID_BRIDGE) || (hid_pid == ELAN_TS_HID_PID_BRIDGE_B))
		elan_ts_input_report_id = ELAN_TS_HID_INPUT_REPORT_ID_BRIDGE;
	else
		elan_ts_input_report_id = ELAN_TS_HID_INPUT_REPORT_ID;

	/* loop to read data, discarding asynchronous touch or pen reports */
	for (attempt_index = 0; attempt_index < ELAN_TS_IO_MAX_RETRIES; attempt_index++) {
		/* perform raw read with retries using fixed input report size */
		if (!fu_elan_ts_hidraw_read_with_retry(device,
						       in_report_buf,
						       sizeof(in_report_buf),
						       error)) {
			return FALSE;
		}

		/* standard expected command response */
		if (in_report_buf[0] == elan_ts_input_report_id)
			break;

		/* ignore interleaved runtime touch/pen input reports and retry */
		if (in_report_buf[0] == ELAN_TS_HID_FINGER_REPORT_ID ||
		    in_report_buf[0] == ELAN_TS_HID_PEN_REPORT_ID ||
		    in_report_buf[0] == ELAN_TS_HID_PEN_DEBUG_REPORT_ID) {
			g_debug("ignoring runtime touch/pen report 0x%02x during firmware update",
				in_report_buf[0]);
			continue;
		}

		/* unexpected report ID received */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid report id: 0x%02x",
			    in_report_buf[0]);
		return FALSE;
	}

	/* failed to receive the expected command response within retry limit */
	if (attempt_index == ELAN_TS_IO_MAX_RETRIES) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "timed out waiting for command response report");
		return FALSE;
	}

	/* copy data to output buffer based on filter flag */
	if (filter) {
		g_autoptr(FuStructElanTsInputReport) st_report = NULL;
		const guint8 *payload_data = NULL;
		gsize payload_size = 0;

		/* use rustgen parser wrapper to interpret full 65-byte hid report safely */
		st_report = fu_struct_elan_ts_input_report_parse(in_report_buf,
								 sizeof(in_report_buf),
								 0,
								 error);
		if (st_report == NULL) {
			g_prefix_error_literal(error,
					       "failed to parse hid input report structure: ");
			return FALSE;
		}

		/* rustgen automatically strips header and returns pointer with payload_size = 63 */
		payload_data = fu_struct_elan_ts_input_report_get_payload(st_report, &payload_size);

		/* typesafe memory copy utilizing rustgen-guaranteed buffer boundary */
		if (!fu_memcpy_safe(p_data_buf,
				    data_len,
				    0, /* dst_offset */
				    payload_data,
				    payload_size, /* src_sz (fixed 63) */
				    0,		  /* src_offset */
				    data_len,	  /* n */
				    error)) {
			g_prefix_error_literal(error, "failed to copy filtered data: ");
			return FALSE;
		}
	} else {
		/* keep everything including the header */
		if (!fu_memcpy_safe(p_data_buf,
				    data_len,
				    0, /* dst_offset */
				    in_report_buf,
				    sizeof(in_report_buf),
				    0,	      /* src_offset */
				    data_len, /* n */
				    error)) {
			g_prefix_error_literal(error, "failed to copy raw data: ");
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_hello_packet_bc_version:
 * @device: a #FuHidrawDevice
 * @p_hello_packet: (out): stores the hello packet value (see #FuElanTsHelloPacket)
 * @p_bc_version: (out): stores the boot code version if in recovery mode
 * @error: (nullable): a #GError, or %NULL
 *
 * Requests the hello packet and boot code version from the device.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
fu_elan_ts_hid_read_hello_packet_bc_version(FuHidrawDevice *device,
					    guint8 *p_hello_packet,
					    guint16 *p_bc_version,
					    GError **error)
{
	g_autoptr(FuStructElanTsVendorCmd) st_cmd = fu_struct_elan_ts_vendor_cmd_new();
	g_autoptr(FuStructElanTsHelloPktAndBcVerRsp) st_rsp = NULL;
	guint8 cmd_data[4] = {0};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_hello_packet != NULL, FALSE);
	g_return_val_if_fail(p_bc_version != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* prepare vendor command using the new enum */
	fu_struct_elan_ts_vendor_cmd_set_cmd(st_cmd,
					     FU_ELAN_TS_VENDOR_CMD_READ_HELLO_PKT_AND_BC_VER);

	/* send the request via the retry-enabled vendor command wrapper using the byte array data
	 */
	if (!fu_elan_ts_hid_write_vendor_command(device,
						 st_cmd->buf->data,
						 st_cmd->buf->len,
						 error)) {
		g_prefix_error_literal(error, "failed to send hello command: ");
		return FALSE;
	}

	/* read the response using the retry-enabled read wrapper with filter=TRUE */
	if (!fu_elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		g_prefix_error_literal(error, "failed to read hello response: ");
		return FALSE;
	}

	/* use the auto-generated _parse API to parse the byte array directly */
	st_rsp =
	    fu_struct_elan_ts_hello_pkt_and_bc_ver_rsp_parse(cmd_data, sizeof(cmd_data), 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse hello response structure: ");
		return FALSE;
	}

	/* extract values using type-safe getters */
	*p_hello_packet = fu_struct_elan_ts_hello_pkt_and_bc_ver_rsp_get_hello_packet(st_rsp);
	*p_bc_version = fu_struct_elan_ts_hello_pkt_and_bc_ver_rsp_get_bc_version(st_rsp);

	g_debug("hello packet: 0x%02x", *p_hello_packet);
	g_debug("boot code version: 0x%04x", *p_bc_version);

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_hello_retry_cb:
 * @device: a #FuDevice
 * @user_data: a #FuElanTsHelloHelper pointer to store results
 * @error: (nullable): a #GError
 *
 * The callback function used by fu_device_retry_full to perform a single
 * hello packet transaction.
 *
 * Returns: %TRUE for success, %FALSE for failure (triggers retry)
 */
static gboolean
fu_elan_ts_hid_read_hello_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsHelloHelper *helper = (FuElanTsHelloHelper *)user_data;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(helper != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/*
	 * Call the underlying communication function which already has its
	 * own internal I/O retries for maximum reliability.
	 */
	return fu_elan_ts_hid_read_hello_packet_bc_version(FU_HIDRAW_DEVICE(device),
							   &helper->hello_packet,
							   &helper->bc_version,
							   error);
}

/**
 * fu_elan_ts_hid_read_hello_packet_bc_version_with_retry:
 * @device: a #FuHidrawDevice
 * @p_hello_packet: (out): stores the hello packet value (e.g., 0x20 or 0x56)
 * @p_bc_version: (out): stores the 16-bit boot code version
 * @error: (nullable): a #GError, or %NULL
 *
 * Requests the hello packet and BC version with an automatic retry mechanism.
 * This function uses ELAN_TS_IO_MAX_RETRIES internally to manage multiple
 * attempts, ensuring the device has sufficient time to respond.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed
 */
gboolean
fu_elan_ts_hid_read_hello_packet_bc_version_with_retry(FuHidrawDevice *device,
						       guint8 *p_hello_packet,
						       guint16 *p_bc_version,
						       GError **error)
{
	FuElanTsHelloHelper helper = {0};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_hello_packet != NULL, FALSE);
	g_return_val_if_fail(p_bc_version != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/*
	 * Invoke the retry framework using predefined constants for
	 * maximum retry attempts and interval.
	 */
	if (!fu_device_retry_full(FU_DEVICE(device),
				  fu_elan_ts_hid_read_hello_retry_cb,
				  ELAN_TS_IO_MAX_RETRIES,
				  0,
				  &helper,
				  error))
		return FALSE;

	/* write results back to caller's pointers upon success */
	*p_hello_packet = helper.hello_packet;
	*p_bc_version = helper.bc_version;

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_boot_code_version:
 * @device: a #FuHidrawDevice
 * @p_bc_version: (out): stores the parsed 16-bit boot code version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the boot code version from the device while in normal mode.
 * It sends the type-safe Boot Code Version command and parses the 4-byte response.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
fu_elan_ts_hid_read_boot_code_version(FuHidrawDevice *device, guint16 *p_bc_version, GError **error)
{
	g_autoptr(FuStructElanTsBcVersionCmd) st_cmd = fu_struct_elan_ts_bc_version_cmd_new();
	g_autoptr(FuStructElanTsBcVersionRsp) st_rsp = NULL;
	guint8 cmd_data[4] = {0};
	const guint8 *payload = NULL;
	gsize buf_size = 0;
	guint32 payload_value = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_bc_version != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* send the type-safe boot code version command using the internal GByteArray */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send BC version command: ");
		return FALSE;
	}

	/* read the response */
	if (!fu_elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		g_prefix_error_literal(error, "failed to read BC version data: ");
		return FALSE;
	}

	/*
	 * use the auto-generated _parse API to parse the byte array directly.
	 * Note: This automatically validates if rsp_type == FU_ELAN_TS_RSP_TYPE_READ_CMD_RSP.
	 */
	st_rsp = fu_struct_elan_ts_bc_version_rsp_parse(cmd_data, sizeof(cmd_data), 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse BC version response structure: ");
		return FALSE;
	}

	/* payload points to cmd_data[1], cmd_data[2], cmd_data[3] */
	payload = fu_struct_elan_ts_bc_version_rsp_get_payload(st_rsp, &buf_size);

	/* pattern check: high nibble of cmd_data[1] must be 0x1 */
	if ((payload[0] >> 4) != 0x01) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid BC version pattern: %02x %02x %02x %02x",
			    cmd_data[0],
			    cmd_data[1],
			    cmd_data[2],
			    cmd_data[3]);
		return FALSE;
	}

	/* read the 3-byte payload stream as a single 24-bit big-endian integer */
	payload_value = fu_memread_uint24(payload, G_BIG_ENDIAN);

	/*
	 * extract the 16-bit BC version directly from the 24-bit integer value by
	 * discarding the pattern high nibble and the trailing blank low nibble.
	 */
	*p_bc_version = (guint16)((payload_value >> 4) & 0xFFFF);
	g_debug("boot code version: 0x%04x", *p_bc_version);

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_fw_id:
 * @device: a #FuHidrawDevice
 * @p_fw_id: (out): stores the parsed 16-bit firmware ID
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Firmware ID from the device.
 * It sends the type-safe FW ID command and parses the 4-byte response.
 */
gboolean
fu_elan_ts_hid_read_fw_id(FuHidrawDevice *device, guint16 *p_fw_id, GError **error)
{
	g_autoptr(FuStructElanTsFwIdCmd) st_cmd = fu_struct_elan_ts_fw_id_cmd_new();
	g_autoptr(FuStructElanTsFwIdRsp) st_rsp = NULL;
	guint8 cmd_data[4] = {0};
	const guint8 *payload = NULL;
	gsize buf_size = 0;
	guint32 payload_value = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_fw_id != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* send FW ID Command using the auto-initialized type-safe struct buffer */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send FW ID command: ");
		return FALSE;
	}

	/* read the response */
	if (!fu_elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		g_prefix_error_literal(error, "failed to read FW ID response: ");
		return FALSE;
	}

	/*
	 * use the auto-generated _parse API to parse the byte array directly.
	 * Note: This automatically validates if rsp_type == FU_ELAN_TS_RSP_TYPE_READ_CMD_RSP
	 * (0x52).
	 */
	st_rsp = fu_struct_elan_ts_fw_id_rsp_parse(cmd_data, sizeof(cmd_data), 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse FW ID response structure: ");
		return FALSE;
	}

	/* payload points to cmd_data[1], cmd_data[2], cmd_data[3] */
	payload = fu_struct_elan_ts_fw_id_rsp_get_payload(st_rsp, &buf_size);

	/* validate pattern: High Nibble of Byte[1] (payload[0]) must be 0xF */
	if ((payload[0] >> 4) != 0x0F) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FW ID pattern: %02x %02x %02x %02x",
			    cmd_data[0],
			    cmd_data[1],
			    cmd_data[2],
			    cmd_data[3]);
		return FALSE;
	}

	/* read the 3-byte payload stream as a single 24-bit big-endian integer */
	payload_value = fu_memread_uint24(payload, G_BIG_ENDIAN);

	/*
	 * extract the 16-bit FW ID directly from the 24-bit integer value by
	 * discarding the pattern high nibble and the trailing blank low nibble.
	 */
	*p_fw_id = (guint16)((payload_value >> 4) & 0xFFFF);
	g_debug("firmware id: 0x%04x", *p_fw_id);

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_fw_version:
 * @device: a #FuHidrawDevice
 * @p_fw_version: (out): stores the parsed 16-bit firmware version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Firmware Version from the device.
 * It sends the type-safe FW version command and parses the 4-byte response.
 */
gboolean
fu_elan_ts_hid_read_fw_version(FuHidrawDevice *device, guint16 *p_fw_version, GError **error)
{
	g_autoptr(FuStructElanTsFwVersionCmd) st_cmd = fu_struct_elan_ts_fw_version_cmd_new();
	g_autoptr(FuStructElanTsFwVersionRsp) st_rsp = NULL;
	guint8 cmd_data[4] = {0};
	const guint8 *payload = NULL;
	gsize buf_size = 0;
	guint32 payload_value = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_fw_version != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* send FW Version Command using the auto-initialized type-safe struct buffer */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send FW version command: ");
		return FALSE;
	}

	/* read the response */
	if (!fu_elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		g_prefix_error_literal(error, "failed to read FW version response: ");
		return FALSE;
	}

	/*
	 * use the auto-generated _parse API to parse the byte array directly.
	 * Note: This automatically validates if rsp_type == FU_ELAN_TS_RSP_TYPE_READ_CMD_RSP
	 * (0x52).
	 */
	st_rsp = fu_struct_elan_ts_fw_version_rsp_parse(cmd_data, sizeof(cmd_data), 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse FW version response structure: ");
		return FALSE;
	}

	/* payload points to cmd_data[1], cmd_data[2], cmd_data[3] */
	payload = fu_struct_elan_ts_fw_version_rsp_get_payload(st_rsp, &buf_size);

	/* validate pattern: High Nibble of Byte[0] (payload[0]) must be 0x0 */
	if ((payload[0] >> 4) != 0x00) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FW version pattern: %02x %02x %02x %02x",
			    cmd_data[0],
			    cmd_data[1],
			    cmd_data[2],
			    cmd_data[3]);
		return FALSE;
	}

	/* read the 3-byte payload stream as a single 24-bit big-endian integer */
	payload_value = fu_memread_uint24(payload, G_BIG_ENDIAN);

	/*
	 * extract the 16-bit FW version directly from the 24-bit integer value by
	 * discarding the pattern high nibble and the trailing blank low nibble.
	 */
	*p_fw_version = (guint16)((payload_value >> 4) & 0xFFFF);
	g_debug("firmware version: 0x%04x", *p_fw_version);

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_test_solution_version:
 * @device: a #FuHidrawDevice
 * @p_test_solution_version: (out): stores the parsed 16-bit test-solution version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Test-Solution version from the device.
 * It sends the type-safe Test-Solution version command and parses the 4-byte response.
 */
gboolean
fu_elan_ts_hid_read_test_solution_version(FuHidrawDevice *device,
					  guint16 *p_test_solution_version,
					  GError **error)
{
	g_autoptr(FuStructElanTsTestSolutionVersionCmd) st_cmd =
	    fu_struct_elan_ts_test_solution_version_cmd_new();
	g_autoptr(FuStructElanTsTestSolutionVersionRsp) st_rsp = NULL;
	guint8 cmd_data[4] = {0};
	guint8 test_ver = 0;
	guint8 solution_ver = 0;
	const guint8 *payload = NULL;
	gsize buf_size = 0;
	guint32 payload_value = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_test_solution_version != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* send Test-Solution Version Command using the auto-initialized type-safe struct buffer */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send Test-Solution Version Command: ");
		return FALSE;
	}

	/* read the response */
	if (!fu_elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		g_prefix_error_literal(error,
				       "failed to read Test-Solution Version Command Response: ");
		return FALSE;
	}

	/*
	 * use the auto-generated _parse API to parse the byte array directly.
	 * Note: This automatically validates if rsp_type == FU_ELAN_TS_RSP_TYPE_READ_CMD_RSP
	 * (0x52).
	 */
	st_rsp =
	    fu_struct_elan_ts_test_solution_version_rsp_parse(cmd_data, sizeof(cmd_data), 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(
		    error,
		    "failed to parse Test-Solution version response structure: ");
		return FALSE;
	}

	/* payload points to cmd_data[1], cmd_data[2], cmd_data[3] */
	payload = fu_struct_elan_ts_test_solution_version_rsp_get_payload(st_rsp, &buf_size);

	/* validate pattern: High Nibble of Byte (payload) must be 0x0E */
	if ((payload[0] >> 4) != 0x0E) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Invalid Test-Solution Version Command Response: %02x %02x %02x %02x",
			    cmd_data[0],
			    cmd_data[1],
			    cmd_data[2],
			    cmd_data[3]);
		return FALSE;
	}

	/* read the 3-byte payload stream as a single 24-bit big-endian integer */
	payload_value = fu_memread_uint24(payload, G_BIG_ENDIAN);

	/* generate the combined 16-bit version first */
	*p_test_solution_version = (guint16)((payload_value >> 4) & 0xFFFF);

	/* extract sub-versions from the combined 16-bit parameter for debugging */
	test_ver = (guint8)(((*p_test_solution_version) >> 8) & 0xFF);
	solution_ver = (guint8)((*p_test_solution_version) & 0xFF);

	g_debug("test-solution version: 0x%04x (test version: 0x%02x, solution version: 0x%02x)",
		*p_test_solution_version,
		test_ver,
		solution_ver);

	return TRUE;
}

/**
 * fu_elan_ts_hid_set_test_mode:
 * @device: a #FuHidrawDevice
 * @enabled: %TRUE to enter test mode, %FALSE to exit
 * @error: (nullable): a #GError, or %NULL
 *
 * Switches the ELAN touchscreen device between normal mode and test mode.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
fu_elan_ts_hid_set_test_mode(FuHidrawDevice *device, gboolean enabled, GError **error)
{
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	if (enabled) {
		g_autoptr(FuStructElanTsEnterTestModeCmd) st_cmd =
		    fu_struct_elan_ts_enter_test_mode_cmd_new();

		/* enter Test Mode using the type-safe struct buffer */
		g_debug("sending Enter Test Mode Command");
		if (!fu_elan_ts_hid_write_command(device,
						  st_cmd->buf->data,
						  st_cmd->buf->len,
						  error)) {
			g_prefix_error_literal(error, "failed to Send Enter Test Mode Command: ");
			return FALSE;
		}
	} else {
		g_autoptr(FuStructElanTsExitTestModeCmd) st_cmd =
		    fu_struct_elan_ts_exit_test_mode_cmd_new();

		/* exit Test Mode using the type-safe struct buffer */
		g_debug("sending Exit Test Mode Command");
		if (!fu_elan_ts_hid_write_command(device,
						  st_cmd->buf->data,
						  st_cmd->buf->len,
						  error)) {
			g_prefix_error_literal(error, "failed to Send Exit Test Mode Command: ");
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_is_gen6_gen7_ic:
 * @touch_state: the current state of the device (Normal or Recovery)
 * @fw_version: the firmware version, where the high byte is the Solution ID
 * @bc_version: the boot code version, used for identification in recovery mode
 *
 * Checks if the integrated circuit (IC) belongs to the Gen6 or Gen7 series.
 *
 * This identification is critical because Gen6/Gen7 ICs require a specific
 * information byte (0x21) in the ROM read command, whereas legacy ICs
 * (like Gen5) use 0x11.
 *
 * Returns: %TRUE if the IC is Gen6 or Gen7, %FALSE otherwise.
 */
static gboolean
fu_elan_ts_hid_is_gen6_gen7_ic(FuElanTsState touch_state, guint16 fw_version, guint16 bc_version)
{
	guint8 solution_id = 0;
	guint8 bc_ver_h = 0;

	if (touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		/*
		 * In Normal Mode, the high byte of the firmware version
		 * is the Solution ID. We use this to identify Gen6/7 ICs.
		 */
		solution_id = (guint8)(fw_version >> 8);
		g_debug("normal mode: Solution ID 0x%02x", solution_id);

		return ((solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X1) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X2) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_5015M) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_3915P) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6308X1) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X1) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X2) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7318X1));
	} else {
		/*
		 * In Recovery Mode, we cannot access the Solution ID, so we
		 * rely on the high byte of the Boot Code (BC) version instead.
		 */
		bc_ver_h = (guint8)((bc_version & 0xFF00) >> 8);
		g_debug("recovery mode: BC Version High Byte 0x%02x", bc_ver_h);

		return ((bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTA6315X1) ||
			(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTH6315_TO_5015M) ||
			(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTH6315_TO_3915P) ||
			(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTA6308X1) ||
			(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTA7315X1));
	}
}

/**
 * fu_elan_ts_hid_read_rom_data:
 * @device: a #FuHidrawDevice
 * @touch_state: current device state (normal or recovery)
 * @fw_version: firmware version used for ic generation identification
 * @bc_version: boot code version used for ic generation identification
 * @address: rom memory address to read from
 * @p_rom_data: (out): pointer to store 2-byte rom data
 * @error: (nullable): a #GError, or %NULL
 *
 * reads 2 bytes of data from device's rom at specified address.
 *
 * This function automatically determines the correct information byte (0x21 or 0x11)
 * based on whether the ic belongs to the gen6/gen7 series.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
fu_elan_ts_hid_read_rom_data(FuHidrawDevice *device,
			     FuElanTsState touch_state,
			     guint16 fw_version,
			     guint16 bc_version,
			     guint16 address,
			     guint16 *p_rom_data,
			     GError **error)
{
	g_autoptr(FuStructElanTsReadRomCmd) st_cmd = fu_struct_elan_ts_read_rom_cmd_new();
	g_autoptr(FuStructElanTsReadRomRsp) st_rsp = NULL;
	FuElanTsReadRomCmdMode mode = FU_ELAN_TS_READ_ROM_CMD_MODE_EKTH53XX;
	guint8 cmd_data[6] = {0};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_rom_data != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* mode is 0x21 for gen6/gen7 and 0x11 for legacy ics */
	if (fu_elan_ts_hid_is_gen6_gen7_ic(touch_state, fw_version, bc_version))
		mode = FU_ELAN_TS_READ_ROM_CMD_MODE_EKTH63XX_73XX;

	/* prepare command using the updated setter names */
	fu_struct_elan_ts_read_rom_cmd_set_mem_addr(st_cmd, address);
	fu_struct_elan_ts_read_rom_cmd_set_mode(st_cmd, mode);

	g_debug("reading rom data (address: 0x%04x), hardware %s (cmd_data[5]: 0x%02x)",
		address,
		fu_elan_ts_read_rom_cmd_mode_to_string(mode),
		mode);

	/* send read rom data command */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send read rom data command: ");
		return FALSE;
	}

	/* receive rom data response */
	if (!fu_elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		g_prefix_error_literal(error, "failed to receive rom data response: ");
		return FALSE;
	}

	/* use auto-generated _parse API to validate header (0x95) and extract fields safely */
	st_rsp = fu_struct_elan_ts_read_rom_rsp_parse(cmd_data, sizeof(cmd_data), 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "invalid rom data response structure: ");
		return FALSE;
	}

	/* optional check: verify if the response echoes back the correct memory address */
	if (fu_struct_elan_ts_read_rom_rsp_get_mem_addr(st_rsp) != address) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "address mismatch in response: 0x%04x (expected 0x%04x)",
			    fu_struct_elan_ts_read_rom_rsp_get_mem_addr(st_rsp),
			    address);
		return FALSE;
	}

	/* optional check: verify if the response echoes back the correct mode */
	if (fu_struct_elan_ts_read_rom_rsp_get_mode(st_rsp) != mode) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "mode mismatch in response: 0x%02x (expected 0x%02x)",
			    fu_struct_elan_ts_read_rom_rsp_get_mode(st_rsp),
			    mode);
		return FALSE;
	}

	/* extract values using type-safe getter */
	*p_rom_data = fu_struct_elan_ts_read_rom_rsp_get_rom_data(st_rsp);

	g_debug("rom data at 0x%04x: 0x%04x", address, *p_rom_data);
	return TRUE;
}

/**
 * fu_elan_ts_hid_read_remark_id:
 * @device: a #FuHidrawDevice
 * @touch_state: current device state (Normal or Recovery)
 * @fw_version: firmware version (used for IC generation identification)
 * @bc_version: boot code version (used for IC generation identification)
 * @p_remark_id: (out): pointer to store the 2-byte Remark ID
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads the Remark ID from the device's ROM at the predefined address (0x801F).
 *
 * The Remark ID is essential for identifying hardware sub-models and ensuring
 * that the firmware image is fully compatible with the physical touch controller.
 *
 * Returns: %TRUE if the Remark ID was successfully read, %FALSE otherwise.
 */
gboolean
fu_elan_ts_hid_read_remark_id(FuHidrawDevice *device,
			      FuElanTsState touch_state,
			      guint16 fw_version,
			      guint16 bc_version,
			      guint16 *p_remark_id,
			      GError **error)
{
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_remark_id != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/*
	 * Use the generic ROM read function to fetch the Remark ID.
	 * ELAN_TS_MEM_REMARK_ID_ADDR (0x801F) is the specific memory offset
	 * for hardware identification.
	 */
	if (!fu_elan_ts_hid_read_rom_data(device,
					  touch_state,
					  fw_version,
					  bc_version,
					  ELAN_TS_MEM_REMARK_ID_ADDR,
					  p_remark_id,
					  error)) {
		g_prefix_error_literal(error, "failed to read Remark ID from ROM: ");
		return FALSE;
	}
	g_debug("remark id: 0x%04x", *p_remark_id);

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_page_data:
 * @device: a #FuHidrawDevice
 * @mem_addr: target memory address
 * @p_page_data_buf: (out): memory page data buffer
 * @page_data_buf_size: length of @p_page_data_buf
 * @error: (nullable): a #GError, or %NULL
 *
 * reads a single memory page from the device using fragmented hid read frames.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
fu_elan_ts_hid_read_page_data(FuHidrawDevice *device,
			      guint16 mem_addr,
			      guint8 *p_page_data_buf,
			      gsize page_data_buf_size,
			      GError **error)
{
	g_autoptr(FuStructElanTsShowBulkRomDataCmd) st_cmd =
	    fu_struct_elan_ts_show_bulk_rom_data_cmd_new();
	guint8 data_buf[ELAN_TS_HID_DATA_BUFFER_SIZE] = {0};
	guint page_frame_count = 0;
	gsize page_data_size_words = page_data_buf_size / 2;
	gsize page_frame_data_len = 0;
	gsize data_len = 0;
	gsize page_frame_index = 0;
	gsize page_data_index = 0;
	gsize rsp_data_size_words = 0;
	gsize rsp_data_buf_size = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_page_data_buf != NULL, FALSE);
	g_return_val_if_fail(page_data_buf_size == ELAN_TS_MEMORY_PAGE_SIZE, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* prepare command fields using typesafe setters and enums */
	fu_struct_elan_ts_show_bulk_rom_data_cmd_set_mem_addr(st_cmd, mem_addr);
	fu_struct_elan_ts_show_bulk_rom_data_cmd_set_data_size_words(st_cmd,
								     (guint16)page_data_size_words);
	fu_struct_elan_ts_show_bulk_rom_data_cmd_set_mode(st_cmd,
							  FU_ELAN_TS_SHOW_BULK_ROM_MODE_MAIN_CODE);

	/* send show bulk rom data command */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send show bulk rom command: ");
		return FALSE;
	}

	/* wait for hardware execution (20ms) */
	fu_device_sleep(FU_DEVICE(device), 20);

	/* fragmented read loop */
	page_frame_count = (guint)((page_data_buf_size + (ELAN_TS_HID_READ_PAGE_FRAME_SIZE - 1)) /
				   ELAN_TS_HID_READ_PAGE_FRAME_SIZE);

	for (page_frame_index = 0; page_frame_index < page_frame_count; page_frame_index++) {
		g_autoptr(FuStructElanTsShowBulkRomRsp) st_rsp = NULL;
		const guint8 *rsp_data = NULL;

		rsp_data_buf_size = 0;
		rsp_data_size_words = 0;

		if (page_frame_index == (page_frame_count - 1))
			page_frame_data_len = page_data_buf_size - page_data_index;
		else
			page_frame_data_len = ELAN_TS_HID_READ_PAGE_FRAME_SIZE;

		/* calculate dynamic read length to avoid hardware timeout on last fragment */
		data_len = 3 + page_frame_data_len;

		if (!fu_elan_ts_hid_read_data(FU_HIDRAW_DEVICE(device),
					      data_buf,
					      data_len,
					      TRUE,
					      error)) {
			g_prefix_error(error,
				       "failed to read frame %" G_GSIZE_FORMAT ": ",
				       page_frame_index);
			return FALSE;
		}

		/* use auto-generated _parse API to validate header (0x99) and unpack 63 bytes */
		st_rsp = fu_struct_elan_ts_show_bulk_rom_rsp_parse(data_buf,
								   ELAN_TS_HID_DATA_BUFFER_SIZE,
								   0,
								   error);
		if (st_rsp == NULL) {
			g_prefix_error_literal(error, "invalid rom page response structure: ");
			return FALSE;
		}

		/* extract hardware-reported word length and validate against expected byte length
		 */
		rsp_data_size_words =
		    fu_struct_elan_ts_show_bulk_rom_rsp_get_data_size_words(st_rsp);
		if ((rsp_data_size_words * 2) != page_frame_data_len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "response data reported payload length mismatch: expected "
				    "%" G_GSIZE_FORMAT " bytes, got %" G_GSIZE_FORMAT " bytes",
				    page_frame_data_len,
				    (rsp_data_size_words * 2));
			return FALSE;
		}

		/* retrieve safe pointer to data array inside struct */
		rsp_data = fu_struct_elan_ts_show_bulk_rom_rsp_get_data(st_rsp, &rsp_data_buf_size);

		/* copy frame data to page buffer safely using verified hardware length boundary */
		if (!fu_memcpy_safe(p_page_data_buf,
				    page_data_buf_size,
				    page_data_index, /* dst_offset */
				    rsp_data,
				    (rsp_data_size_words * 2), /* src_sz */
				    0, /* src_offset: now 0 because rustgen strips metadata */
				    page_frame_data_len, /* n */
				    error)) {
			g_prefix_error_literal(error, "failed to copy frame data: ");
			return FALSE;
		}

		page_data_index += page_frame_data_len;
	}

	if (page_data_index != page_data_buf_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "read size mismatch: expected %" G_GSIZE_FORMAT
			    ", got %" G_GSIZE_FORMAT,
			    page_data_buf_size,
			    page_data_index);
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_info_page:
 * @device: a #FuHidrawDevice
 * @p_info_page_buf: (out): Buffer to store the information page (must be 128 bytes)
 * @info_page_buf_size: Size of the buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads the Information Page from the device by entering test mode,
 * reading the specific memory page, and then exiting test mode.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
fu_elan_ts_hid_read_info_page(FuHidrawDevice *device,
			      guint8 *p_info_page_buf,
			      gsize info_page_buf_size,
			      GError **error)
{
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_info_page_buf != NULL, FALSE);
	g_return_val_if_fail(info_page_buf_size == ELAN_TS_MEMORY_PAGE_SIZE, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* enter Test Mode */
	if (!fu_elan_ts_hid_set_test_mode(device, TRUE, error)) {
		/* Error is already set by fu_elan_ts_hid_set_test_mode */
		return FALSE;
	}

	/* read Information Page (Target: ELAN_TS_MEM_INFO_PAGE_1_ADDR) */
	if (!fu_elan_ts_hid_read_page_data(device,
					   ELAN_TS_MEM_INFO_PAGE_1_ADDR,
					   p_info_page_buf,
					   info_page_buf_size,
					   error)) {
		g_prefix_error_literal(error, "failed to read Information Page: ");

		/* attempt to exit test mode even if read fails to restore state */
		fu_elan_ts_hid_set_test_mode(device, FALSE, NULL);
		return FALSE;
	}

	/* exit Test Mode */
	if (!fu_elan_ts_hid_set_test_mode(device, FALSE, error))
		return FALSE;

	g_debug("successfully retrieve Information Page");
	return TRUE;
}

/**
 * fu_elan_ts_hid_read_info_page_retry_cb:
 * @device: a #FuDevice
 * @user_data: a #FuElanTsInfoPageHelper pointer
 * @error: (nullable): a #GError, or %NULL
 *
 * Callback used by fu_device_retry_full to attempt reading the Info Page.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 */
static gboolean
fu_elan_ts_hid_read_info_page_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsInfoPageHelper *helper = (FuElanTsInfoPageHelper *)user_data;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(helper != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* call the underlying page read function using the updated name */
	return fu_elan_ts_hid_read_info_page(FU_HIDRAW_DEVICE(device),
					     helper->info_page_buf,
					     sizeof(helper->info_page_buf),
					     error);
}

/**
 * fu_elan_ts_hid_read_info_page_with_retry:
 * @device: a #FuHidrawDevice
 * @p_info_page_buf: (out): Buffer to store the information page (must be 128 bytes)
 * @info_page_buf_size: Size of the buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * High-level function to retrieve the Info Page with an automatic retry mechanism.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
fu_elan_ts_hid_read_info_page_with_retry(FuHidrawDevice *device,
					 guint8 *p_info_page_buf,
					 gsize info_page_buf_size,
					 GError **error)
{
	FuElanTsInfoPageHelper helper = {0};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_info_page_buf != NULL, FALSE);
	g_return_val_if_fail(info_page_buf_size == ELAN_TS_MEMORY_PAGE_SIZE, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	g_debug("attempting to read Information Page with retry mechanism");

	/* invoke the fwupd retry framework using the updated callback name */
	if (!fu_device_retry_full(FU_DEVICE(device),
				  fu_elan_ts_hid_read_info_page_retry_cb,
				  ELAN_TS_DEFAULT_ERROR_RETRY_COUNT,
				  0,
				  &helper,
				  error)) {
		g_prefix_error_literal(error, "all attempts to read Info Page failed: ");
		return FALSE;
	}

	/* copy the retrieved page back to the caller's buffer safely (8-parameter version) */
	if (!fu_memcpy_safe(p_info_page_buf,
			    info_page_buf_size,
			    0, /* dst_offset */
			    helper.info_page_buf,
			    sizeof(helper.info_page_buf),
			    0,			      /* src_offset */
			    ELAN_TS_MEMORY_PAGE_SIZE, /* n */
			    error)) {
		g_prefix_error_literal(error, "failed to copy info page result: ");
		return FALSE;
	}

	g_debug("successfully read Information Page after retries");
	return TRUE;
}

/**
 * fu_elan_ts_hid_unlock_flash:
 * @device: a #FuHidrawDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the Write Flash Key command to unlock the flash memory for writing.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
fu_elan_ts_hid_unlock_flash(FuHidrawDevice *device, GError **error)
{
	g_autoptr(FuStructElanTsWriteFlashKeyCmd) st_cmd =
	    fu_struct_elan_ts_write_flash_key_cmd_new();

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* unlock Flash by sending the type-safe Write Flash Key command buffer */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send Write Flash Key command: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_enter_iap_mode:
 * @device: a #FuHidrawDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the command to trigger the device to enter IAP (Bootloader) mode.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
fu_elan_ts_hid_enter_iap_mode(FuHidrawDevice *device, GError **error)
{
	g_autoptr(FuStructElanTsEnterIapCmd) st_cmd = fu_struct_elan_ts_enter_iap_cmd_new();

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* enter IAP Mode by sending the type-safe Enter IAP command buffer */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send Enter IAP mode command: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_check_i2c_address:
 * @device: a #FuHidrawDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Verifies Bootloader readiness by performing a i2c address handshake.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 */
gboolean
fu_elan_ts_hid_check_i2c_address(FuHidrawDevice *device, GError **error)
{
	g_autoptr(FuStructElanTsI2cAddrCmd) st_cmd = fu_struct_elan_ts_i2c_addr_cmd_new();
	g_autoptr(FuStructElanTsI2cAddrRsp) st_rsp = NULL;
	guint8 cmd_data = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* send 7-bit I2C Address as a type-safe trigger command to the bootloader */
	if (!fu_elan_ts_hid_write_command(device, st_cmd->buf->data, st_cmd->buf->len, error)) {
		g_prefix_error_literal(error, "failed to send 7-bit addr handshake: ");
		return FALSE;
	}

	/*
	 * Read back the 1-byte response.
	 * filter=TRUE to strip HID headers and retrieve the raw address byte.
	 */
	if (!fu_elan_ts_hid_read_data(device, &cmd_data, 1, TRUE, error)) {
		g_prefix_error_literal(error, "failed to read ID pattern from HID report: ");
		return FALSE;
	}

	/*
	 * use the auto-generated _parse API to parse the byte array directly.
	 * Note: This automatically validates if addr_8bit == ELAN_TS_I2C_SLAVE_ADDR (0x20).
	 */
	st_rsp = fu_struct_elan_ts_i2c_addr_rsp_parse(&cmd_data, 1, 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "i2c address handshake failed (ID mismatch): ");
		return FALSE;
	}

	g_debug("i2c address 0x%02x verified", cmd_data);
	return TRUE;
}

/**
 * fu_elan_ts_hid_write_frame_data:
 * @device: a #FuHidrawDevice
 * @data_offset: word-aligned offset for the current frame
 * @data_len: length of valid data in @p_frame_buf
 * @p_frame_buf: raw firmware data for this frame
 * @error: (nullable): a #GError, or %NULL
 *
 * Wraps IAP frame data with the HID Output Report ID (0x03) and
 * the IAP protocol header (0x21), then sends a fixed-length report.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
fu_elan_ts_hid_write_frame_data(FuHidrawDevice *device,
				guint16 data_offset,
				gsize data_len,
				const guint8 *p_frame_buf,
				GError **error)
{
	/* use the unified fixed-size output report buffer */
	guint8 out_report_buf[ELAN_TS_OUTPUT_REPORT_SIZE] = {0x00};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_frame_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* [byte 0] HID Output Report ID (0x03) */
	out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID;

	/* [byte 1] IAP Sub-command for frame data */
	out_report_buf[1] = 0x21;

	/* [byte 2..3] Data Offset (Big-Endian) */
	out_report_buf[2] = (guint8)((data_offset >> 8) & 0xFF);
	out_report_buf[3] = (guint8)(data_offset & 0xFF);

	/* [byte 4] Data Length */
	out_report_buf[4] = (guint8)data_len;

	/*
	 * [byte 5..n] Copy raw firmware data into the report safely (8-parameter version).
	 * fu_memcpy_safe handles the boundary check (5 + data_len) internally.
	 */
	if (!fu_memcpy_safe(out_report_buf,
			    sizeof(out_report_buf),
			    5, /* dst_offset */
			    p_frame_buf,
			    data_len, /* src_sz */
			    0,	      /* src_offset */
			    data_len, /* n */
			    error)) {
		g_prefix_error_literal(error, "failed to copy frame payload: ");
		return FALSE;
	}

	/*
	 * Send the fixed-length report via the retry framework.
	 * The entire out_report_buf (ELAN_TS_OUTPUT_REPORT_SIZE) is sent.
	 */
	return fu_elan_ts_hidraw_write_with_retry(device,
						  out_report_buf,
						  sizeof(out_report_buf),
						  error);
}

/**
 * fu_elan_ts_hid_send_flash_write_command:
 * @device: a #FuHidrawDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the vendor-specific command (0x22) to trigger the flash
 * burning process on the touch controller.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
fu_elan_ts_hid_send_flash_write_command(FuHidrawDevice *device, GError **error)
{
	g_autoptr(FuStructElanTsVendorCmd) st_cmd = fu_struct_elan_ts_vendor_cmd_new();

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* prepare the flash write vendor command using the type-safe enum identifier */
	fu_struct_elan_ts_vendor_cmd_set_cmd(st_cmd, FU_ELAN_TS_VENDOR_CMD_FLASH_WRITE);
	g_debug("sending Flash Write Command (0x%02x)", st_cmd->buf->data[0]);

	/*
	 * Use the vendor command wrapper which:
	 * 1. Wraps the payload with HID Output Report ID (0x03)
	 * 2. Sends it as a fixed-length report (ELAN_TS_OUTPUT_REPORT_SIZE)
	 */
	if (!fu_elan_ts_hid_write_vendor_command(device,
						 st_cmd->buf->data,
						 st_cmd->buf->len,
						 error)) {
		g_prefix_error(error,
			       "Failed to write vendor command 0x%02x: ",
			       st_cmd->buf->data[0]);
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_flash_write_response:
 * @device: a #FuHidrawDevice
 * @p_response: (out): stores the combined 16-bit response value
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads the flash write response from the device and validates the pattern.
 * Expected pattern is 0xAA 0xAA.
 *
 * Returns: %TRUE for success, %FALSE for failure or pattern mismatch.
 */
gboolean
fu_elan_ts_hid_read_flash_write_response(FuHidrawDevice *device,
					 guint16 *p_response,
					 GError **error)
{
	g_autoptr(FuStructElanTsFlashWriteRsp) st_rsp = NULL;
	guint8 flash_write_response_data[2] = {0};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_response != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* read Flash Write Response (using retry-enabled read wrapper with filter=TRUE) */
	if (!fu_elan_ts_hid_read_data(device,
				      flash_write_response_data,
				      sizeof(flash_write_response_data),
				      TRUE,
				      error)) {
		g_prefix_error_literal(error, "Fail to receive Flash Write Response data: ");
		return FALSE;
	}
	g_debug("flash write response: %02x %02x",
		flash_write_response_data[0],
		flash_write_response_data[1]);

	/*
	 * use auto-generated _parse API to parse byte array directly
	 * note: this automatically validates if payload == 0xAAAA (0xaa 0xaa)
	 */
	st_rsp = fu_struct_elan_ts_flash_write_rsp_parse(flash_write_response_data,
							 sizeof(flash_write_response_data),
							 0,
							 error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "invalid flash write response pattern: ");
		return FALSE;
	}

	/* use fwupd native memory reader API to safely parse big-endian byte array to 16-bit word
	 */
	*p_response = fu_memread_uint16(flash_write_response_data, G_BIG_ENDIAN);

	return TRUE;
}

/**
 * fu_elan_ts_hid_device_recalibrate:
 * @device: a #FuHidrawDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Triggers the hardware re-calibration process using encapsulated I/O helpers.
 * The process involves sending a flash key followed by the REK command,
 * and finally validating the 0x66666666 success pattern.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 */
static gboolean
fu_elan_ts_hid_device_recalibrate(FuHidrawDevice *device, GError **error)
{
	g_autoptr(FuStructElanTsWriteFlashKeyCmd) st_flash_key_cmd =
	    fu_struct_elan_ts_write_flash_key_cmd_new();
	g_autoptr(FuStructElanTsRekCmd) st_rek_cmd = fu_struct_elan_ts_rek_cmd_new();
	g_autoptr(FuStructElanTsCalibrationRsp) st_rsp = NULL;
	guint8 cmd_data[4] = {0};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	g_debug("starting touch re-calibration");

	/* send Write Flash Key Command using the type-safe struct buffer */
	if (!fu_elan_ts_hid_write_command(device,
					  st_flash_key_cmd->buf->data,
					  st_flash_key_cmd->buf->len,
					  error)) {
		g_prefix_error_literal(error, "failed to send Write Flash Key command: ");
		return FALSE;
	}

	/* send Re-Calibration Command (Re-K) using the type-safe struct buffer */
	if (!fu_elan_ts_hid_write_command(device,
					  st_rek_cmd->buf->data,
					  st_rek_cmd->buf->len,
					  error)) {
		g_prefix_error_literal(error, "failed to send Re-calibration command: ");
		return FALSE;
	}

	/* receive and Verify Calibration Response */
	g_debug("waiting for calibration response");
	if (!fu_elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		g_prefix_error_literal(error, "failed to receive re-calibration response: ");
		return FALSE;
	}

	/*
	 * use the auto-generated _parse API to parse the byte array directly.
	 * Note: This automatically validates if payload == 0x66666666.
	 */
	st_rsp = fu_struct_elan_ts_calibration_rsp_parse(cmd_data, sizeof(cmd_data), 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "re-calibration failed (invalid response pattern): ");
		return FALSE;
	}

	g_debug("re-calibration success");
	return TRUE;
}

/**
 * fu_elan_ts_hid_recalibrate_retry_cb:
 * @device: a #FuDevice
 * @user_data: unused
 * @error: (nullable): a #GError
 *
 * The callback function used by fu_device_retry_full to perform a single
 * re-calibration transaction.
 *
 * Returns: %TRUE for success, %FALSE for failure (triggers retry)
 */
static gboolean
fu_elan_ts_hid_recalibrate_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* call the atomic calibration function we just finalized */
	return fu_elan_ts_hid_device_recalibrate(FU_HIDRAW_DEVICE(device), error);
}

/**
 * fu_elan_ts_hid_device_recalibrate_with_retry:
 * @device: a #FuHidrawDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Triggers the hardware re-calibration process with an automatic retry mechanism
 * using the fwupd retry framework.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed.
 **/
gboolean
fu_elan_ts_hid_device_recalibrate_with_retry(FuHidrawDevice *device, GError **error)
{
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/*
	 * Invoke the retry framework.
	 * Since recalibrate doesn't need to pass back data through a helper struct,
	 * we pass NULL for user_data.
	 */
	if (!fu_device_retry_full(FU_DEVICE(device),
				  fu_elan_ts_hid_recalibrate_retry_cb,
				  ELAN_TS_IO_MAX_RETRIES,
				  0,
				  NULL,
				  error))
		return FALSE;

	return TRUE;
}

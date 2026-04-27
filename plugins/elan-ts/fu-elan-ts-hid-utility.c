/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-hidraw-utility.h"
#include "fu-elan-ts-hid-utility.h"

/* Helper structure to pass results back from the retry callback */
typedef struct {
	guint8 hello_packet;
	guint16 bc_version;
} ElanTsHelloHelper;

/* Helper structure for info page retrieval via retry mechanism */
typedef struct {
	guint8 info_page_buf[ELAN_TS_MEMORY_PAGE_SIZE];
} ElanTsInfoPageHelper;

/**
 * elan_ts_hid_get_debug_setting:
 * @device: a #FuDevice
 *
 * Helper function to retrieve the debug bitmask from the underlying utility.
 * This provides a cleaner interface within the device logic.
 *
 * Returns: the debug setting bitmask.
 **/
static guint32
elan_ts_hid_get_debug_setting(FuDevice *device)
{
	/* Forward the request to the low-level hidraw utility */
	return elan_ts_hidraw_get_debug_setting(device);
}

/**
 * elan_ts_hid_write_vendor_command:
 * @device: a #FuDevice
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
elan_ts_hid_write_vendor_command(FuDevice *device,
				 const guint8 *p_vendor_cmd_buf,
				 gsize vendor_cmd_len,
				 GError **error)
{
	guint8 out_report_buf[ELAN_TS_OUTPUT_REPORT_SIZE] = {0x00};
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_vendor_cmd_buf != NULL, FALSE);

	if (vendor_cmd_len > (ELAN_TS_OUTPUT_REPORT_SIZE - 1)) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "Vendor command length %u exceeds report capacity",
				  (guint)vendor_cmd_len);
		return FALSE;
	}

	out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID;
	memcpy(&out_report_buf[1], p_vendor_cmd_buf, vendor_cmd_len);

	/* Internal timeout and retries are managed by write_with_retry */
	return elan_ts_hidraw_write_with_retry(device, 
					       out_report_buf, 
					       sizeof(out_report_buf), 
					       error);
}

/**
 * elan_ts_hid_write_command:
 * @device: a #FuDevice
 * @p_cmd_buf: the raw command payload (e.g., 4 or 6 bytes)
 * @cmd_len: length of @p_cmd_buf
 * @error: (nullable): a #GError, or %NULL
 *
 * Wraps the TP command with the appropriate HID Report ID (based on PID) 
 * and a 2-byte header, then sends it as a fixed-length HID Output Report with retries.
 * The timeout and retry logic are managed internally by the retry framework.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_write_command(FuDevice *device,
			  const guint8 *p_cmd_buf,
			  gsize cmd_len,
			  GError **error)
{
	guint8 out_report_buf[ELAN_TS_OUTPUT_REPORT_SIZE] = {0x00};
	guint16 hid_pid = fu_device_get_pid(device);
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_cmd_buf != NULL, FALSE);

	/* 
	 * Ensure the payload fits: 
	 * 1 byte (Report ID) + 2 bytes (Header) + cmd_len must be within limits.
	 */
	if ((3 + cmd_len) > ELAN_TS_OUTPUT_REPORT_SIZE) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "Command length %u with header exceeds HID report limit",
				  (guint)cmd_len);
		return FALSE;
	}

	/* [Byte 0] Select Report ID based on hardware PID */
	if ((hid_pid == ELAN_TS_HID_PID_BRIDGE) || (hid_pid == ELAN_TS_HID_PID_BRIDGE_B)) {
		out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID_BRIDGE;
	} else {
		out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID;
	}

	/* [Byte 1] Bridge Command byte (Fixed 0x00) */
	out_report_buf[1] = 0x00;

	/* [Byte 2] The length of the raw command payload */
	out_report_buf[2] = (guint8)cmd_len;

	/* [Byte 3..n] Copy the raw command payload into the report */
	memcpy(&out_report_buf[3], p_cmd_buf, cmd_len);

	/* 
	 * Use the retry-enabled write function. The internal timeout 
	 * is handled by elan_ts_hidraw_write_with_retry.
	 */
	return elan_ts_hidraw_write_with_retry(device, 
					       out_report_buf, 
					       sizeof(out_report_buf), 
					       error);
}

/**
 * elan_ts_hid_read_data:
 * @device: a #FuDevice
 * @p_data_buf: (out): destination buffer to store the read data
 * @data_len: length of data to copy into @p_data_buf
 * @filter: if %TRUE, strips the 2-byte HID header (Report ID + Status/Length)
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads an input report from ELAN TS with retries and validates the Report ID.
 * The timeout and retry logic are managed internally.
 *
 * Returns: %TRUE for success, %FALSE for failure or invalid data pattern.
 */
gboolean
elan_ts_hid_read_data(FuDevice *device,
		      guint8 *p_data_buf,
		      gsize data_len,
		      gboolean filter,
		      GError **error)
{
	guint8 in_report_buf[ELAN_TS_INPUT_REPORT_SIZE] = {0x00};
	guint16 hid_pid = fu_device_get_pid(device);
	guint8 elan_ts_input_report_id = 0;
	gsize input_report_data_len = 0;
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_data_buf != NULL, FALSE);

	/* 
	 * Perform raw read into internal fixed-size buffer with retries.
	 * Internal timeout is handled by elan_ts_hidraw_read_with_retry.
	 */
	if (!elan_ts_hidraw_read_with_retry(device, 
					    in_report_buf, 
					    sizeof(in_report_buf), 
					    error)) {
		return FALSE;
	}

	/* Select the expected Command Response Report ID based on PID */
	if ((hid_pid == ELAN_TS_HID_PID_BRIDGE) || (hid_pid == ELAN_TS_HID_PID_BRIDGE_B)) {
		elan_ts_input_report_id = ELAN_TS_HID_INPUT_REPORT_ID_BRIDGE; /* 0x00 */
	} else {
		elan_ts_input_report_id = ELAN_TS_HID_INPUT_REPORT_ID;        /* 0x02 */
	}

	/* Validate if the received Report ID matches the expected command response */
	if ((in_report_buf[0] != elan_ts_input_report_id) &&
	    (in_report_buf[0] != ELAN_TS_HID_FINGER_REPORT_ID) &&
	    (in_report_buf[0] != ELAN_TS_HID_PEN_REPORT_ID) &&
	    (in_report_buf[0] != ELAN_TS_HID_PEN_DEBUG_REPORT_ID)) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "invalid report ID: 0x%02X",
				  in_report_buf[0]);
		return FALSE;
	}

	/* Copy data to output buffer based on filter flag */
	if (filter) {
		/* Strip 2-byte header (Report ID + Status/Length byte) */
		input_report_data_len = MIN(data_len, (ELAN_TS_INPUT_REPORT_SIZE - 2));
		memcpy(p_data_buf, &in_report_buf[2], input_report_data_len);
	} else {
		/* Keep everything including the header */
		input_report_data_len = MIN(data_len, ELAN_TS_INPUT_REPORT_SIZE);
		memcpy(p_data_buf, in_report_buf, input_report_data_len);
	}

	return TRUE;
}

/**
 * elan_ts_hid_read_hello_packet_bc_version:
 * @device: a #FuDevice
 * @p_hello_packet: (out): stores the hello packet (e.g., 0x20 or 0x56)
 * @p_bc_version: (out): stores the boot code version if in IAP mode
 * @error: (nullable): a #GError, or %NULL
 *
 * Requests the Hello Packet and BC Version from the device.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
elan_ts_hid_read_hello_packet_bc_version(FuDevice *device,
                                         guint8 *p_hello_packet,
                                         guint16 *p_bc_version,
                                         GError **error)
{
	guint8 hello_cmd = 0x18;
	guint8 cmd_data[4] = {0};
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);

	/* Send the request via the retry-enabled vendor command wrapper */
	if (!elan_ts_hid_write_vendor_command(device, &hello_cmd, sizeof(hello_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send hello command: ");
		return FALSE;
	}

	/* Read the response using the retry-enabled read wrapper with filter=TRUE */
	if (!elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read hello response: ");
		return FALSE;
	}

	/* Parse the filtered data */
	if (p_hello_packet != NULL)
		*p_hello_packet = cmd_data[0];
	if (p_bc_version != NULL) {
		/*
		 * BC Version is valid only when Hello Packet is 0x56 (Recovery Mode).
		 * If 0x20 (Normal Mode), it typically returns 0xFFFF.
		 */
		*p_bc_version = (guint16)(cmd_data[2] << 8) | cmd_data[3];
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_read_hello_retry_cb:
 * @device: a #FuDevice
 * @user_data: a #ElanTsHelloHelper pointer to store results
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
	ElanTsHelloHelper *helper = (ElanTsHelloHelper *)user_data;

	/*
	 * Call the underlying communication function which already has its
	 * own internal I/O retries for maximum reliability.
	 */
	return elan_ts_hid_read_hello_packet_bc_version(device,
							&helper->hello_packet,
							&helper->bc_version,
							error);
}

/**
 * elan_ts_hid_read_hello_packet_bc_version_with_retry:
 * @device: a #FuDevice
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
elan_ts_hid_read_hello_packet_bc_version_with_retry(FuDevice *device,
                                                   guint8 *p_hello_packet,
                                                   guint16 *p_bc_version,
                                                   GError **error)
{
	ElanTsHelloHelper helper = {0};

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_hello_packet != NULL, FALSE);
	g_return_val_if_fail(p_bc_version != NULL, FALSE);

	/*
	 * Invoke the retry framework using predefined constants for
	 * maximum retry attempts and interval.
	 */
	if (!fu_device_retry_full(device,
	                          fu_elan_ts_hid_read_hello_retry_cb,
	                          ELAN_TS_IO_MAX_RETRIES,
	                          ELAN_TS_DEFAULT_RETRY_INTERVAL_MS,
	                          &helper,
	                          error)) {
		return FALSE;
	}

	/* write results back to caller's pointers upon success */
	*p_hello_packet = helper.hello_packet;
	*p_bc_version = helper.bc_version;

	return TRUE;
}

/**
 * elan_ts_hid_read_boot_code_version:
 * @device: a #FuDevice
 * @p_bc_version: (out): stores the parsed 16-bit boot code version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Boot Code version from the device while in Normal Mode.
 * It sends command 0x53 and expects a response starting with 0x52.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_read_boot_code_version(FuDevice *device, guint16 *p_bc_version, GError **error)
{
	guint8 bc_ver_cmd[] = {0x53, 0x10, 0x00, 0x01};
	guint8 cmd_data[4] = {0};
	guint8 bc_ver_major = 0;
	guint8 bc_ver_minor = 0;
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_bc_version != NULL, FALSE);

	/* Send Boot Code Version Command (0x53...) */
	if (!elan_ts_hid_write_command(device, bc_ver_cmd, sizeof(bc_ver_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send BC version command: ");
		return FALSE;
	}

	/* Read the response (Expected to start with 0x52) */
	if (!elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read BC version data: ");
		return FALSE;
	}

	/* Validate and parse the response pattern
	 * Pattern check: Byte[0] == 0x52 and high nibble of Byte[1] == 0x1 */
	if ((cmd_data[0] == 0x52) && ((cmd_data[1] >> 4) == 0x01)) {
		/*
		 * Major: Low nibble of Byte[1] | High nibble of Byte[2]
		 * Minor: Low nibble of Byte[2] | High nibble of Byte[3]
		 */
		bc_ver_major = ((cmd_data[1] & 0x0F) << 4) | (cmd_data[2] >> 4);
		bc_ver_minor = ((cmd_data[2] & 0x0F) << 4) | (cmd_data[3] >> 4);
		*p_bc_version = (guint16)(bc_ver_major << 8) | bc_ver_minor;

		return TRUE;
	}

	/* Use SET_ERROR for pattern mismatch cases */
	ELAN_TS_SET_ERROR(debug_setting,
			  error,
			  FWUPD_ERROR_INVALID_DATA,
			  "invalid BC version pattern: %02x %02x %02x %02x",
			  cmd_data[0],
			  cmd_data[1],
			  cmd_data[2],
			  cmd_data[3]);
	return FALSE;
}

/**
 * elan_ts_hid_read_fw_id:
 * @device: a #FuDevice
 * @p_fw_id: (out): stores the parsed 16-bit firmware ID
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Firmware ID from the device.
 * It sends command {0x53, 0xf0, 0x00, 0x01} and expects 0x52 pattern in response.
 */
gboolean
elan_ts_hid_read_fw_id(FuDevice *device, guint16 *p_fw_id, GError **error)
{
	guint8 fw_id_cmd[] = {0x53, 0xf0, 0x00, 0x01};
	guint8 cmd_data[4] = {0};
	guint8 major_fw_id = 0;
	guint8 minor_fw_id = 0;
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_fw_id != NULL, FALSE);

	/* end FW ID Command (0x53 0xF0...) */
	ELAN_TS_DEBUG(debug_setting,
		      "%s: Sending FW ID command: 0x%02x 0x%02x 0x%02x 0x%02x",
		      G_STRFUNC,
		      fw_id_cmd[0],
		      fw_id_cmd[1],
		      fw_id_cmd[2],
		      fw_id_cmd[3]);

	if (!elan_ts_hid_write_command(device, fw_id_cmd, sizeof(fw_id_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send FW ID command: ");
		return FALSE;
	}

	/* Read the response (Expected to start with 0x52) */
	/* Filter=TRUE assumes your read_data handles HID report ID stripping */
	if (!elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read FW ID response: ");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting,
		      "%s: FW ID response data: 0x%02x 0x%02x 0x%02x 0x%02x",
		      G_STRFUNC,
		      cmd_data[0],
		      cmd_data[1],
		      cmd_data[2],
		      cmd_data[3]);

	/* Validate pattern: Byte[0] == 0x52 and High Nibble of Byte[1] == 0xF */
	if ((cmd_data[0] == 0x52) && ((cmd_data[1] >> 4) == 0x0F)) {
		/*
		 * Major: Low nibble of Byte[1] | High nibble of Byte[2]
		 * Minor: Low nibble of Byte[2] | High nibble of Byte[3]
		 */
		major_fw_id = ((cmd_data[1] & 0x0F) << 4) | (cmd_data[2] >> 4);
		minor_fw_id = ((cmd_data[2] & 0x0F) << 4) | (cmd_data[3] >> 4);
		*p_fw_id = (guint16)(major_fw_id << 8) | minor_fw_id;

		ELAN_TS_DEBUG(debug_setting, "%s: Parsed Firmware ID: 0x%04x", G_STRFUNC, *p_fw_id);
		return TRUE;
	}

	/* Pattern mismatch error */
	ELAN_TS_SET_ERROR(debug_setting,
			  error,
			  FWUPD_ERROR_INVALID_DATA,
			  "invalid FW ID pattern: %02x %02x %02x %02x",
			  cmd_data[0],
			  cmd_data[1],
			  cmd_data[2],
			  cmd_data[3]);
	return FALSE;
}

/**
 * elan_ts_hid_read_fw_version:
 * @device: a #FuDevice
 * @p_fw_version: (out): stores the parsed 16-bit firmware version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Firmware Version from the device.
 * It sends command {0x53, 0x00, 0x00, 0x01} and expects 0x52 pattern in response.
 */
gboolean
elan_ts_hid_read_fw_version(FuDevice *device, guint16 *p_fw_version, GError **error)
{
	guint8 fw_ver_cmd[] = {0x53, 0x00, 0x00, 0x01};
	guint8 cmd_data[4] = {0};
	guint8 major_fw_ver = 0;
	guint8 minor_fw_ver = 0;
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_fw_version != NULL, FALSE);

	/* Send FW Version Command (0x53 0x00...) */
	if (!elan_ts_hid_write_command(device, fw_ver_cmd, sizeof(fw_ver_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send FW version command: ");
		return FALSE;
	}

	/* Read the response (Expected to start with 0x52) */
	if (!elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read FW version response: ");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting,
		      "%s: FW version response: 0x%02x 0x%02x 0x%02x 0x%02x",
		      G_STRFUNC,
		      cmd_data[0],
		      cmd_data[1],
		      cmd_data[2],
		      cmd_data[3]);

	/* Validate pattern: Byte[0] == 0x52 and High Nibble of Byte[1] == 0x0 */
	if ((cmd_data[0] == 0x52) && ((cmd_data[1] >> 4) == 0x00)) {
		/*
		 * Major: Low nibble of Byte[1] | High nibble of Byte[2]
		 * Minor: Low nibble of Byte[2] | High nibble of Byte[3]
		 */
		major_fw_ver = ((cmd_data[1] & 0x0F) << 4) | (cmd_data[2] >> 4);
		minor_fw_ver = ((cmd_data[2] & 0x0F) << 4) | (cmd_data[3] >> 4);
		*p_fw_version = (guint16)(major_fw_ver << 8) | minor_fw_ver;

		ELAN_TS_DEBUG(debug_setting,
			      "%s: Parsed Firmware Version: 0x%04x",
			      G_STRFUNC,
			      *p_fw_version);
		return TRUE;
	}

	/* Pattern mismatch error */
	ELAN_TS_SET_ERROR(debug_setting,
			  error,
			  FWUPD_ERROR_INVALID_DATA,
			  "invalid FW version pattern: %02x %02x %02x %02x",
			  cmd_data[0],
			  cmd_data[1],
			  cmd_data[2],
			  cmd_data[3]);
	return FALSE;
}

/**
 * elan_ts_hid_read_test_solution_version:
 * @device: a #FuDevice
 * @p_test_solution_version: (out): stores the parsed 16-bit test-solution version
 * @error: (nullable): a #GError, or %NULL
 *
 * Fetches the Test-Solution version from the device.
 * It sends command {0x53, 0xe0, 0x00, 0x01} and expects 0x52/0xE pattern.
 */
gboolean
elan_ts_hid_read_test_solution_version(FuDevice *device,
                                       guint16 *p_test_solution_version,
                                       GError **error)
{
	guint8 test_solution_ver_cmd[] = {0x53, 0xe0, 0x00, 0x01};
	guint8 cmd_data[4] = {0};
	guint8 test_ver = 0;
	guint8 solution_ver = 0;
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_test_solution_version != NULL, FALSE);

	/* Send Test-Solution Version Command (0x53 0xE0...) */
	if (!elan_ts_hid_write_command(device, test_solution_ver_cmd, sizeof(test_solution_ver_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send Test-Solution Version Command: ");
		return FALSE;
	}

	/* Read the response (Expected to start with 0x52) */
	if (!elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read Test-Solution Version Response: ");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting,
		      "%s: Test-Solution Version Response: 0x%02x 0x%02x 0x%02x 0x%02x",
		      G_STRFUNC,
		      cmd_data[0],
		      cmd_data[1],
		      cmd_data[2],
		      cmd_data[3]);

	/* Validate pattern: Byte[0] == 0x52 and High Nibble of Byte[1] == 0xE */
	if ((cmd_data[0] == 0x52) && ((cmd_data[1] >> 4) == 0x0E)) {
		/*
		 * Test Ver.: Low nibble of Byte[1] | High nibble of Byte[2]
		 * Solution Ver.: Low nibble of Byte[2] | High nibble of Byte[3]
		 */
		test_ver = ((cmd_data[1] & 0x0F) << 4) | (cmd_data[2] >> 4);
		solution_ver = ((cmd_data[2] & 0x0F) << 4) | (cmd_data[3] >> 4);
		*p_test_solution_version = (guint16)(test_ver << 8) | solution_ver;

		ELAN_TS_DEBUG(debug_setting,
			      "%s: Parsed Test-Solution Version: %02x.%02x (%04x)",
			      G_STRFUNC,
			      test_ver,
			      solution_ver,
			      *p_test_solution_version);
		return TRUE;
	}

	/* Pattern mismatch error */
	ELAN_TS_SET_ERROR(debug_setting,
			  error,
			  FWUPD_ERROR_INVALID_DATA,
			  "invalid Test version pattern: %02x %02x %02x %02x",
			  cmd_data[0],
			  cmd_data[1],
			  cmd_data[2],
			  cmd_data[3]);
	return FALSE;
}

/**
 * elan_ts_hid_set_test_mode:
 * @device: a #FuDevice
 * @enabled: %TRUE to enter test mode, %FALSE to exit
 * @error: (nullable): a #GError, or %NULL
 *
 * Switches the ELAN touchscreen device between normal mode and test mode.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_set_test_mode(FuDevice *device, gboolean enabled, GError **error)
{
	guint8 enter_test_mode_cmd[4] = {0x55, 0x55, 0x55, 0x55};
	guint8 exit_test_mode_cmd[4] = {0xa5, 0xa5, 0xa5, 0xa5};
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	/* Basic sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);

	if (enabled) {
		/* Enter Test Mode */
		ELAN_TS_DEBUG(debug_setting, "Sending enter test mode command...");
		if (!elan_ts_hid_write_command(device, enter_test_mode_cmd, sizeof(enter_test_mode_cmd), error)) {
			ELAN_TS_SET_ERROR(debug_setting,
					  error,
					  FWUPD_ERROR_WRITE,
					  "failed to enter test mode");
			return FALSE;
		}
	} else {
		/* Exit Test Mode */
		ELAN_TS_DEBUG(debug_setting, "Sending exit test mode command...");
		if (!elan_ts_hid_write_command(device, exit_test_mode_cmd, sizeof(exit_test_mode_cmd), error)) {
			ELAN_TS_SET_ERROR(debug_setting,
					  error,
					  FWUPD_ERROR_WRITE,
					  "failed to exit test mode");
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * elan_ts_hid_is_gen6_gen7_ic:
 * @device: a #FuDevice (used for debug logging)
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
elan_ts_hid_is_gen6_gen7_ic(FuDevice *device,
			    FuElanTsState touch_state,
			    guint16 fw_version,
			    guint16 bc_version)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);
	guint8 solution_id = 0;
	guint8 bc_ver_h = 0;

	if (touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		/* 
		 * In Normal Mode, the high byte of the firmware version 
		 * is the Solution ID. We use this to identify Gen6/7 ICs.
		 */
		solution_id = (guint8)(fw_version >> 8);
		ELAN_TS_DEBUG(debug_setting, "Normal Mode: Solution ID 0x%02x", solution_id);
		
		return ((solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315x1) ||
			(solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315x2) ||
			(solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_5015M) ||
			(solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6315_TO_3915P) ||
			(solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH6308x1) ||
			(solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH7315x1) ||
			(solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH7315x2) ||
			(solution_id == ELAN_TS_HID_SOLUTION_ID_EKTH7318x1));
	} else {
		/* 
		 * In Recovery Mode, we cannot access the Solution ID, so we 
		 * rely on the high byte of the Boot Code (BC) version instead.
		 */
		bc_ver_h = (guint8)((bc_version & 0xFF00) >> 8);
		ELAN_TS_DEBUG(debug_setting, "Recovery Mode: BC Version High Byte 0x%02x", bc_ver_h);
		
		return ((bc_ver_h == ELAN_TS_HID_BC_VER_H_BYTE_EKTA6315_HID) ||
			(bc_ver_h == ELAN_TS_HID_BC_VER_H_BYTE_EKTH6315_TO_5015M_HID) ||
			(bc_ver_h == ELAN_TS_HID_BC_VER_H_BYTE_EKTH6315_TO_3915P_HID) ||
			(bc_ver_h == ELAN_TS_HID_BC_VER_H_BYTE_EKTA6308_HID) ||
			(bc_ver_h == ELAN_TS_HID_BC_VER_H_BYTE_EKTA7315_HID));
	}
}

/**
 * elan_ts_hid_get_rom_data:
 * @device: a #FuDevice
 * @touch_state: current device state (Normal or Recovery)
 * @fw_version: firmware version (used for IC generation identification)
 * @bc_version: boot code version (used for IC generation identification)
 * @address: the ROM memory address to read from
 * @p_rom_data: (out): pointer to store the 2-byte ROM data
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads 2 bytes of data from the device's ROM at the specified address.
 *
 * This function automatically determines the correct information byte (0x21 or 0x11)
 * based on whether the IC belongs to the Gen6/Gen7 series.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
elan_ts_hid_get_rom_data(FuDevice *device,
			 FuElanTsState touch_state,
			 guint16 fw_version,
			 guint16 bc_version,
			 guint16 address,
			 guint16 *p_rom_data,
			 GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);
	guint8 read_rom_data_cmd[6] = {0x96, 0x00, 0x00, 0x00, 0x00, 0x11};
	guint8 cmd_data[6] = {0};

	/* Basic sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(((error == NULL) || (*error == NULL)), FALSE);

	/* Check if the output pointer is valid */
	if (p_rom_data == NULL) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "invalid parameter: p_rom_data is NULL");
		return FALSE;
	}

	/* Set memory address (High Byte / Low Byte) */
	read_rom_data_cmd[1] = (address & 0xFF00) >> 8;
	read_rom_data_cmd[2] = address & 0x00FF;

	/* 
	 * Gen6 and Gen7 series ICs require information byte 0x21 
	 * to access specific ROM areas; legacy ICs use 0x11.
	 */
	if (elan_ts_hid_is_gen6_gen7_ic(device, touch_state, fw_version, bc_version))
		read_rom_data_cmd[5] = 0x21;

	ELAN_TS_DEBUG(debug_setting, "Reading ROM address: 0x%04x using read_rom_data_cmd[5]: 0x%02x", address, read_rom_data_cmd[5]);

	/* Send Read ROM Data Command (0x96) */
	if (!elan_ts_hid_write_command(device, read_rom_data_cmd, sizeof(read_rom_data_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send Read ROM Data Command: ");
		return FALSE;
	}

	/* Receive ROM data Response */
	if (!elan_ts_hid_read_data(device, 
				   cmd_data, 
				   sizeof(cmd_data), 
				   FALSE,
				   error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to receive ROM data: ");
		return FALSE;
	}

	/* 
	 * Validate Response Header. 
	 * The device should respond with 0x95 followed by the data.
	 */
	if (cmd_data[0] != 0x95) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "invalid ROM data header: 0x%02x (expected 0x95)", cmd_data[0]);
		return FALSE;
	}

	/* Load 2-byte ROM data from the response (typically at index 3 and 4) */
	*p_rom_data = (guint16)((cmd_data[3] << 8) | cmd_data[4]);
	ELAN_TS_DEBUG(debug_setting, "ROM data at 0x%04x: 0x%04x", address, *p_rom_data);

	return TRUE;
}

/**
 * elan_ts_hid_read_remark_id:
 * @device: a #FuDevice
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
elan_ts_hid_read_remark_id(FuDevice *device,
			   FuElanTsState touch_state,
			   guint16 fw_version,
			   guint16 bc_version,
			   guint16 *p_remark_id,
			   GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	/* Basic sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* Check if the output pointer is valid */
	if (p_remark_id == NULL) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "invalid parameter: p_remark_id is NULL");
		return FALSE;
	}

	/* 
	 * Use the generic ROM read function to fetch the Remark ID.
	 * ELAN_TS_MEM_REMARK_ID_ADDR (0x801F) is the specific memory offset 
	 * for hardware identification.
	 */
	if (!elan_ts_hid_get_rom_data(device,
				      touch_state,
				      fw_version,
				      bc_version,
				      ELAN_TS_MEM_REMARK_ID_ADDR,
				      p_remark_id,
				      error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read Remark ID from ROM: ");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "Successfully obtained Remark ID: 0x%04x", *p_remark_id);

	return TRUE;
}

/**
 * elan_ts_hid_read_page_data:
 * @device: a #FuDevice
 * @mem_addr: Target memory address
 * @p_page_data_buf: (out): Memory page data buffer
 * @page_data_buf_size: Length of @p_page_data_buf
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads a single memory page from the device using fragmented HID read frames.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
static gboolean
elan_ts_hid_read_page_data(FuDevice *device,
			   guint16 mem_addr,
			   guint8 *p_page_data_buf,
			   gsize page_data_buf_size,
			   GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);
	guint8 show_bulk_rom_data_cmd[6] = {0x59, 0x10, 0x00, 0x00, 0x00, 0x00};
	guint8 data_buf[ELAN_TS_HID_DATA_BUFFER_SIZE] = {0};
	guint page_frame_count = 0;
	gsize page_data_size_words = page_data_buf_size / 2;
	gsize page_frame_data_len = 0;
	gsize data_len = 0;
	gsize page_frame_index = 0;
	gsize page_data_index = 0;

	/* Basic sanity checks */
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(device), FALSE);
	g_return_val_if_fail(p_page_data_buf != NULL, FALSE);
	g_return_val_if_fail(page_data_buf_size == ELAN_TS_MEMORY_PAGE_SIZE, FALSE);

	/* Send Show Bulk ROM Data Command */
	fu_memwrite_uint16(show_bulk_rom_data_cmd + 2, mem_addr, G_BIG_ENDIAN);
	fu_memwrite_uint16(show_bulk_rom_data_cmd + 4, (guint16)page_data_size_words, G_BIG_ENDIAN);

	if (!elan_ts_hid_write_command(device, show_bulk_rom_data_cmd, sizeof(show_bulk_rom_data_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send Show Bulk ROM Command: ");
		return FALSE;
	}

	g_usleep(20 * 1000);

	/* Fragmented read loop */
	page_frame_count = (guint)((page_data_buf_size + (ELAN_TS_HID_READ_PAGE_FRAME_SIZE - 1)) / 
		           ELAN_TS_HID_READ_PAGE_FRAME_SIZE);

	for (page_frame_index = 0; page_frame_index < page_frame_count; page_frame_index++) {
		if (page_frame_index == (page_frame_count - 1)) {
			page_frame_data_len = page_data_buf_size - page_data_index;
		} else {
			page_frame_data_len = ELAN_TS_HID_READ_PAGE_FRAME_SIZE;
		}

		data_len = 3 + page_frame_data_len;

		if (!elan_ts_hid_read_data(device, data_buf, data_len, TRUE, error)) {
			ELAN_TS_ERROR(debug_setting, error, "failed to read frame %" G_GSIZE_FORMAT ": ", page_frame_index);
			return FALSE;
		}

		/* 
		 * Validate Response Header.
		 * The device must respond with 0x99 for ROM bulk data packets.
		 */
		if (data_buf[0] != 0x99) {
			ELAN_TS_SET_ERROR(debug_setting, error, FWUPD_ERROR_INVALID_DATA,
					  "invalid ROM page response header: 0x%02x (expected 0x99)", data_buf[0]);
			return FALSE;
		}

		memcpy(p_page_data_buf + page_data_index, &data_buf[3], page_frame_data_len);
		page_data_index += page_frame_data_len;
	}
	
	if (page_data_index != page_data_buf_size) {
		ELAN_TS_SET_ERROR(debug_setting, error, FWUPD_ERROR_INVALID_DATA,
				  "read size mismatch: expected %" G_GSIZE_FORMAT ", got %" G_GSIZE_FORMAT,
				  page_data_buf_size, page_data_index);
		return FALSE;
	}

	return TRUE;
}

/**
 * elan_ts_hid_read_info_page:
 * @self: a #FuDevice
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
elan_ts_hid_read_info_page(FuDevice *self,
			   guint8 *p_info_page_buf,
			   gsize info_page_buf_size,
			   GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(self);

	/* Basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(p_info_page_buf != NULL, FALSE);
	g_return_val_if_fail(info_page_buf_size == ELAN_TS_MEMORY_PAGE_SIZE, FALSE);

	/* Enter Test Mode */
	if (!elan_ts_hid_set_test_mode(self, TRUE, error)) {
		/* Error is already set by elan_ts_hid_set_test_mode */
		return FALSE;
	}

	/* Read Information Page (Target: ELAN_TS_MEM_INFO_PAGE_1_ADDR) */
	if (!elan_ts_hid_read_page_data(self,
				       ELAN_TS_MEM_INFO_PAGE_1_ADDR,
				       p_info_page_buf,
				       info_page_buf_size,
				       error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read info page: ");

		/* Attempt to exit test mode even if read fails to restore state */
		elan_ts_hid_set_test_mode(self, FALSE, NULL);
		return FALSE;
	}

	/* Leave Test Mode */
	if (!elan_ts_hid_set_test_mode(self, FALSE, error)) {
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "Successfully retrieved Information Page");
	return TRUE;
}

/**
 * elan_ts_hid_read_info_page_retry_cb:
 * @self: a #FuDevice
 * @user_data: a #ElanTsInfoPageHelper pointer
 * @error: (nullable): a #GError, or %NULL
 *
 * Callback used by fu_device_retry_full to attempt reading the Info Page.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 */
static gboolean
elan_ts_hid_read_info_page_retry_cb(FuDevice *self, gpointer user_data, GError **error)
{
	ElanTsInfoPageHelper *helper = (ElanTsInfoPageHelper *)user_data;

	/* Call the underlying page read function using the updated name */
	return elan_ts_hid_read_info_page(self,
					  helper->info_page_buf,
					  sizeof(helper->info_page_buf),
					  error);
}

/**
 * elan_ts_hid_read_info_page_with_retry:
 * @self: a #FuDevice
 * @p_info_page_buf: (out): Buffer to store the information page (must be 128 bytes)
 * @info_page_buf_size: Size of the buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * High-level function to retrieve the Info Page with an automatic retry mechanism.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_read_info_page_with_retry(FuDevice *self,
				      guint8 *p_info_page_buf,
				      gsize info_page_buf_size,
				      GError **error)
{
	ElanTsInfoPageHelper helper = {0};
	guint32 debug_setting = elan_ts_hid_get_debug_setting(self);

	/* Basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(p_info_page_buf != NULL, FALSE);
	g_return_val_if_fail(info_page_buf_size == ELAN_TS_MEMORY_PAGE_SIZE, FALSE);

	ELAN_TS_DEBUG(debug_setting, "Attempting to read Info Page with retry mechanism...");

	/* Invoke the fwupd retry framework using the updated callback name */
	if (!fu_device_retry_full(self,
				  elan_ts_hid_read_info_page_retry_cb,
				  ELAN_TS_DEFAULT_ERROR_RETRY_COUNT,
				  ELAN_TS_DEFAULT_RETRY_INTERVAL_MS,
				  &helper,
				  error)) {
		ELAN_TS_ERROR(debug_setting, error, "all attempts to read Info Page failed: ");
		return FALSE;
	}

	/* Success: Copy the retrieved page back to the caller's buffer */
	memcpy(p_info_page_buf, helper.info_page_buf, ELAN_TS_MEMORY_PAGE_SIZE);

	ELAN_TS_DEBUG(debug_setting, "Successfully read Info Page after retries.");
	return TRUE;
}

/**
 * elan_ts_hid_unlock_flash:
 * @self: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the Write Flash Key command to unlock the flash memory for writing.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_hid_unlock_flash(FuDevice *self, GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(self);
	const guint8 write_flash_key_cmd[] = {0x54, 0xC0, 0xE1, 0x5A};

	/* Basic sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);

	/* Unlock Flash by sending the Write Flash Key command */
	if (!elan_ts_hid_write_command(self, write_flash_key_cmd, sizeof(write_flash_key_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send Write Flash Key command: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * elan_ts_hid_enter_iap_mode:
 * @self: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the command to trigger the device to enter IAP (Bootloader) mode.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_hid_enter_iap_mode(FuDevice *self, GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(self);
	const guint8 enter_iap_cmd[] = {0x54, 0x00, 0x12, 0x34};

	/* Basic sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);

	/* Enter IAP Mode by sending Enter IAP command (This command is bypassed in Recovery mode) */
	if (!elan_ts_hid_write_command(self, enter_iap_cmd, sizeof(enter_iap_cmd), error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send Enter IAP mode command: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * elan_ts_hid_check_slave_address:
 * @self: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Verifies Bootloader readiness by performing a slave address handshake.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_hid_check_slave_address(FuDevice *self, GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(self);
	guint8 slave_addr_7bit = (guint8)ELAN_TS_I2C_SLAVE_ADDR_7BIT;
	guint8 data = 0;

	/* Send 7-bit Slave Address as a trigger command to the bootloader */
	if (!elan_ts_hid_write_command(self, &slave_addr_7bit, 1, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send 7-bit addr handshake: ");
		return FALSE;
	}

	/* 
	 * Read back the 1-byte response. 
	 * filter=TRUE to strip HID headers and retrieve the raw address byte. 
	 */
	if (!elan_ts_hid_read_data(self, &data, 1, TRUE, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to read ID pattern from HID report: ");
		return FALSE;
	}

	/* Validate the returned byte against the expected 8-bit Slave Address (0x20) */
	if (data != ELAN_TS_I2C_SLAVE_ADDR) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_WRITE,
				  "ID mismatch! Expected 0x%02x, got 0x%02x",
				  (guint)ELAN_TS_I2C_SLAVE_ADDR,
				  data);
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "Slave address 0x%02x verified", data);
	return TRUE;
}

/**
 * elan_ts_hid_write_frame_data:
 * @device: a #FuDevice
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
elan_ts_hid_write_frame_data(FuDevice *device,
                            guint16 data_offset,
                            gsize data_len,
                            const guint8 *p_frame_buf,
                            GError **error)
{
	/* Use the unified fixed-size output report buffer */
	guint8 out_report_buf[ELAN_TS_OUTPUT_REPORT_SIZE] = {0x00};
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_frame_buf != NULL, FALSE);

	/* 
	 * Ensure payload fits: 
	 * 1 byte (Report ID) + 4 bytes (IAP Header) + data_len must be within limits.
	 */
	if ((5 + data_len) > ELAN_TS_OUTPUT_REPORT_SIZE) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "Frame payload %u exceeds ELAN_TS_OUTPUT_REPORT_SIZE",
				  (guint)data_len);
		return FALSE;
	}

	/* [Byte 0] HID Output Report ID (0x03) */
	out_report_buf[0] = ELAN_TS_HID_OUTPUT_REPORT_ID;

	/* [Byte 1] IAP Sub-command for frame data */
	out_report_buf[1] = 0x21;

	/* [Byte 2..3] Data Offset (Big-Endian) */
	out_report_buf[2] = (guint8)((data_offset >> 8) & 0xFF);
	out_report_buf[3] = (guint8)(data_offset & 0xFF);

	/* [Byte 4] Data Length */
	out_report_buf[4] = (guint8)data_len;

	/* [Byte 5..n] Copy raw firmware data into the report */
	memcpy(&out_report_buf[5], p_frame_buf, data_len);

	/* 
	 * Send the fixed-length report via the retry framework.
	 * The entire out_report_buf (ELAN_TS_OUTPUT_REPORT_SIZE) is sent.
	 */
	return elan_ts_hidraw_write_with_retry(device,
					       out_report_buf,
					       sizeof(out_report_buf),
					       error);
}

/**
 * elan_ts_hid_send_flash_write_command:
 * @device: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Sends the vendor-specific command (0x22) to trigger the flash 
 * burning process on the touch controller.
 *
 * Returns: %TRUE for success, %FALSE for failure
 */
gboolean
elan_ts_hid_send_flash_write_command(FuDevice *device, GError **error)
{
	/* Based on reference: unsigned char write_to_flash_cmd = 0x22; */
	guint8 write_to_flash_cmd = 0x22;
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	/* Basic sanity check */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);

	ELAN_TS_DEBUG(debug_setting, "%s: Sending Flash Write trigger (0x%02x)...", 
	              G_STRFUNC, write_to_flash_cmd);

	/* 
	 * Use the vendor command wrapper which:
	 * 1. Wraps the payload with HID Output Report ID (0x03)
	 * 2. Sends it as a fixed-length report (ELAN_TS_OUTPUT_REPORT_SIZE)
	 */
	if (!elan_ts_hid_write_vendor_command(device, 
					      &write_to_flash_cmd, 
					      sizeof(write_to_flash_cmd), 
					      error)) {
		ELAN_TS_ERROR(debug_setting, error, "Failed to write vendor command 0x%02x: ", 
		              write_to_flash_cmd);
		return FALSE;
	}

	return TRUE;
}

/**
 * elan_ts_hid_read_flash_write_response:
 * @device: a #FuDevice
 * @p_response: (out): stores the combined 16-bit response value
 * @error: (nullable): a #GError, or %NULL
 *
 * Reads the flash write response from the device and validates the pattern.
 * Expected pattern is 0xAA 0xAA.
 *
 * Returns: %TRUE for success, %FALSE for failure or pattern mismatch.
 */
gboolean
elan_ts_hid_read_flash_write_response(FuDevice *device, guint16 *p_response, GError **error)
{
	guint8 flash_write_response_data[2] = {0};
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);

	/* Basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(p_response != NULL, FALSE);

	/* Read Flash Write Response (using retry-enabled read wrapper with filter=TRUE) */
	if (!elan_ts_hid_read_data(device,
				   flash_write_response_data,
				   sizeof(flash_write_response_data),
				   TRUE,
				   error)) {
		ELAN_TS_ERROR(debug_setting, error, "Fail to receive Flash Write Response data: ");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting,
		      "flash_write_response: 0x%02x, 0x%02x",
		      flash_write_response_data[0],
		      flash_write_response_data[1]);

	/* Combine bytes into a 16-bit word */
	*p_response = (guint16)(flash_write_response_data[0] << 8) | flash_write_response_data[1];

	/* Check if Correct Response (0xAAAA) */
	if ((flash_write_response_data[0] != 0xAA) || (flash_write_response_data[1] != 0xAA)) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "Unknown Response: 0x%02x 0x%02x (expected 0xAAAA)",
				  flash_write_response_data[0],
				  flash_write_response_data[1]);
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_hid_device_recalibrate:
 * @device: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Triggers the hardware re-calibration process using encapsulated I/O helpers.
 * The process involves sending a flash key followed by the REK command,
 * and finally validating the 0x66666666 success pattern.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
static gboolean
fu_elan_ts_hid_device_recalibrate(FuDevice *device, GError **error)
{
	guint32 debug_setting = elan_ts_hid_get_debug_setting(device);
	guint8 write_flash_key_cmd[] = {0x54, 0xc0, 0xe1, 0x5a};
	guint8 rek_cmd[] = {0x54, 0x29, 0x00, 0x01};
	guint8 cmd_data[4] = {0};

	ELAN_TS_DEBUG(debug_setting, "Starting touch re-calibration...");

	/* Send Write Flash Key Command */
	if (!elan_ts_hid_write_command(device,
				       write_flash_key_cmd,
				       sizeof(write_flash_key_cmd),
				       error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send Write Flash Key command: ");
		return FALSE;
	}

	/* Send Re-Calibration Command (Re-K) */
	if (!elan_ts_hid_write_command(device,
				       rek_cmd,
				       sizeof(rek_cmd),
				       error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to send Re-calibration command: ");
		return FALSE;
	}

	/* Receive and Verify Calibration Response */
	ELAN_TS_DEBUG(debug_setting, "waiting for calibration response...");
	if (!elan_ts_hid_read_data(device, cmd_data, sizeof(cmd_data), TRUE, error)) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_READ,
				  "failed to receive re-calibration response: %s",
				  (*error)->message);
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "received command response: 0x%02x, 0x%02x, 0x%02x, 0x%02x",
		      cmd_data[0], cmd_data[1], cmd_data[2], cmd_data[3]);

	/* Check if the response is the expected success pattern (0x66 0x66 0x66 0x66) */
	if ((cmd_data[0] != 0x66) || (cmd_data[1] != 0x66) || (cmd_data[2] != 0x66) || (cmd_data[3] != 0x66)) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "re-calibration failed (invalid response: 0x%02x 0x%02x 0x%02x 0x%02x)",
				  cmd_data[0], cmd_data[1], cmd_data[2], cmd_data[3]);
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "Re-calibration success");
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
	/* Call the atomic calibration function we just finalized */
	return fu_elan_ts_hid_device_recalibrate(device, error);
}

/**
 * fu_elan_ts_hid_device_recalibrate_with_retry:
 * @device: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Triggers the hardware re-calibration process with an automatic retry mechanism
 * using the fwupd retry framework.
 *
 * Returns: %TRUE for success, %FALSE if all retry attempts have failed.
 **/
gboolean
fu_elan_ts_hid_device_recalibrate_with_retry(FuDevice *device, GError **error)
{
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);

	/* 
	 * Invoke the retry framework. 
	 * Since recalibrate doesn't need to pass back data through a helper struct,
	 * we pass NULL for user_data.
	 */
	if (!fu_device_retry_full(device,
				  fu_elan_ts_hid_recalibrate_retry_cb,
				  ELAN_TS_IO_MAX_RETRIES,
				  ELAN_TS_DEFAULT_RETRY_INTERVAL_MS,
				  NULL,
				  error)) {
		return FALSE;
	}

	return TRUE;
}


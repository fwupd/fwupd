/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-device.h"
#include "fu-elan-ts-firmware.h"

struct _FuElanTsDevice {
	FuHidrawDevice parent_instance;
	FuElanTsState touch_state; /* operating mode state */
	guint16 bc_version;	   /* boot code */
	guint16 fw_id;
	guint16 remark_id;
	guint16 fw_version;
	guint16 test_version;
};

G_DEFINE_TYPE(FuElanTsDevice, fu_elan_ts_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_ELAN_TS_MEMORY_PAGE_SIZE 128

#define FU_ELAN_TS_REMARK_ID_NONE 0xFFFF

#define FU_ELAN_TS_OUTPUT_REPORT_SIZE 33
#define FU_ELAN_TS_INPUT_REPORT_SIZE  65

#define FU_ELAN_TS_IO_MAX_RETRIES 3

#define FU_ELAN_TS_REPORT_ID_OUTPUT_BRIDGE 0x00
#define FU_ELAN_TS_REPORT_ID_INPUT_BRIDGE  0x00

#define FU_ELAN_TS_READ_PAGE_FRAME_SIZE 0x3C

#define FU_ELAN_TS_PAGE_FRAME_SIZE 0x1C

#define FU_ELAN_TS_FW_PAGE_DATA_SIZE  128 /* bytes */
#define FU_ELAN_TS_FW_PAGES_PER_BLOCK 30  /* maximum pages per write cycle */

static gboolean
fu_elan_ts_device_write_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	GByteArray *buf = (GByteArray *)user_data;
	g_autoptr(GByteArray) buf_mut = g_byte_array_new();

	fu_byte_array_append_array(buf_mut, buf);
	fu_byte_array_set_size(buf_mut, FU_ELAN_TS_OUTPUT_REPORT_SIZE, 0x0);
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   buf_mut->data,
					   buf_mut->len,
					   FU_IO_CHANNEL_FLAG_NONE,
					   error);
}

static gboolean
fu_elan_ts_device_write(FuElanTsDevice *self, GByteArray *buf, GError **error)
{
	/* sanity check */
	if (buf->len > FU_ELAN_TS_OUTPUT_REPORT_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "length %u exceeds fixed report size %u",
			    buf->len,
			    (guint)FU_ELAN_TS_OUTPUT_REPORT_SIZE);
		return FALSE;
	}
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_elan_ts_device_write_cb,
				    FU_ELAN_TS_IO_MAX_RETRIES,
				    0,
				    buf,
				    error);
}

static gboolean
fu_elan_ts_device_write_command(FuElanTsDevice *self, GByteArray *buf, GError **error)
{
	g_autoptr(FuStructElanTsHidWriteCommand) st = fu_struct_elan_ts_hid_write_command_new();
	if (buf->len > G_MAXUINT8) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "length invalid");
		return FALSE;
	}
	if (fu_device_get_pid(FU_DEVICE(self)) == FU_ELAN_TS_PID_BRIDGE ||
	    fu_device_get_pid(FU_DEVICE(self)) == FU_ELAN_TS_PID_BRIDGE_B) {
		fu_struct_elan_ts_hid_write_command_set_report_id(
		    st,
		    FU_ELAN_TS_REPORT_ID_OUTPUT_BRIDGE);
	}
	fu_struct_elan_ts_hid_write_command_set_data_len(st, buf->len);
	fu_byte_array_append_array(st->buf, buf);
	return fu_elan_ts_device_write(self, st->buf, error);
}

static gboolean
fu_elan_ts_device_read_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	GByteArray *buf = (GByteArray *)user_data;
	return fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self),
					   buf->data,
					   buf->len,
					   FU_IO_CHANNEL_FLAG_NONE,
					   error);
}

static gboolean
fu_elan_ts_device_read_data_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	GByteArray *buf_mut = (GByteArray *)user_data;
	guint16 hid_pid = fu_device_get_pid(device);
	guint8 report_id = 0;
	guint8 report_id_expected = FU_ELAN_TS_REPORT_ID_INPUT;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_elan_ts_device_read_cb,
				  FU_ELAN_TS_IO_MAX_RETRIES,
				  0,
				  buf_mut,
				  error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf_mut->data, buf_mut->len, 0x0, &report_id, error))
		return FALSE;

	/* ignore interleaved runtime touch/pen input reports and retry */
	if (report_id == FU_ELAN_TS_REPORT_ID_FINGER || report_id == FU_ELAN_TS_REPORT_ID_PEN ||
	    report_id == FU_ELAN_TS_REPORT_ID_PEN_DEBUG) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "ignoring runtime touch/pen report 0x%02x during firmware update",
			    report_id);
		return FALSE;
	}

	/* standard expected command response */
	if ((hid_pid == FU_ELAN_TS_PID_BRIDGE) || (hid_pid == FU_ELAN_TS_PID_BRIDGE_B))
		report_id_expected = FU_ELAN_TS_REPORT_ID_INPUT_BRIDGE;
	if (report_id != report_id_expected) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid report id: 0x%02x",
			    report_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_elan_ts_device_read_data(FuElanTsDevice *self, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(GByteArray) buf_mut = g_byte_array_new();
	g_autoptr(FuStructElanTsInputReport) st_report = NULL;

	/* loop to read data, discarding asynchronous touch or pen reports */
	fu_byte_array_set_size(buf_mut, FU_ELAN_TS_INPUT_REPORT_SIZE, 0x0);
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_elan_ts_device_read_data_cb,
			     FU_ELAN_TS_IO_MAX_RETRIES,
			     buf_mut,
			     error))
		return NULL;

	/* return payload */
	st_report = fu_struct_elan_ts_input_report_parse(buf_mut->data, buf_mut->len, 0, error);
	if (st_report == NULL)
		return NULL;
	buf = fu_struct_elan_ts_input_report_get_payload(st_report, &bufsz);
	return g_bytes_new(buf, bufsz);
}

static gboolean
fu_elan_ts_device_ensure_bc_version_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	guint8 *hello_packet = (guint8 *)user_data;
	g_autoptr(FuStructElanTsVendorCmd) st_cmd = fu_struct_elan_ts_vendor_cmd_new();
	g_autoptr(FuStructElanTsHelloPktAndBcVerRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	fu_struct_elan_ts_vendor_cmd_set_cmd(st_cmd,
					     FU_ELAN_TS_VENDOR_CMD_READ_HELLO_PKT_AND_BC_VER);
	if (!fu_elan_ts_device_write(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send hello command: ");
		return FALSE;
	}

	/* read the response */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to read hello response: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_hello_pkt_and_bc_ver_rsp_parse_bytes(payload, 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse hello response structure: ");
		return FALSE;
	}

	/* extract values */
	if (hello_packet != NULL)
		*hello_packet = fu_struct_elan_ts_hello_pkt_and_bc_ver_rsp_get_hello_packet(st_rsp);
	self->bc_version = fu_struct_elan_ts_hello_pkt_and_bc_ver_rsp_get_bc_version(st_rsp);
	return TRUE;
}

static gboolean
fu_elan_ts_device_ensure_bc_version(FuElanTsDevice *self, guint8 *hello_packet, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_elan_ts_device_ensure_bc_version_cb,
				    FU_ELAN_TS_IO_MAX_RETRIES,
				    0,
				    hello_packet,
				    error);
}

static gboolean
fu_elan_ts_device_ensure_boot_code_version(FuElanTsDevice *self, GError **error)
{
	const guint8 *buf = NULL;
	gsize bufsz = 0;
	guint32 payload_value = 0;
	g_autoptr(FuStructElanTsBcVersionCmd) st_cmd = fu_struct_elan_ts_bc_version_cmd_new();
	g_autoptr(FuStructElanTsBcVersionRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* send the boot code version */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send BC version command: ");
		return FALSE;
	}

	/* read the response */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to read BC version data: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_bc_version_rsp_parse_bytes(payload, 0x0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse BC version response structure: ");
		return FALSE;
	}

	/* pattern check */
	buf = fu_struct_elan_ts_bc_version_rsp_get_payload(st_rsp, &bufsz);
	if ((buf[0] >> 4) != 0x01) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid BC version pattern");
		return FALSE;
	}

	/* read the 3-byte payload */
	if (!fu_memread_uint24_safe(buf, bufsz, 0x0, &payload_value, G_BIG_ENDIAN, error))
		return FALSE;
	self->bc_version = (guint16)((payload_value >> 4) & 0xFFFF);
	return TRUE;
}

static gboolean
fu_elan_ts_device_ensure_fw_id(FuElanTsDevice *self, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 payload_value = 0;
	g_autoptr(FuStructElanTsFwIdCmd) st_cmd = fu_struct_elan_ts_fw_id_cmd_new();
	g_autoptr(FuStructElanTsFwIdRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* send fw id command */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send FW ID command: ");
		return FALSE;
	}

	/* read the response */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to read FW ID response: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_fw_id_rsp_parse_bytes(payload, 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse FW ID response structure: ");
		return FALSE;
	}

	/* read the 3-byte payload */
	buf = fu_struct_elan_ts_fw_id_rsp_get_payload(st_rsp, &bufsz);
	if ((buf[0] >> 4) != 0x0F) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid FW ID pattern");
		return FALSE;
	}
	if (!fu_memread_uint24_safe(buf, bufsz, 0x0, &payload_value, G_BIG_ENDIAN, error))
		return FALSE;
	self->fw_id = (guint16)((payload_value >> 4) & 0xFFFF);
	return TRUE;
}

static gboolean
fu_elan_ts_device_ensure_fw_version(FuElanTsDevice *self, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 payload_value = 0;
	g_autoptr(FuStructElanTsFwVersionCmd) st_cmd = fu_struct_elan_ts_fw_version_cmd_new();
	g_autoptr(FuStructElanTsFwVersionRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* send fw version command */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send FW version command: ");
		return FALSE;
	}

	/* read the response */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to read FW version response: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_fw_version_rsp_parse_bytes(payload, 0x0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "failed to parse FW version response structure: ");
		return FALSE;
	}

	/* read the 3-byte payload */
	buf = fu_struct_elan_ts_fw_version_rsp_get_payload(st_rsp, &bufsz);
	if ((buf[0] >> 4) != 0x00) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid FW version pattern");
		return FALSE;
	}
	if (!fu_memread_uint24_safe(buf, bufsz, 0x0, &payload_value, G_BIG_ENDIAN, error))
		return FALSE;
	self->fw_version = (guint16)((payload_value >> 4) & 0xFFFF);
	return TRUE;
}

static gboolean
fu_elan_ts_device_ensure_test_solution_version(FuElanTsDevice *self, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 payload_value;
	g_autoptr(FuStructElanTsTestSolutionVersionCmd) st_cmd =
	    fu_struct_elan_ts_test_solution_version_cmd_new();
	g_autoptr(FuStructElanTsTestSolutionVersionRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* send test-solution version command */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send: ");
		return FALSE;
	}

	/* read the response */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to read: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_test_solution_version_rsp_parse_bytes(payload, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;

	/* read the 3-byte payload */
	buf = fu_struct_elan_ts_test_solution_version_rsp_get_payload(st_rsp, &bufsz);
	if ((buf[0] >> 4) != 0x0E) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid response");
		return FALSE;
	}
	if (!fu_memread_uint24_safe(buf, bufsz, 0x0, &payload_value, G_BIG_ENDIAN, error))
		return FALSE;
	self->test_version = (((payload_value >> 4) & 0xFFFF) & 0xFF00) >> 8;
	return TRUE;
}

static gboolean
fu_elan_ts_device_enable_test_mode_cb(FuDevice *device, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	g_autoptr(FuStructElanTsEnterTestModeCmd) st_cmd =
	    fu_struct_elan_ts_enter_test_mode_cmd_new();
	return fu_elan_ts_device_write_command(self, st_cmd->buf, error);
}

static gboolean
fu_elan_ts_device_disable_test_mode_cb(FuDevice *device, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	g_autoptr(FuStructElanTsExitTestModeCmd) st_cmd =
	    fu_struct_elan_ts_exit_test_mode_cmd_new();
	return fu_elan_ts_device_write_command(self, st_cmd->buf, error);
}

/* Gen6/Gen7 ICs require a specific information byte (0x21) in the ROM read command */
static gboolean
fu_elan_ts_device_is_gen6_gen7_ic(FuElanTsDevice *self)
{
	guint8 solution_id = 0;
	guint8 bc_ver_h = 0;

	if (self->touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		/* the high byte of the firmware version is the Solution ID */
		solution_id = (guint8)(self->fw_version >> 8);
		return ((solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X1) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X2) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_5015M) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_3915P) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6308X1) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X1) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X2) ||
			(solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7318X1));
	}

	/* we cannot access the Solution ID, so use the high byte of the Boot Code */
	bc_ver_h = (guint8)((self->bc_version & 0xFF00) >> 8);
	return ((bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTA6315X1) ||
		(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTH6315_TO_5015M) ||
		(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTH6315_TO_3915P) ||
		(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTA6308X1) ||
		(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTA7315X1) ||
		(bc_ver_h == FU_ELAN_TS_BC_VER_HIGH_BYTE_EKTH7318X1));
}

/* reads 2 bytes of data from device's rom at specified address */
static gboolean
fu_elan_ts_device_read_rom_data(FuElanTsDevice *self,
				guint16 address,
				guint16 *value,
				GError **error)
{
	FuElanTsReadRomCmdMode mode = FU_ELAN_TS_READ_ROM_CMD_MODE_EKTH53XX;
	g_autoptr(FuStructElanTsReadRomCmd) st_cmd = fu_struct_elan_ts_read_rom_cmd_new();
	g_autoptr(FuStructElanTsReadRomRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* mode is 0x21 for gen6/gen7 and 0x11 for legacy ics */
	if (fu_elan_ts_device_is_gen6_gen7_ic(self))
		mode = FU_ELAN_TS_READ_ROM_CMD_MODE_EKTH63XX_73XX;

	/* prepare command using the updated setter names */
	fu_struct_elan_ts_read_rom_cmd_set_mem_addr(st_cmd, address);
	fu_struct_elan_ts_read_rom_cmd_set_mode(st_cmd, mode);

	/* send read rom data command */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send read rom data command: ");
		return FALSE;
	}

	/* receive rom data response */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to receive rom data response: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_read_rom_rsp_parse_bytes(payload, 0x0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "invalid rom data response structure: ");
		return FALSE;
	}

	/* verify if the response echoes back the correct memory address */
	if (fu_struct_elan_ts_read_rom_rsp_get_mem_addr(st_rsp) != address) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "address mismatch in response: 0x%04x (expected 0x%04x)",
			    fu_struct_elan_ts_read_rom_rsp_get_mem_addr(st_rsp),
			    address);
		return FALSE;
	}

	/* verify if the response echoes back the correct mode */
	if (fu_struct_elan_ts_read_rom_rsp_get_mode(st_rsp) != mode) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "mode mismatch in response: 0x%02x (expected 0x%02x)",
			    fu_struct_elan_ts_read_rom_rsp_get_mode(st_rsp),
			    mode);
		return FALSE;
	}

	/* success */
	if (value != NULL)
		*value = fu_struct_elan_ts_read_rom_rsp_get_rom_data(st_rsp);
	return TRUE;
}

/* the Remark ID is essential for identifying hardware sub-models and ensuring
 * that the firmware image is fully compatible with the physical touch controller */
static gboolean
fu_elan_ts_device_ensure_remark_id(FuElanTsDevice *self, GError **error)
{
	if (!fu_elan_ts_device_read_rom_data(self,
					     FU_ELAN_TS_MEM_ADDR_REMARK_ID,
					     &self->remark_id,
					     error)) {
		g_prefix_error_literal(error, "failed to read remark id from ROM: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_read_page_data(FuElanTsDevice *self,
				 guint16 mem_addr,
				 GByteArray *buf,
				 GError **error)
{
	g_autoptr(FuStructElanTsShowBulkRomDataCmd) st_cmd =
	    fu_struct_elan_ts_show_bulk_rom_data_cmd_new();
	g_autoptr(GPtrArray) chunks = NULL;

	/* prepare command fields */
	fu_struct_elan_ts_show_bulk_rom_data_cmd_set_mem_addr(st_cmd, mem_addr);
	fu_struct_elan_ts_show_bulk_rom_data_cmd_set_data_size_words(st_cmd,
								     (guint16)(buf->len / 2));
	fu_struct_elan_ts_show_bulk_rom_data_cmd_set_mode(st_cmd,
							  FU_ELAN_TS_SHOW_BULK_ROM_MODE_MAIN_CODE);

	/* send show bulk rom data command */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send show bulk rom command: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 20);

	/* fragmented read loop */
	chunks = fu_chunk_array_mutable_new(buf->data,
					    buf->len,
					    0x0,
					    FU_CHUNK_PAGESZ_NONE,
					    FU_ELAN_TS_READ_PAGE_FRAME_SIZE,
					    error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		const guint8 *rsp_buf;
		gsize page_frame_data_len = fu_chunk_get_data_sz(chk);
		gsize rsp_bufsz = 0;
		gsize rsp_data_size_words;
		g_autoptr(FuStructElanTsShowBulkRomRsp) st_rsp = NULL;
		g_autoptr(GBytes) payload = NULL;

		/* calculate dynamic read length to avoid hardware timeout on last fragment */
		payload = fu_elan_ts_device_read_data(self, error);
		if (payload == NULL) {
			g_prefix_error(error, "failed to read frame %u: ", i);
			return FALSE;
		}
		st_rsp = fu_struct_elan_ts_show_bulk_rom_rsp_parse_bytes(payload, 0x0, error);
		if (st_rsp == NULL) {
			g_prefix_error_literal(error, "invalid rom page response structure: ");
			return FALSE;
		}

		/* validate against expected byte length */
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

		/* copy frame data to page buffer */
		rsp_buf = fu_struct_elan_ts_show_bulk_rom_rsp_get_data(st_rsp, &rsp_bufsz);
		if (!fu_memcpy_safe(fu_chunk_get_data_out(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* dst_offset */
				    rsp_buf,
				    rsp_bufsz,
				    0, /* src_offset: now 0 because rustgen strips metadata */
				    page_frame_data_len, /* n */
				    error)) {
			g_prefix_error_literal(error, "failed to copy frame data: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_read_info_page_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	GByteArray *buf = (GByteArray *)user_data;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* enter test mode */
	locker = fu_device_locker_new_full(device,
					   fu_elan_ts_device_enable_test_mode_cb,
					   fu_elan_ts_device_disable_test_mode_cb,
					   error);
	if (locker == NULL)
		return FALSE;

	/* read information page */
	if (!fu_elan_ts_device_read_page_data(self, FU_ELAN_TS_MEM_ADDR_PAGE1, buf, error))
		return FALSE;

	/* exit test mode */
	return fu_device_locker_close(locker, error);
}

static GByteArray *
fu_elan_ts_device_read_info_page(FuElanTsDevice *self, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, FU_ELAN_TS_MEMORY_PAGE_SIZE, 0x0);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_elan_ts_device_read_info_page_cb,
				  3,
				  0,
				  buf,
				  error)) {
		g_prefix_error_literal(error, "failed to read information page: ");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_elan_ts_device_unlock_flash(FuElanTsDevice *self, GError **error)
{
	g_autoptr(FuStructElanTsWriteFlashKeyCmd) st_cmd =
	    fu_struct_elan_ts_write_flash_key_cmd_new();

	/* unlock flash */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send Write Flash Key command: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_enter_iap_mode(FuElanTsDevice *self, GError **error)
{
	g_autoptr(FuStructElanTsEnterIapCmd) st_cmd = fu_struct_elan_ts_enter_iap_cmd_new();

	/* enter IAP mode */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send enter IAP mode command: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* verifies bootloader readiness by performing a i2c address handshake */
static gboolean
fu_elan_ts_device_check_i2c_address(FuElanTsDevice *self, GError **error)
{
	g_autoptr(FuStructElanTsI2cAddrCmd) st_cmd = fu_struct_elan_ts_i2c_addr_cmd_new();
	g_autoptr(FuStructElanTsI2cAddrRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* send 7-bit I2C address to the bootloader */
	if (!fu_elan_ts_device_write_command(self, st_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send 7-bit addr handshake: ");
		return FALSE;
	}

	/* read back response */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to read ID pattern from HID report: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_i2c_addr_rsp_parse_bytes(payload, 0x0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "i2c address handshake failed (ID mismatch): ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_send_flash_write_command(FuElanTsDevice *self, GError **error)
{
	g_autoptr(FuStructElanTsVendorCmd) st_cmd = fu_struct_elan_ts_vendor_cmd_new();

	fu_struct_elan_ts_vendor_cmd_set_cmd(st_cmd, FU_ELAN_TS_VENDOR_CMD_FLASH_WRITE);
	if (!fu_elan_ts_device_write(self, st_cmd->buf, error)) {
		g_prefix_error(error,
			       "failed to write vendor command 0x%02x: ",
			       st_cmd->buf->data[0]);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_read_flash_write_response(FuElanTsDevice *self, GError **error)
{
	g_autoptr(FuStructElanTsFlashWriteRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* read flash write response with filter */
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "fail to receive flash write response data: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_flash_write_rsp_parse_bytes(payload, 0x0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "invalid flash write response pattern: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));

	/* ensure the device belongs to the functional hidraw interface */
	if (g_strcmp0(subsystem, "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device has incorrect subsystem %s, expected hidraw",
			    (subsystem != NULL) ? subsystem : "(null)");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_setup(FuDevice *device, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	guint8 hello_packet = 0;
	g_autofree gchar *version = NULL;
	g_autofree gchar *summary = NULL;

	/* make look nicer */
	if ((fu_device_get_name(device) == NULL) ||
	    (g_str_has_prefix(fu_device_get_name(device), "hidraw"))) {
		fu_device_set_name(device, "Elan Touchscreen");
	}

	/* hardware probe */
	if (!fu_elan_ts_device_ensure_bc_version(self, &hello_packet, error)) {
		g_prefix_error_literal(error, "hardware communication test failed: ");
		return FALSE;
	}
	g_debug("hello packet: 0x%02x", hello_packet);

	/* mode-specific: identify hw series and touch state based on hello packet */
	switch (hello_packet) {
	case FU_ELAN_TS_HELLO_PACKET_NORMAL_MODE:
		g_debug("normal mode detected");

		/* update internal data */
		self->touch_state = FU_ELAN_TS_STATE_NORMAL_MODE;
		if (!fu_elan_ts_device_ensure_boot_code_version(self, error)) {
			g_prefix_error_literal(error, "failed to read bc version in normal mode: ");
			return FALSE;
		}
		if (!fu_elan_ts_device_ensure_fw_id(self, error)) {
			g_prefix_error_literal(error, "failed to read fw id in normal mode: ");
			return FALSE;
		}
		if (!fu_elan_ts_device_ensure_fw_version(self, error)) {
			g_prefix_error_literal(error, "failed to read fw version in normal mode: ");
			return FALSE;
		}
		if (!fu_elan_ts_device_ensure_test_solution_version(self, error)) {
			g_prefix_error_literal(
			    error,
			    "failed to read test-solution version in normal mode: ");
			return FALSE;
		}
		if (!fu_elan_ts_device_ensure_remark_id(self, error)) {
			g_prefix_error_literal(error, "failed to get remark id in normal mode: ");
			return FALSE;
		}

		/* display combined fw and test versions in the main version field */
		version = g_strdup_printf("%x.%x", self->fw_version, self->test_version);
		fu_device_set_version(device, version);
		summary = g_strdup_printf("FWID: 0x%04x", self->fw_id);
		fu_device_set_summary(device, summary);
		break;

	case FU_ELAN_TS_HELLO_PACKET_RECOVERY_MODE:
		self->touch_state = FU_ELAN_TS_STATE_RECOVERY_MODE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

		/* update internal data */
		if (!fu_elan_ts_device_ensure_remark_id(self, error)) {
			g_prefix_error_literal(error, "failed to get remark id in recovery mode: ");
			return FALSE;
		}

		fu_device_set_version(device, "0");
		summary = g_strdup_printf("Elan Touchscreen (Recovery Mode)");
		fu_device_set_summary(device, summary);
		break;

	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown hello packet (0x%02x)",
			    hello_packet);
		return FALSE;
	}

	/* set boot code version as the bootloader version */
	fu_device_set_version_bootloader_raw(device, self->bc_version);

	/* success */
	return TRUE;
}

static void
fu_elan_ts_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	if (self->bc_version != 0)
		fwupd_codec_string_append_hex(str, idt, "BcVersion", self->bc_version);
	if (self->touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		if (self->fw_id != 0)
			fwupd_codec_string_append_hex(str, idt, "FwId", self->fw_id);
		if (self->remark_id != 0)
			fwupd_codec_string_append_hex(str, idt, "RemarkId", self->remark_id);
		if (self->fw_version != 0)
			fwupd_codec_string_append_hex(str, idt, "FwVersion", self->fw_version);
		fwupd_codec_string_append_hex(str, idt, "TestVersion", self->test_version);
	}
}

static gboolean
fu_elan_ts_device_recalibrate_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	g_autoptr(FuStructElanTsWriteFlashKeyCmd) st_flash_key_cmd =
	    fu_struct_elan_ts_write_flash_key_cmd_new();
	g_autoptr(FuStructElanTsRekCmd) st_rek_cmd = fu_struct_elan_ts_rek_cmd_new();
	g_autoptr(FuStructElanTsCalibrationRsp) st_rsp = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* send write flash key command */
	if (!fu_elan_ts_device_write_command(self, st_flash_key_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send write flash key command: ");
		return FALSE;
	}

	/* send re-calibration command (Re-K) */
	if (!fu_elan_ts_device_write_command(self, st_rek_cmd->buf, error)) {
		g_prefix_error_literal(error, "failed to send re-calibration command: ");
		return FALSE;
	}
	payload = fu_elan_ts_device_read_data(self, error);
	if (payload == NULL) {
		g_prefix_error_literal(error, "failed to receive re-calibration response: ");
		return FALSE;
	}
	st_rsp = fu_struct_elan_ts_calibration_rsp_parse_bytes(payload, 0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "re-calibration failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_recalibrate(FuElanTsDevice *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_elan_ts_device_recalibrate_cb,
				    FU_ELAN_TS_IO_MAX_RETRIES,
				    0,
				    NULL,
				    error);
}

static gboolean
fu_elan_ts_device_reload(FuDevice *device, GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);

	if (!fu_elan_ts_device_setup(device, error))
		return FALSE;
	if (!fu_elan_ts_device_recalibrate(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_elan_ts_device_iap_read_and_update_info_page(FuElanTsDevice *self, GError **error)
{
	g_autoptr(FuStructElanTsInfoPage) st = NULL;
	g_autoptr(GByteArray) buf = NULL;

	/* get current fw update information */
	buf = fu_elan_ts_device_read_info_page(self, error);
	if (buf == NULL)
		return NULL;
	st = fu_struct_elan_ts_info_page_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return NULL;

	/* refresh fw update info */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATION_TAG)) {
		fu_struct_elan_ts_info_page_set_update_counter(st, 0x1);
		fu_struct_elan_ts_info_page_set_update_year(st, fu_common_to_bcd_u16(2026));
		fu_struct_elan_ts_info_page_set_update_month(st, fu_common_to_bcd_u8(6));
		fu_struct_elan_ts_info_page_set_update_day(st, fu_common_to_bcd_u8(3));
		fu_struct_elan_ts_info_page_set_update_hour(st, fu_common_to_bcd_u8(12));
		fu_struct_elan_ts_info_page_set_update_minute(st, fu_common_to_bcd_u8(0));
	} else {
		guint16 update_counter = fu_struct_elan_ts_info_page_get_update_counter(st);
		g_autoptr(GDateTime) dt_now = NULL;

		if (update_counter == 0xFFFF) {
			fu_struct_elan_ts_info_page_set_update_counter(st, 0x1);
		} else if (update_counter >= 0xFFFE) {
			g_debug("firmware update counter has reached 0xFFFE, clamping");
		} else {
			fu_struct_elan_ts_info_page_set_update_counter(st, update_counter + 1);
		}

		dt_now = g_date_time_new_now_utc();
		if (dt_now == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "failed to get current time");
			return NULL;
		}
		fu_struct_elan_ts_info_page_set_update_year(
		    st,
		    fu_common_to_bcd_u16((guint16)g_date_time_get_year(dt_now)));
		fu_struct_elan_ts_info_page_set_update_month(
		    st,
		    fu_common_to_bcd_u8((guint8)g_date_time_get_month(dt_now)));
		fu_struct_elan_ts_info_page_set_update_day(
		    st,
		    fu_common_to_bcd_u8((guint8)g_date_time_get_day_of_month(dt_now)));
		fu_struct_elan_ts_info_page_set_update_hour(
		    st,
		    fu_common_to_bcd_u8((guint8)g_date_time_get_hour(dt_now)));
		fu_struct_elan_ts_info_page_set_update_minute(
		    st,
		    fu_common_to_bcd_u8((guint8)g_date_time_get_minute(dt_now)));
	}

	/* success */
	return g_byte_array_ref(st->buf);
}

static gboolean
fu_elan_ts_device_iap_check_remark_id(FuElanTsDevice *self,
				      FuElanTsFirmware *firmware,
				      GError **error)
{
	guint16 remark_id_fw = 0;
	FuElanTsDebugSetting debug_setting = fu_elan_ts_firmware_get_debug_setting(firmware);

	/* firmware tells us to skip */
	if (debug_setting & FU_ELAN_TS_DEBUG_SETTING_SKIP_REMARK_ID_CHECK)
		return TRUE;

	/* get remark id from hardware rom - this is a mandatory prerequisite */
	if (self->remark_id == FU_ELAN_TS_REMARK_ID_NONE) {
		g_debug("non-remark ic (0x%04x), bypassing check", self->remark_id);
		return TRUE;
	}

	/* strict match required for remark ics */
	remark_id_fw = fu_elan_ts_firmware_get_remark_id(firmware);
	if (self->remark_id != remark_id_fw) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "remark ID mismatched (ROM: 0x%04x, FW: 0x%04x)",
			    self->remark_id,
			    remark_id_fw);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_check_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	gboolean remark_id_check = FALSE;

	/* check type */
	if (fu_elan_ts_firmware_get_fw_type(FU_ELAN_TS_FIRMWARE(firmware)) !=
	    FU_ELAN_TS_FW_TYPE_EKT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware format: %u (expected EKT)",
			    (guint)fu_elan_ts_firmware_get_fw_type(FU_ELAN_TS_FIRMWARE(firmware)));
		return FALSE;
	}

	/* remark id compatibility check */
	if (self->touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		guint8 iap_version = (guint8)(self->bc_version & 0x00FF);
		if (iap_version >= 0x60)
			remark_id_check = TRUE;
	} else {
		guint8 bc_hbyte = (guint8)((self->bc_version & 0xFF00) >> 8);
		guint8 bc_lbyte = (guint8)(self->bc_version & 0x00FF);
		if (bc_hbyte != bc_lbyte)
			remark_id_check = TRUE;
	}
	if (remark_id_check) {
		if (!fu_elan_ts_device_iap_check_remark_id(self,
							   FU_ELAN_TS_FIRMWARE(firmware),
							   error)) {
			g_prefix_error_literal(error, "remark id check failed: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_iap_switch_to_boot_code(FuElanTsDevice *self, gboolean recovery, GError **error)
{
	/* unlock flash */
	if (!fu_elan_ts_device_unlock_flash(self, error)) {
		g_prefix_error_literal(error, "failed to unlock flash: ");
		return FALSE;
	}

	/* trigger IAP entry */
	if (!recovery) {
		if (!fu_elan_ts_device_enter_iap_mode(self, error)) {
			g_prefix_error_literal(error, "failed to enter IAP mode: ");
			return FALSE;
		}
	} else {
		g_debug("device in recovery mode: already in IAP mode");
	}

	/* wait for hardware re-initialization */
	fu_device_sleep(FU_DEVICE(self), 15);

	/* verify bootloader responsiveness */
	if (!fu_elan_ts_device_check_i2c_address(self, error)) {
		g_prefix_error_literal(error, "device failed to respond if in boot mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_iap_write_frame(FuElanTsDevice *self,
				  guint16 offset,
				  const guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autoptr(FuStructElanTsHidWriteFrameData) st =
	    fu_struct_elan_ts_hid_write_frame_data_new();

	if (bufsz > G_MAXUINT8) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "length invalid");
		return FALSE;
	}
	fu_struct_elan_ts_hid_write_frame_data_set_offset(st, offset);
	fu_struct_elan_ts_hid_write_frame_data_set_data_len(st, bufsz);
	g_byte_array_append(st->buf, buf, bufsz);
	return fu_elan_ts_device_write(self, st->buf, error);
}

static gboolean
fu_elan_ts_device_write_page(FuElanTsDevice *self,
			     gsize base_address,
			     GBytes *blob,
			     FuProgress *progress,
			     GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(blob,
					       base_address,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_ELAN_TS_PAGE_FRAME_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_elan_ts_device_iap_write_frame(self,
						       fu_chunk_get_address(chk),
						       fu_chunk_get_data(chk),
						       fu_chunk_get_data_sz(chk),
						       error)) {
			g_prefix_error(error, "failed to send data frame %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_write_block(FuElanTsDevice *self,
			      GBytes *blob,
			      FuProgress *progress,
			      GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       5 * FU_ELAN_TS_PAGE_FRAME_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) blob_page = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		blob_page = fu_chunk_get_bytes(chk);
		if (!fu_elan_ts_device_write_page(self,
						  fu_chunk_get_address(chk),
						  blob_page,
						  fu_progress_get_child(progress),
						  error)) {
			g_prefix_error(error, "failed to write page %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* request hardware to execute flash write operation */
	if (!fu_elan_ts_device_send_flash_write_command(self, error)) {
		g_prefix_error_literal(error, "failed to send flash write command: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), fu_chunk_array_length(chunks) > 0 ? 360 : 15);
	if (!fu_elan_ts_device_read_flash_write_response(self, error)) {
		g_prefix_error_literal(error, "failed to read flash write response: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elan_ts_device_write_blocks(FuElanTsDevice *self,
			       GBytes *blob,
			       FuProgress *progress,
			       GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_ELAN_TS_FIRMWARE_PAGE_SIZE *
						   FU_ELAN_TS_FW_PAGES_PER_BLOCK);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) blob_block = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		blob_block = fu_chunk_get_bytes(chk);
		if (!fu_elan_ts_device_write_block(self,
						   blob_block,
						   fu_progress_get_child(progress),
						   error)) {
			g_prefix_error(error, "failed to write block %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_elan_ts_device_build_page(FuElanTsDevice *self, GByteArray *buf, GError **error)
{
	guint8 solution_id = (guint8)((self->fw_version & 0xFF00) >> 8);
	guint16 mem_addr;
	guint16 page_checksum = 0;
	g_autoptr(GByteArray) buf_mut = g_byte_array_new();

	/* determine write address based on solution id */
	if ((solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X1) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315X2) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_5015M) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6315_TO_3915P) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH6308X1) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X1) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7315X2) ||
	    (solution_id == FU_ELAN_TS_SOLUTION_ID_EKTH7318X1))
		mem_addr = FU_ELAN_TS_MEM_ADDR_INFO_PAGE_WRITE; /* 0x0040 */
	else
		mem_addr = FU_ELAN_TS_MEM_ADDR_PAGE1; /* 0x8040 */

	/* page address */
	fu_byte_array_append_uint16(buf_mut, mem_addr, G_LITTLE_ENDIAN);
	fu_byte_array_append_array(buf_mut, buf);

	/* if page address is 0x0040, use 0x8040 for checksum calculation */
	if (buf_mut->len % 2 != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "page must be aligned");
		return NULL;
	}
	for (guint i = 0; i < buf_mut->len; i += 2) {
		guint16 page_data = fu_memread_uint16(buf_mut->data + i, G_LITTLE_ENDIAN);
		if (i == 0 && page_data == FU_ELAN_TS_MEM_ADDR_INFO_PAGE_WRITE)
			page_data = FU_ELAN_TS_MEM_ADDR_PAGE1;
		page_checksum += page_data;
	}

	/* page checksum */
	fu_byte_array_append_uint16(buf_mut, page_checksum, G_LITTLE_ENDIAN);

	/* success */
	return g_bytes_new(buf_mut->data, buf_mut->len);
}

static gboolean
fu_elan_ts_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuElanTsDevice *self = FU_ELAN_TS_DEVICE(device);
	gboolean skip_info_data_update;
	g_autoptr(GByteArray) buf_info = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "init");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "information-page");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "restart");
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_STRICT_EMULATION_ORDER);

	/* retrieve firmware binary size and calculate pagination */
	blob = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (blob == NULL)
		return FALSE;

	/* prepare information page update for devices in normal mode */
	skip_info_data_update =
	    (fu_elan_ts_firmware_get_debug_setting(FU_ELAN_TS_FIRMWARE(firmware)) &
	     FU_ELAN_TS_DEBUG_SETTING_SKIP_INFO_DATA_UPDATE) != 0;
	if (self->touch_state == FU_ELAN_TS_STATE_NORMAL_MODE && !skip_info_data_update) {
		buf_info = fu_elan_ts_device_iap_read_and_update_info_page(self, error);
		if (buf_info == NULL) {
			g_prefix_error_literal(error, "failed to update information page: ");
			return FALSE;
		}
	}

	/* switch device to bootloader/IAP mode */
	if (!fu_elan_ts_device_iap_switch_to_boot_code(
		self,
		(self->touch_state != FU_ELAN_TS_STATE_NORMAL_MODE),
		error)) {
		g_prefix_error_literal(error, "failed to switch to bootloader: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write prepared information page if applicable */
	if (buf_info != NULL) {
		g_autoptr(GBytes) blob_tmp = NULL;

		blob_tmp = fu_elan_ts_device_build_page(self, buf_info, error);
		if (blob_tmp == NULL) {
			g_prefix_error_literal(error, "failed to build firmware page: ");
			return FALSE;
		}
		if (!fu_elan_ts_device_write_block(self,
						   blob_tmp,
						   fu_progress_get_child(progress),
						   error)) {
			g_prefix_error_literal(error, "failed to write information page: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* main firmware data transfer */
	if (!fu_elan_ts_device_write_blocks(self, blob, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the device performs an internal reset after the final block is written --
	 * it remains connected, but requires a cooldown period before new HID requests */
	fu_device_sleep(device, 1000);
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_elan_ts_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "reload");
}

static void
fu_elan_ts_device_init(FuElanTsDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ELAN_TS_FIRMWARE);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TABLET);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elants");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static void
fu_elan_ts_device_class_init(FuElanTsDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_elan_ts_device_probe;
	device_class->setup = fu_elan_ts_device_setup;
	device_class->to_string = fu_elan_ts_device_to_string;
	device_class->check_firmware = fu_elan_ts_device_check_firmware;
	device_class->write_firmware = fu_elan_ts_device_write_firmware;
	device_class->set_progress = fu_elan_ts_device_set_progress;
	device_class->reload = fu_elan_ts_device_reload;
}

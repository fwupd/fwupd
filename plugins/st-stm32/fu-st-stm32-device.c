/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-st-stm32-common.h"
#include "fu-st-stm32-device.h"
#include "fu-st-stm32-struct.h"

#define FU_ST_STM32_DEVICE_FLAG_NO_MASS_ERASE	    "no-mass-erase"
#define FU_ST_STM32_DEVICE_FLAG_CLEAR_PEMPTY	    "clear-pempty"
#define FU_ST_STM32_DEVICE_FLAG_OBL_LAUNCH_REQUIRED "obl-launch-required"

/* nocheck:magic-inlines=90 -- this is due to the ARM thumb shellcode */

struct _FuStStm32Device {
	FuUsbDevice parent_instance;
	guint32 sram_addr;
	guint32 sram_len;
	guint32 flash_addr;
	guint32 flash_len;
	guint32 pages_per_sector;
	guint32 page_size;
	guint32 option_addr;
	guint32 option_len;
	guint32 mem_addr;
	guint32 mem_len;
	GPtrArray *cmd_items; /* element-type FuStStm32DeviceCmdItem */
};

typedef struct {
	FuStStm32Cmd cmd_base;
	FuStStm32Cmd cmd;
} FuStStm32DeviceCmdItem;

G_DEFINE_TYPE(FuStStm32Device, fu_st_stm32_device, FU_TYPE_USB_DEVICE)

static void
fu_st_stm32_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "SramAddr", self->sram_addr);
	fwupd_codec_string_append_hex(str, idt, "SramLen", self->sram_len);
	fwupd_codec_string_append_hex(str, idt, "FlashAddr", self->flash_addr);
	fwupd_codec_string_append_hex(str, idt, "FlashLen", self->flash_len);
	fwupd_codec_string_append_hex(str, idt, "PagesPerSector", self->pages_per_sector);
	fwupd_codec_string_append_hex(str, idt, "PageSize", self->page_size);
	fwupd_codec_string_append_hex(str, idt, "OptionAddr", self->option_addr);
	fwupd_codec_string_append_hex(str, idt, "OptionLen", self->option_len);
	fwupd_codec_string_append_hex(str, idt, "MemAddr", self->mem_addr);
	fwupd_codec_string_append_hex(str, idt, "MemLen", self->mem_len);
	for (guint i = 0; i < self->cmd_items->len; i++) {
		FuStStm32DeviceCmdItem *item = g_ptr_array_index(self->cmd_items, i);
		g_autofree gchar *title =
		    g_strdup_printf("Cmd:%s", fu_st_stm32_cmd_to_string(item->cmd_base));
		fwupd_codec_string_append(str, idt, title, fu_st_stm32_cmd_to_string(item->cmd));
	}
}

static FuStStm32DeviceCmdItem *
fu_st_stm32_device_get_cmd_item(FuStStm32Device *self, FuStStm32Cmd cmd_base)
{
	for (guint i = 0; i < self->cmd_items->len; i++) {
		FuStStm32DeviceCmdItem *item = g_ptr_array_index(self->cmd_items, i);
		if (item->cmd_base == cmd_base)
			return item;
	}
	return NULL;
}

static void
fu_st_stm32_device_add_cmd(FuStStm32Device *self, FuStStm32Cmd cmd)
{
	FuStStm32Cmd cmd_base = fu_st_stm32_cmd_base(cmd);
	FuStStm32DeviceCmdItem *item;

	item = fu_st_stm32_device_get_cmd_item(self, cmd_base);
	if (item != NULL) {
		if (cmd > item->cmd)
			item->cmd = cmd;
		return;
	}
	item = g_new0(FuStStm32DeviceCmdItem, 1);
	item->cmd = cmd;
	item->cmd_base = cmd_base;
	g_ptr_array_add(self->cmd_items, item);
}

static gboolean
fu_st_stm32_device_get_cmd(FuStStm32Device *self,
			   FuStStm32Cmd cmd_base,
			   FuStStm32Cmd *cmd,
			   GError **error)
{
	FuStStm32DeviceCmdItem *item = fu_st_stm32_device_get_cmd_item(self, cmd_base);
	if (item == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "base cmd %s [0x%02x], not supported",
			    fu_st_stm32_cmd_to_string(cmd_base),
			    cmd_base);
		return FALSE;
	}
	if (cmd != NULL)
		*cmd = item->cmd;
	return TRUE;
}

static gboolean
fu_st_stm32_device_get_ack_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);
	guint8 val = 0x0;

	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), &val, sizeof(val), error))
		return FALSE;
	if (val != FU_ST_STM32_STATUS_ACK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got %s [0x%02x], expected ack",
			    fu_st_stm32_status_to_string(val),
			    val);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_get_ack(FuStStm32Device *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_st_stm32_device_get_ack_cb,
				    100,
				    0,
				    NULL,
				    error);
}

static gboolean
fu_st_stm32_device_send_command(FuStStm32Device *self,
				FuStStm32Cmd cmd_base,
				FuStStm32Cmd *cmd_actual,
				GError **error)
{
	FuStStm32Cmd cmd = 0;
	guint8 buf[2] = {0};

	/* map the base cmd to the actual implementation, if available */
	if (self->cmd_items->len > 0) {
		if (!fu_st_stm32_device_get_cmd(self, cmd_base, &cmd, error))
			return FALSE;
	} else {
		cmd = cmd_base;
	}

	/* cmd + xor checksum */
	buf[0] = cmd;
	buf[1] = cmd ^ 0xFF;
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), buf, sizeof(buf), error)) {
		g_prefix_error(error,
			       "failed to send command %s: ",
			       fu_st_stm32_cmd_to_string(cmd));
		return FALSE;
	}
	if (!fu_st_stm32_device_get_ack(self, error)) {
		g_prefix_error(error, "unexpected reply for command 0x%02x: ", cmd);
		return FALSE;
	}

	/* success */
	if (cmd_actual != NULL)
		*cmd_actual = cmd;
	return TRUE;
}

static gboolean
fu_st_stm32_device_ensure_version_bootloader(FuStStm32Device *self, GError **error)
{
	guint8 buf[1] = {0};

	/* only UART bootloader returns 3 bytes, but we don't care here */
	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_GET_VERSION, NULL, error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), buf, sizeof(buf), error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error))
		return FALSE;

	/* success */
	fu_device_set_version_bootloader_raw(FU_DEVICE(self), buf[0]);
	return TRUE;
}

static gboolean
fu_st_stm32_device_resync_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);
	guint8 buf[2] = {FU_ST_STM32_CMD_INVALID, FU_ST_STM32_CMD_INVALID ^ 0xFF};
	guint8 val = 0x0;

	/* send a wrong command and expect a NACK */
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), buf, sizeof(buf), error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), &val, sizeof(val), error))
		return FALSE;
	if (val != FU_ST_STM32_STATUS_NACK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "got %s [0x%02x], expected nack",
			    fu_st_stm32_status_to_string(val),
			    val);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_resync(FuStStm32Device *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_st_stm32_device_resync_cb,
				    70,
				    500,
				    NULL,
				    error);
}

static gboolean
fu_st_stm32_device_inc_variable_length(FuStStm32Device *self, gsize *bufsz, GError **error)
{
	guint8 buf[1] = {0};

	/* variable-sized, so request and only read the length */
	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_GET, NULL, error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), buf, sizeof(buf), error))
		return FALSE;
	if (bufsz != NULL)
		*bufsz += buf[0];
	return fu_st_stm32_device_resync(self, error);
}

static gboolean
fu_st_stm32_device_ensure_cmds(FuStStm32Device *self, GError **error)
{
	guint32 version = fu_device_get_version_bootloader_raw(FU_DEVICE(self));
	gsize bufsz = FU_STRUCT_ST_STM32_GET_RSP_SIZE;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuStructStStm32GetRsp) st = NULL;

	/* get the supported commands */
	if (version == FU_ST_STM_I2C_PROTOCOL_V1_0) {
		bufsz += 11;
	} else if (version == FU_ST_STM_I2C_PROTOCOL_V1_1) {
		bufsz += 17;
	} else if (version == FU_ST_STM_I2C_PROTOCOL_V1_2) {
		bufsz += 18;
	} else if (version >= FU_ST_STM_I2C_PROTOCOL_V2) {
		if (!fu_st_stm32_device_inc_variable_length(self, &bufsz, error))
			return FALSE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unable to handle bootloader version 0x%x",
			    version);
		return FALSE;
	}
	fu_byte_array_set_size(buf, bufsz, 0xFF);
	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_GET, NULL, error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), buf->data, buf->len, error))
		return FALSE;
	st = fu_struct_st_stm32_get_rsp_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_st_stm32_get_rsp_get_length(st) != bufsz - FU_STRUCT_ST_STM32_GET_RSP_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid response, got 0x%x, expected 0x%x",
			    fu_struct_st_stm32_get_rsp_get_length(st),
			    (guint)bufsz - FU_STRUCT_ST_STM32_GET_RSP_SIZE);
		return FALSE;
	}
	if (fu_struct_st_stm32_get_rsp_get_protocol_ver(st) != version) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid bootloader version, got 0x%x, expected 0x%x",
			    fu_struct_st_stm32_get_rsp_get_protocol_ver(st),
			    version);
		return FALSE;
	}

	/* look at the list of supported commands */
	for (gsize i = FU_STRUCT_ST_STM32_GET_RSP_SIZE; i < buf->len; i++) {
		FuStStm32Cmd cmd = buf->data[i];
		fu_st_stm32_device_add_cmd(self, cmd);
	}
	if (!fu_st_stm32_device_get_ack(self, error))
		return FALSE;
	if (!fu_st_stm32_device_get_cmd(self, FU_ST_STM32_CMD_GET, NULL, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_ensure_chip_id(FuStStm32Device *self, GError **error)
{
	guint16 cid = 0;
	guint8 buf[3] = {0};
	g_autoptr(FuStructStStm32GetIdRsp) st = NULL;

	/* request identification */
	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_GET_ID, NULL, error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), buf, sizeof(buf), error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error))
		return FALSE;
	st = fu_struct_st_stm32_get_id_rsp_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;

	/* check recognized as quirk entry */
	cid = fu_struct_st_stm32_get_id_rsp_get_cid(st);
	fu_device_add_instance_u16(FU_DEVICE(self), "CID", cid);
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "STM32", "CID", NULL))
		return FALSE;
	if (self->flash_addr == 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "chip ID 0x%x not supported",
			    cid);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_setup(FuDevice *device, GError **error)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_st_stm32_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_st_stm32_device_ensure_version_bootloader(self, error)) {
		g_prefix_error_literal(error, "failed to get bootloader version: ");
		return FALSE;
	}
	if (!fu_st_stm32_device_ensure_cmds(self, error)) {
		g_prefix_error_literal(error, "failed to get supported commands: ");
		return FALSE;
	}
	if (!fu_st_stm32_device_ensure_chip_id(self, error)) {
		g_prefix_error_literal(error, "failed to get supported commands: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_go(FuStStm32Device *self, guint32 address, GError **error)
{
	g_autoptr(FuStructStStm32Addr) st = fu_struct_st_stm32_addr_new();

	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_GO, NULL, error))
		return FALSE;
	fu_struct_st_stm32_addr_set_address(st, address);
	fu_struct_st_stm32_addr_set_checksum(st, fu_xor8(st->buf->data, st->buf->len - 1));
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), st->buf->data, st->buf->len, error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_mass_erase(FuStStm32Device *self, GError **error)
{
	FuStStm32Cmd cmd = FU_ST_STM32_CMD_INVALID;

	/* todo: implement this */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_ST_STM32_DEVICE_FLAG_NO_MASS_ERASE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "mass erase not supported");
		return FALSE;
	}

	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_ERASE, &cmd, error))
		return FALSE;

	/* regular erase or extended erase */
	if (cmd == FU_ST_STM32_CMD_ERASE) {
		if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_INVALID, NULL, error)) {
			g_prefix_error_literal(error, "failed to send invalid after mass erase: ");
			return FALSE;
		}
	} else {
		guint8 buf[] = {FU_ST_STM32_CMD_INVALID, 0xFF, 0x00};
		if (!fu_i2c_device_write(FU_I2C_DEVICE(self), buf, sizeof(buf), error)) {
			g_prefix_error_literal(error, "mass erase failed: ");
			return FALSE;
		}
		if (!fu_st_stm32_device_get_ack(self, error)) {
			g_prefix_error_literal(error, "mass erase ack failed: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_read_memory(FuStStm32Device *self,
			       guint32 address,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	g_autoptr(FuStructStStm32Addr) st = fu_struct_st_stm32_addr_new();

	/* sanity check */
	if (bufsz > 256) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "read length limit at 256 bytes");
		return FALSE;
	}

	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_READ_MEMORY, NULL, error))
		return FALSE;
	fu_struct_st_stm32_addr_set_address(st, address);
	fu_struct_st_stm32_addr_set_checksum(st, fu_xor8(st->buf->data, st->buf->len - 1));
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), st->buf->data, st->buf->len, error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error))
		return FALSE;
	if (!fu_st_stm32_device_send_command(self, bufsz - 1, NULL, error))
		return FALSE;
	if (!fu_i2c_device_read(FU_I2C_DEVICE(self), buf, bufsz, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GBytes *
fu_st_stm32_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* read entire memory space */
	fu_byte_array_set_size(buf, self->mem_len, 0x0);
	chunks = fu_chunk_array_mutable_new(buf->data,
					    buf->len,
					    self->mem_addr,
					    FU_CHUNK_PAGESZ_NONE,
					    64,
					    error);
	if (chunks == NULL)
		return NULL;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_st_stm32_device_read_memory(self,
						    fu_chunk_get_address(chk),
						    fu_chunk_get_data_out(chk),
						    fu_chunk_get_data_sz(chk),
						    error))
			return NULL;
		fu_progress_step_done(progress);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_st_stm32_device_write_memory(FuStStm32Device *self,
				guint32 address,
				const guint8 *data,
				gsize len,
				GError **error)
{
	gsize len_aligned = fu_common_align_up(len, 2);
	g_autoptr(FuStructStStm32Addr) st = fu_struct_st_stm32_addr_new();
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* sanity check */
	if (address & 0x3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "write address must be 4 byte aligned");
		return FALSE;
	}

	/* send the address and checksum */
	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_WRITE_MEMORY, NULL, error))
		return FALSE;
	fu_struct_st_stm32_addr_set_address(st, address);
	fu_struct_st_stm32_addr_set_checksum(st, fu_xor8(st->buf->data, st->buf->len - 1));
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), st->buf->data, st->buf->len, error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error))
		return FALSE;

	/* write length, padded-data, checksum */
	fu_byte_array_append_uint8(buf, len_aligned);
	g_byte_array_append(buf, data, len);
	fu_byte_array_set_size(buf, len_aligned + 1, 0xFF);
	fu_byte_array_append_uint8(buf, fu_xor8(buf->data, buf->len));
	if (!fu_i2c_device_write(FU_I2C_DEVICE(self), buf->data, buf->len, error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_run_code(FuStStm32Device *self,
			    guint32 target_address,
			    const guint8 *code,
			    guint32 codesz,
			    GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) chunks = NULL;

	/* must be 32-bit aligned */
	if (target_address & 0x3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "code address must be 4 byte aligned");
		return FALSE;
	}

	fu_byte_array_append_uint32(buf, 0x20002000, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, target_address + 8 + 1, G_LITTLE_ENDIAN); /* thumb mode */
	g_byte_array_append(buf, code, codesz);

	chunks = fu_chunk_array_new(buf->data,
				    buf->len,
				    FU_CHUNK_ADDR_OFFSET_NONE,
				    FU_CHUNK_PAGESZ_NONE,
				    256,
				    error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_st_stm32_device_write_memory(self,
						     fu_chunk_get_address(chk),
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error))
			return FALSE;
	}
	return fu_st_stm32_device_go(self, target_address, error);
}

static gboolean
fu_st_stm32_device_read_unprotect(FuStStm32Device *self, GError **error)
{
	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_READ_UNPROTECT, NULL, error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error)) {
		g_prefix_error_literal(error, "failed to ack read unprotect: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_read_protect(FuStStm32Device *self, GError **error)
{
	if (!fu_st_stm32_device_send_command(self, FU_ST_STM32_CMD_READ_PROTECT, NULL, error))
		return FALSE;
	if (!fu_st_stm32_device_get_ack(self, error)) {
		g_prefix_error_literal(error, "failed to ack read protect: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_reset(FuStStm32Device *self, GError **error)
{
	/*
	 * reset code for Cortex-M3 and Cortex-M0 -- see the Architecture Reference Manual
	 * nocheck:magic
	 */
	static const guint8 code_generic[] = {
	    0x01,
	    0x49, /* ldr     r1, [pc, #4] ; (<AIRCR_OFFSET>) */
	    0x02,
	    0x4A, /* ldr     r2, [pc, #8] ; (<AIRCR_RESET_VALUE>) */
	    0x0A,
	    0x60, /* str     r2, [r1, #0] */
	    0xfe,
	    0xe7, /* endless: b endless */
	    0x0c,
	    0xed,
	    0x00,
	    0xe0, /* .word 0xe000ed0c <AIRCR_OFFSET> = NVIC AIRCR register address */
	    0x04,
	    0x00,
	    0xfa,
	    0x05, /* .word 0x05fa0004 <AIRCR_RESET_VALUE> = VECTKEY | SYSRESETREQ */
	};

	/* set the OBL_LAUNCH bit to reset device (see RM0360, 2.5) */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_ST_STM32_DEVICE_FLAG_OBL_LAUNCH_REQUIRED)) {
		/* nocheck:magic */
		static const guint8 code_obll[] = {
		    0x01,
		    0x49, /* ldr     r1, [pc, #4] ; (<FLASH_CR>) */
		    0x02,
		    0x4A, /* ldr     r2, [pc, #8] ; (<OBL_LAUNCH>) */
		    0x0A,
		    0x60, /* str     r2, [r1, #0] */
		    0xfe,
		    0xe7, /* endless: b endless */
		    0x10,
		    0x20,
		    0x02,
		    0x40, /* address: FLASH_CR = 40022010 */
		    0x00,
		    0x20,
		    0x00,
		    0x00 /* value: OBL_LAUNCH = 00002000 */
		};
		return fu_st_stm32_device_run_code(self,
						   self->sram_addr,
						   code_obll,
						   sizeof(code_obll),
						   error);
	}

	/* clear the PEMPTY bit to reset the device (see RM0394) */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_ST_STM32_DEVICE_FLAG_CLEAR_PEMPTY)) {
		/* nocheck:magic */
		static const guint8 code_pempty[] = {
		    0x08, 0x48, /*		ldr     r0, [pc, #32] ; (<BASE_FLASH>) */
		    0x00, 0x68, /*		ldr     r0, [r0, #0] */
		    0x01, 0x30, /*		adds    r0, #1 */
		    0x41, 0x1e, /*		subs    r1, r0, #1 */
		    0x88, 0x41, /*		sbcs    r0, r1 */
		    0x07, 0x49, /*		ldr     r1, [pc, #28] ; (<FLASH_SR>) */
		    0x07, 0x4a, /*		ldr     r2, [pc, #28] ; (<PEMPTY_MASK>) */
		    0x0b, 0x68, /*		ldr     r3, [r1, #0] */
		    0x13, 0x40, /*		ands    r3, r2 */
		    0x5c, 0x1e, /*		subs    r4, r3, #1 */
		    0xa3, 0x41, /*		sbcs    r3, r4 */
		    0x98, 0x42, /*		cmp     r0, r3 */
		    0x00, 0xd1, /*		bne.n   skip1 */
		    0x0a, 0x60, /*		str     r2, [r1, #0] */
		    0x04, 0x48, /* skip1:	ldr     r0, [pc, #16] ; (<AIRCR_OFFSET>) */
		    0x05, 0x49, /*		ldr     r1, [pc, #16] ; (<AIRCR_RESET_VALUE>) */
		    0x01, 0x60, /*		str     r1, [r0, #0] */
		    0xfe, 0xe7, /* endless:	b.n	endless */
		    0x00, 0x00,
		    0x00, 0x08, /* .word 0x08000000 <BASE_FLASH> */
		    0x10, 0x20,
		    0x02, 0x40, /* .word 0x40022010 <FLASH_SR> */
		    0x00, 0x00,
		    0x02, 0x00, /* .word 0x00020000 <PEMPTY_MASK> */
		    0x0c, 0xed,
		    0x00, 0xe0, /* .word 0xe000ed0c <AIRCR_OFFSET> = NVIC AIRCR register address */
		    0x04, 0x00,
		    0xfa, 0x05 /* .word 0x05fa0004 <AIRCR_RESET_VALUE> = VECTKEY | SYSRESETREQ */
		};
		return fu_st_stm32_device_run_code(self,
						   self->sram_addr,
						   code_pempty,
						   sizeof(code_pempty),
						   error);
	}
	return fu_st_stm32_device_run_code(self,
					   self->sram_addr,
					   code_generic,
					   sizeof(code_generic),
					   error);
}

static gboolean
fu_st_stm32_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);

	/* switch the device into runtime mode */
	if (!fu_st_stm32_device_reset(self, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_st_stm32_device_write_blocks(FuStStm32Device *self,
				FuChunkArray *chunks,
				FuProgress *progress,
				GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_st_stm32_device_write_memory(self,
						     fu_chunk_get_address(chk),
						     fu_chunk_get_data(chk),
						     fu_chunk_get_data_sz(chk),
						     error)) {
			g_prefix_error(error, "failed to send chunk %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_st_stm32_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* erase */
	if (!fu_st_stm32_device_mass_erase(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	chunks =
	    fu_chunk_array_new_from_stream(stream, self->mem_addr, FU_CHUNK_PAGESZ_NONE, 64, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_st_stm32_device_write_blocks(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* TODO: verify each block */
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_st_stm32_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(device);

	if (g_strcmp0(key, "StStm32SramAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_addr = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32SramLen") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_len = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32FlashAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->flash_addr = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32FlashLen") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->flash_len = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32PagesPerSector") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->pages_per_sector = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32PageSize") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->page_size = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32OptionAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->option_addr = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32OptionLen") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->option_len = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32MemAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->mem_addr = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "StStm32MemLen") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->mem_len = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_st_stm32_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_st_stm32_device_init(FuStStm32Device *self)
{
	self->cmd_items = g_ptr_array_new_with_free_func(g_free);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.st.stm32");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), "icon-name");
	fu_device_register_private_flag(FU_DEVICE(self), FU_ST_STM32_DEVICE_FLAG_NO_MASS_ERASE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_ST_STM32_DEVICE_FLAG_CLEAR_PEMPTY);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_ST_STM32_DEVICE_FLAG_OBL_LAUNCH_REQUIRED);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x01);
}

static void
fu_st_stm32_device_finalize(GObject *object)
{
	FuStStm32Device *self = FU_ST_STM32_DEVICE(object);
	g_ptr_array_unref(self->cmd_items);
	G_OBJECT_CLASS(fu_st_stm32_device_parent_class)->finalize(object);
}

static void
fu_st_stm32_device_class_init(FuStStm32DeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_st_stm32_device_finalize;
	device_class->to_string = fu_st_stm32_device_to_string;
	device_class->setup = fu_st_stm32_device_setup;
	device_class->dump_firmware = fu_st_stm32_device_dump_firmware;
	device_class->attach = fu_st_stm32_device_attach;
	device_class->write_firmware = fu_st_stm32_device_write_firmware;
	device_class->set_quirk_kv = fu_st_stm32_device_set_quirk_kv;
	device_class->set_progress = fu_st_stm32_device_set_progress;
}

/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-genesys-gl32xx-device.h"
#include "fu-genesys-gl32xx-firmware.h"

#define FU_GENESYS_GL32XX_FW_START_ADDR	    0x0
#define FU_GENESYS_GL32XX_FW_SIZE	    0x00010000
#define FU_GENESYS_GL32XX_CLEAR_WP_SLEEP_MS 800

struct _FuGenesysGl32xxDevice {
	FuBlockDevice parent_instance;
	gchar *chip_name;
	guint32 packetsz;
	guint32 customer_id;
	guint16 compatible_model;
};

G_DEFINE_TYPE(FuGenesysGl32xxDevice, fu_genesys_gl32xx_device, FU_TYPE_BLOCK_DEVICE)

static void
fu_genesys_gl32xx_device_set_chip_name(FuGenesysGl32xxDevice *self, const gchar *chip_name)
{
	g_return_if_fail(chip_name != NULL);
	g_free(self->chip_name);
	self->chip_name = g_strdup(chip_name);
}

static GByteArray *
fu_genesys_gl32xx_device_cmd_get_version(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0x12, 0x00, 0x00, 0x00, 0x2e, 0x00};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	fu_byte_array_set_size(buf, 0x2E, 0x0);
	if (!fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					    cmd,
					    sizeof(cmd),
					    buf->data,
					    buf->len,
					    error))
		return NULL;
	return g_steal_pointer(&buf);
}

static gboolean
fu_genesys_gl32xx_device_cmd_switch_to_rom_mode(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x06, 0x00, 0x00, 0x00, 0x00};
	if (!fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), cmd, sizeof(cmd), error)) {
		g_prefix_error(error, "failed to switch into ROM mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_cmd_reset_usb(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xE6, 0x00, 0x00, 0x00, 0x00, 0x00};
	if (!fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), cmd, sizeof(cmd), error)) {
		g_prefix_error(error, "failed to reset USB: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_cmd_write_sr(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x01, 0x00, 0x00, 0x01, 0x00};
	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), cmd, sizeof(cmd), error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_write_enable(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x00, 0x00, 0x00, 0x06, 0x00};
	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), cmd, sizeof(cmd), error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_write_disable(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x00, 0x00, 0x00, 0x04, 0x00};
	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), cmd, sizeof(cmd), error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_clear_wp(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x02, 0x00, 0x02, 0x00, 0x00};
	const guint8 data[] = {0x01, 0x00};
	return fu_block_device_sg_io_cmd_write(FU_BLOCK_DEVICE(self),
					       cmd,
					       sizeof(cmd),
					       data,
					       sizeof(data),
					       error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_chip_erase(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x00, 0x00, 0x00, 0xC7, 0x00};
	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), cmd, sizeof(cmd), error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_wait_wip(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x03, 0x01, 0x00, 0x05, 0x00};
	return fu_block_device_sg_io_cmd_none(FU_BLOCK_DEVICE(self), cmd, sizeof(cmd), error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_read_flash(FuGenesysGl32xxDevice *self,
					gsize addr,
					guint8 *data,
					gsize datasz,
					GError **error)
{
	guint8 cmd[] = {0xE4, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	g_return_val_if_fail(data != NULL && datasz != 0, FALSE);

	/* start address */
	if (!fu_memwrite_uint32_safe(cmd, sizeof(cmd), 2, addr, G_BIG_ENDIAN, error))
		return FALSE;

	/* block size */
	if (!fu_memwrite_uint16_safe(cmd, sizeof(cmd), 6, (guint16)datasz, G_BIG_ENDIAN, error))
		return FALSE;

	return fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					      cmd,
					      sizeof(cmd),
					      data,
					      datasz,
					      error);
}

static gboolean
fu_genesys_gl32xx_device_ensure_version(FuGenesysGl32xxDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autofree gchar *version_prefix = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_genesys_gl32xx_device_cmd_get_version(self, error);
	if (buf == NULL) {
		g_prefix_error(error, "failed to read version: ");
		return FALSE;
	}
	if (buf->len < 0x24) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to read version");
		return FALSE;
	}

	version = fu_memstrsafe(buf->data, buf->len, 0x20, 4, error);
	if (version == NULL)
		return FALSE;
	fu_device_set_version(FU_DEVICE(self), version);

	/* this is used to differentiate standard firmware versions */
	version_prefix = fu_memstrsafe(buf->data, buf->len, 0x20, 2, error);
	if (version_prefix == NULL)
		return FALSE;
	fu_device_add_instance_str(FU_DEVICE(self), "VER", version_prefix);
	return fu_device_build_instance_id_full(FU_DEVICE(self),
						FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						error,
						"BLOCK",
						"VEN",
						"DEV",
						"VER",
						NULL);
}

static gboolean
fu_genesys_gl32xx_device_check_rom_mode(FuGenesysGl32xxDevice *self,
					guint8 *cmd,
					gsize cmdsz,
					GError **error)
{
	const guint8 ext_rom_mode[] = {0x58, 0x52, 0x4F, 0x4D}; /* "XROM" */
	const guint8 int_rom_mode[] = {0x49, 0x4E, 0x54, 0x2D}; /* "INT-" */
	guint8 data[4] = {0};

	if (!fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					    cmd,
					    cmdsz,
					    data,
					    sizeof(data),
					    error))
		return FALSE;
	if (fu_memcmp_safe(int_rom_mode,
			   sizeof(int_rom_mode),
			   0,
			   data,
			   sizeof(data),
			   0,
			   sizeof(data),
			   NULL)) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}
	if (fu_memcmp_safe(ext_rom_mode,
			   sizeof(ext_rom_mode),
			   0,
			   data,
			   sizeof(data),
			   0,
			   sizeof(data),
			   NULL)) {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no supported devices detected");
	return FALSE;
}

/* safe to call in any mode */
static gboolean
fu_genesys_gl32xx_device_ensure_rom_mode(FuGenesysGl32xxDevice *self, GError **error)
{
	guint8 cmd_gl323x[] = {0xE4, 0x01, 0x00, 0xDC, 0x04, 0x00};
	guint8 cmd_gl3224[] = {0xE4, 0x01, 0x00, 0xFC, 0x04, 0x00};
	g_autoptr(GError) error_local = NULL;

	/* check for 3230, 3231, 3232, 3230S, 3231S, 3232S first */
	/* ignore error here */
	if (!fu_genesys_gl32xx_device_check_rom_mode(self,
						     cmd_gl323x,
						     sizeof(cmd_gl323x),
						     &error_local)) {
		g_debug("ignoring: %s", error_local->message);
	} else {
		fu_genesys_gl32xx_device_set_chip_name(self, "GL323x");
		return TRUE;
	}

	/* check the 3224 */
	if (!fu_genesys_gl32xx_device_check_rom_mode(self, cmd_gl3224, sizeof(cmd_gl3224), error))
		return FALSE;

	fu_genesys_gl32xx_device_set_chip_name(self, "GL3224");
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_verify_chip_id(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd_req[] = {0xF3, 0x02, 0x00, 0x01, 0x00, 0x03};
	const guint8 data_req[] = {0x9F};
	const guint8 cmd_get[] = {0xF3, 0x04, 0x00, 0x00, 0x00, 0x03};
	guint8 buf[3] = {0};
	g_autofree gchar *flash_id = NULL;
	g_autoptr(FuCfiDevice) cfi_device = NULL;

	if (!fu_block_device_sg_io_cmd_write(FU_BLOCK_DEVICE(self),
					     cmd_req,
					     sizeof(cmd_req),
					     data_req,
					     sizeof(data_req),
					     error))
		return FALSE;
	if (!fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					    cmd_get,
					    sizeof(cmd_get),
					    buf,
					    sizeof(buf),
					    error))
		return FALSE;
	flash_id = g_strdup_printf("%02X%02X%02X", buf[0], buf[1], buf[2]);
	cfi_device = fu_cfi_device_new(fu_device_get_context(FU_DEVICE(self)), flash_id);
	if (!fu_device_setup(FU_DEVICE(cfi_device), error))
		return FALSE;
	if (fu_device_get_name(cfi_device) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "not supported flash type");
		return FALSE;
	}
	g_debug("flash type detected: %s", fu_device_get_name(cfi_device));

	/* success */
	return TRUE;
}

static void
fu_genesys_gl32xx_device_ensure_enforce_requires(FuGenesysGl32xxDevice *self)
{
	const gchar *version = fu_device_get_version(FU_DEVICE(self));
	const guint16 model = fu_device_get_pid(FU_DEVICE(self));

	/* GL3224 */
	if (model == 0x0749 && self->customer_id == 0xFFFFFFFF && g_str_has_prefix(version, "15")) {
		fu_device_add_private_flag(FU_DEVICE(self),
					   FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
		return;
	}

	/* GL323X */
	if (model == 0x0764 && self->customer_id == 0x22FFFFFF && g_str_has_prefix(version, "29")) {
		fu_device_add_private_flag(FU_DEVICE(self),
					   FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
		return;
	}
}

static gboolean
fu_genesys_gl32xx_device_ensure_cid(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd_gl3224_cid[] = {0xE4, 0x01, 0xBF, 0x80, 0x04, 0x00};
	const guint8 cmd_gl323x_cid[] = {0xE4, 0x01, 0x35, 0x00, 0x04, 0x00};
	const guint8 *cmd = NULL;
	guint16 model = fu_device_get_pid(FU_DEVICE(self));
	guint8 data[4] = {0};

	if (self->compatible_model != 0)
		model = self->compatible_model;

	switch (model) {
	case 0x0749:
		cmd = cmd_gl3224_cid;
		break;
	case 0x0764:
		cmd = cmd_gl323x_cid;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "unsupported model [0x%04X]",
			    model);
		return FALSE;
	}

	if (!fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					    cmd,
					    sizeof(cmd_gl323x_cid),
					    data,
					    sizeof(data),
					    error))
		return FALSE;
	self->customer_id = fu_memread_uint32(data, G_BIG_ENDIAN);
	fu_device_add_instance_u32(FU_DEVICE(self), "CID", self->customer_id);

	/* valid GUID with the pair of FW version stream and customer ID */
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "BLOCK",
					   "VEN",
					   "DEV",
					   "VER",
					   "CID",
					   NULL);
}

static gboolean
fu_genesys_gl32xx_device_get_usb_mode(FuGenesysGl32xxDevice *self, GError **error)
{
	guint8 mode = 0;
	const guint8 cmd[] = {0xF2, 0xFF, 0x00, 0x00, 0x00, 0x00};

	if (!fu_block_device_sg_io_cmd_read(FU_BLOCK_DEVICE(self),
					    cmd,
					    sizeof(cmd),
					    &mode,
					    sizeof(mode),
					    error)) {
		g_prefix_error(error, "failed to read USB mode: ");
		return FALSE;
	}
	switch (mode) {
	case 1:
		self->packetsz = 64;
		break;
	case 2:
		self->packetsz = 512;
		break;
	case 3:
		self->packetsz = 1024;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown USB mode 0x%02x read from device",
			    mode);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_erase(FuGenesysGl32xxDevice *self, GError **error)
{
	/* write enable */
	if (!fu_genesys_gl32xx_device_cmd_write_enable(self, error)) {
		g_prefix_error(error, "failed to write enable: ");
		return FALSE;
	}

	/* clear write protect */
	if (!fu_genesys_gl32xx_device_cmd_clear_wp(self, error)) {
		g_prefix_error(error, "failed to clear WP: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), FU_GENESYS_GL32XX_CLEAR_WP_SLEEP_MS);

	/* write enable */
	if (!fu_genesys_gl32xx_device_cmd_write_enable(self, error)) {
		g_prefix_error(error, "failed to write enable: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), FU_GENESYS_GL32XX_CLEAR_WP_SLEEP_MS);

	/* chip erase */
	if (!fu_genesys_gl32xx_device_cmd_chip_erase(self, error)) {
		g_prefix_error(error, "failed to erase chip: ");
		return FALSE;
	}

	/* wait WIP to reset back to 0 */
	if (!fu_genesys_gl32xx_device_cmd_wait_wip(self, error)) {
		g_prefix_error(error, "failed to wait WIP: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_gl32xx_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);
	fwupd_codec_string_append(str, idt, "ChipName", self->chip_name);
	fwupd_codec_string_append_hex(str, idt, "BlockTransferSize", self->packetsz);
	fwupd_codec_string_append_hex(str, idt, "CustomerId", self->customer_id);
}

static gboolean
fu_genesys_gl32xx_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);

	/* switch to internal, request and check chip ID */
	if (!fu_genesys_gl32xx_device_cmd_switch_to_rom_mode(self, error))
		return FALSE;

	/* get USB mode */
	if (!fu_genesys_gl32xx_device_get_usb_mode(self, error))
		return FALSE;
	if (!fu_genesys_gl32xx_device_verify_chip_id(self, error))
		return FALSE;

	/* clear SR */
	if (!fu_genesys_gl32xx_device_cmd_write_sr(self, error)) {
		g_prefix_error(error, "failed to clear SR: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return fu_genesys_gl32xx_device_cmd_reset_usb(self, error);
}

static gboolean
fu_genesys_gl32xx_device_setup(FuDevice *device, GError **error)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);
	g_autofree gchar *name = NULL;

	if (!fu_genesys_gl32xx_device_ensure_version(self, error))
		return FALSE;

	if (!fu_genesys_gl32xx_device_ensure_rom_mode(self, error)) {
		g_prefix_error(error, "failed to check ROM mode: ");
		return FALSE;
	}

	/* if not detected above */
	if (self->chip_name == NULL)
		fu_genesys_gl32xx_device_set_chip_name(self, "GL32xx");
	if (fu_device_has_vendor_id(device, "BLOCK:0x05E3")) {
		name = g_strdup_printf("%s SD reader [0x%04X]",
				       self->chip_name,
				       fu_device_get_pid(device));
		fu_device_set_name(device, name);
	}

	if (!fu_genesys_gl32xx_device_ensure_cid(self, error))
		return FALSE;
	fu_genesys_gl32xx_device_ensure_enforce_requires(self);

	/* success */
	return TRUE;
}

static GBytes *
fu_genesys_gl32xx_device_dump_bytes(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);
	const gsize fwsz = fu_device_get_firmware_size_max(device);
	g_autoptr(GPtrArray) chunks = NULL;
	g_autofree guint8 *buf = g_malloc0(fwsz);

	chunks = fu_chunk_array_mutable_new(buf, fwsz, 0x0, 0x0, self->packetsz);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_genesys_gl32xx_device_cmd_read_flash(self,
							     fu_chunk_get_address(chk),
							     fu_chunk_get_data_out(chk),
							     fu_chunk_get_data_sz(chk),
							     error)) {
			g_prefix_error(error, "failed to read flash data on chunk 0x%x: ", i);
			return NULL;
		}
		fu_progress_step_done(progress);
	}

	return g_bytes_new_take(g_steal_pointer(&buf), fwsz);
}

static GBytes *
fu_genesys_gl32xx_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;

	g_autoptr(FuDeviceLocker) locker =
	    fu_device_locker_new_full(FU_DEVICE(self),
				      (FuDeviceLockerFunc)fu_genesys_gl32xx_device_detach,
				      (FuDeviceLockerFunc)fu_genesys_gl32xx_device_attach,
				      error);

	if (locker == NULL)
		return NULL;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fw = fu_genesys_gl32xx_device_dump_bytes(device, progress, error);
	if (fw == NULL)
		return NULL;

	/* success */
	return g_steal_pointer(&fw);
}

static FuFirmware *
fu_genesys_gl32xx_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_genesys_gl32xx_firmware_new();
	g_autoptr(GBytes) fw = NULL;

	fw = fu_genesys_gl32xx_device_dump_firmware(device, progress, error);
	if (fw == NULL)
		return NULL;
	if (!fu_firmware_parse(firmware, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static FuFirmware *
fu_genesys_gl32xx_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_genesys_gl32xx_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* check size */
	if (fu_firmware_get_size(firmware) != fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size is [%" G_GSIZE_FORMAT
			    "] bytes while expecting [%" G_GUINT64_FORMAT "] bytes",
			    fu_firmware_get_size(firmware),
			    fu_device_get_firmware_size_max(device));
		return NULL;
	}

	/* TODO: validate compatibility? */

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_genesys_gl32xx_device_write_block(FuGenesysGl32xxDevice *self, FuChunk *chunk, GError **error)
{
	gsize addr = fu_chunk_get_address(chunk);
	gsize datasz = fu_chunk_get_data_sz(chunk);
	const guint8 *data = fu_chunk_get_data(chunk);
	guint8 cmd[] = {0xE5, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};

	/* build command */
	if (!fu_memwrite_uint32_safe(cmd, sizeof(cmd), 2, addr, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_memwrite_uint16_safe(cmd, sizeof(cmd), 6, (guint16)datasz, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_block_device_sg_io_cmd_write(FU_BLOCK_DEVICE(self),
					     cmd,
					     sizeof(cmd),
					     data,
					     datasz,
					     error)) {
		g_prefix_error(error, "failed to write flash data: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_write_blocks(FuGenesysGl32xxDevice *self,
				      FuChunkArray *chunks,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_genesys_gl32xx_device_write_block(self, chk, error)) {
			g_prefix_error(error, "failed on block 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_read = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 9, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase */
	if (!fu_genesys_gl32xx_device_erase(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	chunks = fu_chunk_array_new_from_bytes(fw, FU_GENESYS_GL32XX_FW_START_ADDR, self->packetsz);
	if (!fu_genesys_gl32xx_device_write_blocks(self,
						   chunks,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify written data */
	fw_read = fu_genesys_gl32xx_device_dump_bytes(device, progress, error);
	if (fw_read == NULL)
		return FALSE;
	fu_progress_step_done(progress);
	if (g_bytes_compare(fw, fw_read) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "unable to verify written firmware");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write disable */
	if (!fu_genesys_gl32xx_device_cmd_write_disable(self, error)) {
		g_prefix_error(error, "failed to write disable: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_gl32xx_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 55, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 45, "reload");
}

static gboolean
fu_genesys_gl32xx_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(device);
	guint64 tmp;

	/* load from quirks */
	if (g_strcmp0(key, "GenesysGl32xxCompatibleModel") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->compatible_model = (guint16)tmp;
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
fu_genesys_gl32xx_device_init(FuGenesysGl32xxDevice *self)
{
	self->packetsz = 64;

	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_firmware_size(FU_DEVICE(self),
				    FU_GENESYS_GL32XX_FW_SIZE); /* defaults to 64K */
	fu_device_add_protocol(FU_DEVICE(self), "com.genesys.gl32xx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_SERIAL_NUMBER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
}

static void
fu_genesys_gl32xx_device_finalize(GObject *object)
{
	FuGenesysGl32xxDevice *self = FU_GENESYS_GL32XX_DEVICE(object);
	g_free(self->chip_name);
	G_OBJECT_CLASS(fu_genesys_gl32xx_device_parent_class)->finalize(object);
}

static void
fu_genesys_gl32xx_device_class_init(FuGenesysGl32xxDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_genesys_gl32xx_device_finalize;
	device_class->to_string = fu_genesys_gl32xx_device_to_string;
	device_class->setup = fu_genesys_gl32xx_device_setup;
	device_class->detach = fu_genesys_gl32xx_device_detach;
	device_class->attach = fu_genesys_gl32xx_device_attach;
	device_class->dump_firmware = fu_genesys_gl32xx_device_dump_firmware;
	device_class->write_firmware = fu_genesys_gl32xx_device_write_firmware;
	device_class->read_firmware = fu_genesys_gl32xx_device_read_firmware;
	device_class->prepare_firmware = fu_genesys_gl32xx_device_prepare_firmware;
	device_class->set_progress = fu_genesys_gl32xx_device_set_progress;
	device_class->set_quirk_kv = fu_genesys_gl32xx_device_set_quirk_kv;
}

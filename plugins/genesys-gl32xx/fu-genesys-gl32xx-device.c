/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_SCSI_SG_H
#include <scsi/sg.h>
#endif

#include "fu-genesys-gl32xx-device.h"
#include "fu-genesys-gl32xx-firmware.h"

#define FU_GENESYS_GL32XX_FW_START_ADDR	    0x0
#define FU_GENESYS_GL32XX_FW_SIZE	    0x00010000
#define FU_GENESYS_GL32XX_CLEAR_WP_SLEEP_MS 800

struct _FuGenesysGl32xxDevice {
	FuUdevDevice parent_instance;
	gchar *chip_name;
	guint32 packetsz;
};

G_DEFINE_TYPE(FuGenesysGl32xxDevice, fu_genesys_gl32xx_device, FU_TYPE_UDEV_DEVICE)

#define FU_GENESYS_GL32XX_BUFFER_LEN	   32
#define FU_GENESYS_GL32XX_IOCTL_TIMEOUT_MS 20000

/* based on function from scsi plugin */
static gboolean
fu_genesys_gl32xx_device_send_scsi_cmd_v3(FuGenesysGl32xxDevice *self,
					  const guint8 *cdb,
					  guint8 cdbsz,
					  const guint8 *buf,
					  guint32 bufsz,
					  FuGenesysGl32xxDirection dir,
					  GError **error)
{
#ifdef HAVE_SCSI_SG_H
	guint8 sense_buffer[FU_GENESYS_GL32XX_BUFFER_LEN] = {0};
	struct sg_io_hdr io_hdr = {.interface_id = 'S'};
	gint rc = 0;

	io_hdr.cmd_len = cdbsz;
	io_hdr.mx_sb_len = sizeof(sense_buffer);

	switch (dir) {
	case FU_GENESYS_GL32XX_DIRECTION_IN:
		io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
		break;
	case FU_GENESYS_GL32XX_DIRECTION_OUT:
		io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
		break;
	case FU_GENESYS_GL32XX_DIRECTION_NO_DATA:
	default:
		io_hdr.dxfer_direction = SG_DXFER_NONE;
		break;
	}

	io_hdr.dxfer_len = bufsz;
	io_hdr.dxferp = (guint8 *)buf;
	/* pointer to command buf */
	io_hdr.cmdp = (guint8 *)cdb;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = FU_GENESYS_GL32XX_IOCTL_TIMEOUT_MS;
	io_hdr.flags = SG_FLAG_DIRECT_IO;

	fu_dump_raw(G_LOG_DOMAIN, "cmd", cdb, cdbsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  SG_IO,
				  (guint8 *)&io_hdr,
				  &rc,
				  5 * FU_GENESYS_GL32XX_IOCTL_TIMEOUT_MS,
				  error))
		return FALSE;

	if (io_hdr.status) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Command fail with status %x, senseKey 0x%02x, asc 0x%02x, ascq 0x%02x",
			    io_hdr.status,
			    sense_buffer[2],
			    sense_buffer[12],
			    sense_buffer[13]);
		return FALSE;
	}

	if (bufsz > 0)
		fu_dump_raw(G_LOG_DOMAIN, "cmd data", buf, bufsz);

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported as scsi/sg.h not found");
	return FALSE;
#endif
}

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
	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd,
						       sizeof(cmd),
						       buf->data,
						       buf->len,
						       FU_GENESYS_GL32XX_DIRECTION_IN,
						       error))
		return NULL;
	return g_steal_pointer(&buf);
}

static gboolean
fu_genesys_gl32xx_device_cmd_switch_to_rom_mode(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x06, 0x00, 0x00, 0x00, 0x00};
	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd,
						       sizeof(cmd),
						       NULL,
						       0,
						       FU_GENESYS_GL32XX_DIRECTION_NO_DATA,
						       error)) {
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
	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd,
						       sizeof(cmd),
						       NULL,
						       0,
						       FU_GENESYS_GL32XX_DIRECTION_NO_DATA,
						       error)) {
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
	return fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
							 cmd,
							 sizeof(cmd),
							 NULL,
							 0,
							 FU_GENESYS_GL32XX_DIRECTION_NO_DATA,
							 error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_write_enable(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x00, 0x00, 0x00, 0x06, 0x00};
	return fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
							 cmd,
							 sizeof(cmd),
							 NULL,
							 0,
							 FU_GENESYS_GL32XX_DIRECTION_NO_DATA,
							 error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_write_disable(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x00, 0x00, 0x00, 0x04, 0x00};
	return fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
							 cmd,
							 sizeof(cmd),
							 NULL,
							 0,
							 FU_GENESYS_GL32XX_DIRECTION_NO_DATA,
							 error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_clear_wp(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x02, 0x00, 0x02, 0x00, 0x00};
	const guint8 data[] = {0x01, 0x00};
	return fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
							 cmd,
							 sizeof(cmd),
							 data,
							 sizeof(data),
							 FU_GENESYS_GL32XX_DIRECTION_OUT,
							 error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_chip_erase(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x00, 0x00, 0x00, 0xC7, 0x00};
	return fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
							 cmd,
							 sizeof(cmd),
							 NULL,
							 0,
							 FU_GENESYS_GL32XX_DIRECTION_NO_DATA,
							 error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_wait_wip(FuGenesysGl32xxDevice *self, GError **error)
{
	const guint8 cmd[] = {0xF3, 0x03, 0x01, 0x00, 0x05, 0x00};
	return fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
							 cmd,
							 sizeof(cmd),
							 NULL,
							 0,
							 FU_GENESYS_GL32XX_DIRECTION_NO_DATA,
							 error);
}

static gboolean
fu_genesys_gl32xx_device_cmd_read_flash(FuGenesysGl32xxDevice *self,
					guint32 addr,
					guint8 *data,
					guint32 datasz,
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

	return fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
							 cmd,
							 sizeof(cmd),
							 data,
							 datasz,
							 FU_GENESYS_GL32XX_DIRECTION_IN,
							 error);
}

static gboolean
fu_genesys_gl32xx_device_ensure_version(FuGenesysGl32xxDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_genesys_gl32xx_device_cmd_get_version(self, error);
	if (buf == NULL) {
		g_prefix_error(error, "failed to read version: ");
		return FALSE;
	}
	if (buf->len < 0x24) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to read version");
		return FALSE;
	}

	version = g_strdup_printf("%c%c%c%c",
				  buf->data[0x20],
				  buf->data[0x21],
				  buf->data[0x22],
				  buf->data[0x23]);
	fu_device_set_version(FU_DEVICE(self), version);

	return TRUE;
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

	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd,
						       cmdsz,
						       data,
						       sizeof(data),
						       FU_GENESYS_GL32XX_DIRECTION_IN,
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

	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd_req,
						       sizeof(cmd_req),
						       data_req,
						       sizeof(data_req),
						       FU_GENESYS_GL32XX_DIRECTION_OUT,
						       error))
		return FALSE;
	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd_get,
						       sizeof(cmd_get),
						       buf,
						       sizeof(buf),
						       FU_GENESYS_GL32XX_DIRECTION_IN,
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

static gboolean
fu_genesys_gl32xx_device_get_usb_mode(FuGenesysGl32xxDevice *self, GError **error)
{
	guint8 mode = 0;
	const guint8 cmd[] = {0xF2, 0xFF, 0x00, 0x00, 0x00, 0x00};

	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd,
						       sizeof(cmd),
						       &mode,
						       sizeof(mode),
						       FU_GENESYS_GL32XX_DIRECTION_IN,
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
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
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

	FU_DEVICE_CLASS(fu_genesys_gl32xx_device_parent_class)->to_string(device, idt, str);
	fu_string_append(str, idt, "ChipName", self->chip_name);
	fu_string_append_kx(str, idt, "BlockTransferSize", self->packetsz);
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
fu_genesys_gl32xx_device_probe(FuDevice *device, GError **error)
{
	const gchar *device_bus = NULL;

	/* UdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_genesys_gl32xx_device_parent_class)->probe(device, error))
		return FALSE;

	/* success */
	device_bus = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), device_bus, error);
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
	name = g_strdup_printf("%s SD reader [0x%04X]",
			       self->chip_name,
			       fu_udev_device_get_model(FU_UDEV_DEVICE(device)));
	fu_device_set_name(device, name);

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
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_genesys_gl32xx_firmware_new();

	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* check size */
	if (fu_firmware_get_size(firmware) != fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size is [%lu] bytes while expecting [%lu] bytes",
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
	guint32 addr = fu_chunk_get_address(chunk);
	guint16 datasz = fu_chunk_get_data_sz(chunk);
	const guint8 *data = fu_chunk_get_data(chunk);
	guint8 cmd[] = {0xE5, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};

	/* build command */
	if (!fu_memwrite_uint32_safe(cmd, sizeof(cmd), 2, addr, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_memwrite_uint16_safe(cmd, sizeof(cmd), 6, (guint16)datasz, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_genesys_gl32xx_device_send_scsi_cmd_v3(self,
						       cmd,
						       sizeof(cmd),
						       data,
						       datasz,
						       FU_GENESYS_GL32XX_DIRECTION_OUT,
						       error)) {
		g_prefix_error(error, "failed to write flash data: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_gl32xx_device_write_blocks(FuGenesysGl32xxDevice *self,
				      GPtrArray *chunks,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
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
	g_autoptr(GPtrArray) chunks = NULL;

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
	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_GENESYS_GL32XX_FW_START_ADDR,
					       0x00, /* page_sz */
					       self->packetsz /* block_size */);
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

static void
fu_genesys_gl32xx_device_init(FuGenesysGl32xxDevice *self)
{
	self->packetsz = 64;
	fu_device_set_vendor(FU_DEVICE(self), "Genesys");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_firmware_size(FU_DEVICE(self),
				    FU_GENESYS_GL32XX_FW_SIZE); /* defaults to 64K */
	fu_device_add_protocol(FU_DEVICE(self), "com.genesys.gl32xx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK |
				     FU_UDEV_DEVICE_FLAG_IOCTL_RETRY);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER);
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_genesys_gl32xx_device_finalize;
	klass_device->to_string = fu_genesys_gl32xx_device_to_string;
	klass_device->probe = fu_genesys_gl32xx_device_probe;
	klass_device->setup = fu_genesys_gl32xx_device_setup;
	klass_device->detach = fu_genesys_gl32xx_device_detach;
	klass_device->attach = fu_genesys_gl32xx_device_attach;
	klass_device->dump_firmware = fu_genesys_gl32xx_device_dump_firmware;
	klass_device->write_firmware = fu_genesys_gl32xx_device_write_firmware;
	klass_device->read_firmware = fu_genesys_gl32xx_device_read_firmware;
	klass_device->prepare_firmware = fu_genesys_gl32xx_device_prepare_firmware;
	klass_device->set_progress = fu_genesys_gl32xx_device_set_progress;
}

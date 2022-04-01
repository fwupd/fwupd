/*
 * Copyright (C) 2017 VIA Corporation
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-vli-device.h"

typedef struct {
	FuVliDeviceKind kind;
	FuCfiDevice *cfi_device;
	gboolean spi_auto_detect;
	guint8 spi_cmd_read_id_sz;
	guint32 flash_id;
} FuVliDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuVliDevice, fu_vli_device, FU_TYPE_USB_DEVICE)

#define GET_PRIVATE(o) (fu_vli_device_get_instance_private(o))

enum { PROP_0, PROP_KIND, PROP_LAST };

FuCfiDevice *
fu_vli_device_get_cfi_device(FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	return priv->cfi_device;
}

static gboolean
fu_vli_device_spi_write_enable(FuVliDevice *self, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS(self);
	if (klass->spi_write_enable != NULL) {
		if (!klass->spi_write_enable(self, error)) {
			g_prefix_error(error, "failed to write enable SPI: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_chip_erase(FuVliDevice *self, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS(self);
	if (klass->spi_chip_erase != NULL) {
		if (!klass->spi_chip_erase(self, error)) {
			g_prefix_error(error, "failed to erase SPI data: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_write_status(FuVliDevice *self, guint8 status, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS(self);
	if (klass->spi_write_status != NULL) {
		if (!klass->spi_write_status(self, status, error)) {
			g_prefix_error(error, "failed to write SPI status 0x%x: ", status);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_read_status(FuVliDevice *self, guint8 *status, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS(self);
	if (klass->spi_read_status != NULL) {
		if (!klass->spi_read_status(self, status, error)) {
			g_prefix_error(error, "failed to read status: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_sector_erase(FuVliDevice *self, guint32 addr, GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS(self);
	if (klass->spi_sector_erase != NULL) {
		if (!klass->spi_sector_erase(self, addr, error)) {
			g_prefix_error(error, "failed to erase SPI data @0x%x: ", addr);
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
fu_vli_device_spi_read_block(FuVliDevice *self,
			     guint32 addr,
			     guint8 *buf,
			     gsize bufsz,
			     GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS(self);
	if (klass->spi_read_data != NULL) {
		if (!klass->spi_read_data(self, addr, buf, bufsz, error)) {
			g_prefix_error(error, "failed to read SPI data @0x%x: ", addr);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_write_data(FuVliDevice *self,
			     guint32 addr,
			     const guint8 *buf,
			     gsize bufsz,
			     GError **error)
{
	FuVliDeviceClass *klass = FU_VLI_DEVICE_GET_CLASS(self);
	if (klass->spi_write_data != NULL) {
		if (!klass->spi_write_data(self, addr, buf, bufsz, error)) {
			g_prefix_error(error, "failed to write SPI data @0x%x: ", addr);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_vli_device_spi_wait_finish(FuVliDevice *self, GError **error)
{
	const guint32 rdy_cnt = 2;
	guint32 cnt = 0;

	for (guint32 idx = 0; idx < 1000; idx++) {
		guint8 status = 0x7f;

		/* must get bit[1:0] == 0 twice in a row for success */
		if (!fu_vli_device_spi_read_status(self, &status, error))
			return FALSE;
		if ((status & 0x03) == 0x00) {
			if (cnt++ >= rdy_cnt)
				return TRUE;
		} else {
			cnt = 0;
		}
		g_usleep(500 * 1000);
	}
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "failed to wait for SPI");
	return FALSE;
}

gboolean
fu_vli_device_spi_erase_sector(FuVliDevice *self, guint32 addr, GError **error)
{
	const guint32 bufsz = 0x1000;

	/* erase sector */
	if (!fu_vli_device_spi_write_enable(self, error)) {
		g_prefix_error(error, "->spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_status(self, 0x00, error)) {
		g_prefix_error(error, "->spi_write_status failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_enable(self, error)) {
		g_prefix_error(error, "->spi_write_enable failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_sector_erase(self, addr, error)) {
		g_prefix_error(error, "->spi_sector_erase failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_wait_finish(self, error)) {
		g_prefix_error(error, "->spi_wait_finish failed: ");
		return FALSE;
	}

	/* verify it really was blanked */
	for (guint32 offset = 0; offset < bufsz; offset += FU_VLI_DEVICE_TXSIZE) {
		guint8 buf[FU_VLI_DEVICE_TXSIZE] = {0x0};
		if (!fu_vli_device_spi_read_block(self, addr + offset, buf, sizeof(buf), error)) {
			g_prefix_error(error, "failed to read back empty: ");
			return FALSE;
		}
		for (guint i = 0; i < sizeof(buf); i++) {
			if (buf[i] != 0xff) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "failed to check blank @0x%x",
					    addr + offset + i);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

GBytes *
fu_vli_device_spi_read(FuVliDevice *self,
		       guint32 address,
		       gsize bufsz,
		       FuProgress *progress,
		       GError **error)
{
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(GPtrArray) chunks = NULL;

	/* get data from hardware */
	chunks = fu_chunk_array_mutable_new(buf, bufsz, address, 0x0, FU_VLI_DEVICE_TXSIZE);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_vli_device_spi_read_block(self,
						  fu_chunk_get_address(chk),
						  fu_chunk_get_data_out(chk),
						  fu_chunk_get_data_sz(chk),
						  error)) {
			g_prefix_error(error,
				       "SPI data read failed @0x%x: ",
				       fu_chunk_get_address(chk));
			return NULL;
		}
		fu_progress_step_done(progress);
	}
	return g_bytes_new_take(g_steal_pointer(&buf), bufsz);
}

gboolean
fu_vli_device_spi_write_block(FuVliDevice *self,
			      guint32 address,
			      const guint8 *buf,
			      gsize bufsz,
			      FuProgress *progress,
			      GError **error)
{
	g_autofree guint8 *buf_tmp = g_malloc0(bufsz);

	/* sanity check */
	if (bufsz > FU_VLI_DEVICE_TXSIZE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "cannot write 0x%x in one block",
			    (guint)bufsz);
		return FALSE;
	}

	/* write */
	if (g_getenv("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
		g_debug("writing 0x%x block @0x%x", (guint)bufsz, address);
	if (!fu_vli_device_spi_write_enable(self, error)) {
		g_prefix_error(error, "enabling SPI write failed: ");
		return FALSE;
	}
	if (!fu_vli_device_spi_write_data(self, address, buf, bufsz, error)) {
		g_prefix_error(error, "SPI data write failed: ");
		return FALSE;
	}
	g_usleep(800);

	/* verify */
	if (!fu_vli_device_spi_read_block(self, address, buf_tmp, bufsz, error)) {
		g_prefix_error(error, "SPI data read failed: ");
		return FALSE;
	}
	return fu_common_bytes_compare_raw(buf, bufsz, buf_tmp, bufsz, error);
}

gboolean
fu_vli_device_spi_write(FuVliDevice *self,
			guint32 address,
			const guint8 *buf,
			gsize bufsz,
			FuProgress *progress,
			GError **error)
{
	FuChunk *chk;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1); /* chk0 */

	/* write SPI data, then CRC bytes last */
	g_debug("writing 0x%x bytes @0x%x", (guint)bufsz, address);
	chunks = fu_chunk_array_new(buf, bufsz, 0x0, 0x0, FU_VLI_DEVICE_TXSIZE);
	if (chunks->len > 1) {
		FuProgress *progress_local = fu_progress_get_child(progress);
		fu_progress_set_id(progress_local, G_STRLOC);
		fu_progress_set_steps(progress_local, chunks->len - 1);
		for (guint i = 1; i < chunks->len; i++) {
			chk = g_ptr_array_index(chunks, i);
			if (!fu_vli_device_spi_write_block(self,
							   fu_chunk_get_address(chk) + address,
							   fu_chunk_get_data(chk),
							   fu_chunk_get_data_sz(chk),
							   fu_progress_get_child(progress_local),
							   error)) {
				g_prefix_error(error,
					       "failed to write block 0x%x: ",
					       fu_chunk_get_idx(chk));
				return FALSE;
			}
			fu_progress_step_done(progress_local);
		}
	}
	fu_progress_step_done(progress);

	/* chk0 */
	chk = g_ptr_array_index(chunks, 0);
	if (!fu_vli_device_spi_write_block(self,
					   fu_chunk_get_address(chk) + address,
					   fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
					   fu_progress_get_child(progress),
					   error)) {
		g_prefix_error(error, "failed to write CRC block: ");
		return FALSE;
	}
	fu_progress_step_done(progress);
	return TRUE;
}

gboolean
fu_vli_device_spi_erase_all(FuVliDevice *self, FuProgress *progress, GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 99);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1);

	if (!fu_vli_device_spi_write_enable(self, error))
		return FALSE;
	if (!fu_vli_device_spi_write_status(self, 0x00, error))
		return FALSE;
	if (!fu_vli_device_spi_write_enable(self, error))
		return FALSE;
	if (!fu_vli_device_spi_chip_erase(self, error))
		return FALSE;
	fu_progress_sleep(fu_progress_get_child(progress), 4000);
	fu_progress_step_done(progress);

	/* verify chip was erased */
	for (guint addr = 0; addr < 0x10000; addr += 0x1000) {
		guint8 buf[FU_VLI_DEVICE_TXSIZE] = {0x0};
		if (!fu_vli_device_spi_read_block(self, addr, buf, sizeof(buf), error)) {
			g_prefix_error(error, "failed to read @0x%x: ", addr);
			return FALSE;
		}
		for (guint i = 0; i < sizeof(buf); i++) {
			if (buf[i] != 0xff) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "failed to verify erase @0x%x: ",
					    addr);
				return FALSE;
			}
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)addr + 0x1000,
						(gsize)0x10000);
	}
	fu_progress_step_done(progress);
	return TRUE;
}

gboolean
fu_vli_device_spi_erase(FuVliDevice *self,
			guint32 addr,
			gsize sz,
			FuProgress *progress,
			GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new(NULL, sz, addr, 0x0, 0x1000);
	g_debug("erasing 0x%x bytes @0x%x", (guint)sz, addr);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (g_getenv("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
			g_debug("erasing @0x%x", fu_chunk_get_address(chk));
		if (!fu_vli_device_spi_erase_sector(FU_VLI_DEVICE(self),
						    fu_chunk_get_address(chk),
						    error)) {
			g_prefix_error(error,
				       "failed to erase FW sector @0x%x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gchar *
fu_vli_device_get_flash_id_str(FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->spi_cmd_read_id_sz == 4)
		return g_strdup_printf("%08X", priv->flash_id);
	if (priv->spi_cmd_read_id_sz == 2)
		return g_strdup_printf("%04X", priv->flash_id);
	if (priv->spi_cmd_read_id_sz == 1)
		return g_strdup_printf("%02X", priv->flash_id);
	return g_strdup_printf("%X", priv->flash_id);
}

void
fu_vli_device_set_kind(FuVliDevice *self, FuVliDeviceKind device_kind)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	guint32 sz;

	/* set and notify if different */
	if (priv->kind != device_kind) {
		priv->kind = device_kind;
		g_object_notify(G_OBJECT(self), "kind");
	}

	/* newer chips use SHA-256 and ECDSA-256 */
	switch (device_kind) {
	case FU_VLI_DEVICE_KIND_MSP430:
	case FU_VLI_DEVICE_KIND_PS186:
	case FU_VLI_DEVICE_KIND_RTD21XX:
	case FU_VLI_DEVICE_KIND_VL100:
	case FU_VLI_DEVICE_KIND_VL101:
	case FU_VLI_DEVICE_KIND_VL102:
	case FU_VLI_DEVICE_KIND_VL103:
	case FU_VLI_DEVICE_KIND_VL104:
	case FU_VLI_DEVICE_KIND_VL105:
	case FU_VLI_DEVICE_KIND_VL120:
	case FU_VLI_DEVICE_KIND_VL210:
	case FU_VLI_DEVICE_KIND_VL211:
	case FU_VLI_DEVICE_KIND_VL212:
	case FU_VLI_DEVICE_KIND_VL810:
	case FU_VLI_DEVICE_KIND_VL811:
	case FU_VLI_DEVICE_KIND_VL811PB0:
	case FU_VLI_DEVICE_KIND_VL811PB3:
	case FU_VLI_DEVICE_KIND_VL812B0:
	case FU_VLI_DEVICE_KIND_VL812B3:
	case FU_VLI_DEVICE_KIND_VL812Q4S:
	case FU_VLI_DEVICE_KIND_VL813:
	case FU_VLI_DEVICE_KIND_VL815:
	case FU_VLI_DEVICE_KIND_VL817:
	case FU_VLI_DEVICE_KIND_VL819Q7:
	case FU_VLI_DEVICE_KIND_VL819Q8:
	case FU_VLI_DEVICE_KIND_VL820Q7:
	case FU_VLI_DEVICE_KIND_VL820Q8:
	case FU_VLI_DEVICE_KIND_VL821Q7:
	case FU_VLI_DEVICE_KIND_VL821Q8:
	case FU_VLI_DEVICE_KIND_VL822Q5:
	case FU_VLI_DEVICE_KIND_VL822Q7:
	case FU_VLI_DEVICE_KIND_VL822Q8:
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		break;
	case FU_VLI_DEVICE_KIND_VL107:
	case FU_VLI_DEVICE_KIND_VL650:
	case FU_VLI_DEVICE_KIND_VL830:
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
		break;
	default:
		g_warning("device kind %s [0x%02x] does not indicate unsigned/signed payload",
			  fu_vli_common_device_kind_to_string(device_kind),
			  device_kind);
		break;
	}

	/* set maximum firmware size */
	sz = fu_vli_common_device_kind_get_size(device_kind);
	if (sz > 0x0)
		fu_device_set_firmware_size_max(FU_DEVICE(self), sz);

	/* add extra DEV GUID too */
	fu_device_add_instance_str(FU_DEVICE(self),
				   "DEV",
				   fu_vli_common_device_kind_to_string(priv->kind));
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "USB", "VID", "PID", "DEV", NULL);
}

void
fu_vli_device_set_spi_auto_detect(FuVliDevice *self, gboolean spi_auto_detect)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	priv->spi_auto_detect = spi_auto_detect;
}

FuVliDeviceKind
fu_vli_device_get_kind(FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	return priv->kind;
}

guint32
fu_vli_device_get_offset(FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	return fu_vli_common_device_kind_get_offset(priv->kind);
}

static void
fu_vli_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuVliDevice *self = FU_VLI_DEVICE(device);
	FuVliDevicePrivate *priv = GET_PRIVATE(self);

	/* parent */
	FU_DEVICE_CLASS(fu_vli_device_parent_class)->to_string(device, idt, str);

	if (priv->kind != FU_VLI_DEVICE_KIND_UNKNOWN) {
		fu_common_string_append_kv(str,
					   idt,
					   "DeviceKind",
					   fu_vli_common_device_kind_to_string(priv->kind));
	}
	fu_common_string_append_kb(str, idt, "SpiAutoDetect", priv->spi_auto_detect);
	if (priv->flash_id != 0x0) {
		g_autofree gchar *tmp = fu_vli_device_get_flash_id_str(self);
		fu_common_string_append_kv(str, idt, "FlashId", tmp);
	}
	fu_device_add_string(FU_DEVICE(priv->cfi_device), idt + 1, str);
}

static gboolean
fu_vli_device_spi_read_flash_id(FuVliDevice *self, GError **error)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	guint8 buf[4] = {0x0};
	guint8 spi_cmd = 0x0;

	if (!fu_cfi_device_get_cmd(priv->cfi_device, FU_CFI_DEVICE_CMD_READ_ID, &spi_cmd, error))
		return FALSE;
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   0xc0 | (priv->spi_cmd_read_id_sz * 2),
					   spi_cmd,
					   0x0000,
					   buf,
					   sizeof(buf),
					   NULL,
					   FU_VLI_DEVICE_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to read chip ID: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_VLI_USBHUB_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "SpiCmdReadId", buf, sizeof(buf));
	if (priv->spi_cmd_read_id_sz == 4) {
		if (!fu_common_read_uint32_safe(buf,
						sizeof(buf),
						0x0,
						&priv->flash_id,
						G_BIG_ENDIAN,
						error))
			return FALSE;
	} else if (priv->spi_cmd_read_id_sz == 2) {
		guint16 tmp = 0;
		if (!fu_common_read_uint16_safe(buf, sizeof(buf), 0x0, &tmp, G_BIG_ENDIAN, error))
			return FALSE;
		priv->flash_id = tmp;
	} else if (priv->spi_cmd_read_id_sz == 1) {
		guint8 tmp = 0;
		if (!fu_common_read_uint8_safe(buf, sizeof(buf), 0x0, &tmp, error))
			return FALSE;
		priv->flash_id = tmp;
	}
	return TRUE;
}

static gboolean
fu_vli_device_setup(FuDevice *device, GError **error)
{
	FuVliDevice *self = FU_VLI_DEVICE(device);
	FuVliDevicePrivate *priv = GET_PRIVATE(self);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_vli_device_parent_class)->setup(device, error))
		return FALSE;

	/* get the flash chip attached */
	if (priv->spi_auto_detect) {
		if (!fu_vli_device_spi_read_flash_id(self, error)) {
			g_prefix_error(error, "failed to read SPI chip ID: ");
			return FALSE;
		}
		if (priv->flash_id != 0x0) {
			g_autofree gchar *flash_id = fu_vli_device_get_flash_id_str(self);

			/* use the correct flash device */
			fu_cfi_device_set_flash_id(priv->cfi_device, flash_id);
			if (!fu_device_setup(FU_DEVICE(priv->cfi_device), error))
				return FALSE;

			/* add extra instance IDs to include the SPI variant */
			fu_device_add_instance_str(device, "SPI", flash_id);
			if (!fu_device_build_instance_id(device,
							 error,
							 "USB",
							 "VID",
							 "PID",
							 "SPI",
							 NULL))
				return FALSE;
			fu_device_build_instance_id(device,
						    NULL,
						    "USB",
						    "VID",
						    "PID",
						    "SPI",
						    "REV",
						    NULL);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vli_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuVliDevice *self = FU_VLI_DEVICE(device);
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp = 0;

	if (g_strcmp0(key, "CfiDeviceCmdReadIdSz") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->spi_cmd_read_id_sz = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "VliSpiAutoDetect") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->spi_auto_detect = tmp > 0;
		return TRUE;
	}
	if (g_strcmp0(key, "VliDeviceKind") == 0) {
		FuVliDeviceKind device_kind;
		device_kind = fu_vli_common_device_kind_from_string(value);
		if (device_kind == FU_VLI_DEVICE_KIND_UNKNOWN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "VliDeviceKind %s is not supported",
				    value);
			return FALSE;
		}
		fu_vli_device_set_kind(self, device_kind);
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_vli_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	FuVliDevice *self = FU_VLI_DEVICE(device);
	g_hash_table_insert(metadata, g_strdup("GType"), g_strdup(G_OBJECT_TYPE_NAME(self)));
}

static void
fu_vli_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuVliDevice *self = FU_VLI_DEVICE(object);
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_KIND:
		g_value_set_uint(value, priv->kind);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_vli_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuVliDevice *self = FU_VLI_DEVICE(object);
	switch (prop_id) {
	case PROP_KIND:
		fu_vli_device_set_kind(self, g_value_get_uint(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_vli_device_constructed(GObject *obj)
{
	FuVliDevice *self = FU_VLI_DEVICE(obj);
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	priv->cfi_device = fu_cfi_device_new(fu_device_get_context(FU_DEVICE(self)), NULL);
}

static void
fu_vli_device_init(FuVliDevice *self)
{
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	priv->spi_cmd_read_id_sz = 2;
	priv->spi_auto_detect = TRUE;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER);
}

static void
fu_vli_device_finalize(GObject *obj)
{
	FuVliDevice *self = FU_VLI_DEVICE(obj);
	FuVliDevicePrivate *priv = GET_PRIVATE(self);
	g_object_unref(priv->cfi_device);
	G_OBJECT_CLASS(fu_vli_device_parent_class)->finalize(obj);
}

static void
fu_vli_device_class_init(FuVliDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	/* properties */
	object_class->get_property = fu_vli_device_get_property;
	object_class->set_property = fu_vli_device_set_property;
	object_class->constructed = fu_vli_device_constructed;
	object_class->finalize = fu_vli_device_finalize;

	/**
	 * FuVliDevice:kind:
	 *
	 * The kind of VLI device.
	 */
	pspec = g_param_spec_uint("kind",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT,
				  0,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_KIND, pspec);

	klass_device->to_string = fu_vli_device_to_string;
	klass_device->set_quirk_kv = fu_vli_device_set_quirk_kv;
	klass_device->setup = fu_vli_device_setup;
	klass_device->report_metadata_pre = fu_vli_device_report_metadata_pre;
}

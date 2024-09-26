/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Kevin Chen <hsinfu.chen@qsitw.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qsi-dock-child-device.h"
#include "fu-qsi-dock-common.h"
#include "fu-qsi-dock-mcu-device.h"

struct _FuQsiDockMcuDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuQsiDockMcuDevice, fu_qsi_dock_mcu_device, FU_TYPE_HID_DEVICE)

#define FU_QSI_DOCK_MCU_DEVICE_TIMEOUT 90000 /* ms */

#define FU_QSI_DOCK_TX_ISP_LENGTH	   61
#define FU_QSI_DOCK_TX_ISP_LENGTH_MCU	   60
#define FU_QSI_DOCK_EXTERN_FLASH_PAGE_SIZE 256

static gboolean
fu_qsi_dock_mcu_device_tx(FuQsiDockMcuDevice *self,
			  guint8 CmdPrimary,
			  guint8 CmdSecond,
			  const guint8 *inbuf,
			  gsize inbufsz,
			  GError **error)
{
	guint8 buf[64] = {FU_QSI_DOCK_REPORT_ID, CmdPrimary, CmdSecond};

	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					FU_QSI_DOCK_REPORT_ID,
					buf,
					sizeof(buf),
					FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					error);
}

static gboolean
fu_qsi_dock_mcu_device_rx(FuQsiDockMcuDevice *self, guint8 *outbuf, gsize outbufsz, GError **error)
{
	guint8 buf[64];

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_QSI_DOCK_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error)) {
		return FALSE;
	}
	if (outbuf != NULL) {
		if (!fu_memcpy_safe(outbuf,
				    outbufsz,
				    0x0, /* dst */
				    buf,
				    sizeof(buf),
				    0x5, /* src */
				    outbufsz,
				    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_txrx(FuQsiDockMcuDevice *self,
			    guint8 cmd1,
			    guint8 cmd2,
			    const guint8 *inbuf,
			    gsize inbufsz,
			    guint8 *outbuf,
			    gsize outbufsz,
			    GError **error)
{
	if (!fu_qsi_dock_mcu_device_tx(self, cmd1, cmd2, inbuf, inbufsz, error))
		return FALSE;
	return fu_qsi_dock_mcu_device_rx(self, outbuf, outbufsz, error);
}

static gboolean
fu_qsi_dock_mcu_device_get_status(FuQsiDockMcuDevice *self, GError **error)
{
	guint8 response = 0;
	guint8 cmd1 = FU_QSI_DOCK_CMD1_MCU;
	guint8 cmd2 = FU_QSI_DOCK_CMD2_CMD_DEVICE_STATUS;

	if (!fu_qsi_dock_mcu_device_txrx(self,
					 cmd1,
					 cmd2,
					 &cmd1,
					 sizeof(cmd1),
					 &response,
					 sizeof(response),
					 error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_enumerate_children(FuQsiDockMcuDevice *self, GError **error)
{
	guint8 outbuf[49] = {0};
	struct {
		const gchar *name;
		guint8 chip_idx;
		gsize offset;
	} components[] = {
	    {"MCU", FU_QSI_DOCK_FIRMWARE_IDX_MCU, G_STRUCT_OFFSET(FuQsiDockIspVersionInMcu, MCU)},
	    {"bcdVersion",
	     FU_QSI_DOCK_FIRMWARE_IDX_NONE,
	     G_STRUCT_OFFSET(FuQsiDockIspVersionInMcu, bcdVersion)},
	    {NULL, 0, 0}};

	for (guint i = 0; components[i].name != NULL; i++) {
		const guint8 *val = outbuf + components[i].offset;
		g_autofree gchar *version = NULL;
		g_autoptr(FuDevice) child = NULL;

		child = fu_qsi_dock_child_device_new(fu_device_get_context(FU_DEVICE(self)));
		if (g_strcmp0(components[i].name, "bcdVersion") == 0) {
			if ((val[0] == 0x00 && val[1] == 0x00) ||
			    (val[0] == 0xFF && val[1] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}

			version = g_strdup_printf("%x.%x.%02x", val[0] & 0xFu, val[0] >> 4, val[1]);
			g_debug("ignoring %s --> %s", components[i].name, version);

			continue;
		}

		if (g_strcmp0(components[i].name, "MCU") == 0) {
			if ((val[0] == 0x00 && val[1] == 0x00) ||
			    (val[0] == 0xFF && val[1] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}

			version = g_strdup_printf("%X.%X", val[0], val[1]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_PLAIN);

			fu_device_set_version(child, version);
			fu_device_set_name(child, "Dock Management Controller");
		} else {
			g_warning("unhandled %s", components[i].name);
		}

		/* add virtual device */
		fu_device_add_instance_u16(child, "VID", fu_device_get_vid(FU_DEVICE(self)));
		fu_device_add_instance_u16(child, "PID", fu_device_get_pid(FU_DEVICE(self)));
		fu_device_add_instance_str(child, "CID", components[i].name);
		if (!fu_device_build_instance_id(child, error, "USB", "VID", "PID", "CID", NULL))
			return FALSE;
		if (fu_device_get_name(child) == NULL)
			fu_device_set_name(child, components[i].name);
		fu_device_set_logical_id(child, components[i].name);
		fu_qsi_dock_child_device_set_chip_idx(FU_QSI_DOCK_CHILD_DEVICE(child),
						      components[i].chip_idx);
		fu_device_add_child(FU_DEVICE(self), child);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_setup(FuDevice *device, GError **error)
{
	FuQsiDockMcuDevice *self = FU_QSI_DOCK_MCU_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_qsi_dock_mcu_device_parent_class)->setup(device, error))
		return FALSE;

	/* get status and component versions */
	if (!fu_qsi_dock_mcu_device_get_status(self, error))
		return FALSE;
	if (!fu_qsi_dock_mcu_device_enumerate_children(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_checksum(FuQsiDockMcuDevice *self,
				guint32 checksum,
				guint32 length,
				GError **error)
{
	guint8 buf[64] = {FU_QSI_DOCK_REPORT_ID,
			  FU_QSI_DOCK_CMD1_SPI,
			  FU_QSI_DOCK_CMD2_SPI_EXTERNAL_FLASH_CHECKSUM,
			  0};
	guint8 fw_length[4];
	guint8 checksum_val[2];

	fu_memwrite_uint32(fw_length, length, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(checksum_val, checksum, G_LITTLE_ENDIAN);

	/* fw length */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0x03, /* dst */
			    fw_length,
			    sizeof(fw_length),
			    0x0,
			    sizeof(fw_length),
			    error))
		return FALSE;

	/* checksum */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0x07, /* dst */
			    checksum_val,
			    sizeof(checksum_val),
			    0x0,
			    sizeof(checksum_val),
			    error))
		return FALSE;

	/* SetReport+GetReport */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_QSI_DOCK_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;
	memset(buf, 0x0, sizeof(buf));
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_QSI_DOCK_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;

	/* MCU Checksum Compare Result 0:Pass 1:Fail */
	if (buf[2] != 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "checksum did not match");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_write_chunk(FuQsiDockMcuDevice *self,
				   FuChunk *chk_page,
				   guint32 *checksum_tmp,
				   FuProgress *progress,
				   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) chk_bytes = fu_chunk_get_bytes(chk_page);

	chunks = fu_chunk_array_new_from_bytes(chk_bytes, 0x0, FU_QSI_DOCK_TX_ISP_LENGTH_MCU);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		guint8 checksum_buf[FU_QSI_DOCK_TX_ISP_LENGTH_MCU] = {0x0};
		guint8 buf[64] = {
		    FU_QSI_DOCK_REPORT_ID,
		    FU_QSI_DOCK_CMD1_MASS_SPI,
		};

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		buf[2] = fu_chunk_get_data_sz(chk);

		/* SetReport */
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x04, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      FU_QSI_DOCK_REPORT_ID,
					      buf,
					      sizeof(buf),
					      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error))
			return FALSE;

		/* sum checksum value */
		if (!fu_memcpy_safe(checksum_buf,
				    sizeof(checksum_buf),
				    0x0, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		*checksum_tmp += fu_sum32(checksum_buf, sizeof(checksum_buf));

		/* GetReport */
		memset(buf, 0x0, sizeof(buf));
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      FU_QSI_DOCK_REPORT_ID,
					      buf,
					      sizeof(buf),
					      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error))
			return FALSE;

		/* MCU ACK 0:Pass 1:Fail */
		if (buf[2] != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "ACK error for chunk %u",
				    i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_write_chunks(FuQsiDockMcuDevice *self,
				    FuChunkArray *chunks,
				    guint32 *checksum,
				    FuProgress *progress,
				    GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_qsi_dock_mcu_device_write_chunk(self,
							chk,
							checksum,
							fu_progress_get_child(progress),
							error)) {
			g_prefix_error(error, "failed to write chunk 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_wait_for_spi_initial_ready_cb(FuDevice *device,
						     gpointer user_data,
						     GError **error)
{
	FuQsiDockMcuDevice *self = FU_QSI_DOCK_MCU_DEVICE(device);
	guint8 buf[64] = {FU_QSI_DOCK_REPORT_ID,
			  FU_QSI_DOCK_CMD1_SPI,
			  FU_QSI_DOCK_CMD2_SPI_EXTERNAL_FLASH_INI};

	/* SetReport+GetReport */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_QSI_DOCK_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;
	memset(buf, 0x0, sizeof(buf));
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_QSI_DOCK_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_wait_for_spi_erase_ready_cb(FuQsiDockMcuDevice *self,
						   guint32 length,
						   gpointer user_data,
						   GError **error)
{
	guint8 buf[64] = {FU_QSI_DOCK_REPORT_ID,
			  FU_QSI_DOCK_CMD1_SPI,
			  FU_QSI_DOCK_CMD2_SPI_EXTERNAL_FLASH_ERASE};
	guint32 offset = 0;
	guint8 fw_length[4];
	guint8 flash_offset[4];

	fu_memwrite_uint32(fw_length, length, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(flash_offset, offset, G_LITTLE_ENDIAN);

	/* write erase flash size */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0x03, /* dst */
			    fw_length,
			    sizeof(fw_length),
			    0x0,
			    sizeof(fw_length),
			    error))
		return FALSE;
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0x07, /* dst */
			    flash_offset,
			    sizeof(flash_offset),
			    0x0,
			    sizeof(flash_offset),
			    error))
		return FALSE;

	/* SetReport+GetReport */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_QSI_DOCK_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;
	memset(buf, 0x0, sizeof(buf));
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_QSI_DOCK_REPORT_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
fu_qsi_dock_mcu_device_write_firmware_with_idx(FuQsiDockMcuDevice *self,
					       FuFirmware *firmware,
					       guint8 chip_idx,
					       FuProgress *progress,
					       FwupdInstallFlags flags,
					       GError **error)
{
	guint32 checksum_val = 0;
	g_autoptr(GBytes) fw_align = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);

	/* align data */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	fw_align = fu_bytes_align(fw, FU_QSI_DOCK_EXTERN_FLASH_PAGE_SIZE, 0x0);

	/* initial external flash */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_qsi_dock_mcu_device_wait_for_spi_initial_ready_cb,
			     30,
			     NULL,
			     error)) {
		g_prefix_error(error, "failed to wait for initial: ");
		return FALSE;
	}
	if (!fu_qsi_dock_mcu_device_wait_for_spi_erase_ready_cb(self,
								g_bytes_get_size(fw),
								fu_progress_get_child(progress),
								error))
		return FALSE;

	/* write external flash */
	chunks = fu_chunk_array_new_from_bytes(fw_align, 0, FU_QSI_DOCK_EXTERN_FLASH_PAGE_SIZE);
	if (!fu_qsi_dock_mcu_device_write_chunks(self,
						 chunks,
						 &checksum_val,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify flash data */
	if (!fu_qsi_dock_mcu_device_checksum(self, checksum_val, g_bytes_get_size(fw), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	return fu_qsi_dock_mcu_device_write_firmware_with_idx(FU_QSI_DOCK_MCU_DEVICE(device),
							      firmware,
							      0xFF,
							      progress,
							      flags,
							      error);
}

static gboolean
fu_qsi_dock_mcu_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuQsiDockMcuDevice *self = FU_QSI_DOCK_MCU_DEVICE(device);

	if (!fu_qsi_dock_mcu_device_get_status(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_qsi_dock_mcu_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_qsi_dock_mcu_device_init(FuQsiDockMcuDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_AUTODETECT_EPS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_SERIAL_NUMBER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_add_protocol(FU_DEVICE(self), "com.qsi.dock");
}

static void
fu_qsi_dock_mcu_device_class_init(FuQsiDockMcuDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->setup = fu_qsi_dock_mcu_device_setup;
	device_class->attach = fu_qsi_dock_mcu_device_attach;
	device_class->set_progress = fu_qsi_dock_mcu_device_set_progress;
	device_class->write_firmware = fu_qsi_dock_mcu_device_write_firmware;
}

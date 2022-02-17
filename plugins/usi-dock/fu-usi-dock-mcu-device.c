/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Victor Cheng <victor_cheng@usiglobal.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-usi-dock-child-device.h"
#include "fu-usi-dock-common.h"
#include "fu-usi-dock-dmc-device.h"
#include "fu-usi-dock-mcu-device.h"

struct _FuUsiDockMcuDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuUsiDockMcuDevice, fu_usi_dock_mcu_device, FU_TYPE_HID_DEVICE)

#define FU_USI_DOCK_MCU_DEVICE_TIMEOUT 5000 /* ms */

#define FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP (1 << 0)

static gboolean
fu_usi_dock_mcu_device_tx(FuUsiDockMcuDevice *self,
			  guint8 tag2,
			  const guint8 *inbuf,
			  gsize inbufsz,
			  GError **error)
{
	UsiDockSetReportBuf tx_buffer = {
	    .id = USB_HID_REPORT_ID2,
	    .length = 0x3 + inbufsz,
	    .mcutag1 = 0xFE,
	    .mcutag2 = 0xFF,
	    .mcutag3 = tag2,
	};

	if (inbuf != NULL) {
		if (!fu_memcpy_safe(tx_buffer.inbuf,
				    sizeof(tx_buffer.inbuf),
				    0x0, /* dst */
				    inbuf,
				    inbufsz,
				    0x0, /* src */
				    inbufsz,
				    error))
			return FALSE;
	}

	/* special cases */
	if (tx_buffer.inbuf[0] == USBUID_ISP_INTERNAL_FW_CMD_UPDATE_FW) {
		tx_buffer.inbuf[1] = 0xFF;
	}

	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					USB_HID_REPORT_ID2,
					(guint8 *)&tx_buffer,
					sizeof(tx_buffer),
					FU_USI_DOCK_MCU_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

static gboolean
fu_usi_dock_mcu_device_rx(FuUsiDockMcuDevice *self,
			  guint8 cmd,
			  guint8 *outbuf,
			  gsize outbufsz,
			  GError **error)
{
	guint8 buf[64] = {0};

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      buf,
				      sizeof(buf),
				      FU_USI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		return FALSE;
	}
	if (buf[0] != USB_HID_REPORT_ID2) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid ID, expected 0x%02x, got 0x%02x",
			    USB_HID_REPORT_ID2,
			    buf[0]);
		return FALSE;
	}
	if (buf[2] != 0xFE || buf[3] != 0xFF) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid tags, expected 0x%02x:0x%02x, got 0x%02x:0x%02x",
			    0xFEu,
			    0xFFu,
			    buf[2],
			    buf[3]);
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
fu_usi_dock_mcu_device_txrx(FuUsiDockMcuDevice *self,
			    guint8 tag2,
			    const guint8 *inbuf,
			    gsize inbufsz,
			    guint8 *outbuf,
			    gsize outbufsz,
			    GError **error)
{
	if (!fu_usi_dock_mcu_device_tx(self, tag2, inbuf, inbufsz, error))
		return FALSE;
	return fu_usi_dock_mcu_device_rx(self, USBUID_ISP_CMD_ALL, outbuf, outbufsz, error);
}

static gboolean
fu_usi_dock_mcu_device_get_status(FuUsiDockMcuDevice *self, GError **error)
{
	guint8 cmd = USBUID_ISP_DEVICE_CMD_MCU_STATUS;
	guint8 response = 0;

	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_MCU,
					 &cmd,
					 sizeof(cmd),
					 &response,
					 sizeof(response),
					 error))
		return FALSE;
	if (response == 0x1) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "device is busy");
		return FALSE;
	}
	if (response == 0xFF) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "device timed out");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_enumerate_children(FuUsiDockMcuDevice *self, GError **error)
{
	guint8 inbuf[] = {USBUID_ISP_DEVICE_CMD_READ_MCU_VERSIONPAGE,
			  DP_VERSION_FROM_MCU | NIC_VERSION_FROM_MCU};
	guint8 outbuf[49] = {0x0};
	struct {
		const gchar *name;
		guint8 chip_idx;
		gsize offset;
	} components[] = {
	    {"DMC", FIRMWARE_IDX_DMC_PD, G_STRUCT_OFFSET(IspVersionInMcu_t, DMC)},
	    {"PD", FIRMWARE_IDX_DP, G_STRUCT_OFFSET(IspVersionInMcu_t, PD)},
	    {"DP5x", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, DP5x)},
	    {"DP6x", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, DP6x)},
	    {"TBT4", FIRMWARE_IDX_TBT4, G_STRUCT_OFFSET(IspVersionInMcu_t, TBT4)},
	    {"USB3", FIRMWARE_IDX_USB3, G_STRUCT_OFFSET(IspVersionInMcu_t, USB3)},
	    {"USB2", FIRMWARE_IDX_USB2, G_STRUCT_OFFSET(IspVersionInMcu_t, USB2)},
	    {"AUDIO", FIRMWARE_IDX_AUDIO, G_STRUCT_OFFSET(IspVersionInMcu_t, AUDIO)},
	    {"I255", FIRMWARE_IDX_I225, G_STRUCT_OFFSET(IspVersionInMcu_t, I255)},
	    {"MCU", FIRMWARE_IDX_MCU, G_STRUCT_OFFSET(IspVersionInMcu_t, MCU)},
	    {"bcdVersion", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, bcdVersion)},
	    {NULL, 0, 0}};

	/* assume DP and NIC in-use */
	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_MCU,
					 inbuf,
					 sizeof(inbuf),
					 outbuf,
					 sizeof(outbuf),
					 error))
		return FALSE;

	for (guint i = 0; components[i].name != NULL; i++) {
		const guint8 *val = outbuf + components[i].offset;
		g_autofree gchar *version = NULL;
		g_autoptr(FuDevice) child = NULL;

		child = fu_usi_dock_child_new(fu_device_get_context(FU_DEVICE(self)));
		if (g_strcmp0(components[i].name, "bcdVersion") == 0) {
			if ((val[0] == 0x00 && val[1] == 0x00) ||
			    (val[0] == 0xFF && val[1] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			if (fu_device_has_private_flag(FU_DEVICE(self),
						       FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
				version = g_strdup_printf("%x.%x.%x.%x",
							  val[0] >> 4,
							  val[0] & 0xFu,
							  val[1] >> 4,
							  val[1] & 0xFu);
				fu_device_set_version_format(FU_DEVICE(self),
							     FWUPD_VERSION_FORMAT_QUAD);
				fu_device_set_version(FU_DEVICE(self), version);
			} else {
				version = g_strdup_printf("%x.%x.%02x",
							  val[0] & 0xFu,
							  val[0] >> 4,
							  val[1]);
				g_debug("ignoring %s --> %s", components[i].name, version);
			}
			continue;
		} else if (g_strcmp0(components[i].name, "DMC") == 0) {
			if ((val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) ||
			    (val[2] == 0xFF && val[3] == 0xFF && val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%d.%d.%d", val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Dock Management Controller");
		} else if (g_strcmp0(components[i].name, "PD") == 0) {
			if ((val[1] == 0x00 && val[2] == 0x00 && val[3] == 0x00 &&
			     val[4] == 0x00) ||
			    (val[1] == 0xFF && val[2] == 0xFF && val[3] == 0xFF &&
			     val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			if (fu_device_has_private_flag(FU_DEVICE(self),
						       FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
				version =
				    g_strdup_printf("%d.%d.%d.%d", val[3], val[4], val[1], val[2]);
				fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_QUAD);
			} else {
				version = g_strdup_printf("%d.%d.%d", val[2], val[3], val[4]);
				fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			}
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Power Delivery");
		} else if (g_strcmp0(components[i].name, "TBT4") == 0) {
			if ((val[1] == 0x00 && val[2] == 0x00 && val[3] == 0x00) ||
			    (val[1] == 0xFF && val[2] == 0xFF && val[3] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%02x.%02x.%02x", val[1], val[2], val[3]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "thunderbolt");
			fu_device_set_name(child, "Thunderbolt 4 Controller");
		} else if (g_strcmp0(components[i].name, "DP5x") == 0) {
			if ((val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) ||
			    (val[2] == 0xFF && val[3] == 0xFF && val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%d.%02d.%03d", val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "video-display");
			fu_device_set_name(child, "Display Port 5");
		} else if (g_strcmp0(components[i].name, "DP6x") == 0) {
			if ((val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) ||
			    (val[2] == 0xFF && val[3] == 0xFF && val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			if (fu_device_has_private_flag(FU_DEVICE(self),
						       FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
				version =
				    g_strdup_printf("%x.%x.%x.%x", val[3], val[4], val[2], val[1]);
				fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_QUAD);
				fu_device_set_name(child, "USB/PD HUB");
			} else {
				version = g_strdup_printf("%d.%02d.%03d", val[2], val[3], val[4]);
				fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
				fu_device_set_name(child, "Display Port 6");
			}
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "video-display");
		} else if (g_strcmp0(components[i].name, "USB3") == 0) {
			if ((val[3] == 0x00 && val[4] == 0x00) ||
			    (val[3] == 0xFF && val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%02X%02X", val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_NUMBER);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "USB 3 Hub");
		} else if (g_strcmp0(components[i].name, "USB2") == 0) {
			if ((val[0] == 0x00 && val[1] == 0x00 && val[2] == 0x00 && val[3] == 0x00 &&
			     val[4] == 0x00) ||
			    (val[0] == 0xFF && val[1] == 0xFF && val[2] == 0xFF && val[3] == 0xFF &&
			     val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version =
			    g_strdup_printf("%c%c%c%c%c", val[0], val[1], val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "USB 2 Hub");
		} else if (g_strcmp0(components[i].name, "AUDIO") == 0) {
			if ((val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) ||
			    (val[2] == 0xFF && val[3] == 0xFF && val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%02X-%02X-%02X", val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Audio Controller");
		} else if (g_strcmp0(components[i].name, "I255") == 0) {
			if ((val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) ||
			    (val[2] == 0xFF && val[3] == 0xFF && val[4] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%x.%x.%x", val[2] >> 4, val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "network-wired");
			fu_device_set_name(child, "Ethernet Adapter");
		} else if (g_strcmp0(components[i].name, "MCU") == 0) {
			if ((val[0] == 0x00 && val[1] == 0x00) ||
			    (val[0] == 0xFF && val[1] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			if (fu_device_has_private_flag(FU_DEVICE(self),
						       FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
				version = g_strdup_printf("%x.%x.%x.%x",
							  val[0] >> 4,
							  val[0] & 0xFu,
							  val[1] >> 4,
							  val[1] & 0xFu);
				fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_QUAD);
			} else {
				version = g_strdup_printf("%X.%X", val[0], val[1]);
				fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_PLAIN);
			}
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Dock Management Controller");
		} else {
			g_warning("unhandled %s", components[i].name);
		}

		/* add virtual device */
		fu_device_add_instance_u16(child,
					   "VID",
					   fu_usb_device_get_vid(FU_USB_DEVICE(self)));
		fu_device_add_instance_u16(child,
					   "PID",
					   fu_usb_device_get_pid(FU_USB_DEVICE(self)));
		fu_device_add_instance_str(child, "CID", components[i].name);
		if (!fu_device_build_instance_id(child, error, "USB", "VID", "PID", "CID", NULL))
			return FALSE;
		if (fu_device_get_name(child) == NULL)
			fu_device_set_name(child, components[i].name);
		fu_device_set_logical_id(child, components[i].name);
		fu_usi_dock_child_device_set_chip_idx(FU_USI_DOCK_CHILD_DEVICE(child),
						      components[i].chip_idx);
		fu_device_add_child(FU_DEVICE(self), child);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_setup(FuDevice *device, GError **error)
{
	FuUsiDockMcuDevice *self = FU_USI_DOCK_MCU_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_usi_dock_mcu_device_parent_class)->setup(device, error))
		return FALSE;

	/* get status and component versions */
	if (!fu_usi_dock_mcu_device_get_status(self, error))
		return FALSE;
	if (!fu_usi_dock_mcu_device_enumerate_children(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_write_chunk(FuUsiDockMcuDevice *self, FuChunk *chk, GError **error)
{
	guint8 buf[64] = {0x0};
	guint32 length = 0;
	guint32 pagesize = fu_chunk_get_data_sz(chk);

	while (pagesize != 0) {
		memset(buf, 0x0, sizeof(buf));
		buf[63] = TAG_TAG2_MASS_DATA_SPI;
		buf[0] = USB_HID_REPORT_ID2;

		/* set length and buffer */
		if (pagesize >= TX_ISP_LENGTH) {
			length = TX_ISP_LENGTH;
			pagesize -= TX_ISP_LENGTH;
		} else {
			length = pagesize;
			pagesize = 0;
		}
		buf[1] = length;

		/* SetReport */
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x2, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    fu_chunk_get_data_sz(chk) - pagesize - length, /* src */
				    length,
				    error))
			return FALSE;

		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      USB_HID_REPORT_ID2,
					      buf,
					      sizeof(buf),
					      FU_USI_DOCK_MCU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_NONE,
					      error))
			return FALSE;

		/* GetReport */
		memset(buf, 0x0, sizeof(buf));
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      USB_HID_REPORT_ID2,
					      buf,
					      sizeof(buf),
					      FU_USI_DOCK_MCU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_NONE,
					      error)) {
			return FALSE;
		}
		if (buf[0] != USB_HID_REPORT_ID2) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid ID, expected 0x%02x, got 0x%02x",
				    USB_HID_REPORT_ID2,
				    buf[0]);
			return FALSE;
		}
		if (buf[63] != TAG_TAG2_CMD_SPI) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid tag2, expected 0x%02x, got 0x%02x",
				    (guint)TAG_TAG2_CMD_SPI,
				    buf[58]);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_write_chunks(FuUsiDockMcuDevice *self,
				    GPtrArray *chunks,
				    FuProgress *progress,
				    GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_usi_dock_mcu_device_write_chunk(self, chk, error)) {
			g_prefix_error(error, "failed to write chunk 0x%x", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_wait_for_spi_ready_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuUsiDockMcuDevice *self = FU_USI_DOCK_MCU_DEVICE(device);
	guint8 buf[] = {USBUID_ISP_DEVICE_CMD_FWBUFER_READ_STATUS};
	guint8 val = 0;

	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_SPI,
					 buf,
					 sizeof(buf),
					 &val,
					 sizeof(val),
					 error))
		return FALSE;
	if (val != SPI_STATE_READY) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_BUSY,
			    "SPI state is %s [0x%02x]",
			    fu_usi_dock_spi_state_to_string(val),
			    val);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_wait_for_checksum_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuUsiDockMcuDevice *self = FU_USI_DOCK_MCU_DEVICE(device);

	if (!fu_usi_dock_mcu_device_rx(self,
				       USBUID_ISP_CMD_ALL,
				       (guint8 *)user_data,
				       sizeof(guint8),
				       error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
fu_usi_dock_mcu_device_write_firmware_with_idx(FuUsiDockMcuDevice *self,
					       FuFirmware *firmware,
					       guint8 chip_idx,
					       FuProgress *progress,
					       FwupdInstallFlags flags,
					       GError **error)
{
	guint8 cmd;
	guint8 val = 0x0;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	guint8 checksum = 0xFF;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 6);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 40);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 13);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 42);

	/* initial external flash */
	cmd = USBUID_ISP_DEVICE_CMD_FWBUFER_INITIAL;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_SPI,
					 &cmd,
					 sizeof(cmd),
					 &val,
					 sizeof(val),
					 error))
		return FALSE;
	if (val != SPI_STATE_READY) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid state for CMD_FWBUFER_INITIAL, got 0x%02x",
			    val);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* erase external flash */
	cmd = USBUID_ISP_DEVICE_CMD_FWBUFER_ERASE_FLASH;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_SPI,
					 &cmd,
					 sizeof(cmd),
					 NULL,
					 0x0,
					 error))
		return FALSE;
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_usi_dock_mcu_device_wait_for_spi_ready_cb,
			     30,
			     NULL,
			     error)) {
		g_prefix_error(error, "failed to wait for erase: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write external flash */
	cmd = USBUID_ISP_DEVICE_CMD_FWBUFER_PROGRAM;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_SPI,
					 &cmd,
					 sizeof(cmd),
					 NULL,
					 0x0,
					 error))
		return FALSE;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, W25Q16DV_PAGE_SIZE);
	if (!fu_usi_dock_mcu_device_write_chunks(self,
						 chunks,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* file transfer – finished */
	cmd = USBUID_ISP_DEVICE_CMD_FWBUFER_TRANSFER_FINISH;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_SPI,
					 &cmd,
					 sizeof(cmd),
					 NULL,
					 0x0,
					 error))
		return FALSE;

	/* MCU checksum */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_usi_dock_mcu_device_wait_for_checksum_cb,
			     300,
			     &checksum,
			     error)) {
		g_prefix_error(error, "failed to wait for checksum: ");
		return FALSE;
	}

	if (checksum != 0x0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid checksum result for CMD_FWBUFER_CHECKSUM, got 0x%02x",
			    checksum);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* internal flash */
	cmd = USBUID_ISP_INTERNAL_FW_CMD_UPDATE_FW;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 TAG_TAG2_CMD_MCU,
					 &cmd,
					 sizeof(cmd),
					 NULL,
					 0x0,
					 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	return fu_usi_dock_mcu_device_write_firmware_with_idx(FU_USI_DOCK_MCU_DEVICE(device),
							      firmware,
							      0xFF, /* all */
							      progress,
							      flags,
							      error);
}

static gboolean
fu_usi_dock_mcu_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	fu_device_set_remove_delay(device, 500000);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static void
fu_usi_dock_mcu_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 2);	/* erase */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 6); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2);	/* reload */
}

static void
fu_usi_dock_mcu_device_init(FuUsiDockMcuDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);

	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_INHIBIT_CHILDREN);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);

	fu_device_register_private_flag(FU_DEVICE(self),
					FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP,
					"verfmt-hp");
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER);
	fu_device_add_protocol(FU_DEVICE(self), "com.usi.dock");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_device_add_icon(FU_DEVICE(self), "dock");
}

static void
fu_usi_dock_mcu_device_class_init(FuUsiDockMcuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->write_firmware = fu_usi_dock_mcu_device_write_firmware;
	klass_device->attach = fu_usi_dock_mcu_device_attach;
	klass_device->setup = fu_usi_dock_mcu_device_setup;
	klass_device->set_progress = fu_usi_dock_mcu_device_set_progress;
}

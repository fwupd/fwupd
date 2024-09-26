/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Victor Cheng <victor_cheng@usiglobal.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-usi-dock-child-device.h"
#include "fu-usi-dock-dmc-device.h"
#include "fu-usi-dock-mcu-device.h"
#include "fu-usi-dock-struct.h"

struct _FuUsiDockMcuDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuUsiDockMcuDevice, fu_usi_dock_mcu_device, FU_TYPE_HID_DEVICE)

#define FU_USI_DOCK_MCU_DEVICE_TIMEOUT 5000 /* ms */

#define USB_HID_REPORT_ID1 1u
#define USB_HID_REPORT_ID2 2u

#define DP_VERSION_FROM_MCU  0x01 /* if in use */
#define NIC_VERSION_FROM_MCU 0x2  /* if in use */

#define W25Q16DV_PAGE_SIZE 256

#define FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP	   "verfmt-hp"
#define FU_USI_DOCK_DEVICE_FLAG_SET_CHIP_TYPE	   "set-chip-type"
#define FU_USI_DOCK_DEVICE_FLAG_WAITING_FOR_UNPLUG "waiting-for-unplug"

static gboolean
fu_usi_dock_mcu_device_tx(FuUsiDockMcuDevice *self,
			  FuUsiDockTag2 tag2,
			  const guint8 *buf,
			  gsize bufsz,
			  GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_usi_dock_mcu_cmd_req_new();

	fu_struct_usi_dock_mcu_cmd_req_set_length(st, 0x3 + bufsz);
	fu_struct_usi_dock_mcu_cmd_req_set_tag3(st, tag2);
	if (buf != NULL) {
		if (!fu_struct_usi_dock_mcu_cmd_req_set_buf(st, buf, bufsz, error))
			return FALSE;
	}

	/* special cases */
	if (st->data[FU_STRUCT_USI_DOCK_MCU_CMD_REQ_OFFSET_BUF + 0] ==
	    FU_USI_DOCK_MCU_CMD_FW_UPDATE)
		st->data[FU_STRUCT_USI_DOCK_MCU_CMD_REQ_OFFSET_BUF + 1] = 0xFF;

	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					USB_HID_REPORT_ID2,
					st->data,
					st->len,
					FU_USI_DOCK_MCU_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
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
	g_autoptr(GByteArray) st_rsp = NULL;

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      buf,
				      sizeof(buf),
				      FU_USI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER |
					  FU_HID_DEVICE_FLAG_RETRY_FAILURE,
				      error)) {
		return FALSE;
	}
	st_rsp = fu_struct_usi_dock_mcu_cmd_res_parse(buf, sizeof(buf), 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (outbuf != NULL) {
		if (!fu_memcpy_safe(outbuf,
				    outbufsz,
				    0x0, /* dst */
				    buf,
				    sizeof(buf),
				    FU_STRUCT_USI_DOCK_MCU_CMD_RES_OFFSET_BUF, /* src */
				    outbufsz,
				    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_txrx(FuUsiDockMcuDevice *self,
			    FuUsiDockTag2 tag2,
			    const guint8 *inbuf,
			    gsize inbufsz,
			    guint8 *outbuf,
			    gsize outbufsz,
			    GError **error)
{
	if (!fu_usi_dock_mcu_device_tx(self, tag2, inbuf, inbufsz, error)) {
		g_prefix_error(error, "failed to transmit: ");
		return FALSE;
	}
	if (!fu_usi_dock_mcu_device_rx(self, FU_USI_DOCK_MCU_CMD_ALL, outbuf, outbufsz, error)) {
		g_prefix_error(error, "failed to receive: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_get_status(FuUsiDockMcuDevice *self, GError **error)
{
	guint8 cmd = FU_USI_DOCK_MCU_CMD_MCU_STATUS;
	guint8 response = 0;

	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_MCU,
					 &cmd,
					 sizeof(cmd),
					 &response,
					 sizeof(response),
					 error)) {
		g_prefix_error(error, "failed to send CMD MCU: ");
		return FALSE;
	}
	if (response == 0x1) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "device is busy");
		return FALSE;
	}
	if (response == 0xFF) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT, "device timed out");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_enumerate_children(FuUsiDockMcuDevice *self, GError **error)
{
	guint8 inbuf[] = {FU_USI_DOCK_MCU_CMD_READ_MCU_VERSIONPAGE,
			  DP_VERSION_FROM_MCU | NIC_VERSION_FROM_MCU};
	guint8 outbuf[49] = {0x0};
	struct {
		const gchar *name;
		guint8 chip_idx;
		gsize offset;
	} components[] = {
	    {"DMC", FU_USI_DOCK_FIRMWARE_IDX_DMC_PD, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_DMC},
	    {"PD", FU_USI_DOCK_FIRMWARE_IDX_DP, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_PD},
	    {"DP5x", FU_USI_DOCK_FIRMWARE_IDX_NONE, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_DP5X},
	    {"DP6x", FU_USI_DOCK_FIRMWARE_IDX_NONE, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_DP6X},
	    {"TBT4", FU_USI_DOCK_FIRMWARE_IDX_TBT4, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_TBT4},
	    {"USB3", FU_USI_DOCK_FIRMWARE_IDX_USB3, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_USB3},
	    {"USB2", FU_USI_DOCK_FIRMWARE_IDX_USB2, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_USB2},
	    {"AUDIO", FU_USI_DOCK_FIRMWARE_IDX_AUDIO, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_AUDIO},
	    {"I255", FU_USI_DOCK_FIRMWARE_IDX_I225, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_I255},
	    {"MCU", FU_USI_DOCK_FIRMWARE_IDX_MCU, FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_MCU},
	    {"bcdVersion",
	     FU_USI_DOCK_FIRMWARE_IDX_NONE,
	     FU_STRUCT_USI_DOCK_ISP_VERSION_OFFSET_BCDVERSION},
	    {NULL, 0, 0}};

	/* assume DP and NIC in-use */
	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_MCU,
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

		child = fu_usi_dock_child_device_new(fu_device_get_context(FU_DEVICE(self)));
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
		}

		if (g_strcmp0(components[i].name, "DMC") == 0) {
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
		fu_device_add_instance_u16(child, "VID", fu_device_get_vid(FU_DEVICE(self)));
		fu_device_add_instance_u16(child, "PID", fu_device_get_pid(FU_DEVICE(self)));
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
	if (!fu_usi_dock_mcu_device_get_status(self, error)) {
		g_prefix_error(error, "failed to get status: ");
		return FALSE;
	}
	if (!fu_usi_dock_mcu_device_enumerate_children(self, error)) {
		g_prefix_error(error, "failed to enumerate children: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_write_chunk(FuUsiDockMcuDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_usi_dock_hid_req_new();

	fu_struct_usi_dock_hid_req_set_length(st_req, fu_chunk_get_data_sz(chk));
	fu_struct_usi_dock_hid_req_set_tag3(st_req, FU_USI_DOCK_TAG2_MASS_DATA_SPI);
	if (!fu_memcpy_safe(st_req->data,
			    st_req->len,
			    FU_STRUCT_USI_DOCK_HID_REQ_OFFSET_BUF, /* dst */
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk),
			    0x0, /* src */
			    fu_chunk_get_data_sz(chk),
			    error))
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      st_req->data,
				      st_req->len,
				      FU_USI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;
	return fu_usi_dock_mcu_device_rx(self, FU_USI_DOCK_MCU_CMD_ALL, NULL, 0x0, error);
}

static gboolean
fu_usi_dock_mcu_device_write_page(FuUsiDockMcuDevice *self, FuChunk *chk_page, GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) chk_blob = fu_chunk_get_bytes(chk_page);

	chunks = fu_chunk_array_new_from_bytes(chk_blob, 0x0, FU_STRUCT_USI_DOCK_HID_REQ_SIZE_BUF);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_usi_dock_mcu_device_write_chunk(self, chk, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_write_pages(FuUsiDockMcuDevice *self,
				   FuChunkArray *chunks,
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
		if (!fu_usi_dock_mcu_device_write_page(self, chk, error)) {
			g_prefix_error(error, "failed to write chunk 0x%x: ", i);
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
	guint8 buf[] = {FU_USI_DOCK_SPI_CMD_READ_STATUS};
	guint8 val = 0;

	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_SPI,
					 buf,
					 sizeof(buf),
					 &val,
					 sizeof(val),
					 error))
		return FALSE;
	if (val != FU_USI_DOCK_SPI_STATE_READY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "SPI state is %s [0x%02x]",
			    fu_usi_dock_spi_state_to_string(val),
			    val);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_wait_for_spi_initial_ready_cb(FuDevice *device,
						     gpointer user_data,
						     GError **error)
{
	FuUsiDockMcuDevice *self = FU_USI_DOCK_MCU_DEVICE(device);
	guint8 buf[] = {FU_USI_DOCK_SPI_CMD_INITIAL};
	guint8 val = 0;

	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_SPI,
					 buf,
					 sizeof(buf),
					 &val,
					 sizeof(val),
					 error))
		return FALSE;
	if (val != FU_USI_DOCK_SPI_STATE_READY) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
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
				       FU_USI_DOCK_MCU_CMD_ALL,
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
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	guint8 checksum = 0xFF;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 69, "write-external");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 25, "wait-for-checksum");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "internal-flash");

	/* initial external flash */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_usi_dock_mcu_device_wait_for_spi_initial_ready_cb,
			     30,
			     NULL,
			     error)) {
		g_prefix_error(error, "failed to wait for initial: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* erase external flash */
	cmd = FU_USI_DOCK_SPI_CMD_ERASE_FLASH;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_SPI,
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
	cmd = FU_USI_DOCK_SPI_CMD_PROGRAM;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_SPI,
					 &cmd,
					 sizeof(cmd),
					 NULL,
					 0x0,
					 error))
		return FALSE;

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream, 0x0, W25Q16DV_PAGE_SIZE, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_usi_dock_mcu_device_write_pages(self,
						chunks,
						fu_progress_get_child(progress),
						error))
		return FALSE;
	fu_progress_step_done(progress);

	/* file transfer – finished */
	cmd = FU_USI_DOCK_SPI_CMD_TRANSFER_FINISH;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_SPI,
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
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid checksum result for CMD_FWBUFER_CHECKSUM, got 0x%02x",
			    checksum);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* internal flash */
	cmd = FU_USI_DOCK_MCU_CMD_FW_UPDATE;
	if (!fu_usi_dock_mcu_device_txrx(self,
					 FU_USI_DOCK_TAG2_CMD_MCU,
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
fu_usi_dock_mcu_device_reload(FuDevice *device, GError **error)
{
	FuUsiDockMcuDevice *self = FU_USI_DOCK_MCU_DEVICE(device);
	guint8 inbuf[] = {FU_USI_DOCK_MCU_CMD_SET_CHIP_TYPE, 1, 1};

	if (fu_device_has_private_flag(device, FU_USI_DOCK_DEVICE_FLAG_SET_CHIP_TYPE)) {
		g_info("repairing device with CMD_SET_CHIP_TYPE");
		if (!fu_usi_dock_mcu_device_txrx(self,
						 FU_USI_DOCK_TAG2_CMD_MCU,
						 inbuf,
						 sizeof(inbuf),
						 NULL,
						 0x0,
						 error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	fu_device_set_remove_delay(device, 900000);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_usi_dock_mcu_device_insert_cb(gpointer user_data)
{
	FuDevice *device = FU_DEVICE(user_data);
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	g_autoptr(GError) error_local = NULL;

	/* interactive request to start the SPI write */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_INSERT_USB_CABLE);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	if (!fu_device_emit_request(device, request, NULL, &error_local))
		g_critical("%s", error_local->message);

	/* success */
	return G_SOURCE_REMOVE;
}

static void
fu_usi_dock_mcu_device_internal_flags_notify_cb(FuDevice *device,
						GParamSpec *pspec,
						gpointer user_data)
{
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED) &&
	    fu_device_has_private_flag(device, FU_USI_DOCK_DEVICE_FLAG_WAITING_FOR_UNPLUG)) {
		g_debug("starting 40s countdown");
		g_timeout_add_seconds_full(G_PRIORITY_DEFAULT,
					   40, /* seconds */
					   fu_usi_dock_mcu_device_insert_cb,
					   g_object_ref(device),
					   g_object_unref);
		fu_device_remove_private_flag(device, FU_USI_DOCK_DEVICE_FLAG_WAITING_FOR_UNPLUG);
	}
}

static gboolean
fu_usi_dock_mcu_device_cleanup(FuDevice *device,
			       FuProgress *progress,
			       FwupdInstallFlags install_flags,
			       GError **error)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	/* wait for the user to unplug then start the 40 second timer */
	fu_device_add_private_flag(device, FU_USI_DOCK_DEVICE_FLAG_WAITING_FOR_UNPLUG);
	fu_device_set_remove_delay(device, 900000);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_BUSY);

	/* interactive request to start the SPI write */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_USB_CABLE);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	return fu_device_emit_request(device, request, progress, error);
}

static void
fu_usi_dock_mcu_device_replace(FuDevice *device, FuDevice *donor)
{
	if (fu_device_has_private_flag(donor, FU_USI_DOCK_DEVICE_FLAG_SET_CHIP_TYPE))
		fu_device_add_private_flag(device, FU_USI_DOCK_DEVICE_FLAG_SET_CHIP_TYPE);
}

static void
fu_usi_dock_mcu_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 48, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 52, "reload");
}

static void
fu_usi_dock_mcu_device_init(FuUsiDockMcuDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);

	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_SERIAL_NUMBER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	g_signal_connect(FWUPD_DEVICE(self),
			 "notify::private-flags",
			 G_CALLBACK(fu_usi_dock_mcu_device_internal_flags_notify_cb),
			 NULL);

	fu_device_register_private_flag(FU_DEVICE(self), FU_USI_DOCK_DEVICE_FLAG_VERFMT_HP);
	fu_device_register_private_flag(FU_DEVICE(self), FU_USI_DOCK_DEVICE_FLAG_SET_CHIP_TYPE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_USI_DOCK_DEVICE_FLAG_WAITING_FOR_UNPLUG);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_AUTODETECT_EPS);
	fu_device_add_protocol(FU_DEVICE(self), "com.usi.dock");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_retry_set_delay(FU_DEVICE(self), 1000);
	fu_device_add_icon(FU_DEVICE(self), "dock");
}

static void
fu_usi_dock_mcu_device_class_init(FuUsiDockMcuDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->write_firmware = fu_usi_dock_mcu_device_write_firmware;
	device_class->attach = fu_usi_dock_mcu_device_attach;
	device_class->setup = fu_usi_dock_mcu_device_setup;
	device_class->set_progress = fu_usi_dock_mcu_device_set_progress;
	device_class->cleanup = fu_usi_dock_mcu_device_cleanup;
	device_class->reload = fu_usi_dock_mcu_device_reload;
	device_class->replace = fu_usi_dock_mcu_device_replace;
}

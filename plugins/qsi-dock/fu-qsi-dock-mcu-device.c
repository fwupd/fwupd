/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Kevin Chen <hsinfu.chen@qsitw.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

#define FU_QSI_DOCK_DEVICE_FLAG_VERFMT_HP (1 << 0)

static gboolean
fu_qsi_dock_mcu_device_tx(FuQsiDockMcuDevice *self,
			  guint8 CmdPrimary,
			  guint8 CmdSecond,
			  const guint8 *inbuf,
			  gsize inbufsz,
			  GError **error)
{
 	guint8 buf[64] = {0};
	buf[0] = Report_ID;
	buf[1] = CmdPrimary;
	buf[2] = CmdSecond;
	
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					Report_ID,
					buf,
					sizeof(buf),
					FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);

}

static gboolean
fu_qsi_dock_mcu_device_rx(FuQsiDockMcuDevice *self,
			  guint8 *outbuf,
			  gsize outbufsz,
			  GError **error)
{
	guint8 buf[64] = {0};
	
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      Report_ID,
				      buf,
				      sizeof(buf),
				      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
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

	guint8 cmd1 = CmdPrimary_CMD_MCU;
	guint8 cmd2 = CmdSecond_CMD_DEVICE_STATUS;

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
	    /*{"DMC", FIRMWARE_IDX_DMC_PD, G_STRUCT_OFFSET(IspVersionInMcu_t, DMC)},
	    {"PD", FIRMWARE_IDX_DP, G_STRUCT_OFFSET(IspVersionInMcu_t, PD)},
	    {"DP5x", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, DP5x)},
	    {"DP6x", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, DP6x)},
	    {"TBT4", FIRMWARE_IDX_TBT4, G_STRUCT_OFFSET(IspVersionInMcu_t, TBT4)},
	    {"USB3", FIRMWARE_IDX_USB3, G_STRUCT_OFFSET(IspVersionInMcu_t, USB3)},
	    {"USB2", FIRMWARE_IDX_USB2, G_STRUCT_OFFSET(IspVersionInMcu_t, USB2)},
	    {"AUDIO", FIRMWARE_IDX_AUDIO, G_STRUCT_OFFSET(IspVersionInMcu_t, AUDIO)},
	    {"I255", FIRMWARE_IDX_I225, G_STRUCT_OFFSET(IspVersionInMcu_t, I255)},
	    */
	    {"MCU", FIRMWARE_IDX_MCU, G_STRUCT_OFFSET(IspVersionInMcu_t, MCU)},
	    {"bcdVersion", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, bcdVersion)},
	    {NULL, 0, 0}};

	for (guint i = 0; components[i].name != NULL; i++) {
		const guint8 *val = outbuf + components[i].offset;
		g_autofree gchar *version = NULL;
		g_autoptr(FuDevice) child = NULL;

		child = fu_qsi_dock_child_new(fu_device_get_context(FU_DEVICE(self)));
		if (g_strcmp0(components[i].name, "bcdVersion") == 0) {
			if ((val[0] == 0x00 && val[1] == 0x00) ||
			    (val[0] == 0xFF && val[1] == 0xFF)) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			if (fu_device_has_private_flag(FU_DEVICE(self),
						       FU_QSI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
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
						       FU_QSI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
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
						       FU_QSI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
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
						       FU_QSI_DOCK_DEVICE_FLAG_VERFMT_HP)) {
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
fu_qsi_dock_mcu_device_checksum(FuQsiDockMcuDevice *self, guint32 checksum, guint32 check_length, GError **error)
{
	guint8 buf[64] = {0};
	
	buf[0] = Report_ID;
	buf[1] = CmdPrimary_CMD_SPI;
	buf[2] = CmdSecond_SPI_External_Flash_Checksum;

        buf[3] = (check_length) & 0xff;
        buf[4] = (check_length >> 8) & 0xff;
        buf[5] = (check_length >> 16) & 0xff;
        buf[6] = (check_length >> 24) & 0xff;
        
	buf[7] = (checksum) & 0xff;
        buf[8] = (checksum >> 8) & 0xff;

	// Set Report	
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					Report_ID,
					buf,
					sizeof(buf),
					FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error))
			return FALSE;

	// GetReport 
	memset(buf, 0x0, sizeof(buf));
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					Report_ID,
					buf,
					sizeof(buf),
					FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error)) 
			return FALSE;

	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_write_chunk(FuQsiDockMcuDevice *self, FuChunk *chk, GError **error, guint32 *checksum_tmp)
{
	guint8 buf[64] = {0};
	guint32 length = 60;
	guint32 pagesize = fu_chunk_get_data_sz(chk);
	guint16 page_data_count = 256;

	while (pagesize != 0 || page_data_count != 0) {
		memset(buf, 0x0, sizeof(buf));
		buf[0] = Report_ID;
		buf[1] = CmdPrimary_Mass_SPI;

		/* set length and buffer */
		if (pagesize > TX_ISP_LENGTH_MCU) {
			length = TX_ISP_LENGTH_MCU;
			pagesize -= TX_ISP_LENGTH_MCU;
			page_data_count -= TX_ISP_LENGTH_MCU;
			buf[2] = length;
		}else{
			length = pagesize;
			if(page_data_count == pagesize){
				page_data_count = 0;
				buf[2] = length;
			}else if(page_data_count >= TX_ISP_LENGTH_MCU){
				page_data_count -= TX_ISP_LENGTH_MCU;
				buf[2] =  TX_ISP_LENGTH_MCU;
			}else{
				buf[2] = page_data_count;
				page_data_count = 0;
			}
			pagesize = 0;
		}

		// SetReport
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x04, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    fu_chunk_get_data_sz(chk) - pagesize - length, /* src */
				    length,
				    error))
			return FALSE;
		
		// Checksum value
		for(int i=4;i<64;i++){
			*checksum_tmp +=buf[i];
		}
		
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      Report_ID,
					      buf,
					      sizeof(buf),
					      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_NONE,
					      error))
			return FALSE;

		// GetReport 
		memset(buf, 0x0, sizeof(buf));
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      Report_ID,
					      buf,
					      sizeof(buf),
					      FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_NONE,
					      error)) 
			return FALSE;

		if(!buf[2] == 0) {
			return FALSE;
		}
		
	}
	return TRUE;
}

static gboolean
fu_qsi_dock_mcu_device_write_chunks(FuQsiDockMcuDevice *self,
				    GPtrArray *chunks,
				    FuProgress *progress,
				    GError **error,
				    guint32 *checksum)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_qsi_dock_mcu_device_write_chunk(self, chk, error, checksum)) {
			g_prefix_error(error, "failed to write chunk 0x%x", i);
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
	guint8 buf[64] = {0};

	buf[0] = Report_ID;
        buf[1] = CmdPrimary_CMD_SPI;
        buf[2] = CmdSecond_SPI_External_Flash_Ini;

	// SetReport
        if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
                                        Report_ID,
                                        buf,
                                        sizeof(buf),
                                        FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
                                        FU_HID_DEVICE_FLAG_NONE,
                                        error))
                        return FALSE;
                
        // GetReport 
        memset(buf, 0x0, sizeof(buf));
        if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
                                        Report_ID,
                                        buf,
                                        sizeof(buf),
                                        FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
                                        FU_HID_DEVICE_FLAG_NONE,
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
	guint8 buf[64] = {0};
	guint32 offset = 0;
	
	buf[0] = Report_ID;
        buf[1] = CmdPrimary_CMD_SPI;
        buf[2] = CmdSecond_SPI_External_Flash_Erase;

        buf[3] = (length) & 0xff;
        buf[4] = (length >> 8) & 0xff;
        buf[5] = (length >> 16) & 0xff;
        buf[6] = (length >> 24) & 0xff;

        buf[7] = (offset) & 0xff;
        buf[8] = (offset >> 8) & 0xff;
        buf[9] = (offset >> 16) & 0xff;
        buf[10] = (offset >> 24) & 0xff;

	// SetReport
        if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
                                        Report_ID,
                                        buf,
                                        sizeof(buf),
                                        FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
                                        FU_HID_DEVICE_FLAG_NONE,
                                        error))
                        return FALSE;
                
        // GetReport 
        memset(buf, 0x0, sizeof(buf));
        if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
                                        Report_ID,
                                        buf,
                                        sizeof(buf),
                                        FU_QSI_DOCK_MCU_DEVICE_TIMEOUT,
                                        FU_HID_DEVICE_FLAG_NONE,
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
	g_autoptr(GBytes) fw =fu_firmware_get_bytes(firmware, error);
	g_autoptr(GPtrArray) chunks = NULL;

	guint32 fw_length = fu_firmware_get_size(firmware);
	guint32 checksum_val = 0;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, NULL);
	
	/* initial external flash */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_qsi_dock_mcu_device_wait_for_spi_initial_ready_cb,
			     30,
			     NULL,
			     error)) {
		g_prefix_error(error, "failed to wait for initial: ");
		return FALSE;
	}
	
	if(!fu_qsi_dock_mcu_device_wait_for_spi_erase_ready_cb(self,
                                                 fw_length,
                                                 fu_progress_get_child(progress),
                                                 error))
		return FALSE;
		
	/* write external flash */		
	if (fw == NULL)
		return FALSE;
	
	chunks = fu_chunk_array_new_from_bytes(fw, 0, 0, EXTERN_FLASH_PAGE_SIZE);
	
	if (!fu_qsi_dock_mcu_device_write_chunks(self,
						 chunks,
						 fu_progress_get_child(progress),
						 error, 
						 &checksum_val))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify flash data */
	if (!fu_qsi_dock_mcu_device_checksum(self,
					     checksum_val,
					     fw_length,
					     error))
		return FALSE;

	fu_progress_step_done(progress);

	/* restart device */
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
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_INHIBIT_CHILDREN);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_add_protocol(FU_DEVICE(self), "com.qsi.dock");
}

static void
fu_qsi_dock_mcu_device_class_init(FuQsiDockMcuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->setup = fu_qsi_dock_mcu_device_setup;
	klass_device->attach = fu_qsi_dock_mcu_device_attach;
	klass_device->set_progress = fu_qsi_dock_mcu_device_set_progress;
	klass_device->write_firmware = fu_qsi_dock_mcu_device_write_firmware;
}

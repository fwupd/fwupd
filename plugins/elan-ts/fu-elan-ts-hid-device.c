/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-hid-device.h"
#include "fu-elan-ts-hidraw-utility.h"
#include "fu-elan-ts-hid-utility.h"
#include "fu-elan-ts-iap-api.h"

/**
 * FuElanTsHidDevice:
 * @parent_instance:	Inherited parent instance
 * @ic_type:		Integrated circuit type identifier
 * @touch_state:	Current operating mode state
 * @bc_version:		Cached boot code version
 * @fw_version:		Cached firmware version
 *
 * An ELAN Touchscreen HID device.
 */
struct _FuElanTsHidDevice {
	FuHidrawDevice	parent_instance;
	FuElanTsIcType	ic_type;
	FuElanTsState	touch_state;
	guint16		bc_version;
	guint16		fw_id;
	guint16		fw_version;
	guint16		test_version;
};

G_DEFINE_TYPE(FuElanTsHidDevice, fu_elan_ts_hid_device, FU_TYPE_HIDRAW_DEVICE)

/**
 * FuElanTsHidDeviceProperty:
 *
 * The properties for the ElanTsHidDevice object.
 */
enum {
	PROP_0,
	PROP_FIRMWARE,		/* The firmware version of the device */
	PROP_LAST
};

/**
 * elan_ts_device_get_debug_setting:
 * @device: a #FuDevice
 *
 * Helper function to retrieve the debug bitmask from the underlying utility.
 * This provides a cleaner interface within the device logic.
 *
 * Returns: the debug setting bitmask.
 **/
static guint32
elan_ts_device_get_debug_setting(FuDevice *device)
{
	/* Forward the request to the low-level hidraw utility */
	return elan_ts_hidraw_get_debug_setting(device);
}

/**
 * fu_elan_ts_hid_device_get_property:
 * @object: a #GObject
 * @prop_id: the property ID
 * @value: the value to return
 * @pspec: the #GParamSpec
 *
 * Gets a property from the ElanTsHidDevice.
 **/
static void
fu_elan_ts_hid_device_get_property(GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec)
{
	switch (prop_id) {
		case PROP_FIRMWARE:
			    /* Return 0 to satisfy the engine without using your internal variable */
			    g_value_set_uint(value, 0);
			    break;
		default:
			    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			    break;
	}
}

/**
 * fu_elan_ts_hid_device_set_property:
 * @object: a #GObject
 * @prop_id: the property ID
 * @value: the new value
 * @pspec: the #GParamSpec
 *
 * Sets a property on the ElanTsHidDevice.
 **/
static void
fu_elan_ts_hid_device_set_property(GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec)
{
	switch (prop_id) {
		case PROP_FIRMWARE:
			    /* Do nothing: we don't want the property system to touch our data */
			    break;
		default:
			    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			    break;
	}
}

/**
 * fu_elan_ts_hid_device_probe:
 * @device: a #FuDevice
 * @error: a #GError, or %NULL
 * 
 * Checks if the device is compatible with this plugin before opening.
 **/
static gboolean
fu_elan_ts_hid_device_probe(FuDevice *device, GError **error)
{
	const gchar *subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	guint16 hid_vid = fu_device_get_vid(device);
	guint16 hid_pid = fu_device_get_pid(device);

	g_debug("%s: probing device '%s' (subsystem: %s)", G_STRFUNC, 
		fu_device_get_name(device), 
		(subsystem != NULL) ? subsystem : "unknown");

	/* 
	 * Accept both 'hidraw' and 'i2c' subsystems.
	 * The 'i2c' subsystem is required for identifying the physical I2C node,
	 * while 'hidraw' provides the functional interface for firmware operations.
	 */
	if ((subsystem == NULL) || 
	    ((g_ascii_strcasecmp(subsystem, "hidraw") != 0) && 
	     (g_ascii_strcasecmp(subsystem, "i2c") != 0))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device has incorrect subsystem=%s, expected hidraw or i2c",
			    (subsystem != NULL) ? subsystem : "(null)");
		return FALSE;
	}

	/* 
	 * For raw I2C nodes, we rely on quirk matching rather than VID/PID 
	 * checks, as these nodes may not expose HID descriptors yet.
	 */
	if (g_ascii_strcasecmp(subsystem, "i2c") == 0) {
		g_debug("%s: accepted physical I2C node", G_STRFUNC);
		return TRUE;
	}

	/* 
	 * For hidraw devices, verify the Vendor ID to ensure it's an ELAN device.
	 */
	g_debug("%s: checking HIDRAW VID: 0x%04x, PID: 0x%04x", G_STRFUNC, hid_vid, hid_pid);
	if (hid_vid != ELAN_TS_HID_VID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "incorrect HID VID: 0x%04x (expected 0x%04x)",
			    (guint)hid_vid, (guint)ELAN_TS_HID_VID);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_elan_ts_hid_device_setup:
 * @device: a #FuDevice
 * @error: a #GError, or %NULL
 * 
 * Initializes the device and reads hardware information like firmware version.
 **/
static gboolean
fu_elan_ts_hid_device_setup(FuDevice *device, GError **error)
{
	FuElanTsHidDevice *self = FU_ELAN_TS_HID_DEVICE(device);
	const gchar *subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	g_autofree gchar *summary = NULL;
	g_autofree gchar *bc_version_str = NULL;
	g_autofree gchar *fw_test_version_str = NULL;
	guint8 hello_packet = 0;
	guint16 bc_version = 0;
	guint16 fw_id = 0;
	guint16 fw_version = 0;
	guint16 test_solution_version = 0;
	guint16 test_version = 0;
	guint32 debug_setting = elan_ts_device_get_debug_setting(device);

        /* Skip setup for physical I2C nodes to avoid interference */
	if ((subsystem != NULL) && (g_ascii_strcasecmp(subsystem, "i2c") == 0)) {
		ELAN_TS_DEBUG(debug_setting, "%s: physical I2C node detected, skipping setup...", G_STRFUNC);
		return TRUE;
	}

	/* Basic Metadata & Flags */
	fu_device_set_vendor(device, "Elan Microelectronics");
	if ((fu_device_get_name(device) == NULL) || 
	    (g_str_has_prefix(fu_device_get_name(device), "hidraw"))) { 
		fu_device_set_name(device, "Elan Touchscreen");
	}
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_HEX);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REPORTED);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);

	/* Common Instance ID components: Vendor and Product ID */
	fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(device));
	fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(device));

	/* Hardware Probe: Read Hello Packet and initial BC Version */
	if (!elan_ts_hid_read_hello_packet_bc_version_with_retry(device, &hello_packet, &bc_version, error)){
		ELAN_TS_ERROR(debug_setting, error, "%s: Hardware communication test failed: ", G_STRFUNC);
		return FALSE;
	}
	ELAN_TS_DEBUG(debug_setting, "%s: Hello Packet: 0x%02x", G_STRFUNC, hello_packet);

	/* Mode-specific Logic: Identify HW Series and Touch State based on Hello Packet */
	switch (hello_packet) {
		case ELAN_TS_HID_NORMAL_MODE_HELLO_PACKET:
			ELAN_TS_DEBUG(debug_setting, "%s: Normal Mode detected", G_STRFUNC);

			/* Update internal data */
			self->ic_type = FU_ELAN_TS_IC_TYPE_ELAN_TOUCH;
			self->touch_state = FU_ELAN_TS_STATE_NORMAL_MODE;

                        /* Read Boot Code Version */
			if (!elan_ts_hid_read_boot_code_version(device, &bc_version, error)) {
				  ELAN_TS_ERROR(debug_setting, error, "failed to read BC version in Normal Mode: ");
				  return FALSE;
			}
			ELAN_TS_DEBUG(debug_setting, "%s: BC Version: 0x%04x", G_STRFUNC, bc_version);
			self->bc_version = bc_version;

                        /* Read FW ID */
                        if (!elan_ts_hid_read_fw_id(device, &fw_id, error)) {
				  ELAN_TS_ERROR(debug_setting, error, "failed to read FW ID in Normal Mode: ");
				  return FALSE;
			}
			ELAN_TS_DEBUG(debug_setting, "%s: FW ID: 0x%04x", G_STRFUNC, fw_id);
			self->fw_id = fw_id;

			/* Read FW Version */
                        if (!elan_ts_hid_read_fw_version(device, &fw_version, error)) {
				  ELAN_TS_ERROR(debug_setting, error, "failed to read FW Version in Normal Mode: ");
				  return FALSE;
			}
			ELAN_TS_DEBUG(debug_setting, "%s: FW Version: 0x%04x", G_STRFUNC, fw_version);
			self->fw_version = fw_version;

                        /* Red Test-Solution Version */
                        if (!elan_ts_hid_read_test_solution_version(device, &test_solution_version, error)) {
				  ELAN_TS_ERROR(debug_setting, error, "failed to read Test-Solution Version in Normal Mode: ");
				  return FALSE;
			}
			ELAN_TS_DEBUG(debug_setting, "%s: Test-Solution Version: 0x%04x", G_STRFUNC, test_solution_version);
			test_version = (test_solution_version & 0xFF00) >> 8;
			self->test_version = test_version;
                        
			/* Display Boot Code version in the Bootloader Version field */
			//fu_device_set_version_bootloader(device, g_strdup_printf("0x%04x", bc_version));
			bc_version_str = g_strdup_printf("0x%04x", bc_version);
			fu_device_set_version_bootloader(device, bc_version_str);

			/* Display combined FW and Test versions in the main Version field */
			//fu_device_set_version(device, g_strdup_printf("0x%04x%04x", fw_version, test_version));
			fw_test_version_str = g_strdup_printf("0x%04x%04x", fw_version, test_version);
			fu_device_set_version(device, fw_test_version_str);

			/* set device summary visible in 'get-devices --show-all' */
			summary = g_strdup_printf("Elan Touchscreen (FWID: 0x%04x)", fw_id);
			fu_device_set_summary(device, summary);
			break;

		case ELAN_TS_HID_RECOVERY_MODE_HELLO_PACKET:
			ELAN_TS_DEBUG(debug_setting, "%s: Recovery Mode detected", G_STRFUNC);
			ELAN_TS_DEBUG(debug_setting, "%s: BC Version: 0x%04x", G_STRFUNC, bc_version);

			/* Update internal data */
			self->ic_type = FU_ELAN_TS_IC_TYPE_ELAN_TOUCH;
			self->touch_state = FU_ELAN_TS_STATE_RECOVERY_MODE;
			self->bc_version = bc_version;

			/* Mark as bootloader - Recovery-mode device won't have FWID */
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
			
			/* Display BC version in the Bootloader Version field */
			//fu_device_set_version_bootloader(device, g_strdup_printf("0x%04x", bc_version));
			bc_version_str = g_strdup_printf("0x%04x", bc_version);
			fu_device_set_version_bootloader(device, bc_version_str);
			
			/* Force update by setting version to 0x0000 */
			fu_device_set_version(device, "0x0000");
			
			/* set device summary visible in 'get-devices --show-all' */
			summary = g_strdup_printf("Elan Touchscreen (Recovery Mode)");
			fu_device_set_summary(device, summary);
			break;

		default:
			/* Handle unexpected hello packet values */
			ELAN_TS_SET_ERROR(debug_setting, 
                                          error, 
                                          FWUPD_ERROR_NOT_SUPPORTED, 
                                          "Unknown Hello Packet! (0x%02x)", 
                                          hello_packet);
			return FALSE;
	}

        /* Build Instance ID */
	/* If in Recovery, it will be HIDRAW\VEN_04F3&DEV_0732 */
	/* If in Normal, it will be HIDRAW\VEN_04F3&DEV_XXXX (where XXXX is the product PID) */
	if (!fu_device_build_instance_id_full(device, 
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS, 
					     error, 
					     "HIDRAW", "VEN", "DEV", NULL)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to build instance ID: ");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "%s: setup() completed successfully for %s", G_STRFUNC, fu_device_get_id(device));
	return TRUE;
}

/**
 * fu_elan_ts_hid_device_to_string:
 * @device: a #FuDevice
 * @idt: indentation level
 * @str: a #GString to append to
 *
 * Adds device-specific state to the string representation for debugging.
 **/
static void
fu_elan_ts_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElanTsHidDevice *self = FU_ELAN_TS_HID_DEVICE(device);

	/* Always show BC-Version if available */
	if (self->bc_version != 0)
		fwupd_codec_string_append_hex(str, idt, "BC-Version", self->bc_version);
	
	/* Only show full firmware details in normal mode */
	if (self->touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		if (self->fw_id != 0)
			fwupd_codec_string_append_hex(str, idt, "FW-ID", self->fw_id);
		if (self->fw_version != 0)
			fwupd_codec_string_append_hex(str, idt, "FW-Version", self->fw_version);
		
		/* Test version is always shown as it could be valid even if zero */
		fwupd_codec_string_append_hex(str, idt, "Test-Version", self->test_version);
	}
}

/**
 * fu_elan_ts_hid_device_reload:
 * @device: a #FuDevice
 * @error: (nullable): a #GError, or %NULL
 *
 * Refreshes the device state and performs post-update calibration.
 * This is called automatically by the fwupd core after write_firmware returns TRUE.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 */
static gboolean
fu_elan_ts_hid_device_reload(FuDevice *device, GError **error)
{
	guint32 debug_setting = elan_ts_device_get_debug_setting(device);

	ELAN_TS_DEBUG(debug_setting, "%s: Starting post-update reload process", G_STRFUNC);

	/* Setup device: refresh cached versions and hardware info from the new firmware */
	ELAN_TS_DEBUG(debug_setting, "refreshing device hardware information...");
	if (!fu_elan_ts_hid_device_setup(device, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to setup device during reload: ");
		return FALSE;
	}

	/* Run Re-calibration with fwupd retry framework to ensure touch accuracy */
	ELAN_TS_DEBUG(debug_setting, "Triggering post-update re-calibration...");
	if (!fu_elan_ts_hid_device_recalibrate_with_retry(device, error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to re-calibrate during reload: ");
		return FALSE;
	}

	ELAN_TS_DEBUG(debug_setting, "%s: reload process completed successfully", G_STRFUNC);
	return TRUE;
}

/**
 * fu_elan_ts_hid_device_write_firmware:
 * @device: a #FuDevice
 * @firmware: a #FuFirmware
 * @progress: a #FuProgress
 * @flags: a #FwupdInstallFlags
 * @error: a #GError, or %NULL
 * 
 * Performs the firmware update process by writing the firmware image to the device.
 **/
static gboolean
fu_elan_ts_hid_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuElanTsHidDevice *self = FU_ELAN_TS_HID_DEVICE(device);
	FuElanTsFirmware *elan_ts_fw = FU_ELAN_TS_FIRMWARE(firmware);
	FuElanTsIcType ic_type = FU_ELAN_TS_IC_TYPE_UNKNOWN;
	FuElanTsFwType fw_type = FU_ELAN_TS_FW_TYPE_UNKNOWN;
	FuElanTsState touch_state = FU_ELAN_TS_STATE_UNKNOWN;
	FuProgress *p_progress_write = NULL;
	g_autoptr(GBytes) p_fw_bin = NULL;
	g_autofree guint8 *p_page_block_buf = NULL;
	guint32 debug_setting = elan_ts_device_get_debug_setting(device);
	guint32 fw_bin_size = 0;
	guint16 bc_version = 0;
	guint16 fw_version = 0;
	guint16 dev_remark_id = 0;
	guint8 solution_id = 0;
	guint8 iap_version = 0;
	guint8 bc_hbyte = 0;
	guint8 bc_lbyte = 0;
	guint8 info_fw_page_buf[ELAN_TS_FW_PAGE_SIZE] = {0};
	guint page_count = 0;
	guint block_count = 0;
	guint block_index = 0;
	guint block_page_num = 0;
	gsize page_block_buf_size = 0;
	gsize max_page_block_buf_size = 0; 
	gboolean remark_id_check = FALSE;
	gboolean skip_info_data_update =
	    (debug_setting & FU_ELAN_TS_DEBUG_SETTING_SKIP_INFO_DATA_UPDATE) != 0;
	gboolean skip_remark_id_check =
	    (debug_setting & FU_ELAN_TS_DEBUG_SETTING_SKIP_REMARK_ID_CHECK) != 0;

	/* Sanity Checks: Ensure all incoming objects are valid */
	g_return_val_if_fail(FU_IS_ELAN_TS_HID_DEVICE(device), FALSE);
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);

	/* Retrieve current device status */
	ic_type = self->ic_type;
	touch_state = self->touch_state;
	bc_version = self->bc_version;
	fw_version = self->fw_version;

	ELAN_TS_DEBUG(debug_setting,
		      "%s: Starting FW Update (skip_info=%s, skip_remark=%s)",
		      G_STRFUNC,
		      skip_info_data_update ? "true" : "false",
		      skip_remark_id_check ? "true" : "false");

	/* 
	 * Define clear phases with specific weights:
	 * Phase 1: Validation & Prepare.
	 * Phase 2: Transition to Bootloader
	 * Phase 3: Write Firmware Blocks
	 * Phase 4: Self-restart Delay
	 */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "init");    /* Phase 1 & 2 */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93, "write");  /* Phase 3 */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "restart");  /* Phase 4 */

	/* Phase 1: Validate Firmware Container and Format */
	p_fw_bin = fu_firmware_get_bytes(firmware, error);
	if (p_fw_bin == NULL) {
		ELAN_TS_ERROR(debug_setting, error, "failed to get firmware payload: ");
		return FALSE;
	}

	if (ic_type != FU_ELAN_TS_IC_TYPE_ELAN_TOUCH) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_NOT_SUPPORTED,
				  "Unsupported IC type: 0x%02x (Expected Elan Touch)",
				  (guint)ic_type);
		return FALSE;
	}

	fw_type = elan_ts_firmware_get_fw_type(elan_ts_fw);
	if (fw_type != FU_ELAN_TS_FW_TYPE_EKT) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_FILE,
				  "invalid firmware format: %u (expected EKT)",
				  (guint)fw_type);
		return FALSE;
	}

	/* Retrieve firmware binary size and calculate pagination */
	fw_bin_size = elan_ts_firmware_get_bin_size(elan_ts_fw);
	if (fw_bin_size == 0) {
		ELAN_TS_SET_ERROR(debug_setting,
				  error,
				  FWUPD_ERROR_INVALID_DATA,
				  "invalid firmware binary size");
		return FALSE;
	}

	/* Prepare information page update for devices in normal mode */
	if ((touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) && (!skip_info_data_update)) {
		solution_id = (guint8)((fw_version & 0xFF00) >> 8);
		ELAN_TS_DEBUG(debug_setting,
			      "Preparing Info Page, FW_Version: 0x%04x, Solution_ID: 0x%02x",
			      fw_version,
			      solution_id);
		if (!elan_ts_iap_read_and_update_info_page(device,
							   solution_id,
							   info_fw_page_buf,
							   sizeof(info_fw_page_buf),
							   error)) {
			ELAN_TS_ERROR(debug_setting, error, "failed to update information page: ");
			return FALSE;
		}
		ELAN_TS_DEBUG(debug_setting, "successfully prepared updated information page");
	}

	/* Determine if Remark ID verification is required based on BC version */
	if (touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		iap_version = (guint8)(bc_version & 0x00FF);
		if (iap_version >= 0x60)
			remark_id_check = TRUE;
	} else {
		bc_hbyte = (guint8)((bc_version & 0xFF00) >> 8);
		bc_lbyte = (guint8)(bc_version & 0x00FF);
		if (bc_hbyte != bc_lbyte)
			remark_id_check = TRUE;
	}

	/* Execute Remark ID compatibility check */
	if (remark_id_check) {
		if (!skip_remark_id_check) {
			if (!elan_ts_iap_check_remark_id(device,
							 firmware,
							 touch_state,
							 fw_version,
							 bc_version,
							 error)) {
				ELAN_TS_ERROR(debug_setting, error, "Remark ID Check failed: ");
				return FALSE;
			}
		} else {
			ELAN_TS_DEBUG(debug_setting,
				      "Bypassing comparison, performing mandatory ROM read");
			if (!elan_ts_hid_read_remark_id(device,
							touch_state,
							fw_version,
							bc_version,
							&dev_remark_id,
							error)) {
				ELAN_TS_ERROR(debug_setting, error, "Mandatory Remark ID read failed: ");
				return FALSE;
			}
			ELAN_TS_DEBUG(debug_setting, "Hardware Remark ID: 0x%04x", dev_remark_id);
		}
	}

	/* Phase 2: Detach / Transition to Bootloader
	 * Switch device to bootloader/IAP mode */
	ELAN_TS_DEBUG(debug_setting, "Transitioning to Bootloader mode...");
	if (!elan_ts_iap_switch_to_boot_code(device,
					     (touch_state != FU_ELAN_TS_STATE_NORMAL_MODE),
					     error)) {
		ELAN_TS_ERROR(debug_setting, error, "failed to switch to bootloader: ");
		return FALSE;
	}
	fu_progress_step_done(progress); // Phase 1 & 2 Done

	/* Phase 3: Write - Main firmware data transfer */
	p_progress_write = fu_progress_get_child(progress);
	if (p_progress_write == NULL) {
		ELAN_TS_SET_ERROR(debug_setting, 
				  error, 
				  FWUPD_ERROR_INTERNAL, 
				  "failed to get child progress for write phase");
		return FALSE;
	}
	fu_progress_set_id(p_progress_write, G_STRLOC); 

	ELAN_TS_DEBUG(debug_setting, "Start Firmware Update Process...");

	/* Write prepared information page first if applicable */
	if ((touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) && (!skip_info_data_update)) {
		ELAN_TS_DEBUG(debug_setting, "Write Information Page...");
		if (!elan_ts_iap_write_firmware_pages(device,
						      info_fw_page_buf,
						      sizeof(info_fw_page_buf),
						      error)) {
			ELAN_TS_ERROR(debug_setting, error, "failed to write information page: ");
			return FALSE;
		}
		ELAN_TS_DEBUG(debug_setting, "successfully wrote information page to device");
	}

	/* Calculate pagination parameters for the main firmware payload (fw_bin) */
	page_count = elan_ts_firmware_get_page_count(elan_ts_fw);
	block_count = (page_count + ELAN_TS_FW_PAGES_PER_BLOCK - 1) / ELAN_TS_FW_PAGES_PER_BLOCK;
	ELAN_TS_DEBUG(debug_setting,
		      "Main Payload (FW_BIN) - Size: %u, Total Pages: %u, Total Blocks: %u",
		      fw_bin_size,
		      page_count,
		      block_count);

	/* Prepare a fixed-size buffer for page blocks to avoid frequent reallocations.
	 * The maximum size is a full block of pages. */
	max_page_block_buf_size = (gsize)(ELAN_TS_FW_PAGES_PER_BLOCK * ELAN_TS_FW_PAGE_SIZE);
	p_page_block_buf = g_malloc0(max_page_block_buf_size);
	fu_progress_set_steps(p_progress_write, block_count);

	/* Main firmware data transfer loop */
	for (block_index = 0; block_index < block_count; block_index++) {
		/* Clear the buffer before reuse to ensure no stale data remains */
		memset(p_page_block_buf, 0, max_page_block_buf_size);

		/* Determine number of pages in the current block */
		if ((block_index == (block_count - 1)) && ((page_count % ELAN_TS_FW_PAGES_PER_BLOCK) != 0))
			block_page_num = page_count % ELAN_TS_FW_PAGES_PER_BLOCK;
		else
			block_page_num = ELAN_TS_FW_PAGES_PER_BLOCK;

		/* Calculate current block size */
		page_block_buf_size = (gsize)block_page_num * ELAN_TS_FW_PAGE_SIZE;

		/* Retrieve the page block data from firmware bin */
		if (!elan_ts_firmware_get_page(elan_ts_fw,
					       block_index * ELAN_TS_FW_PAGES_PER_BLOCK,
					       block_page_num,
					       p_page_block_buf,
					       page_block_buf_size,
					       error)) {
			ELAN_TS_ERROR(debug_setting, error, "failed to retrieve FW page block: ");
			return FALSE;
		}

		/* Write the page block data to hardware via HID I/O */
		if (!elan_ts_iap_write_firmware_pages(device,
						      p_page_block_buf,
						      page_block_buf_size,
						      error)) {
			ELAN_TS_ERROR(debug_setting, error, "failed to write FW page block: ");
			return FALSE;
		}

		/* Report progress within the "write" step */
		fu_progress_step_done(p_progress_write);
	}
	fu_progress_step_done(progress); // Phase 3 Done

	/* Phase 4: Self-Restart
	 * The device performs an internal reset after the final block is written. 
	 * It remains connected to the bus, but requires a cooldown period before 
	 * responding to new HID requests. */
	ELAN_TS_DEBUG(debug_setting, "waiting 1000ms for internal self-restart");
	
	/* Update status to inform the user why the process is pausing */
	fu_device_sleep(device, 1000);

	/* Finalize the overall progress */
	fu_progress_step_done(progress); // Phase 4 Done

	return TRUE;
}

/**
 * fu_elan_ts_hid_device_set_progress:
 * @device: a #FuDevice
 * @progress: a #FuProgress
 *
 * Configures the progress bar steps and their relative weights for the update process.
 **/
static void
fu_elan_ts_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	/* Identify this progress node with the current code location */
	fu_progress_set_id(progress, G_STRLOC);

	/* 
	 * Define the sequence of steps for the firmware update:
	 * 1. prepare-fw: Validation and decompression (Quick)
	 * 2. detach: Transitioning to IAP/Bootloader mode (Quick)
	 * 3. write: Main firmware transfer loop (Most time-consuming)
	 * 4. attach: Returning to normal mode / Self-restart (Requires 1s sleep)
	 * 5. reload: Refreshing info and performing re-calibration (Important)
	 */
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 1, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 88, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "reload");
}

/**
 * fu_elan_ts_hid_device_convert_version:
 * @device: a #FuDevice
 * @version_raw: a raw version integer
 * 
 * Converts a raw version integer into a formatted string based on the device version format.
 * 
 * Returns: (transfer full): a formatted version string
 **/
static gchar *
fu_elan_ts_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
        /* Convert the raw 64-bit integer to a hex string representation */
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

/**
 * fu_elan_ts_hid_device_init:
 * @self: a #FuElanTsHidDevice
 *
 * Initializes the device instance with default flags, metadata, and I/O settings.
 **/
static void
fu_elan_ts_hid_device_init(FuElanTsHidDevice *self)
{
	/* Device capabilities and security flags */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	/* Assign the expected firmware GType for this device class */
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ELAN_TS_FIRMWARE);

	/* Metadata for UI representation and update protocol */
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TABLET);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elants");

	/* Version string format and plugin execution priority */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 255); /* better than i2c */

	/* Behavior control for device removal and reconnection */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);

	/* I/O backend configuration for hidraw node access */
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

/**
 * fu_elan_ts_hid_device_class_init:
 * @klass: a #FuElanTsHidDeviceClass
 *
 * Initializes the FuElanTsHidDevice class by overriding virtual functions (vfuncs)
 * and installing object properties. This defines the behavior and capabilities
 * of the ELAN touchscreen device plugin.
 **/
static void
fu_elan_ts_hid_device_class_init(FuElanTsHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	/* Override property handlers for GObject */
	object_class->get_property = fu_elan_ts_hid_device_get_property;
	object_class->set_property = fu_elan_ts_hid_device_set_property;

        /* 
	 * Register "firmware" as READABLE only.
	 * This satisfies the engine's requirement and stops the warning messages.
	 */
	pspec = g_param_spec_uint("firmware",
				  NULL, NULL,
				  0, G_MAXUINT, 0,
				  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property(object_class, PROP_FIRMWARE, pspec);

        /* Assign device-specific virtual function implementations */
	device_class->probe = fu_elan_ts_hid_device_probe;
	device_class->setup = fu_elan_ts_hid_device_setup;
	device_class->to_string = fu_elan_ts_hid_device_to_string;

	/* Update process related vfuncs */
	device_class->write_firmware = fu_elan_ts_hid_device_write_firmware;
	device_class->set_progress = fu_elan_ts_hid_device_set_progress;
	device_class->reload = fu_elan_ts_hid_device_reload;
	device_class->convert_version = fu_elan_ts_hid_device_convert_version;
}

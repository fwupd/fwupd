/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-hid-device.h"
#include "fu-elan-ts-hid.h"
#include "fu-elan-ts-hidraw.h"
#include "fu-elan-ts-iap.h"

/**
 * FuElanTsHidDevice:
 * @parent_instance:	Inherited parent instance
 * @touch_state:	Current operating mode state
 * @bc_version:		Cached boot code version
 * @fw_version:		Cached firmware version
 *
 * An ELAN Touchscreen HID device.
 */
struct _FuElanTsHidDevice {
	FuHidrawDevice parent_instance;
	FuElanTsState touch_state;
	guint16 bc_version;
	guint16 fw_id;
	guint16 fw_version;
	guint16 test_version;
};

G_DEFINE_TYPE(FuElanTsHidDevice, fu_elan_ts_hid_device, FU_TYPE_HIDRAW_DEVICE)

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
	const gchar *subsystem = NULL;
	guint16 hid_vid = 0;
	guint16 hid_pid = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* assignments */
	hid_vid = fu_device_get_vid(device);
	hid_pid = fu_device_get_pid(device);

	if (FU_IS_UDEV_DEVICE(device))
		subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));

	g_debug("probing device '%s' (subsystem: %s)",
		fu_device_get_name(device),
		(subsystem != NULL) ? subsystem : "unknown");

	/* ensure the device belongs to the functional hidraw interface */
	if ((subsystem == NULL) || (g_ascii_strcasecmp(subsystem, "hidraw") != 0)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device has incorrect subsystem %s, expected hidraw",
			    (subsystem != NULL) ? subsystem : "(null)");
		return FALSE;
	}

	/* verify the Vendor ID to ensure it's an ELAN device */
	g_debug("checking HIDRAW VID: 0x%04x, PID: 0x%04x", hid_vid, hid_pid);
	if (hid_vid != ELAN_TS_HID_VID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "incorrect HID VID 0x%04x, expected 0x%04x",
			    (guint)hid_vid,
			    (guint)ELAN_TS_HID_VID);
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
	FuElanTsHidDevice *self = NULL;
	g_autofree gchar *summary = NULL;
	g_autofree gchar *bc_version_str = NULL;
	g_autofree gchar *fw_test_version_str = NULL;
	guint8 hello_packet = 0;
	guint16 bc_version = 0;
	guint16 fw_id = 0;
	guint16 fw_version = 0;
	guint16 test_solution_version = 0;
	guint16 test_version = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_ELAN_TS_HID_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* assignment */
	self = FU_ELAN_TS_HID_DEVICE(device);

	/* basic metadata & flags */
	fu_device_set_vendor(device, "Elan Microelectronics");
	if ((fu_device_get_name(device) == NULL) ||
	    (g_str_has_prefix(fu_device_get_name(device), "hidraw"))) {
		fu_device_set_name(device, "Elan Touchscreen");
	}

	/* common instance ID components: Vendor and Product ID */
	fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(device));
	fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(device));

	/* hardware probe: Read Hello Packet and initial BC Version */
	if (!fu_elan_ts_hid_read_hello_packet_bc_version_with_retry(FU_HIDRAW_DEVICE(self),
								    &hello_packet,
								    &bc_version,
								    error)) {
		g_prefix_error_literal(error, "hardware communication test failed: ");
		return FALSE;
	}
	g_debug("hello packet: 0x%02x", hello_packet);

	/* mode-specific: Identify HW Series and Touch State based on Hello Packet */
	switch (hello_packet) {
	case FU_ELAN_TS_HELLO_PACKET_NORMAL_MODE:
		g_debug("normal mode detected");

		/* update internal data */
		self->touch_state = FU_ELAN_TS_STATE_NORMAL_MODE;

		/* read Boot Code Version */
		if (!fu_elan_ts_hid_read_boot_code_version(FU_HIDRAW_DEVICE(self),
							   &bc_version,
							   error)) {
			g_prefix_error_literal(error, "failed to read BC version in normal mode: ");
			return FALSE;
		}
		g_debug("bc version: 0x%04x", bc_version);
		self->bc_version = bc_version;

		/* read FW ID */
		if (!fu_elan_ts_hid_read_fw_id(FU_HIDRAW_DEVICE(self), &fw_id, error)) {
			g_prefix_error_literal(error, "failed to read FW ID in normal mode: ");
			return FALSE;
		}
		g_debug("fw id: 0x%04x", fw_id);
		self->fw_id = fw_id;

		/* read FW Version */
		if (!fu_elan_ts_hid_read_fw_version(FU_HIDRAW_DEVICE(self), &fw_version, error)) {
			g_prefix_error_literal(error, "failed to read FW Version in normal mode: ");
			return FALSE;
		}
		g_debug("fw version: 0x%04x", fw_version);
		self->fw_version = fw_version;

		/* read Test-Solution Version */
		if (!fu_elan_ts_hid_read_test_solution_version(FU_HIDRAW_DEVICE(self),
							       &test_solution_version,
							       error)) {
			g_prefix_error_literal(
			    error,
			    "failed to read test-solution version in normal mode: ");
			return FALSE;
		}
		g_debug("test-solution version: 0x%04x", test_solution_version);
		test_version = (test_solution_version & 0xFF00) >> 8;
		self->test_version = test_version;

		/* display combined FW and Test versions in the main Version field */
		fw_test_version_str = g_strdup_printf("%04x%04x", fw_version, test_version);
		fu_device_set_version(device, fw_test_version_str);

		/* set device summary visible in 'get-devices --show-all' */
		summary = g_strdup_printf("Elan Touchscreen (FWID: 0x%04x)", fw_id);
		fu_device_set_summary(device, summary);
		break;

	case FU_ELAN_TS_HELLO_PACKET_RECOVERY_MODE:
		g_debug("recovery mode detected");
		g_debug("bc version: 0x%04x", bc_version);

		/* update internal data */
		self->touch_state = FU_ELAN_TS_STATE_RECOVERY_MODE;
		self->bc_version = bc_version;

		/* mark as bootloader - Recovery-mode device won't have FWID */
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

		/* force update by setting version to 0x0000 */
		fu_device_set_version(device, "0000");

		/* set device summary visible in 'get-devices --show-all' */
		summary = g_strdup_printf("Elan Touchscreen (Recovery Mode)");
		fu_device_set_summary(device, summary);
		break;

	default:
		/* handle unexpected hello packet values */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown hello packet (0x%02x)",
			    hello_packet);
		return FALSE;
	}

	/* display Boot Code version in the Bootloader Version field */
	bc_version_str = g_strdup_printf("%04x", bc_version);
	fu_device_set_version_bootloader(device, bc_version_str);

	/* Build Instance ID
	 * If in Recovery, it will be HIDRAW\VEN_04F3&DEV_0732
	 * If in Normal, it will be HIDRAW\VEN_04F3&DEV_XXXX (where XXXX is the product PID) */
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "HIDRAW",
					      "VEN",
					      "DEV",
					      NULL)) {
		g_prefix_error_literal(error, "failed to build instance ID: ");
		return FALSE;
	}

	g_debug("setup() completed successfully for %s", fu_device_get_id(device));
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
	FuElanTsHidDevice *self = NULL;

	/* basic sanity checks */
	g_return_if_fail(FU_IS_ELAN_TS_HID_DEVICE(device));
	g_return_if_fail(str != NULL);

	/* assignment */
	self = FU_ELAN_TS_HID_DEVICE(device);

	/* always show BC-Version if available */
	if (self->bc_version != 0)
		fwupd_codec_string_append_hex(str, idt, "BC-Version", self->bc_version);

	/* only show full firmware details in normal mode */
	if (self->touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) {
		if (self->fw_id != 0)
			fwupd_codec_string_append_hex(str, idt, "FW-ID", self->fw_id);
		if (self->fw_version != 0)
			fwupd_codec_string_append_hex(str, idt, "FW-Version", self->fw_version);

		/* test version is always shown as it could be valid even if zero */
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
	FuElanTsHidDevice *self = NULL;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_ELAN_TS_HID_DEVICE(device), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* assignment */
	self = FU_ELAN_TS_HID_DEVICE(device);

	g_debug("starting post-update reload process");

	/* setup device: refresh cached versions and hardware info from the new firmware */
	g_debug("refreshing device hardware information");
	if (!fu_elan_ts_hid_device_setup(device, error)) {
		g_prefix_error_literal(error, "failed to setup device during reload: ");
		return FALSE;
	}

	/* run Re-calibration with fwupd retry framework to ensure touch accuracy */
	g_debug("triggering post-update re-calibration");
	if (!fu_elan_ts_hid_device_recalibrate_with_retry(FU_HIDRAW_DEVICE(self), error)) {
		g_prefix_error_literal(error, "failed to re-calibrate during reload: ");
		return FALSE;
	}

	g_debug("reload process completed successfully");
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
	FuElanTsHidDevice *self = NULL;
	FuElanTsFirmware *elan_ts_fw = NULL;
	FuElanTsFwType fw_type = FU_ELAN_TS_FW_TYPE_UNKNOWN;
	FuElanTsState touch_state = FU_ELAN_TS_STATE_UNKNOWN;
	FuProgress *p_progress_write = NULL;
	g_autoptr(GBytes) p_fw_bin = NULL;
	g_autofree guint8 *p_page_block_buf = NULL;
	guint32 debug_setting = 0;
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
	gboolean skip_info_data_update = FALSE;
	gboolean skip_remark_id_check = FALSE;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_ELAN_TS_HID_DEVICE(device), FALSE);
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* assignments */
	self = FU_ELAN_TS_HID_DEVICE(device);
	elan_ts_fw = FU_ELAN_TS_FIRMWARE(firmware);
	debug_setting = fu_elan_ts_firmware_get_debug_setting(elan_ts_fw);
	skip_info_data_update =
	    (debug_setting & FU_ELAN_TS_DEBUG_SETTING_SKIP_INFO_DATA_UPDATE) != 0;
	skip_remark_id_check = (debug_setting & FU_ELAN_TS_DEBUG_SETTING_SKIP_REMARK_ID_CHECK) != 0;

	/* retrieve current device status */
	touch_state = self->touch_state;
	bc_version = self->bc_version;
	fw_version = self->fw_version;

	g_debug("starting FW Update (skip_info=%s, skip_remark=%s)",
		skip_info_data_update ? "true" : "false",
		skip_remark_id_check ? "true" : "false");

	/*
	 * Define clear phases with specific weights:
	 * Phase 1: Validation & Prepare.
	 * Phase 2: Transition to Bootloader
	 * Phase 3: Write Firmware Blocks
	 * Phase 4: Self-restart Delay
	 *
	 * Subdivide the incoming 95% write node into 3 internal sub-phases.
	 * Weight allocation: Phase 1 & 2 (2%), Phase 3 (93%), Phase 4 (5%)
	 */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "init");	/* phase 1 & 2 */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93, "write"); /* phase 3 */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "restart"); /* phase 4 */

	/* phase 1: Validate Firmware Container and Format */
	p_fw_bin = fu_firmware_get_bytes(firmware, error);
	if (p_fw_bin == NULL) {
		g_prefix_error_literal(error, "failed to get firmware payload: ");
		return FALSE;
	}

	fw_type = fu_elan_ts_firmware_get_fw_type(elan_ts_fw);
	if (fw_type != FU_ELAN_TS_FW_TYPE_EKT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware format: %u (expected EKT)",
			    (guint)fw_type);
		return FALSE;
	}

	/* retrieve firmware binary size and calculate pagination */
	fw_bin_size = fu_elan_ts_firmware_get_bin_size(elan_ts_fw);
	if (fw_bin_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid firmware binary size");
		return FALSE;
	}

	/* prepare information page update for devices in normal mode */
	if ((touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) && (!skip_info_data_update)) {
		solution_id = (guint8)((fw_version & 0xFF00) >> 8);
		g_debug("preparing Info Page, FW_Version: 0x%04x, Solution_ID: 0x%02x",
			fw_version,
			solution_id);
		if (!fu_elan_ts_iap_read_and_update_info_page(FU_HIDRAW_DEVICE(self),
							      solution_id,
							      info_fw_page_buf,
							      sizeof(info_fw_page_buf),
							      error)) {
			g_prefix_error_literal(error, "failed to update information page: ");
			return FALSE;
		}
		g_debug("successfully prepared updated information page");
	}

	/* determine if Remark ID verification is required based on BC version */
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

	/* execute Remark ID compatibility check */
	if (remark_id_check) {
		if (!skip_remark_id_check) {
			if (!fu_elan_ts_iap_check_remark_id(FU_HIDRAW_DEVICE(self),
							    firmware,
							    touch_state,
							    fw_version,
							    bc_version,
							    error)) {
				g_prefix_error_literal(error, "Remark ID Check failed: ");
				return FALSE;
			}
		} else {
			g_debug("bypassing comparison, performing mandatory ROM read");
			if (!fu_elan_ts_hid_read_remark_id(FU_HIDRAW_DEVICE(self),
							   touch_state,
							   fw_version,
							   bc_version,
							   &dev_remark_id,
							   error)) {
				g_prefix_error_literal(error,
						       "failed to read Mandatory Remark ID: ");
				return FALSE;
			}
			g_debug("hardware Remark ID: 0x%04x", dev_remark_id);
		}
	}

	/* phase 2: Detach / Transition to Bootloader
	 * Switch device to bootloader/IAP mode */
	g_debug("transitioning to Bootloader mode");
	if (!fu_elan_ts_iap_switch_to_boot_code(FU_HIDRAW_DEVICE(self),
						(touch_state != FU_ELAN_TS_STATE_NORMAL_MODE),
						error)) {
		g_prefix_error_literal(error, "failed to switch to bootloader: ");
		return FALSE;
	}
	fu_progress_step_done(progress); /* phase 1 & 2 Done */

	/* phase 3: Write - Main firmware data transfer */
	p_progress_write = fu_progress_get_child(progress);
	if (p_progress_write == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get child progress for write phase");
		return FALSE;
	}
	fu_progress_set_id(p_progress_write, G_STRLOC);

	g_debug("start Firmware Update Process");

	/* write prepared information page first if applicable */
	if ((touch_state == FU_ELAN_TS_STATE_NORMAL_MODE) && (!skip_info_data_update)) {
		g_debug("writing Information Page");
		if (!fu_elan_ts_iap_write_firmware_pages(FU_HIDRAW_DEVICE(self),
							 info_fw_page_buf,
							 sizeof(info_fw_page_buf),
							 error)) {
			g_prefix_error_literal(error, "failed to write information page: ");
			return FALSE;
		}
		g_debug("successfully wrote information page to device");
	}

	/* calculate pagination parameters for the main firmware payload (fw_bin) */
	page_count = fu_elan_ts_firmware_get_page_count(elan_ts_fw);
	block_count = (page_count + ELAN_TS_FW_PAGES_PER_BLOCK - 1) / ELAN_TS_FW_PAGES_PER_BLOCK;
	g_debug("main payload (FW_BIN) - size: %u, total pages: %u, total blocks: %u",
		fw_bin_size,
		page_count,
		block_count);

	/* prepare a fixed-size buffer for page blocks to avoid frequent reallocations.
	 * The maximum size is a full block of pages. */
	max_page_block_buf_size = (gsize)(ELAN_TS_FW_PAGES_PER_BLOCK * ELAN_TS_FW_PAGE_SIZE);
	p_page_block_buf = g_malloc0(max_page_block_buf_size);
	fu_progress_set_steps(p_progress_write, block_count);

	/* main firmware data transfer loop */
	for (block_index = 0; block_index < block_count; block_index++) {
		/* clear the buffer before reuse to ensure no stale data remains */
		memset(p_page_block_buf, 0, max_page_block_buf_size);

		/* determine number of pages in the current block */
		if ((block_index == (block_count - 1)) &&
		    ((page_count % ELAN_TS_FW_PAGES_PER_BLOCK) != 0))
			block_page_num = page_count % ELAN_TS_FW_PAGES_PER_BLOCK;
		else
			block_page_num = ELAN_TS_FW_PAGES_PER_BLOCK;

		/* calculate current block size */
		page_block_buf_size = (gsize)block_page_num * ELAN_TS_FW_PAGE_SIZE;

		/* retrieve the page block data from firmware bin */
		if (!fu_elan_ts_firmware_get_page(elan_ts_fw,
						  block_index * ELAN_TS_FW_PAGES_PER_BLOCK,
						  block_page_num,
						  p_page_block_buf,
						  page_block_buf_size,
						  error)) {
			g_prefix_error_literal(error, "failed to retrieve FW page block: ");
			return FALSE;
		}

		/* write the page block data to hardware via HID I/O */
		if (!fu_elan_ts_iap_write_firmware_pages(FU_HIDRAW_DEVICE(self),
							 p_page_block_buf,
							 page_block_buf_size,
							 error)) {
			g_prefix_error_literal(error, "failed to write FW page block: ");
			return FALSE;
		}

		/* report progress within the "write" step */
		fu_progress_step_done(p_progress_write);
	}
	fu_progress_step_done(progress); /* phase 3 done */

	/* phase 4: self-restart
	 * The device performs an internal reset after the final block is written.
	 * It remains connected to the bus, but requires a cooldown period before
	 * responding to new HID requests. */
	g_debug("waiting %ums for internal self-restart", (guint)ELAN_TS_SELF_RESTART_DELAY_MS);

	/* update status to inform the user why the process is pausing */
	fu_device_sleep(device, ELAN_TS_SELF_RESTART_DELAY_MS);

	/* finalize the overall progress */
	fu_progress_step_done(progress); /* phase 4 done */

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
	/* basic sanity checks - Specialize to the exact subclass type */
	g_return_if_fail(FU_IS_ELAN_TS_HID_DEVICE(device));
	g_return_if_fail(FU_IS_PROGRESS(progress));

	/* identify this progress node with the current code location */
	fu_progress_set_id(progress, G_STRLOC);

	/*
	 * Simplify progress stages to align with internal write_firmware logic.
	 * The preparation, bootloader transition, and 1000-ms self-restart delay
	 * are all fully handled within the write_firmware vfunc itself (95%).
	 * The final 5% is reserved for the engine to reload the device.
	 */
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
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
	/* basic sanity check */
	g_return_val_if_fail(FU_IS_ELAN_TS_HID_DEVICE(device), NULL);

	/* convert the raw 64-bit integer to a hex string representation */
	return fu_version_from_uint64(version_raw, fu_device_get_version_format(device));
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
	/* device capabilities and security flags */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	/* assign the expected firmware GType for this device class */
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ELAN_TS_FIRMWARE);

	/* metadata for UI representation and update protocol */
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TABLET);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elants");

	/* version string format and plugin execution priority */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1);

	/* behavior control for device removal and reconnection */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);

	/* i/o backend configuration for hidraw node access */
	if (FU_IS_UDEV_DEVICE(self)) {
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	}
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

	/* basic sanity check */
	g_return_if_fail(klass != NULL);

	/* assign device-specific virtual function implementations */
	device_class->probe = fu_elan_ts_hid_device_probe;
	device_class->setup = fu_elan_ts_hid_device_setup;
	device_class->to_string = fu_elan_ts_hid_device_to_string;

	/* update process related vfuncs */
	device_class->write_firmware = fu_elan_ts_hid_device_write_firmware;
	device_class->set_progress = fu_elan_ts_hid_device_set_progress;
	device_class->reload = fu_elan_ts_hid_device_reload;
	device_class->convert_version = fu_elan_ts_hid_device_convert_version;
}

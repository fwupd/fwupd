/*
 * Copyright 2026 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"
#include "fu-elantp-hid-device.h"
#include "fu-elantp-hid-mcu-device.h"
#include "fu-elantp-struct.h"

struct _FuElantpHidMcuDevice {
	FuDevice parent_instance;
	guint16 ic_page_count;
	guint16 ic_type;
	guint16 iap_type;
	guint16 iap_ctrl;
	guint16 iap_password;
	guint16 iap_ver;
	guint16 module_id;
	guint16 fw_page_size;
	guint16 fw_section_size;
	guint16 fw_no_of_sections;
	gboolean force_table_support;
	guint32 force_table_addr;
	guint8 pattern;
};

G_DEFINE_TYPE(FuElantpHidMcuDevice, fu_elantp_hid_mcu_device, FU_TYPE_DEVICE)

static gboolean
fu_elantp_hid_mcu_device_detach(FuElantpHidMcuDevice *self, FuProgress *progress, GError **error);

static void
fu_elantp_hid_mcu_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "IcType", self->ic_type);
	fwupd_codec_string_append_hex(str, idt, "ModuleId", self->module_id);
	fwupd_codec_string_append_hex(str, idt, "Pattern", self->pattern);
	fwupd_codec_string_append_hex(str, idt, "FwPageSize", self->fw_page_size);
	fwupd_codec_string_append_hex(str, idt, "FwSectionSize", self->fw_section_size);
	fwupd_codec_string_append_hex(str, idt, "FwNoOfSections", self->fw_no_of_sections);
	fwupd_codec_string_append_hex(str, idt, "IcPageCount", self->ic_page_count);
	fwupd_codec_string_append_hex(str, idt, "IapVer", self->iap_ver);
	fwupd_codec_string_append_hex(str, idt, "IapType", self->iap_type);
	fwupd_codec_string_append_hex(str, idt, "IapCtrl", self->iap_ctrl);
	fwupd_codec_string_append_bool(str, idt, "ForceTableSupport", self->force_table_support);
	fwupd_codec_string_append_hex(str, idt, "ForceTableAddr", self->force_table_addr);
}

static gboolean
fu_elantp_hid_mcu_device_ensure_iap_ctrl(FuElantpHidMcuDevice *self, GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_IAP_CTRL,
					   &self->iap_ctrl,
					   error)) {
		g_prefix_error_literal(error, "failed to read IAPControl: ");
		return FALSE;
	}

	/* in bootloader mode? */
	if (self->force_table_support && self->iap_ver <= 5) {
		if ((self->iap_ctrl & ETP_I2C_MAIN_MODE_ON2) == 0)
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		else
			fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}

	if ((self->iap_ctrl & ETP_I2C_MAIN_MODE_ON) == 0)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_read_force_table_enable(FuElantpHidMcuDevice *self, GError **error)
{
	FuDevice *proxy;
	guint16 value = 0;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_FORCE_TYPE_ENABLE,
					   &value,
					   error)) {
		g_prefix_error_literal(error, "failed to read force type cmd: ");
		return FALSE;
	}
	if (value == 0xFFFF || value == FU_ETP_CMD_I2C_FORCE_TYPE_ENABLE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "forcetype cmd not supported");
		return FALSE;
	}
	if (((value & 0xFF) & ETP_FW_FORCE_TYPE_ENABLE_BIT) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "force type table not supported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_ensure_force_table_addr(FuElantpHidMcuDevice *self, GError **error)
{
	FuDevice *proxy;
	guint16 addr_wrds = 0;

	if (self->iap_ver == 0x3) {
		if (self->module_id == 0x130 || self->module_id == 0x133)
			self->force_table_addr = 0xFF40 * 2;
		else
			self->force_table_addr = 0;
		return TRUE;
	}
	/* FIXME: I have no idea why we should hardcode this here -- we used to get it from the
	 * firmware file (which is bad) -- we should probably delete this block and get the address
	 * from the hardware and re-record the emulation */
	if (self->ic_type == FU_ETP_IC_NUM14 && self->iap_ver == 4) {
		self->force_table_addr = 0xF600;
		return TRUE;
	}
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_FORCE_ADDR,
					   &addr_wrds,
					   error)) {
		g_prefix_error_literal(error, "failed to read force table address cmd: ");
		return FALSE;
	}
	if (addr_wrds % 32 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "illegal force table address: 0x%x",
			    addr_wrds);
		return FALSE;
	}
	self->force_table_addr = addr_wrds * 2;

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_write_fw_password(FuElantpHidMcuDevice *self,
					   guint16 ic_type,
					   guint16 iap_ver,
					   GError **error)
{
	FuDevice *proxy;
	guint16 pw = ETP_I2C_IC13_IAPV5_PW;
	guint16 value;

	if (iap_ver >= 0x7 && ic_type == FU_ETP_IC_NUM13)
		pw = ETP_I2C_IC13_IAPV7_PW;
	else if (iap_ver >= 0x5 && ic_type == FU_ETP_IC_NUM13)
		pw = ETP_I2C_IC13_IAPV5_PW;
	else if ((iap_ver >= 0x4) && (ic_type == FU_ETP_IC_NUM14 || ic_type == FU_ETP_IC_NUM15))
		pw = ETP_I2C_IC13_IAPV5_PW;
	else
		return TRUE;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_MCU_FEATURE,
					    FU_ETP_CMD_I2C_FW_PW,
					    pw,
					    error)) {
		g_prefix_error_literal(error, "failed to write fw password cmd: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_FW_PW,
					   &value,
					   error)) {
		g_prefix_error_literal(error, "failed to read fw password cmd: ");
		return FALSE;
	}
	if (value != pw) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "can't set fw password got: 0x%x",
			    value);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_setup(FuDevice *device, GError **error)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	FuDevice *proxy;
	const gchar *name;
	guint16 fwver = 0;
	guint16 tmp = 0;
	g_autofree gchar *version_bl = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_forcetable = NULL;

	/* get pattern */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_GET_HID_ID,
					   &tmp,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU HID ID: ");
		return FALSE;
	}
	self->pattern = tmp != 0xFFFF ? (tmp & 0xFF00) >> 8 : 0;

	/* get current firmware version */
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_FW_VERSION,
					   &fwver,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU fw version: ");
		return FALSE;
	}
	if (fwver == 0xFFFF || fwver == FU_ETP_CMD_I2C_FW_VERSION)
		fwver = 0;
	fu_device_set_version_raw(device, fwver);

	/* get IAP firmware version */
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   self->pattern == 0 ? FU_ETP_CMD_I2C_IAP_VERSION
							      : FU_ETP_CMD_I2C_IAP_VERSION_2,
					   &self->iap_ver,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1)
		self->iap_ver >>= 8;
	version_bl = fu_version_from_uint16(self->iap_ver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version_bootloader(device, version_bl);

	/* get module ID */
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_GET_MODULE_ID,
					   &self->module_id,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU module ID: ");
		return FALSE;
	}

	/* get OSM version */
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_OSM_VERSION,
					   &tmp,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU OSM version: ");
		return FALSE;
	}
	if (tmp == FU_ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
						   FU_ETP_RPTID_MCU_FEATURE,
						   FU_ETP_CMD_I2C_IAP_ICBODY,
						   &self->ic_type,
						   error)) {
			g_prefix_error_literal(error, "failed to read MCU IC body: ");
			return FALSE;
		}
		self->ic_type &= 0xFF;
	} else {
		self->ic_type = tmp >> 8;
	}

	/* define the extra instance IDs (ic_type + module_id + driver) */
	fu_device_add_instance_u8(device, "ICTYPE", self->ic_type);
	fu_device_add_instance_u16(device, "MOD", self->module_id);
	fu_device_add_instance_str(device, "PART", "MCU");
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "ELANTP",
					      "ICTYPE",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "ELANTP", "ICTYPE", "MOD", "PART", NULL))
		return FALSE;

	/* no quirk entry */
	if (self->ic_page_count == 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no page count for ELANTP\\ICTYPE_%02X",
			    self->ic_type);
		return FALSE;
	}

	/* ic_page_count is based on 64 bytes/page */
	fu_device_set_firmware_size(device, (guint64)self->ic_page_count * (guint64)64);

	/* is in bootloader mode */
	if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, error))
		return FALSE;
	/* can query for haptic function */
	if (fu_device_has_private_flag(device,
				       FU_ELANTP_DEVICE_PRIVATE_FLAG_CAN_QUERY_HAPTIC_FUNCTION)) {
		if (!fu_elantp_hid_mcu_device_read_force_table_enable(self, &error_forcetable)) {
			g_debug("no MCU forcetable detected: %s", error_forcetable->message);
		} else {
			if (!fu_elantp_hid_mcu_device_ensure_force_table_addr(self, error)) {
				g_prefix_error_literal(error, "get MCU forcetable address fail: ");
				return FALSE;
			}
			self->force_table_support = TRUE;
			/* is in bootloader mode */
			if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, error))
				return FALSE;
		}
	}

	/* fix an unsuitable i²c name, e.g. `VEN 04F3:00 04F3:3XXX` or `0672:00 04F3:3187` */
	name = fu_device_get_name(device);
	if (name != NULL && g_strstr_len(name, -1, ":00 ") != NULL)
		fu_device_set_name(device, "MCU");

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_check_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuFirmwareParseFlags flags,
					GError **error)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	guint16 module_id;
	guint16 ic_type;
	gboolean force_table_support;

	/* check is compatible with hardware */
	module_id = fu_elantp_firmware_get_module_id(FU_ELANTP_FIRMWARE(firmware));
	if (self->module_id != module_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "mcu firmware incompatible, got 0x%04x, expected 0x%04x",
			    module_id,
			    self->module_id);
		return FALSE;
	}
	ic_type = fu_elantp_firmware_get_ic_type(FU_ELANTP_FIRMWARE(firmware));
	if (self->ic_type != ic_type) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "mcu firmware ic type incompatible, got 0x%04x, expected 0x%04x",
			    ic_type,
			    self->ic_type);
		return FALSE;
	}
	force_table_support =
	    fu_elantp_firmware_get_forcetable_support(FU_ELANTP_FIRMWARE(firmware));
	if (self->ic_type == FU_ETP_IC_NUM14 && self->iap_ver == 4) {
		if (self->force_table_support != force_table_support) {
			g_debug("fixing up mcu force-table support %s->%s due to chip errata",
				self->force_table_support ? "enabled" : "disabled",
				force_table_support ? "enabled" : "disabled");
			self->force_table_support = force_table_support;
		}
	} else if (self->force_table_support != force_table_support) {
		g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "mcu firmware incompatible, forcetable incorrect.");
		return FALSE;
	}
	if (force_table_support) {
		guint32 force_table_addr;
		guint32 diff_size;
		force_table_addr =
		    fu_elantp_firmware_get_forcetable_addr(FU_ELANTP_FIRMWARE(firmware));
		if (self->ic_type == FU_ETP_IC_NUM14 && self->iap_ver == 4)
			self->force_table_addr = force_table_addr;
		if (self->force_table_addr < force_table_addr) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "mcu firmware forcetable address incompatible, got 0x%04x and "
				    "expected 0x%04x",
				    force_table_addr / 2,
				    self->force_table_addr / 2);
			return FALSE;
		}
		diff_size = self->force_table_addr - force_table_addr;
		if (diff_size % 64 != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "mcu firmware forcetable address incompatible, got 0x%04x, "
				    "expected 0x%04x",
				    force_table_addr / 2,
				    self->force_table_addr / 2);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_write_chunk(FuElantpHidMcuDevice *self,
				     FuChunk *chk,
				     guint16 *checksum,
				     guint16 *fw_section_cnt,
				     gboolean *done,
				     GError **error)
{
	FuDevice *proxy;
	guint16 csum_tmp =
	    fu_sum16w(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* write block */
	fu_byte_array_append_uint8(buf, FU_ETP_RPTID_MCU_IAP); /* report ID */
	g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	fu_byte_array_append_uint16(buf, csum_tmp, G_LITTLE_ENDIAN);
	fu_byte_array_set_size(buf, self->fw_section_size + 3, 0x0);

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(proxy),
					  buf->data,
					  buf->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	*fw_section_cnt += 1;
	if (self->fw_section_size == self->fw_page_size ||
	    *fw_section_cnt == self->fw_no_of_sections) {
		fu_device_sleep(FU_DEVICE(self),
				self->fw_page_size == 512 ? ELANTP_DELAY_WRITE_BLOCK_512
							  : ELANTP_DELAY_WRITE_BLOCK);

		if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, error))
			return FALSE;
		*fw_section_cnt = 0;
		if (self->iap_ctrl & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "mcu bootloader reports failed write: 0x%x",
				    self->iap_ctrl);
			return FALSE;
		}
		if (self->iap_ctrl & ETP_FW_IAP_END_WAITWDT)
			*done = TRUE;
	}

	/* success */
	*checksum += csum_tmp;
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_write_chunks(FuElantpHidMcuDevice *self,
				      FuChunkArray *chunks,
				      guint16 *checksum,
				      FuProgress *progress,
				      GError **error)
{
	guint16 fw_section_cnt = 0;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		gboolean done = FALSE;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_elantp_hid_mcu_device_write_chunk(self,
							  chk,
							  checksum,
							  &fw_section_cnt,
							  &done,
							  error))
			return FALSE;
		if (done) {
			fu_progress_finished(progress);
			break;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}
static gboolean
fu_elantp_hid_mcu_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	FuDevice *proxy;
	guint16 checksum = 0;
	guint16 checksum_device = 0;
	guint16 iap_addr;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) stream_offset = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 30, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "reset");

	/* detach: FIXME -- why isn't this being done in ->detach? */
	if (!fu_elantp_hid_mcu_device_detach(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	iap_addr = fu_elantp_firmware_get_iap_addr(FU_ELANTP_FIRMWARE(firmware));
	if (iap_addr >= fu_firmware_get_size(firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid IAP start 0x%x for firmware size 0x%x",
			    iap_addr,
			    (guint)fu_firmware_get_size(firmware));
		return FALSE;
	}
	stream_offset = fu_partial_input_stream_new(stream,
						    iap_addr,
						    fu_firmware_get_size(firmware) - iap_addr,
						    error);
	if (stream_offset == NULL)
		return FALSE;
	chunks =
	    fu_chunk_array_new_from_stream(stream_offset, 0x0, 0x0, self->fw_section_size, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_elantp_hid_mcu_device_write_chunks(self,
						   chunks,
						   &checksum,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify the written checksum */
	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_IAP_CHECKSUM,
					   &checksum_device,
					   error))
		return FALSE;
	if (checksum != checksum_device) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "mcu checksum failed 0x%04x != 0x%04x",
			    checksum,
			    checksum_device);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* wait for a reset */
	fu_device_sleep_full(device,
			     ELANTP_DELAY_COMPLETE,
			     fu_progress_get_child(progress)); /* ms */
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_read_iap_type(FuElantpHidMcuDevice *self,
				       guint16 *iap_type,
				       GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_IAP_TYPE,
					   iap_type,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU IAP type: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_detach(FuElantpHidMcuDevice *self, FuProgress *progress, GError **error)
{
	guint16 iap_ver = 0;
	guint16 ic_type;
	guint16 tmp;
	FuDevice *proxy;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_TP_FEATURE,
					    FU_ETP_CMD_I2C_TP_SETTING,
					    ETP_I2C_DISABLE_SCAN,
					    error)) {
		g_prefix_error_literal(error, "cannot disable TP scan: ");
		return FALSE;
	}

	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_TP_FEATURE,
					    FU_ETP_CMD_I2C_IAP_RESET,
					    ETP_I2C_DISABLE_REPORT,
					    error)) {
		g_prefix_error_literal(error, "cannot disable TP report: ");
		return FALSE;
	}

	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_MCU_FEATURE,
					    FU_ETP_CMD_I2C_IAP_RESET,
					    ETP_I2C_DISABLE_REPORT,
					    error)) {
		g_prefix_error_literal(error, "cannot disable MCU report: ");
		return FALSE;
	}

	/* sanity check */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_info("in bootloader mode, reset MCU IC");
		if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
						    FU_ETP_RPTID_MCU_FEATURE,
						    FU_ETP_CMD_I2C_IAP_RESET,
						    ETP_I2C_IAP_RESET,
						    error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);
	}

	/* get OSM version */
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   FU_ETP_CMD_I2C_OSM_VERSION,
					   &tmp,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU OSM version: ");
		return FALSE;
	}
	if (tmp == FU_ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
						   FU_ETP_RPTID_MCU_FEATURE,
						   FU_ETP_CMD_I2C_IAP_ICBODY,
						   &ic_type,
						   error)) {
			g_prefix_error_literal(error, "failed to read MCU IC body: ");
			return FALSE;
		}
		ic_type &= 0xFF;
	} else {
		ic_type = (tmp >> 8) & 0xFF;
	}

	/* get IAP firmware version */
	if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
					   FU_ETP_RPTID_MCU_FEATURE,
					   self->pattern == 0 ? FU_ETP_CMD_I2C_IAP_VERSION
							      : FU_ETP_CMD_I2C_IAP_VERSION_2,
					   &iap_ver,
					   error)) {
		g_prefix_error_literal(error, "failed to read MCU bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1)
		iap_ver >>= 8;

	/* set the page size */
	if (ic_type >= 0x10 && iap_ver >= 1) {
		if (iap_ver >= 2 && (ic_type == FU_ETP_IC_NUM14 || ic_type == FU_ETP_IC_NUM15)) {
			self->fw_page_size = 512;
			if (iap_ver >= 3) {
				if (!fu_elantp_hid_mcu_device_read_iap_type(self,
									    &self->iap_type,
									    error))
					return FALSE;
				self->fw_section_size = self->iap_type * 2;
				if (self->fw_section_size == 0) {
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_NOT_SUPPORTED,
							    "invalid MCU section size");
					return FALSE;
				}
				self->fw_no_of_sections =
				    self->fw_page_size / self->fw_section_size;
			} else {
				self->fw_section_size = 512;
			}
		} else {
			self->fw_page_size = 128;
			self->fw_section_size = 128;
		}
		if (self->fw_page_size == self->fw_section_size) {
			if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
							    FU_ETP_RPTID_MCU_FEATURE,
							    FU_ETP_CMD_I2C_IAP_TYPE,
							    self->fw_page_size / 2,
							    error))
				return FALSE;
			if (!fu_elantp_hid_device_read_cmd(FU_ELANTP_HID_DEVICE(proxy),
							   FU_ETP_RPTID_MCU_FEATURE,
							   FU_ETP_CMD_I2C_IAP_TYPE,
							   &self->iap_type,
							   error)) {
				g_prefix_error_literal(error, "failed to read MCU IAP type: ");
				return FALSE;
			}
			if (self->iap_type != self->fw_page_size / 2) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "failed to set MCU IAP type");
				return FALSE;
			}
		}
	}
	if (!fu_elantp_hid_mcu_device_write_fw_password(self, ic_type, iap_ver, error))
		return FALSE;
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_MCU_FEATURE,
					    FU_ETP_CMD_I2C_IAP,
					    self->iap_password,
					    error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_UNLOCK);
	if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, error))
		return FALSE;
	if ((self->iap_ctrl & ETP_FW_IAP_CHECK_PW) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "unexpected MCU bootloader password");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	FuDevice *proxy;

	proxy = fu_device_get_proxy(FU_DEVICE(self), error);
	if (proxy == NULL)
		return FALSE;

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in MCU runtime mode, skipping");
		return TRUE;
	}

	/* reset back to runtime */
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_MCU_FEATURE,
					    FU_ETP_CMD_I2C_IAP_RESET,
					    ETP_I2C_IAP_RESET,
					    error)) {
		g_prefix_error_literal(error, "cannot reset IAP: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_MCU_FEATURE,
					    FU_ETP_CMD_I2C_IAP_RESET,
					    ETP_I2C_ENABLE_REPORT,
					    error)) {
		g_prefix_error_literal(error, "cannot enable MCU report: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_MCU_FEATURE,
					    0x0306,
					    0x003,
					    error)) {
		g_prefix_error_literal(error, "cannot switch to MCU PTP mode: ");
		return FALSE;
	}
	if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_device_check_fwupd_version(FU_DEVICE(self), "2.1.2")) {
		if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
						    FU_ETP_RPTID_TP_FEATURE,
						    FU_ETP_CMD_I2C_TP_SETTING,
						    ETP_I2C_IAP_MASTER_RESET,
						    error)) {
			g_prefix_error_literal(error, "cannot master reset IAP: ");
			return FALSE;
		}
	}
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET_MASTER);
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_TP_FEATURE,
					    FU_ETP_CMD_I2C_IAP_RESET,
					    ETP_I2C_ENABLE_REPORT,
					    error)) {
		g_prefix_error_literal(error, "cannot enable TP report: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_TP_FEATURE,
					    0x0306,
					    0x003,
					    error)) {
		g_prefix_error_literal(error, "cannot switch to TP PTP mode: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_write_cmd(FU_ELANTP_HID_DEVICE(proxy),
					    FU_ETP_RPTID_TP_FEATURE,
					    FU_ETP_CMD_I2C_TP_SETTING,
					    ETP_I2C_ENABLE_SCAN,
					    error)) {
		g_prefix_error_literal(error, "cannot enable TP scan: ");
		return FALSE;
	}
	if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, error)) {
		g_prefix_error_literal(error, "cannot ensure iap control: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "ElantpIcPageCount") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ic_page_count = (guint16)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "ElantpIapPassword") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->iap_password = (guint16)tmp;
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_elantp_hid_mcu_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_elantp_hid_mcu_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_elantp_hid_mcu_device_init(FuElantpHidMcuDevice *self)
{
	self->fw_page_size = 64;
	self->fw_section_size = 64;
	self->fw_no_of_sections = 1;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elantp.mcu");
	fu_device_set_name(FU_DEVICE(self), "HapticPad MCU");
	fu_device_set_logical_id(FU_DEVICE(self), "mcu");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ELANTP_FIRMWARE);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_ELANTP_HID_DEVICE);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_ELANTP_DEVICE_PRIVATE_FLAG_CAN_QUERY_HAPTIC_FUNCTION);
}

static void
fu_elantp_hid_mcu_device_class_init(FuElantpHidMcuDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_elantp_hid_mcu_device_to_string;
	device_class->attach = fu_elantp_hid_mcu_device_attach;
	device_class->set_quirk_kv = fu_elantp_hid_mcu_device_set_quirk_kv;
	device_class->setup = fu_elantp_hid_mcu_device_setup;
	device_class->reload = fu_elantp_hid_mcu_device_setup;
	device_class->write_firmware = fu_elantp_hid_mcu_device_write_firmware;
	device_class->check_firmware = fu_elantp_hid_mcu_device_check_firmware;
	device_class->set_progress = fu_elantp_hid_mcu_device_set_progress;
	device_class->convert_version = fu_elantp_hid_mcu_device_convert_version;
}

FuElantpHidMcuDevice *
fu_elantp_hid_mcu_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_ELANTP_HID_MCU_DEVICE, "proxy", proxy, NULL);
}

/*
 * Copyright 2026 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"
#include "fu-elantp-hid-mcu-device.h"
#include "fu-elantp-struct.h"

struct _FuElantpHidMcuDevice {
	FuUdevDevice parent_instance;
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

G_DEFINE_TYPE(FuElantpHidMcuDevice, fu_elantp_hid_mcu_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_elantp_hid_mcu_device_detach(FuElantpHidMcuDevice *self, FuProgress *progress, GError **error);

static void
fu_elantp_hid_mcu_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "ModuleId", self->module_id);
	fwupd_codec_string_append_hex(str, idt, "Pattern", self->pattern);
	fwupd_codec_string_append_hex(str, idt, "FwPageSize", self->fw_page_size);
	fwupd_codec_string_append_hex(str, idt, "FwSectionSize", self->fw_section_size);
	fwupd_codec_string_append_hex(str, idt, "FwNoOfSections", self->fw_no_of_sections);
	fwupd_codec_string_append_hex(str, idt, "IcPageCount", self->ic_page_count);
	fwupd_codec_string_append_hex(str, idt, "IapType", self->iap_type);
	fwupd_codec_string_append_hex(str, idt, "IapCtrl", self->iap_ctrl);
}

static gboolean
fu_elantp_hid_mcu_device_tp_send_cmd(FuElantpHidDevice *parent,
				     const guint8 *tx,
				     gsize txsz,
				     guint8 *rx,
				     gsize rxsz,
				     GError **error)
{
	g_autofree guint8 *buf = NULL;
	gsize bufsz = rxsz + 3;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(parent),
					  tx,
					  txsz,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	if (rxsz == 0)
		return TRUE;

	/* GetFeature */
	buf = g_malloc0(bufsz);
	buf[0] = tx[0]; /* report number */
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(parent),
					  buf,
					  bufsz,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* success */
	return fu_memcpy_safe(rx,
			      rxsz,
			      0x0, /* dst */
			      buf,
			      bufsz,
			      0x3, /* src */
			      rxsz,
			      error);
}

static gint
fu_elantp_hid_mcu_device_tp_write_cmd(FuElantpHidDevice *parent,
				      guint16 reg,
				      guint16 cmd,
				      GError **error)
{
	guint8 buf[5] = {FU_ETP_RPTID_TP_FEATURE};
	fu_memwrite_uint16(buf + 0x1, reg, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 0x3, cmd, G_LITTLE_ENDIAN);
	return fu_elantp_hid_mcu_device_tp_send_cmd(parent, buf, sizeof(buf), NULL, 0, error);
}

static gboolean
fu_elantp_hid_mcu_device_read_cmd(FuElantpHidDevice *parent,
				  guint16 reg,
				  guint8 *buf,
				  gsize bufz,
				  GError **error)
{
	guint8 tmp[5] = {FU_ETP_RPTID_MCU_FEATURE, 0x05, 0x03};
	fu_memwrite_uint16(tmp + 0x3, reg, G_LITTLE_ENDIAN);
	return fu_elantp_hid_mcu_device_tp_send_cmd(parent, tmp, sizeof(tmp), buf, bufz, error);
}

static gint
fu_elantp_hid_mcu_device_write_cmd(FuElantpHidDevice *parent,
				   guint16 reg,
				   guint16 cmd,
				   GError **error)
{
	guint8 buf[5] = {FU_ETP_RPTID_MCU_FEATURE};
	fu_memwrite_uint16(buf + 0x1, reg, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 0x3, cmd, G_LITTLE_ENDIAN);
	return fu_elantp_hid_mcu_device_tp_send_cmd(parent, buf, sizeof(buf), NULL, 0, error);
}

static gboolean
fu_elantp_hid_mcu_device_ensure_iap_ctrl(FuElantpHidMcuDevice *self,
					 FuElantpHidDevice *parent,
					 GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_IAP_CTRL,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read IAPControl: ");
		return FALSE;
	}
	self->iap_ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

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
fu_elantp_hid_mcu_device_read_force_table_enable(FuElantpHidDevice *parent, GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 value;

	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_FORCE_TYPE_ENABLE,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read force type cmd: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (value == 0xFFFF || value == FU_ETP_CMD_I2C_FORCE_TYPE_ENABLE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "forcetype cmd not supported");
		return FALSE;
	}
	if ((buf[0] & ETP_FW_FORCE_TYPE_ENABLE_BIT) == 0) {
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
fu_elantp_hid_mcu_device_get_forcetable_address(FuElantpHidMcuDevice *self,
						FuElantpHidDevice *parent,
						GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 addr_wrds;
	g_autoptr(FuFirmware) firmware = fu_elantp_firmware_new();

	if (self->iap_ver == 0x3) {
		if (self->module_id == 0x130 || self->module_id == 0x133) {
			self->force_table_addr = 0xFF40 * 2;
			return TRUE;
		} else {
			return TRUE;
		}
	}
	if (self->ic_type == 0x14 && self->iap_ver == 4)
		return TRUE;
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_FORCE_ADDR,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read force table address cmd: ");
		return FALSE;
	}
	addr_wrds = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (addr_wrds % 32 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "illegal force table address (%x)",
			    addr_wrds);
		return FALSE;
	}

	self->force_table_addr = addr_wrds * 2;

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_write_fw_password(FuElantpHidDevice *parent,
					   guint16 ic_type,
					   guint16 iap_ver,
					   GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 pw = ETP_I2C_IC13_IAPV5_PW;
	guint16 value;

	if (iap_ver >= 0x7 && ic_type == 0x13)
		pw = ETP_I2C_IC13_IAPV7_PW;
	else if (iap_ver >= 0x5 && ic_type == 0x13)
		pw = ETP_I2C_IC13_IAPV5_PW;
	else if ((iap_ver >= 0x4) && (ic_type == 0x14 || ic_type == 0x15))
		pw = ETP_I2C_IC13_IAPV5_PW;
	else
		return TRUE;

	if (!fu_elantp_hid_mcu_device_write_cmd(parent, FU_ETP_CMD_I2C_FW_PW, pw, error)) {
		g_prefix_error_literal(error, "failed to write fw password cmd: ");
		return FALSE;
	}

	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_FW_PW,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read fw password cmd: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (value != pw) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "can't set fw password got:%x",
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
	FuElantpHidDevice *parent;
	guint16 fwver;
	guint16 tmp;
	guint8 buf[2] = {0x0};
	g_autofree gchar *version_bl = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_forcetable = NULL;

	parent = FU_ELANTP_HID_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;

	/* get pattern */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_GET_HID_ID,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU HID ID: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	self->pattern = tmp != 0xFFFF ? (tmp & 0xFF00) >> 8 : 0;

	/* get current firmware version */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_FW_VERSION,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU fw version: ");
		return FALSE;
	}
	fwver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (fwver == 0xFFFF || fwver == FU_ETP_CMD_I2C_FW_VERSION)
		fwver = 0;
	fu_device_set_version_raw(device, fwver);

	/* get IAP firmware version */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       self->pattern == 0 ? FU_ETP_CMD_I2C_IAP_VERSION
								  : FU_ETP_CMD_I2C_IAP_VERSION_2,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1) {
		self->iap_ver = buf[1];
	} else {
		self->iap_ver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	}
	version_bl = fu_version_from_uint16(self->iap_ver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version_bootloader(device, version_bl);

	/* get module ID */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_GET_MODULE_ID,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU module ID: ");
		return FALSE;
	}
	self->module_id = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* get OSM version */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_OSM_VERSION,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU OSM version: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (tmp == FU_ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_mcu_device_read_cmd(parent,
						       FU_ETP_CMD_I2C_IAP_ICBODY,
						       buf,
						       sizeof(buf),
						       error)) {
			g_prefix_error_literal(error, "failed to read MCU IC body: ");
			return FALSE;
		}
		self->ic_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN) & 0xFF;
	} else {
		self->ic_type = (tmp >> 8) & 0xFF;
	}

	/* define the extra instance IDs (ic_type + module_id + driver) */
	fu_device_add_instance_u8(device, "ICTYPE", self->ic_type);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "ELANMCU",
					 "ICTYPE",
					 NULL);
	fu_device_build_instance_id(device, NULL, "ELANMCU", "ICTYPE", "MOD", NULL);
	fu_device_add_instance_str(device, "DRIVER", "HID");
	fu_device_build_instance_id(device, NULL, "ELANMCU", "ICTYPE", "MOD", "DRIVER", NULL);

	/* no quirk entry */
	if (self->ic_page_count == 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no page count for ELANMCU\\ICTYPE_%02X",
			    self->ic_type);
		return FALSE;
	}

	/* ic_page_count is based on 64 bytes/page */
	fu_device_set_firmware_size(device, (guint64)self->ic_page_count * (guint64)64);

	/* is in bootloader mode */
	if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, parent, error))
		return FALSE;

	if (self->ic_type != 0x12 && self->ic_type != 0x13 && self->ic_type != 0x14 &&
	    self->ic_type != 0x15)
		return TRUE;

	if (!fu_elantp_hid_mcu_device_read_force_table_enable(parent, &error_forcetable)) {
		g_debug("no MCU forcetable detected: %s", error_forcetable->message);
	} else {
		if (!fu_elantp_hid_mcu_device_get_forcetable_address(self, parent, error)) {
			g_prefix_error_literal(error, "get MCU forcetable address fail: ");
			return FALSE;
		}
		self->force_table_support = TRUE;
		/* is in bootloader mode */
		if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, parent, error))
			return FALSE;
	}

	/* fix an unsuitable i²c name, e.g. `VEN 04F3:00 04F3:3XXX` or `0672:00 04F3:3187` */
	if (g_strstr_len(fu_device_get_name(device), -1, ":00 ") != NULL)
		fu_device_set_name(device, "MCU");

	/* success */
	return TRUE;
}

static FuFirmware *
fu_elantp_hid_mcu_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FuFirmwareParseFlags flags,
					  GError **error)
{
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	guint16 module_id;
	guint16 ic_type;
	gboolean force_table_support;
	g_autoptr(FuFirmware) firmware = fu_elantp_firmware_new();

	/* check is compatible with hardware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	module_id = fu_elantp_firmware_get_module_id(FU_ELANTP_FIRMWARE(firmware));
	if (self->module_id != module_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "mcu firmware incompatible, got 0x%04x, expected 0x%04x",
			    module_id,
			    self->module_id);
		return NULL;
	}
	ic_type = fu_elantp_firmware_get_ic_type(FU_ELANTP_FIRMWARE(firmware));
	if (self->ic_type != ic_type) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "mcu firmware ic type incompatible, got 0x%04x, expected 0x%04x",
			    ic_type,
			    self->ic_type);
		return NULL;
	}
	force_table_support =
	    fu_elantp_firmware_get_forcetable_support(FU_ELANTP_FIRMWARE(firmware));
	if (self->ic_type == 0x14 && self->iap_ver == 4)
		self->force_table_support = force_table_support;
	if (self->force_table_support != force_table_support) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "mcu firmware incompatible, forcetable incorrect.");
		return NULL;
	}
	if (self->force_table_support) {
		guint32 force_table_addr;
		guint32 diff_size;
		force_table_addr =
		    fu_elantp_firmware_get_forcetable_addr(FU_ELANTP_FIRMWARE(firmware));
		if (self->ic_type == 0x14 && self->iap_ver == 4)
			self->force_table_addr = force_table_addr;
		if (self->force_table_addr < force_table_addr) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "mcu firmware forcetable address incompatible, got 0x%04x, "
				    "expected 0x%04x",
				    force_table_addr / 2,
				    self->force_table_addr / 2);
			return NULL;
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
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_elantp_hid_mcu_device_write_chunks(FuElantpHidMcuDevice *self,
				      FuElantpHidDevice *parent,
				      FuDevice *device,
				      GPtrArray *chunks,
				      guint16 *checksum,
				      FuProgress *progress,
				      GError **error)
{
	guint total_pages = chunks->len;
	guint16 fw_section_cnt = 0;

	for (guint i = 0; i < total_pages; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint16 csum_tmp =
		    fu_sum16w(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
		gsize blksz = self->fw_section_size + 3;
		g_autofree guint8 *blk = g_malloc0(blksz);

		/* write block */
		blk[0] = FU_ETP_RPTID_MCU_IAP; /* report ID */
		if (!fu_memcpy_safe(blk,
				    blksz,
				    0x1, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		fu_memwrite_uint16(blk + fu_chunk_get_data_sz(chk) + 1, csum_tmp, G_LITTLE_ENDIAN);
		if (!fu_elantp_hid_mcu_device_tp_send_cmd(parent, blk, blksz, NULL, 0, error))
			return FALSE;
		fw_section_cnt++;
		if (self->fw_section_size == self->fw_page_size ||
		    fw_section_cnt == self->fw_no_of_sections) {
			fu_device_sleep(device,
					self->fw_page_size == 512 ? ELANTP_DELAY_WRITE_BLOCK_512
								  : ELANTP_DELAY_WRITE_BLOCK);

			if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, parent, error))
				return FALSE;
			fw_section_cnt = 0;
			if (self->iap_ctrl & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_WRITE,
					    "mcu bootloader reports failed write: 0x%x",
					    self->iap_ctrl);
				return FALSE;
			}
			if (self->iap_ctrl & ETP_FW_IAP_END_WAITWDT)
				i = total_pages;
		}

		/* update progress */
		*checksum += csum_tmp;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)chunks->len);
	}
	return TRUE;
}
static gboolean
fu_elantp_hid_mcu_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuElantpHidDevice *parent;
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);
	FuElantpFirmware *firmware_elantp = FU_ELANTP_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint16 checksum = 0;
	guint16 checksum_device = 0;
	guint16 iap_addr;
	const guint8 *buf;
	guint8 csum_buf[2] = {0x0};
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 30, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "reset");

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* detach */
	if (!fu_elantp_hid_mcu_device_detach(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	parent = FU_ELANTP_HID_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;

	/* write each block */
	buf = g_bytes_get_data(fw, &bufsz);
	iap_addr = fu_elantp_firmware_get_iap_addr(firmware_elantp);
	chunks =
	    fu_chunk_array_new(buf + iap_addr, bufsz - iap_addr, 0x0, 0x0, self->fw_section_size);

	if (!fu_elantp_hid_mcu_device_write_chunks(self,
						   parent,
						   device,
						   chunks,
						   &checksum,
						   progress,
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify the written checksum */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_IAP_CHECKSUM,
					       csum_buf,
					       sizeof(csum_buf),
					       error))
		return FALSE;
	if (!fu_memread_uint16_safe(csum_buf,
				    sizeof(csum_buf),
				    0x0,
				    &checksum_device,
				    G_LITTLE_ENDIAN,
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
fu_elantp_hid_mcu_device_read_iap_type(FuElantpHidDevice *parent, guint16 *iap_type, GError **error)
{
	guint8 buf[2] = {0x0};

	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_IAP_TYPE,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU IAP type: ");
		return FALSE;
	}

	*iap_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_elantp_hid_mcu_device_detach(FuElantpHidMcuDevice *self, FuProgress *progress, GError **error)
{
	FuElantpHidDevice *parent;
	guint16 iap_ver;
	guint16 ic_type;
	guint8 buf[2] = {0x0};
	guint16 tmp;

	parent = FU_ELANTP_HID_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;

	if (!fu_elantp_hid_mcu_device_tp_write_cmd(parent,
						   FU_ETP_CMD_I2C_TP_SETTING,
						   ETP_I2C_DISABLE_SCAN,
						   error)) {
		g_prefix_error_literal(error, "cannot disable TP scan: ");
		return FALSE;
	}

	if (!fu_elantp_hid_mcu_device_tp_write_cmd(parent,
						   FU_ETP_CMD_I2C_IAP_RESET,
						   ETP_I2C_DISABLE_REPORT,
						   error)) {
		g_prefix_error_literal(error, "cannot disable TP report: ");
		return FALSE;
	}

	if (!fu_elantp_hid_mcu_device_write_cmd(parent,
						FU_ETP_CMD_I2C_IAP_RESET,
						ETP_I2C_DISABLE_REPORT,
						error)) {
		g_prefix_error_literal(error, "cannot disable MCU report: ");
		return FALSE;
	}

	/* sanity check */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_info("in bootloader mode, reset MCU IC");
		if (!fu_elantp_hid_mcu_device_write_cmd(parent,
							FU_ETP_CMD_I2C_IAP_RESET,
							ETP_I2C_IAP_RESET,
							error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);
	}

	/* get OSM version */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       FU_ETP_CMD_I2C_OSM_VERSION,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU OSM version: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (tmp == FU_ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_mcu_device_read_cmd(parent,
						       FU_ETP_CMD_I2C_IAP_ICBODY,
						       buf,
						       sizeof(buf),
						       error)) {
			g_prefix_error_literal(error, "failed to read MCU IC body: ");
			return FALSE;
		}
		ic_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN) & 0xFF;
	} else {
		ic_type = (tmp >> 8) & 0xFF;
	}

	/* get IAP firmware version */
	if (!fu_elantp_hid_mcu_device_read_cmd(parent,
					       self->pattern == 0 ? FU_ETP_CMD_I2C_IAP_VERSION
								  : FU_ETP_CMD_I2C_IAP_VERSION_2,
					       buf,
					       sizeof(buf),
					       error)) {
		g_prefix_error_literal(error, "failed to read MCU bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1) {
		iap_ver = buf[1];
	} else {
		iap_ver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	}

	/* set the page size */
	self->fw_page_size = 64;
	self->fw_no_of_sections = 1;
	if (ic_type >= 0x10) {
		if (iap_ver >= 1) {
			/* set the IAP type, presumably some kind of ABI */
			if (iap_ver >= 2 && (ic_type == 0x14 || ic_type == 0x15)) {
				self->fw_page_size = 512;
				if (iap_ver >= 3) {
					if (!fu_elantp_hid_mcu_device_read_iap_type(parent,
										    &self->iap_type,
										    error))
						return FALSE;
					self->fw_section_size = self->iap_type * 2;
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
				if (!fu_elantp_hid_mcu_device_write_cmd(parent,
									FU_ETP_CMD_I2C_IAP_TYPE,
									self->fw_page_size / 2,
									error))
					return FALSE;
				if (!fu_elantp_hid_mcu_device_read_cmd(parent,
								       FU_ETP_CMD_I2C_IAP_TYPE,
								       buf,
								       sizeof(buf),
								       error)) {
					g_prefix_error_literal(error,
							       "failed to read MCU IAP type: ");
					return FALSE;
				}
				self->iap_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
				if (self->iap_type != self->fw_page_size / 2) {
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_NOT_SUPPORTED,
							    "failed to set MCU IAP type");
					return FALSE;
				}
			}
		}
	}
	if (!fu_elantp_hid_mcu_device_write_fw_password(parent, ic_type, iap_ver, error))
		return FALSE;
	if (!fu_elantp_hid_mcu_device_write_cmd(parent,
						FU_ETP_CMD_I2C_IAP,
						self->iap_password,
						error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_UNLOCK);
	if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, parent, error))
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
	FuElantpHidDevice *parent;
	FuElantpHidMcuDevice *self = FU_ELANTP_HID_MCU_DEVICE(device);

	/* reset back to runtime */
	parent = FU_ELANTP_HID_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in MCU runtime mode, skipping");
		return TRUE;
	}

	/* reset back to runtime */
	if (!fu_elantp_hid_mcu_device_write_cmd(parent,
						FU_ETP_CMD_I2C_IAP_RESET,
						ETP_I2C_IAP_RESET,
						error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);
	if (!fu_elantp_hid_mcu_device_write_cmd(parent,
						FU_ETP_CMD_I2C_IAP_RESET,
						ETP_I2C_ENABLE_REPORT,
						error)) {
		g_prefix_error_literal(error, "cannot enable MCU report: ");
		return FALSE;
	}
	if (!fu_elantp_hid_mcu_device_write_cmd(parent, 0x0306, 0x003, error)) {
		g_prefix_error_literal(error, "cannot switch to MCU PTP mode: ");
		return FALSE;
	}
	if (!fu_elantp_hid_mcu_device_tp_write_cmd(parent,
						   FU_ETP_CMD_I2C_IAP_RESET,
						   ETP_I2C_IAP_RESET,
						   error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);
	if (!fu_elantp_hid_mcu_device_tp_write_cmd(parent,
						   FU_ETP_CMD_I2C_IAP_RESET,
						   ETP_I2C_ENABLE_REPORT,
						   error)) {
		g_prefix_error_literal(error, "cannot enable TP report: ");
		return FALSE;
	}
	if (!fu_elantp_hid_mcu_device_tp_write_cmd(parent, 0x0306, 0x003, error)) {
		g_prefix_error_literal(error, "cannot switch to TP PTP mode: ");
		return FALSE;
	}
	if (!fu_elantp_hid_mcu_device_tp_write_cmd(parent,
						   FU_ETP_CMD_I2C_TP_SETTING,
						   ETP_I2C_ENABLE_SCAN,
						   error)) {
		g_prefix_error_literal(error, "cannot enable TP scan: ");
		return FALSE;
	}
	if (!fu_elantp_hid_mcu_device_ensure_iap_ctrl(self, parent, error))
		return FALSE;

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
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elantp.mcu");
	fu_device_set_name(FU_DEVICE(self), "HapticPad MCU");
	fu_device_set_logical_id(FU_DEVICE(self), "mcu");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
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
	device_class->prepare_firmware = fu_elantp_hid_mcu_device_prepare_firmware;
	device_class->set_progress = fu_elantp_hid_mcu_device_set_progress;
	device_class->convert_version = fu_elantp_hid_mcu_device_convert_version;
}

FuElantpHidMcuDevice *
fu_elantp_hid_mcu_device_new(void)
{
	FuElantpHidMcuDevice *self;
	self = g_object_new(FU_TYPE_ELANTP_HID_MCU_DEVICE, NULL);
	return FU_ELANTP_HID_MCU_DEVICE(self);
}

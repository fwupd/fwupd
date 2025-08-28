/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-block.h"
#include "fu-ilitek-its-common.h"
#include "fu-ilitek-its-device.h"
#include "fu-ilitek-its-firmware.h"
#include "fu-ilitek-its-struct.h"

#define FU_ILITEK_ITS_HID_ACK_BYTE	  0xAC

#define FU_ILITEK_ITS_LOOKUP_TYPE_EDID	    0x1
#define FU_ILITEK_ITS_LOOKUP_TYPE_SENSOR_ID 0x2

#define FU_ILITEK_ITS_CRC_RECALCULATE 0x0
#define FU_ILITEK_ITS_CRC_GET	      0x1

#define FU_ILITEK_ITS_WRITE_ENABLE_KEY	 0x5AA5
#define FU_ILITEK_ITS_WRITE_ENABLE_START 0x5000
#define FU_ILITEK_ITS_WRITE_ENABLE_END	 0x5001

#define FU_ILITEK_ITS_AP_MODE 0x5A /* device in Application mode */
#define FU_ILITEK_ITS_BL_MODE 0x55 /* device in Bootloader mode */

struct _FuIlitekItsDevice {
	FuHidrawDevice parent_instance;
	gchar *ic_name;
	guint32 protocol_ver;
	guint8 sensor_id_mask;
};

G_DEFINE_TYPE(FuIlitekItsDevice, fu_ilitek_its_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_ilitek_its_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	fwupd_codec_string_append(str, idt, "IcName", self->ic_name);
	fwupd_codec_string_append_hex(str, idt, "ProtocolVer", self->protocol_ver);
	fwupd_codec_string_append_hex(str, idt, "SensorIdMask", self->sensor_id_mask);
}

typedef struct {
	FuIlitekItsCmd cmd;
	gboolean is_ack;
	GByteArray *rbuf;
} FuIlitekItsHidCmdHelper;

static gboolean
fu_ilitek_its_device_read_cb(FuDevice *device, gpointer data, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	FuIlitekItsHidCmdHelper *helper = (FuIlitekItsHidCmdHelper *)data;
	const guint8 *buf_data;
	gsize bufsz_data = 0;
	gsize bufsz = FU_STRUCT_ILITEK_ITS_HID_RES_SIZE;
	g_autofree guint8 *buf = g_new0(guint8, bufsz);
	g_autoptr(FuStructIlitekItsHidRes) st_res = NULL;

	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 buf,
				 bufsz,
				 NULL,
				 200,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error))
		return FALSE;
	st_res = fu_struct_ilitek_its_hid_res_parse(buf, bufsz, 0, error);
	if (st_res == NULL)
		return FALSE;
	if (fu_struct_ilitek_its_hid_res_get_cmd(st_res) != helper->cmd) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid hid response header");
		return FALSE;
	}

	buf_data = fu_struct_ilitek_its_hid_res_get_data(st_res, &bufsz_data);
	if (helper->is_ack && (bufsz_data == 0 || buf_data[0] != FU_ILITEK_ITS_HID_ACK_BYTE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "invalid ack response");
		return FALSE;
	}

	fu_dump_raw(G_LOG_DOMAIN, "HidReadReport", st_res->data, st_res->len);

	if (helper->rbuf != NULL)
		g_byte_array_append(helper->rbuf, buf_data, bufsz_data);

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_send_cmd(FuIlitekItsDevice *self,
			      FuStructIlitekItsHidCmd *st_cmd,
			      GByteArray *rbuf,
			      GError **error)
{
	FuIlitekItsHidCmdHelper helper = {
	    .cmd = fu_struct_ilitek_its_hid_cmd_get_cmd(st_cmd),
	    .rbuf = rbuf,
	    .is_ack = FALSE,
	};

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_cmd->data,
					  st_cmd->len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error_literal(error, "failed to send HID cmd: ");
		return FALSE;
	}
	if (rbuf != NULL) {
		fu_device_sleep(FU_DEVICE(self), 100);
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_ilitek_its_device_read_cb,
					  50,
					  100,
					  &helper,
					  error)) {
			g_prefix_error_literal(error, "failed to recv HID packet: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_send_cmd_then_wake_ack(FuIlitekItsDevice *self,
					    FuStructIlitekItsHidCmd *st_cmd,
					    GError **error)
{
	FuIlitekItsHidCmdHelper helper = {
	    .cmd = fu_struct_ilitek_its_hid_cmd_get_cmd(st_cmd),
	    .rbuf = NULL,
	    .is_ack = TRUE,
	};

	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, NULL, error))
		return FALSE;

	return fu_device_retry_full(FU_DEVICE(self),
				    fu_ilitek_its_device_read_cb,
				    50,
				    100,
				    &helper,
				    error);
}

static gboolean
fu_ilitek_its_device_send_long_cmd_then_wake_ack(FuIlitekItsDevice *self,
						 FuStructIlitekItsLongHidCmd *st_cmd,
						 GError **error)
{
	FuIlitekItsHidCmdHelper helper = {
	    .cmd = fu_struct_ilitek_its_long_hid_cmd_get_cmd(st_cmd),
	    .rbuf = NULL,
	    .is_ack = TRUE,
	};

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  st_cmd->data,
					  st_cmd->len,
					  FU_IOCTL_FLAG_RETRY,
					  error)) {
		g_prefix_error_literal(error, "failed to send long HID cmd: ");
		return FALSE;
	}

	return fu_device_retry_full(FU_DEVICE(self),
				    fu_ilitek_its_device_read_cb,
				    50,
				    100,
				    &helper,
				    error);
}

static gboolean
fu_ilitek_its_device_recalculate_crc(FuIlitekItsDevice *self,
				     guint32 start,
				     guint32 end,
				     GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 8);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_BLOCK_CRC);
	st_cmd->data[FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA] = FU_ILITEK_ITS_CRC_RECALCULATE;
	fu_memwrite_uint24(st_cmd->data + FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA + 1,
			   start,
			   G_LITTLE_ENDIAN);
	fu_memwrite_uint24(st_cmd->data + FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA + 4,
			   end,
			   G_LITTLE_ENDIAN);

	return fu_ilitek_its_device_send_cmd_then_wake_ack(self, st_cmd, error);
}

static gboolean
fu_ilitek_its_device_get_block_crc(FuIlitekItsDevice *self, guint16 *crc, GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 2);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 2);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_BLOCK_CRC);
	st_cmd->data[FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA] = FU_ILITEK_ITS_CRC_GET;

	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;

	return fu_memread_uint16_safe(rbuf->data, rbuf->len, 0, crc, G_LITTLE_ENDIAN, error);
}

static gboolean
fu_ilitek_its_device_flash_enable(FuIlitekItsDevice *self,
				  gboolean in_ap,
				  guint32 start,
				  guint32 end,
				  GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, in_ap ? 3 : 9);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_FLASH_ENABLE);
	fu_memwrite_uint16(st_cmd->data + FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA + 0,
			   FU_ILITEK_ITS_WRITE_ENABLE_KEY,
			   G_BIG_ENDIAN);
	if (!in_ap) {
		fu_memwrite_uint24(st_cmd->data + FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA + 2,
				   start,
				   G_LITTLE_ENDIAN);
		fu_memwrite_uint24(st_cmd->data + FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA + 5,
				   end,
				   G_LITTLE_ENDIAN);
	}

	return fu_ilitek_its_device_send_cmd(self, st_cmd, NULL, error);
}

static gboolean
fu_ilitek_its_device_set_ctrl_mode(FuIlitekItsDevice *self,
				   FuIlitekItsCtrlMode mode,
				   GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 3);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_SET_CTRL_MODE);
	st_cmd->data[FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA] = mode;
	st_cmd->data[FU_STRUCT_ILITEK_ITS_HID_CMD_OFFSET_DATA + 1] = 0x0;

	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, NULL, error))
		return FALSE;

	/* success */
	fu_device_sleep(FU_DEVICE(self), 100);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_enable_tde(FuIlitekItsDevice *self, GError **error)
{
	return fu_ilitek_its_device_set_ctrl_mode(self, FU_ILITEK_ITS_CTRL_MODE_SUSPEND, error);
}

static gboolean
fu_ilitek_its_device_disable_tde(FuIlitekItsDevice *self, GError **error)
{
	return fu_ilitek_its_device_set_ctrl_mode(self, FU_ILITEK_ITS_CTRL_MODE_NORMAL, error);
}

static gboolean
fu_ilitek_its_device_get_fwid(FuIlitekItsDevice *self, guint16 *fwid, GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	/* check fwid protocol is supported */
	if ((fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	     self->protocol_ver < 0x010802) ||
	    (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	     self->protocol_ver < 0x060007)) {
		*fwid = 0xFFFF;
		return TRUE;
	}

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 4);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_FIRMWARE_ID);
	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;

	/* success */
	*fwid = fu_struct_ilitek_its_fwid_get_fwid(rbuf);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_get_sensor_id(FuIlitekItsDevice *self, guint8 *sensor_id, GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	/* check sensor-id protocol is supported */
	if ((fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	     self->protocol_ver < 0x010803) ||
	    (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	     self->protocol_ver < 0x060004)) {
		*sensor_id = 0xFF;
		return TRUE;
	}

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 4);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_SENSOR_ID);
	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;

	/* success */
	*sensor_id = fu_struct_ilitek_its_sensor_id_get_sensor_id(rbuf);
	return TRUE;
}

static gboolean
fu_ilitek_its_device_ensure_protocol_version(FuIlitekItsDevice *self, GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 3);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_PROTOCOL_VERSION);

	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;
	return fu_memread_uint24_safe(rbuf->data,
				      rbuf->len,
				      0,
				      &self->protocol_ver,
				      G_BIG_ENDIAN,
				      error);
}

static gboolean
fu_ilitek_its_device_ensure_fw_version(FuIlitekItsDevice *self, GError **error)
{
	guint64 version;
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 8);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_FIRMWARE_VERSION);

	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;

	if (!fu_memread_uint64_safe(rbuf->data, rbuf->len, 0, &version, G_BIG_ENDIAN, error))
		return FALSE;

	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_version_bootloader_raw(FU_DEVICE(self), version);

		/* fw update forcely */
		fu_device_set_version_raw(FU_DEVICE(self), 0);
	} else {
		fu_device_set_version_raw(FU_DEVICE(self), version);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_ensure_ic_mode(FuIlitekItsDevice *self, GError **error)
{
	guint8 ic_mode;
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 2);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_IC_MODE);
	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;

	if (!fu_memread_uint8_safe(rbuf->data, rbuf->len, 0, &ic_mode, error))
		return FALSE;
	if (ic_mode == FU_ILITEK_ITS_BL_MODE)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_ensure_ic_name_old(FuIlitekItsDevice *self, GError **error)
{
	g_autofree gchar *name = NULL;
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 32);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_MCU_VERSION);
	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;

	self->ic_name = g_strdup_printf("%04x", fu_struct_ilitek_its_mcu_version_get_ic_name(rbuf));
	name = g_strdup_printf("Touchscreen ILI%s", self->ic_name);
	fu_device_set_name(FU_DEVICE(self), name);

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_ensure_ic_name(FuIlitekItsDevice *self, GError **error)
{
	g_autofree gchar *name = NULL;
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();
	g_autoptr(GByteArray) rbuf = g_byte_array_new();

	/* check new protocol is supported */
	if ((fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	     self->protocol_ver < 0x010803) ||
	    (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	     self->protocol_ver < 0x060009))
		return fu_ilitek_its_device_ensure_ic_name_old(self, error);

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_read_len(st_cmd, 32);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_GET_MCU_INFO);
	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, rbuf, error))
		return FALSE;

	self->ic_name = fu_struct_ilitek_its_mcu_info_get_ic_name(rbuf);
	name = g_strdup_printf("Touchscreen ILI%s", self->ic_name);
	fu_device_set_name(FU_DEVICE(self), name);

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_io_channel_write(const gchar *fn, const gchar *buf, GError **error)
{
	g_autoptr(FuIOChannel) io = NULL;

	io = fu_io_channel_new_file(fn, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io == NULL)
		return FALSE;
	return fu_io_channel_write_raw(io,
				       (const guint8 *)buf,
				       strlen(buf),
				       1000,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

static FuDevice *
fu_ilitek_its_device_get_backend_parent(FuDevice *device, GError **error)
{
	switch (fu_hidraw_device_get_bus_type(FU_HIDRAW_DEVICE(device))) {
	case FU_HIDRAW_BUS_TYPE_I2C:
		return fu_device_get_backend_parent_with_subsystem(device, "i2c", error);
	case FU_HIDRAW_BUS_TYPE_PCI:
		return fu_device_get_backend_parent_with_subsystem(device, "pci", error);
	case FU_HIDRAW_BUS_TYPE_USB:
		return fu_device_get_backend_parent_with_subsystem(device, "usb", error);
	default:
		break;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "unexpected bus type: 0x%x",
		    fu_hidraw_device_get_bus_type(FU_HIDRAW_DEVICE(device)));
	return NULL;
}

static gboolean
fu_ilitek_its_device_rebind_driver(FuDevice *device, GError **error)
{
	const gchar *hid_id;
	const gchar *driver;
	const gchar *subsystem;
	g_autofree gchar *fn_bind = NULL;
	g_autofree gchar *fn_unbind = NULL;
	g_auto(GStrv) hid_strs = NULL;
	g_autoptr(FuUdevDevice) parent = NULL;

	/* skip */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	parent = FU_UDEV_DEVICE(fu_ilitek_its_device_get_backend_parent(device, error));
	if (parent == NULL)
		return FALSE;

	/* find the physical ID to use for the rebind */
	hid_strs = g_strsplit(fu_udev_device_get_sysfs_path(parent), "/", -1);
	hid_id = hid_strs[g_strv_length(hid_strs) - 1];
	if (hid_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no HID_PHYS in %s",
			    fu_udev_device_get_sysfs_path(parent));
		return FALSE;
	}

	driver = fu_udev_device_get_driver(parent);
	subsystem = fu_udev_device_get_subsystem(parent);
	fn_bind = g_build_filename("/sys/bus/", subsystem, "drivers", driver, "bind", NULL);
	fn_unbind = g_build_filename("/sys/bus/", subsystem, "drivers", driver, "unbind", NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	if (!fu_ilitek_its_device_io_channel_write(fn_unbind, hid_id, error))
		return FALSE;
	if (!fu_ilitek_its_device_io_channel_write(fn_bind, hid_id, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_switch_mode(FuIlitekItsDevice *self, gboolean to_bootloader, GError **error)
{
	g_autoptr(FuStructIlitekItsHidCmd) st_cmd = fu_struct_ilitek_its_hid_cmd_new();

	if (to_bootloader && fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	if (!to_bootloader && !fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_ilitek_its_device_flash_enable(self,
					       to_bootloader,
					       FU_ILITEK_ITS_WRITE_ENABLE_START,
					       FU_ILITEK_ITS_WRITE_ENABLE_END,
					       error))
		return FALSE;

	fu_struct_ilitek_its_hid_cmd_set_write_len(st_cmd, 1);
	fu_struct_ilitek_its_hid_cmd_set_cmd(st_cmd,
					     to_bootloader ? FU_ILITEK_ITS_CMD_SET_BL_MODE
							   : FU_ILITEK_ITS_CMD_SET_AP_MODE);
	if (!fu_ilitek_its_device_send_cmd(self, st_cmd, NULL, error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 1000);

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_switch_mode_cb(FuDevice *device, gpointer data, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	gboolean to_bootloader = *(gboolean *)data;
	if (!fu_ilitek_its_device_switch_mode(self, to_bootloader, error))
		return FALSE;

	if (!fu_ilitek_its_device_ensure_ic_mode(self, error))
		return FALSE;

	/* check it changed state */
	if ((to_bootloader &&
	     !fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) ||
	    (!to_bootloader &&
	     fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "switch mode failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_ilitek_its_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	gboolean to_bootloader = TRUE;

	/* go to suspend mode before switch to bootloader mode */
	if (!fu_ilitek_its_device_enable_tde(self, error))
		return FALSE;

	switch (fu_hidraw_device_get_bus_type(FU_HIDRAW_DEVICE(self))) {
	case FU_HIDRAW_BUS_TYPE_I2C:
	case FU_HIDRAW_BUS_TYPE_PCI:
		if (!fu_device_retry_full(device,
					  fu_ilitek_its_device_switch_mode_cb,
					  5,
					  100,
					  &to_bootloader,
					  error)) {
			g_prefix_error_literal(error, "failed to switch mode: ");
			return FALSE;
		}
		break;

	case FU_HIDRAW_BUS_TYPE_USB:
		if (!fu_ilitek_its_device_switch_mode(self, to_bootloader, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "unexpected bus type: 0x%x",
			    fu_hidraw_device_get_bus_type(FU_HIDRAW_DEVICE(self)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	gboolean to_bootloader = FALSE;

	switch (fu_hidraw_device_get_bus_type(FU_HIDRAW_DEVICE(self))) {
	case FU_HIDRAW_BUS_TYPE_I2C:
	case FU_HIDRAW_BUS_TYPE_PCI:
		if (!fu_device_retry_full(device,
					  fu_ilitek_its_device_switch_mode_cb,
					  5,
					  100,
					  &to_bootloader,
					  error)) {
			g_prefix_error_literal(error, "failed to switch mode: ");
			return FALSE;
		}

		/* rebind driver to update report descriptor */
		if (!fu_ilitek_its_device_rebind_driver(device, error))
			return FALSE;

		break;

	case FU_HIDRAW_BUS_TYPE_USB:
		if (!fu_ilitek_its_device_switch_mode(self, to_bootloader, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "unexpected bus type: 0x%x",
			    fu_hidraw_device_get_bus_type(FU_HIDRAW_DEVICE(self)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_probe(FuDevice *device, GError **error)
{
	/* ignore unsupported subsystems */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem: %s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_ilitek_its_device_register_drm_device(FuIlitekItsDevice *self,
					 FuDrmDevice *drm_device,
					 GError **error)
{
	FuEdid *edid = fu_drm_device_get_edid(drm_device);

	if (edid == NULL)
		return TRUE;
	if (fu_edid_get_pnp_id(edid) == NULL)
		return TRUE;

	fu_device_add_instance_str(FU_DEVICE(self), "PNPID", fu_edid_get_pnp_id(edid));
	fu_device_add_instance_u16(FU_DEVICE(self), "PCODE", fu_edid_get_product_code(edid));
	if (!fu_device_build_instance_id(FU_DEVICE(self),
					 error,
					 "HIDRAW",
					 "VEN",
					 "DEV",
					 "PNPID",
					 NULL))
		return FALSE;

	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "HIDRAW",
					   "VEN",
					   "DEV",
					   "PNPID",
					   "PCODE",
					   NULL);
}

static gboolean
fu_ilitek_its_device_setup(FuDevice *device, GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	guint16 fwid;
	guint8 sensor_id;
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new_full(FU_DEVICE(self),
					   (FuDeviceLockerFunc)fu_ilitek_its_device_enable_tde,
					   (FuDeviceLockerFunc)fu_ilitek_its_device_disable_tde,
					   error);
	if (locker == NULL)
		return FALSE;

	if (!fu_ilitek_its_device_ensure_ic_mode(self, error))
		return FALSE;
	if (!fu_ilitek_its_device_ensure_protocol_version(self, error))
		return FALSE;

	if (!fu_ilitek_its_device_ensure_ic_name(self, error))
		return FALSE;
	if (!fu_ilitek_its_device_ensure_fw_version(self, error))
		return FALSE;

	if (!fu_ilitek_its_device_get_fwid(self, &fwid, error))
		return FALSE;
	if (!fu_ilitek_its_device_get_sensor_id(self, &sensor_id, error))
		return FALSE;

	fu_device_add_instance_u16(device, "FWID", fwid);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "FWID", NULL))
		return FALSE;

	fu_device_add_instance_u8(device, "SENSORID", sensor_id & self->sensor_id_mask);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "SENSORID", NULL))
		return FALSE;

	/* some SKU needs both EDID and sensor-id */
	fu_device_build_instance_id(device,
				    NULL,
				    "HIDRAW",
				    "VEN",
				    "DEV",
				    "SENSORID",
				    "PNPID",
				    "PCODE",
				    NULL);

	/* FuHidrawDevice->setup */
	return FU_DEVICE_CLASS(fu_ilitek_its_device_parent_class)->setup(device, error);
}

static FuFirmware *
fu_ilitek_its_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	const gchar *fw_ic_name;
	g_autoptr(FuFirmware) firmware = fu_ilitek_its_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	fw_ic_name = fu_ilitek_its_firmware_get_ic_name(FU_ILITEK_ITS_FIRMWARE(firmware));
	if (g_strcmp0(self->ic_name, fw_ic_name) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware ic name %s does not match device ic name %s",
			    fw_ic_name,
			    fu_device_get_name(device));
		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_ilitek_its_device_write_block(FuIlitekItsDevice *self,
				 FuFirmware *block_img,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	guint16 crc;
	guint16 fw_crc;
	guint32 end;
	guint32 idx = fu_firmware_get_idx(block_img);
	guint32 start;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) blob = NULL;

	start = fu_firmware_get_addr(block_img);
	end = start + fu_firmware_get_size(block_img) - 1;

	fw_crc = fu_ilitek_its_block_get_crc(FU_ILITEK_ITS_BLOCK(block_img));
	if (!fu_ilitek_its_device_recalculate_crc(self, start, end, error))
		return FALSE;
	if (!fu_ilitek_its_device_get_block_crc(self, &crc, error))
		return FALSE;

	g_debug("block[%u]: start/end addr: 0x%x/0x%x, ic/file crc: 0x%x/0x%x, need update: %s",
		idx,
		start,
		end,
		crc,
		fw_crc,
		crc == fw_crc ? "no" : "yes");

	/* no need to upgrade block if crc matched */
	if (crc == fw_crc && (flags & FWUPD_INSTALL_FLAG_FORCE) == 0)
		return TRUE;

	blob = fu_firmware_get_bytes(block_img, error);
	if (blob == NULL)
		return FALSE;
	chunks =
	    fu_chunk_array_new_from_bytes(blob, 0, 0, FU_STRUCT_ILITEK_ITS_LONG_HID_CMD_SIZE_DATA);
	if (chunks == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	if (!fu_ilitek_its_device_flash_enable(self, FALSE, start, end, error))
		return FALSE;

	for (guint32 i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chunk = NULL;
		g_autoptr(FuStructIlitekItsLongHidCmd) st_cmd =
		    fu_struct_ilitek_its_long_hid_cmd_new();
		g_autoptr(GBytes) data = NULL;

		chunk = fu_chunk_array_index(chunks, i, error);
		if (chunk == NULL)
			return FALSE;

		fu_struct_ilitek_its_long_hid_cmd_set_write_len(
		    st_cmd,
		    FU_STRUCT_ILITEK_ITS_LONG_HID_CMD_SIZE_DATA + 1);
		fu_struct_ilitek_its_long_hid_cmd_set_cmd(st_cmd, FU_ILITEK_ITS_CMD_WRITE_DATA);

		data = fu_bytes_pad(fu_chunk_get_bytes(chunk),
				    FU_STRUCT_ILITEK_ITS_LONG_HID_CMD_SIZE_DATA,
				    0xff);

		if (!fu_struct_ilitek_its_long_hid_cmd_set_data(st_cmd,
								g_bytes_get_data(data, NULL),
								g_bytes_get_size(data),
								error))
			return FALSE;

		if (!fu_ilitek_its_device_send_long_cmd_then_wake_ack(self, st_cmd, error))
			return FALSE;

		fu_progress_step_done(progress);
	}

	if (!fu_ilitek_its_device_get_block_crc(self, &crc, error))
		return FALSE;

	g_debug("block[%u]: start/end addr: 0x%x/0x%x, ic/file crc: 0x%x/0x%x %s",
		idx,
		start,
		end,
		crc,
		fw_crc,
		crc == fw_crc ? "matched" : "not matched");

	if (crc != fw_crc) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "block crc mismatch: device 0x%04x, firmware 0x%04x",
			    crc,
			    fw_crc);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, imgs->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_ilitek_its_device_write_block(self,
						      img,
						      fu_progress_get_child(progress),
						      flags,
						      error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success! */
	return TRUE;
}

static gboolean
fu_ilitek_its_device_set_quirk_kv(FuDevice *device,
				  const gchar *key,
				  const gchar *value,
				  GError **error)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(device);

	if (g_strcmp0(key, "IlitekItsSensorIdMask") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sensor_id_mask = (guint8)tmp;
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_ilitek_its_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_ilitek_its_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_ilitek_its_convert_version(version_raw);
}

static void
fu_ilitek_its_device_init(FuIlitekItsDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);

	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.ilitek.its");
	fu_device_set_summary(FU_DEVICE(self), "Touch controller");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);

	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_ilitek_its_device_finalize(GObject *object)
{
	FuIlitekItsDevice *self = FU_ILITEK_ITS_DEVICE(object);
	g_free(self->ic_name);
	G_OBJECT_CLASS(fu_ilitek_its_device_parent_class)->finalize(object);
}

static void
fu_ilitek_its_device_class_init(FuIlitekItsDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_ilitek_its_device_finalize;
	device_class->to_string = fu_ilitek_its_device_to_string;
	device_class->probe = fu_ilitek_its_device_probe;
	device_class->setup = fu_ilitek_its_device_setup;
	device_class->attach = fu_ilitek_its_device_attach;
	device_class->detach = fu_ilitek_its_device_detach;
	device_class->set_quirk_kv = fu_ilitek_its_device_set_quirk_kv;
	device_class->prepare_firmware = fu_ilitek_its_device_prepare_firmware;
	device_class->write_firmware = fu_ilitek_its_device_write_firmware;
	device_class->set_progress = fu_ilitek_its_device_set_progress;
	device_class->convert_version = fu_ilitek_its_device_convert_version;
}

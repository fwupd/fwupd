/*
 * Copyright 2026 LXS <support@lxsemicon.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lxs-touch-device.h"
#include "fu-lxs-touch-struct.h"

/* SWIP Protocol Constants */
#define SWIP_REG_ADDR_INFO_PANEL	 0x0110
#define SWIP_REG_ADDR_INFO_INTEGRITY	 0x0140
#define SWIP_REG_ADDR_INFO_INTERFACE	 0x0150
#define SWIP_REG_ADDR_CTRL_GETTER	 0x0600
#define SWIP_REG_ADDR_CTRL_SETTER	 0x0610
#define SWIP_REG_ADDR_FLASH_IAP_CTRL_CMD 0x1400
#define SWIP_REG_ADDR_PARAMETER_BUFFER	 0x6000

/* Commands */
#define FLITFCTRL_COMMAND_FLASH_WRITE 0x03
#define M_WATCH_DOG_RESET	      0x11

/* Mode Types */
typedef enum {
	M_TOUCH_NORMAL = 0,
	M_TOUCH_DIAG = 1,
	M_TOUCH_DFUP = 2
} FuLxsTouchMode;

/* Ready Status */
typedef enum {
	RS_READY = 0xA0,
	RS_NONE = 0x05,
	RS_LOG = 0x77,
	RS_IMAGE = 0xAA
} FuLxsTouchReadyStatus;

#define FU_LXS_TOUCH_DEVICE_TIMEOUT	     1000 /* ms */
#define FU_LXS_TOUCH_DEVICE_HID_REPORT_SIZE  64
#define FU_LXS_TOUCH_DEVICE_IAP_BLOCK_SIZE   128
#define FU_LXS_TOUCH_DEVICE_IAP_CHUNK_SIZE   16
#define FU_LXS_TOUCH_DEVICE_FIRMWARE_SIZE_APP	   (116 * 1024)
#define FU_LXS_TOUCH_DEVICE_FIRMWARE_SIZE_FULL	   (128 * 1024)
#define FU_LXS_TOUCH_DEVICE_FIRMWARE_OFFSET_APP	   0x3000
#define FU_LXS_TOUCH_DEVICE_WAIT_FOR_REPLUG_DELAY  3000 /* ms */
#define FU_LXS_TOUCH_DEVICE_RESET_MONITOR_DELAY	   200	/* ms */
#define FU_LXS_TOUCH_DEVICE_RESET_WAIT_RETRY	   1000

struct _FuLxsTouchDevice {
	FuUdevDevice parent_instance;
	guint8 x_node;
	guint8 y_node;
	guint16 boot_ver;
	guint16 core_ver;
	gboolean in_dfup_mode;
};

G_DEFINE_TYPE(FuLxsTouchDevice, fu_lxs_touch_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_lxs_touch_device_write_cmd(FuLxsTouchDevice *self,
			       guint8 flag,
			       guint16 command,
			       guint16 length,
			       const guint8 *data,
			       GError **error)
{
	guint8 buf[FU_LXS_TOUCH_DEVICE_HID_REPORT_SIZE] = {0};

	/* Build HID report packet */
	buf[0] = 0x09; /* Report ID */
	buf[1] = flag;
	buf[2] = (length & 0xFF);
	buf[3] = ((length >> 8) & 0xFF);
	buf[4] = ((command >> 8) & 0xFF); /* MSB first */
	buf[5] = (command & 0xFF);

	/* Adjust length for write command */
	if (flag == 0x68 && data != NULL && length > 2) {
		guint data_len = length - 2;
		for (guint i = 0; i < data_len; i++)
			buf[6 + i] = data[i];
	}

	/* Write to HID device */
	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self), 0, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to write HID command 0x%04X: ", command);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_read_data(FuLxsTouchDevice *self,
			       guint16 command,
			       guint8 *data,
			       gsize data_len,
			       GError **error)
{
	guint8 buf[FU_LXS_TOUCH_DEVICE_HID_REPORT_SIZE] = {0};

	/* Send read request */
	if (!fu_lxs_touch_device_write_cmd(self, 0x69, command, 0, NULL, error))
		return FALSE;

	/* Wait and read response */
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
				  0,
				  buf,
				  sizeof(buf),
				  error)) {
		g_prefix_error(error, "failed to read HID response for 0x%04X: ", command);
		return FALSE;
	}

	/* Copy response data */
	if (data != NULL && data_len > 0) {
		memcpy(data, buf, MIN(data_len, sizeof(buf)));
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_wait_ready(FuLxsTouchDevice *self, GError **error)
{
	guint8 getter_buf[sizeof(FuStructLxsTouchProtocolGetter)] = {0};
	FuStructLxsTouchProtocolGetter *getter =
	    (FuStructLxsTouchProtocolGetter *)getter_buf;

	for (guint retry = 0; retry < 1000; retry++) {
		if (!fu_lxs_touch_device_read_data(self,
						   SWIP_REG_ADDR_CTRL_GETTER,
						   getter_buf,
						   sizeof(getter_buf),
						   error))
			return FALSE;

		if (fu_struct_lxs_touch_protocol_getter_get_ready_status(getter) == RS_READY)
			return TRUE;

		g_usleep(1000); /* 1ms */
	}

	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "device ready timeout");
	return FALSE;
}

static gboolean
fu_lxs_touch_device_set_mode(FuLxsTouchDevice *self, FuLxsTouchMode mode, GError **error)
{
	guint8 setter_buf[sizeof(FuStructLxsTouchProtocolSetter)] = {0};
	FuStructLxsTouchProtocolSetter *setter =
	    (FuStructLxsTouchProtocolSetter *)setter_buf;

	/* Read current setter */
	if (!fu_lxs_touch_device_read_data(self,
					   SWIP_REG_ADDR_CTRL_SETTER,
					   setter_buf,
					   sizeof(setter_buf),
					   error))
		return FALSE;

	/* Check if mode change needed */
	if (fu_struct_lxs_touch_protocol_setter_get_mode(setter) == (guint8)mode)
		return TRUE;

	/* Set new mode */
	fu_struct_lxs_touch_protocol_setter_set_mode(setter, (guint8)mode);
	fu_struct_lxs_touch_protocol_setter_set_event_trigger_type(setter, 0);

	if (!fu_lxs_touch_device_write_cmd(self,
					   0x68,
					   SWIP_REG_ADDR_CTRL_SETTER,
					   sizeof(setter_buf) + 2,
					   setter_buf,
					   error))
		return FALSE;

	return fu_lxs_touch_device_wait_ready(self, error);
}

static gboolean
fu_lxs_touch_device_probe(FuDevice *device, GError **error)
{
	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_lxs_touch_device_setup(FuDevice *device, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	guint8 interface_buf[8] = {0};
	guint8 panel_buf[sizeof(FuStructLxsTouchPanel)] = {0};
	guint8 version_buf[sizeof(FuStructLxsTouchVersion)] = {0};
	FuStructLxsTouchPanel *panel = (FuStructLxsTouchPanel *)panel_buf;
	FuStructLxsTouchVersion *version = (FuStructLxsTouchVersion *)version_buf;

	/* Read interface protocol name */
	if (!fu_lxs_touch_device_read_data(self,
					   SWIP_REG_ADDR_INFO_INTERFACE,
					   interface_buf,
					   sizeof(interface_buf),
					   error))
		return FALSE;

	/* Check protocol name */
	if (memcmp(interface_buf, "SWIP", 4) == 0) {
		self->in_dfup_mode = FALSE;
		g_debug("device in SWIP (runtime) mode");
	} else if (memcmp(interface_buf, "DFUP", 4) == 0) {
		self->in_dfup_mode = TRUE;
		g_debug("device in DFUP (bootloader) mode");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		/* Skip reading panel/version info in bootloader mode */
		return TRUE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown protocol: %.4s",
			    interface_buf);
		return FALSE;
	}

	/* Read panel info */
	if (!fu_lxs_touch_device_read_data(self,
					   SWIP_REG_ADDR_INFO_PANEL,
					   panel_buf,
					   sizeof(panel_buf),
					   error))
		return FALSE;

	self->x_node = fu_struct_lxs_touch_panel_get_x_node(panel);
	self->y_node = fu_struct_lxs_touch_panel_get_y_node(panel);
	g_debug("panel size: %u x %u nodes", self->x_node, self->y_node);

	/* Read firmware version */
	if (!fu_lxs_touch_device_read_data(self,
					   SWIP_REG_ADDR_INFO_INTEGRITY,
					   version_buf,
					   sizeof(version_buf),
					   error))
		return FALSE;

	self->boot_ver = fu_struct_lxs_touch_version_get_boot_ver(version);
	self->core_ver = fu_struct_lxs_touch_version_get_core_ver(version);

	g_debug("boot version: 0x%04X, core version: 0x%04X", self->boot_ver, self->core_ver);
	fu_device_set_version_raw(device, self->core_ver);

	/* Set normal mode */
	if (!fu_lxs_touch_device_set_mode(self, M_TOUCH_NORMAL, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_lxs_touch_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);

	/* Already in bootloader mode? */
	if (self->in_dfup_mode) {
		g_debug("already in DFUP mode");
		return TRUE;
	}

	/* Set DFUP mode */
	g_debug("switching to DFUP mode");
	if (!fu_lxs_touch_device_set_mode(self, M_TOUCH_DFUP, error))
		return FALSE;

	/* Device will reset and re-enumerate */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_lxs_touch_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	guint32 flash_offset = 0;
	gsize fw_size = 0;
	guint8 temp_buf[FU_LXS_TOUCH_DEVICE_IAP_CHUNK_SIZE] = {0};
	guint8 iap_cmd_buf[sizeof(FuStructLxsTouchFlashIAPCmd)] = {0};
	FuStructLxsTouchFlashIAPCmd *iap_cmd = (FuStructLxsTouchFlashIAPCmd *)iap_cmd_buf;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* Determine firmware size and offset */
	fw_size = fu_firmware_get_size(firmware);
	if (fw_size == FU_LXS_TOUCH_DEVICE_FIRMWARE_SIZE_APP) {
		flash_offset = FU_LXS_TOUCH_DEVICE_FIRMWARE_OFFSET_APP;
		g_debug("application-only firmware update");
	} else if (fw_size == FU_LXS_TOUCH_DEVICE_FIRMWARE_SIZE_FULL) {
		flash_offset = 0x0;
		g_debug("boot + application firmware update");
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware size: 0x%x (expected 0x%x or 0x%x)",
			    (guint)fw_size,
			    (guint)FU_LXS_TOUCH_DEVICE_FIRMWARE_SIZE_APP,
			    (guint)FU_LXS_TOUCH_DEVICE_FIRMWARE_SIZE_FULL);
		return FALSE;
	}

	/* Verify in DFUP mode */
	if (!self->in_dfup_mode) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device not in DFUP mode");
		return FALSE;
	}

	/* Get firmware bytes */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* Create chunks for download blocks */
	chunks = fu_chunk_array_new_from_bytes(fw,
					       flash_offset,
					       0x0, /* page_sz */
					       FU_LXS_TOUCH_DEVICE_IAP_BLOCK_SIZE);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	/* Write firmware in blocks */
	for (guint block_idx = 0; block_idx < chunks->len; block_idx++) {
		FuChunk *chk = g_ptr_array_index(chunks, block_idx);
		const guint8 *block_data = NULL;
		gsize block_data_sz = 0;

		/* Get block data from chunk */
		block_data = fu_chunk_get_data(chk);
		block_data_sz = fu_chunk_get_data_sz(chk);

		/* Write block in 16-byte chunks to parameter buffer */
		for (guint offset = 0; offset < block_data_sz;
		     offset += FU_LXS_TOUCH_DEVICE_IAP_CHUNK_SIZE) {
			guint16 param_addr =
			    SWIP_REG_ADDR_PARAMETER_BUFFER + (guint16)offset;
			gsize chunk_size = MIN(FU_LXS_TOUCH_DEVICE_IAP_CHUNK_SIZE, 
					       block_data_sz - offset);

			/* Copy chunk data */
			memset(temp_buf, 0, sizeof(temp_buf));
			if (!fu_memcpy_safe(temp_buf,
					    sizeof(temp_buf),
					    0x0, /* dst */
					    block_data,
					    block_data_sz,
					    offset, /* src */
					    chunk_size,
					    error))
				return FALSE;

			/* Write to parameter buffer */
			if (!fu_lxs_touch_device_write_cmd(
				self,
				0x68,
				param_addr,
				FU_LXS_TOUCH_DEVICE_IAP_CHUNK_SIZE + 2,
				temp_buf,
				error))
				return FALSE;
		}

		/* Execute flash write command */
		fu_struct_lxs_touch_flash_iap_cmd_set_addr(iap_cmd, fu_chunk_get_address(chk));
		fu_struct_lxs_touch_flash_iap_cmd_set_size(iap_cmd,
							    FU_LXS_TOUCH_DEVICE_IAP_BLOCK_SIZE);
		fu_struct_lxs_touch_flash_iap_cmd_set_status(iap_cmd, 0);
		fu_struct_lxs_touch_flash_iap_cmd_set_cmd(iap_cmd,
							   FLITFCTRL_COMMAND_FLASH_WRITE);

		if (!fu_lxs_touch_device_write_cmd(self,
						   0x68,
						   SWIP_REG_ADDR_FLASH_IAP_CTRL_CMD,
						   sizeof(iap_cmd_buf) + 2,
						   iap_cmd_buf,
						   error))
			return FALSE;

		/* Wait for flash operation to complete */
		if (!fu_lxs_touch_device_wait_ready(self, error))
			return FALSE;

		/* Sleep briefly to avoid overwhelming device */
		g_usleep(2000); /* 2ms */

		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_lxs_touch_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLxsTouchDevice *self = FU_LXS_TOUCH_DEVICE(device);
	guint8 setter_buf[sizeof(FuStructLxsTouchProtocolSetter)] = {0};
	FuStructLxsTouchProtocolSetter *setter =
	    (FuStructLxsTouchProtocolSetter *)setter_buf;

	/* Reset device via watchdog */
	g_debug("resetting device via watchdog");
	fu_struct_lxs_touch_protocol_setter_set_mode(setter, M_WATCH_DOG_RESET);
	if (!fu_lxs_touch_device_write_cmd(self,
					   0x68,
					   SWIP_REG_ADDR_CTRL_SETTER,
					   sizeof(setter_buf) + 2,
					   setter_buf,
					   error))
		return FALSE;

	/* Device will reset */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_lxs_touch_device_reload(FuDevice *device, GError **error)
{
	/* Re-setup to read new version */
	return fu_lxs_touch_device_setup(device, error);
}

static void
fu_lxs_touch_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_lxs_touch_device_init(FuLxsTouchDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_name(FU_DEVICE(self), "LXS Touchpad");
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x29BD");
	fu_device_add_protocol(FU_DEVICE(self), "com.lxsemicon.swip");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_LXS_TOUCH_DEVICE_WAIT_FOR_REPLUG_DELAY);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
}

static void
fu_lxs_touch_device_class_init(FuLxsTouchDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_lxs_touch_device_probe;
	device_class->setup = fu_lxs_touch_device_setup;
	device_class->detach = fu_lxs_touch_device_detach;
	device_class->write_firmware = fu_lxs_touch_device_write_firmware;
	device_class->attach = fu_lxs_touch_device_attach;
	device_class->reload = fu_lxs_touch_device_reload;
	device_class->set_progress = fu_lxs_touch_device_set_progress;
}

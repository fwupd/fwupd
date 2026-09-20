/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Justin Li <justin_li@realsil.com.cn>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <unistd.h>

#include "fu-realtek-edpscaler-common.h"
#include "fu-realtek-edpscaler-device.h"
#include "fu-realtek-edpscaler-struct.h"

struct _FuRealtekEdpscalerDevice {
	FuDrmDevice parent_instance;
	FuUdevDevice *smbus; /* the PCH SMBus adapter /dev/i2c-N, opened as needed */
	gchar *smbus_dev_file;
	gboolean smbus_block_enabled;
	gboolean fg_update_mode_enabled; /* TRUE once the scaler is in foreground (0x3A) mode;
					  * the DDCCI target (0x6E) is then unresponsive and the
					  * block-mode command must NOT be sent to it */
};

G_DEFINE_TYPE(FuRealtekEdpscalerDevice, fu_realtek_edpscaler_device, FU_TYPE_DRM_DEVICE)

/* the I2C_SMBUS argument holds a pointer to the data union, which has to be
 * re-pointed at the data buffer before the ioctl is issued */
static gboolean
fu_realtek_edpscaler_device_ioctl_buffer_cb(FuIoctl *ioctl,
					    gpointer ptr,
					    guint8 *buf,
					    gsize bufsz,
					    GError **error)
{
	struct i2c_smbus_ioctl_data *args = (struct i2c_smbus_ioctl_data *)ptr;
	args->data = (union i2c_smbus_data *)buf;
	return TRUE;
}

/* one SMBus transfer on the currently selected target */
static gboolean
fu_realtek_edpscaler_device_smbus_xfer(FuRealtekEdpscalerDevice *self,
				       guint8 read_write,
				       guint8 sub_addr,
				       guint32 size,
				       union i2c_smbus_data *data,
				       GError **error)
{
	struct i2c_smbus_ioctl_data args = {
	    .read_write = read_write,
	    .command = sub_addr,
	    .size = size,
	    .data = data,
	};
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(self->smbus);

	fu_ioctl_add_key_as_u16(ioctl, "Request", I2C_SMBUS);
	fu_ioctl_add_key_as_u8(ioctl, "ReadWrite", read_write);
	fu_ioctl_add_key_as_u8(ioctl, "Command", sub_addr);
	fu_ioctl_add_mutable_buffer(ioctl,
				    NULL,
				    (guint8 *)data,
				    sizeof(*data),
				    fu_realtek_edpscaler_device_ioctl_buffer_cb);
	return fu_ioctl_execute(ioctl,
				I2C_SMBUS,
				(guint8 *)&args,
				sizeof(args),
				NULL,
				REALTEK_EDPSCALER_IOCTL_TIMEOUT_MS,
				FU_IOCTL_FLAG_NONE,
				error);
}

/* hexdump helper for wire-level diagnostics (payload only, no target/sub) */
static void
fu_realtek_edpscaler_device_dump_payload(const gchar *direction,
					 guint8 target8,
					 guint8 sub_addr,
					 const guint8 *buf,
					 gsize bufsz)
{
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *hex = NULL;

	/* avoid building the string (and the allocations) when debug is off --
	 * this runs for every SMBus transaction */
	if (!g_log_get_debug_enabled())
		return;
	blob = g_bytes_new(buf, bufsz);
	hex = fu_bytes_to_string(blob);
	g_debug("smbus %s target=0x%02x sub=0x%02x len=%zu payload=%s",
		direction,
		target8,
		sub_addr,
		bufsz,
		hex);
}

/* set the 7-bit target address on the wire for the next SMBus transaction.
 * The kernel I2C_SLAVE ioctl wants the 7-bit address, while the scaler uses
 * 8-bit write addresses (0x6E DDCCI / 0x3A FG -> 0x37 / 0x1D on the wire).
 * The address is set per-operation rather than being sticky */
static gboolean
fu_realtek_edpscaler_device_set_target(FuRealtekEdpscalerDevice *self, guint8 addr8, GError **error)
{
	guint8 addr7 = addr8 >> 1;
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(self->smbus);

	if (!fu_ioctl_execute(ioctl,
			      I2C_SLAVE,
			      GUINT_TO_POINTER(addr7),
			      sizeof(guint32),
			      NULL,
			      REALTEK_EDPSCALER_IOCTL_TIMEOUT_MS,
			      FU_IOCTL_FLAG_PTR_AS_INTEGER,
			      error)) {
		g_prefix_error_literal(error, "failed to set target address: ");
		return FALSE;
	}
	return TRUE;
}

/* locate the PCH SMBus adapter by its sysfs name (bus number varies between boots) */
static gchar *
fu_realtek_edpscaler_device_find_smbus_devfile(GError **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *name = NULL;

	dir = g_dir_open("/sys/class/i2c-dev", 0, error);
	if (dir == NULL)
		return NULL;
	while ((name = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *path = NULL;
		g_autofree gchar *adapter_name = NULL;

		if (!g_str_has_prefix(name, "i2c-"))
			continue;
		path = g_build_filename("/sys/class/i2c-dev", name, "name", NULL);
		if (!g_file_get_contents(path, &adapter_name, NULL, NULL))
			continue;
		g_strstrip(adapter_name);
		/* log every candidate so a wrong-name adapter is visible */
		g_debug("i2c adapter %s: %s", name, adapter_name);
		if (g_str_has_prefix(adapter_name, "SMBus I801 adapter")) {
			g_autofree gchar *devfile = g_strdup_printf("/dev/%s", name);
			if (g_file_test(devfile, G_FILE_TEST_EXISTS)) {
				g_info("using SMBus adapter %s (%s)", name, adapter_name);
				return g_steal_pointer(&devfile);
			}
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no SMBus I801 adapter found");
	return NULL;
}

static gboolean
fu_realtek_edpscaler_device_smbus_open(FuRealtekEdpscalerDevice *self, GError **error)
{
	g_autoptr(FuIOChannel) io_channel = NULL;

	if (self->smbus != NULL) {
		g_debug("smbus already open");
		return TRUE;
	}
	if (self->smbus_dev_file == NULL) {
		self->smbus_dev_file = fu_realtek_edpscaler_device_find_smbus_devfile(error);
		if (self->smbus_dev_file == NULL) {
			g_prefix_error_literal(error, "cannot locate SMBus I801 adapter: ");
			return FALSE;
		}
	}
	io_channel =
	    fu_io_channel_new_file(self->smbus_dev_file,
				   FU_IO_CHANNEL_OPEN_FLAG_READ | FU_IO_CHANNEL_OPEN_FLAG_WRITE,
				   error);
	if (io_channel == NULL)
		return FALSE;

	/* the adapter has no sysfs node we want to probe, so use the udev device
	 * purely as the owner of the already-opened channel for ioctls */
	self->smbus = g_object_new(FU_TYPE_UDEV_DEVICE,
				   "context",
				   fu_device_get_context(FU_DEVICE(self)),
				   NULL);
	fu_udev_device_set_device_file(self->smbus, self->smbus_dev_file);
	fu_udev_device_set_io_channel(self->smbus, io_channel);
	g_info("opened SMBus device %s", self->smbus_dev_file);
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_smbus_close(FuRealtekEdpscalerDevice *self, GError **error)
{
	if (self->smbus != NULL) {
		FuIOChannel *io_channel = fu_udev_device_get_io_channel(self->smbus);
		if (io_channel != NULL && !fu_io_channel_shutdown(io_channel, error))
			return FALSE;
		g_debug("closed smbus %s", self->smbus_dev_file);
		g_clear_object(&self->smbus);
	}
	/* do not clear smbus_block_enabled here: SMBus block mode is a scaler-side
	 * hardware state that outlives this fd (the chip keeps it until told to
	 * disable), so the flag must mirror hardware, not the fd lifecycle.  It is
	 * toggled exclusively inside set_smbus_block_enabled(). */
	return TRUE;
}

/* raw SMBus block read */
static gboolean
fu_realtek_edpscaler_device_smbus_read_block(FuRealtekEdpscalerDevice *self,
					     guint8 target8,
					     guint8 sub_addr,
					     guint8 *buf,
					     gsize bufsz,
					     GError **error)
{
	union i2c_smbus_data data = {0};

	if (bufsz > I2C_SMBUS_BLOCK_MAX) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "smbus read length exceeds max");
		return FALSE;
	}
	if (!fu_realtek_edpscaler_device_set_target(self, target8, error))
		return FALSE;
	if (!fu_realtek_edpscaler_device_smbus_xfer(self,
						    I2C_SMBUS_READ,
						    sub_addr,
						    I2C_SMBUS_BLOCK_DATA,
						    &data,
						    error))
		return FALSE;
	if (data.block[0] != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "smbus read returned %u bytes, expected %zu",
			    data.block[0],
			    bufsz);
		return FALSE;
	}
	fu_realtek_edpscaler_device_dump_payload("read-block",
						 target8,
						 sub_addr,
						 data.block + 1,
						 data.block[0]);
	return fu_memcpy_safe(buf, bufsz, 0x0, data.block + 1, data.block[0], 0x0, bufsz, error);
}

/* raw SMBus block write */
static gboolean
fu_realtek_edpscaler_device_smbus_write_block(FuRealtekEdpscalerDevice *self,
					      guint8 target8,
					      guint8 sub_addr,
					      const guint8 *buf,
					      gsize bufsz,
					      GError **error)
{
	union i2c_smbus_data data = {0};

	if (bufsz == 0 || bufsz > I2C_SMBUS_BLOCK_MAX) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "smbus write length out of range");
		return FALSE;
	}
	if (!fu_realtek_edpscaler_device_set_target(self, target8, error))
		return FALSE;
	if (!fu_memcpy_safe(data.block + 1,
			    sizeof(data.block) - 1,
			    0x0,
			    buf,
			    bufsz,
			    0x0,
			    bufsz,
			    error))
		return FALSE;
	data.block[0] = bufsz;
	if (!fu_realtek_edpscaler_device_smbus_xfer(self,
						    I2C_SMBUS_WRITE,
						    sub_addr,
						    I2C_SMBUS_BLOCK_DATA,
						    &data,
						    error))
		return FALSE;
	fu_realtek_edpscaler_device_dump_payload("write-block", target8, sub_addr, buf, bufsz);
	return TRUE;
}

/* SMBus single-byte read (status polling uses BYTE_DATA, as the block read
 * interface does not support a single byte read) */
static gboolean
fu_realtek_edpscaler_device_smbus_read_byte(FuRealtekEdpscalerDevice *self,
					    guint8 target8,
					    guint8 sub_addr,
					    guint8 *buf,
					    GError **error)
{
	union i2c_smbus_data data = {0};

	if (!fu_realtek_edpscaler_device_set_target(self, target8, error))
		return FALSE;
	if (!fu_realtek_edpscaler_device_smbus_xfer(self,
						    I2C_SMBUS_READ,
						    sub_addr,
						    I2C_SMBUS_BYTE_DATA,
						    &data,
						    error))
		return FALSE;
	*buf = data.byte & 0xFF;
	fu_realtek_edpscaler_device_dump_payload("read-byte", target8, sub_addr, buf, 1);
	return TRUE;
}

/* Send a DDCCI-style command to the DDCCI target (0x6E). The DDCCI frame is
 * [target][sub][len|0x80][cmd...][xor-checksum over target..cmd]; the sub
 * address goes in the SMBus I2C command byte and only [len|0x80][cmd][xor]
 * is sent on the wire. */
static gboolean
fu_realtek_edpscaler_device_ddcci_write(FuRealtekEdpscalerDevice *self,
					guint8 sub_addr,
					const guint8 *cmds,
					gsize cmdsz,
					GError **error)
{
	guint8 buf[REALTEK_EDPSCALER_SMBUS_BLOCK_MAXSZ] = {0};
	gsize bufsz = cmdsz + 2; /* [len|0x80] + cmds + [xor] */
	guint8 checksum = 0x00;

	if (bufsz > REALTEK_EDPSCALER_SMBUS_BLOCK_MAXSZ) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "ddcci command too long");
		return FALSE;
	}
	buf[0] = cmdsz | FU_REALTEK_EDPSCALER_DDCCI_FLAG_LEN_MSB;
	if (!fu_memcpy_safe(buf + 1, sizeof(buf) - 1, 0x0, cmds, cmdsz, 0x0, cmdsz, error))
		return FALSE;
	/* checksum over target ^ sub ^ [len|0x80] ^ cmds */
	checksum = FU_REALTEK_EDPSCALER_TARGET_ADDR_DDCCI ^ sub_addr;
	for (gsize i = 0; i < cmdsz + 1; i++)
		checksum ^= buf[i];
	buf[cmdsz + 1] = checksum;

	return fu_realtek_edpscaler_device_smbus_write_block(self,
							     FU_REALTEK_EDPSCALER_TARGET_ADDR_DDCCI,
							     sub_addr,
							     buf,
							     bufsz,
							     error);
}

/* one attempt at toggling SMBus block mode; retried by the wrapper below.
 * All ISP command/data traffic uses SMBus BLOCK transfers, so block mode must be
 * enabled before any of them are issued; status polling uses BYTE_DATA and is not
 * affected. The block-mode command is a DDCCI frame on the DDCCI target:
 * [sub 0x71][len|0x80][opcode 0x77][cmd 0xC8][enable][xor], where the
 * checksum covers target^sub^len|0x80^payload. The sub address goes in the
 * I2C command byte, so only [len|0x80][payload][xor] is sent on the wire. */
static gboolean
fu_realtek_edpscaler_device_set_smbus_block_enabled_cb(FuDevice *device,
						       gpointer user_data,
						       GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);
	gboolean enable = GPOINTER_TO_INT(user_data) != 0;
	guint8 data[5] = {0};
	guint8 buf[3] = {FU_REALTEK_EDPSCALER_DDCCI_OPCODE_VENDOR,
			 FU_REALTEK_EDPSCALER_DDCCI_CMD_BLOCK_RW,
			 enable ? 1 : 0};

	g_debug("block-mode: %s via DDCCI", enable ? "enable" : "disable");
	if (!fu_realtek_edpscaler_device_ddcci_write(self,
						     FU_REALTEK_EDPSCALER_DDCCI_SUB_ADDR_BLOCK_MODE,
						     buf,
						     sizeof(buf),
						     error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_SMBUS_BLOCK_WAIT_MS);

	if (!fu_realtek_edpscaler_device_smbus_read_block(
		self,
		FU_REALTEK_EDPSCALER_TARGET_ADDR_DDCCI,
		FU_REALTEK_EDPSCALER_DDCCI_SUB_ADDR_STATUS,
		data,
		sizeof(data),
		error))
		return FALSE;
	g_debug("block-mode: status readback data[0..4]=0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
		data[0],
		data[1],
		data[2],
		data[3],
		data[4]);
	/* the same readback (0xC8, 0x00) is returned for both enable and disable
	 * on this hardware, so this only confirms the command was acknowledged;
	 * it is intentional that one check covers both directions */
	if (data[2] != FU_REALTEK_EDPSCALER_DDCCI_CMD_BLOCK_RW ||
	    data[3] != FU_REALTEK_EDPSCALER_BLOCK_MODE_STATUS_ACK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "block mode enable failed, got 0x%02x 0x%02x",
			    data[2],
			    data[3]);
		return FALSE;
	}
	self->smbus_block_enabled = enable;
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_set_smbus_block_enabled(FuRealtekEdpscalerDevice *self,
						    gboolean enable,
						    GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_realtek_edpscaler_device_set_smbus_block_enabled_cb,
			       REALTEK_EDPSCALER_BLOCK_ENABLE_RETRY_COUNT,
			       GINT_TO_POINTER(enable),
			       error);
}

/* poll the foreground status register until it leaves the busy state, then
 * require an idle-success terminal status -- an idle-fail means the scaler
 * rejected the command and must not be mistaken for success.
 * This is a plain loop rather than fu_device_retry(): a busy status is the
 * normal case and would otherwise log thousands of "failed on try" lines for
 * every update. The status register lives on the ISP target, and is polled
 * with a single BYTE_DATA read */
static gboolean
fu_realtek_edpscaler_device_poll_status(FuRealtekEdpscalerDevice *self,
					guint8 *status,
					GError **error)
{
	guint8 value = FU_REALTEK_EDPSCALER_STATUS_BUSY;

	for (guint i = 0; i < REALTEK_EDPSCALER_STATUS_POLL_RETRY_COUNT; i++) {
		if (!fu_realtek_edpscaler_device_smbus_read_byte(
			self,
			FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
			FU_REALTEK_EDPSCALER_FG_SUB_ADDR_STATUS,
			&value,
			error))
			return FALSE;
		if (value != FU_REALTEK_EDPSCALER_STATUS_BUSY)
			break;
		fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_RETRY_DELAY_MS);
	}
	if (value == FU_REALTEK_EDPSCALER_STATUS_BUSY) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "timed out waiting for the device to leave the busy state");
		return FALSE;
	}
	g_debug("poll_status: read status=0x%02x (%s)",
		value,
		fu_realtek_edpscaler_status_to_string(value));
	if (value != FU_REALTEK_EDPSCALER_STATUS_IDLE_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device reported status 0x%02x [%s]",
			    value,
			    fu_realtek_edpscaler_status_to_string(value));
		return FALSE;
	}
	*status = value;
	return TRUE;
}

/* enter foreground fw-update mode: send the DDCCI change-mode command on the
 * DDCCI target (0x6E), then read the foreground status register on the FG target
 * (0x3A) once.  Entering the mode is judged solely from this status read, and
 * the outer change_mode() retry re-sends the whole sequence on failure */
static gboolean
fu_realtek_edpscaler_device_change_mode_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);
	guint8 status = 0;
	guint8 buf[3] = {FU_REALTEK_EDPSCALER_DDCCI_OPCODE_VENDOR,
			 FU_REALTEK_EDPSCALER_FG_SUB_ADDR_STATUS,
			 FU_REALTEK_EDPSCALER_FG_SUB_OPCODE_FOREGROUND};

	g_debug("change_mode: entering foreground fw-update mode");
	(void)fu_realtek_edpscaler_device_ddcci_write(
	    self,
	    FU_REALTEK_EDPSCALER_DDCCI_SUB_ADDR_BLOCK_MODE,
	    buf,
	    sizeof(buf),
	    NULL);

	/* let the scaler settle before reading the FG status register */
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS);

	/* read the status once; the status lives on the FG target (0x3A) */
	if (!fu_realtek_edpscaler_device_smbus_read_byte(self,
							 FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							 FU_REALTEK_EDPSCALER_FG_SUB_ADDR_STATUS,
							 &status,
							 error)) {
		g_prefix_error_literal(error, "failed to enter foreground mode: ");
		return FALSE;
	}
	g_debug("change_mode: FG mode status=0x%02x (%s)",
		status,
		fu_realtek_edpscaler_status_to_string(status));
	if (status != FU_REALTEK_EDPSCALER_STATUS_IDLE_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "enter foreground mode status was 0x%02x [%s]",
			    status,
			    fu_realtek_edpscaler_status_to_string(status));
		return FALSE;
	}
	self->fg_update_mode_enabled = TRUE;
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_change_mode(FuRealtekEdpscalerDevice *self, GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_realtek_edpscaler_device_change_mode_cb,
			       REALTEK_EDPSCALER_FG_MODE_ENTER_RETRY_COUNT,
			       NULL, /* user_data */
			       error);
}

/* leave foreground fw-update mode without resetting, so the panel runs
 * normally again after the version read */
static gboolean
fu_realtek_edpscaler_device_exit_mode(FuRealtekEdpscalerDevice *self, GError **error)
{
	guint8 cmd = FU_REALTEK_EDPSCALER_FG_OPCODE_FW_UPDATE_EXIT;

	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   &cmd,
							   sizeof(cmd),
							   error)) {
		g_prefix_error_literal(error, "failed to leave foreground mode: ");
		return FALSE;
	}
	g_debug("exit_mode: sent 0x08 to FG target, leaving foreground mode");
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS);
	/* back in normal mode: the DDCCI target (0x6E) is responsive again, so the
	 * block-mode disable in a following ->close() will now work */
	self->fg_update_mode_enabled = FALSE;
	return TRUE;
}

/* read the running firmware version while in foreground mode (GET_FW_INFO
 * 0x04, returns 7 bytes at sub-address 0x00) */
static gboolean
fu_realtek_edpscaler_device_get_version(FuRealtekEdpscalerDevice *self, GError **error)
{
	guint8 req = FU_REALTEK_EDPSCALER_FG_OPCODE_GET_FW_INFO;
	guint8 buf[7] = {0};
	g_autofree gchar *version = NULL;

	/* the foreground command/data traffic goes to the ISP target */
	g_debug("get_version: requesting FW info 0x04 on FG target");
	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   &req,
							   sizeof(req),
							   error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_GET_VERSION_WAIT_MS);
	if (!fu_realtek_edpscaler_device_smbus_read_block(self,
							  FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							  0x00,
							  buf,
							  sizeof(buf),
							  error))
		return FALSE;
	g_debug("get_version: version payload buf[0..6]=0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x "
		"0x%02x",
		buf[0],
		buf[1],
		buf[2],
		buf[3],
		buf[4],
		buf[5],
		buf[6]);
	version = g_strdup_printf("%u.%u", buf[1], buf[2]);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static void
fu_realtek_edpscaler_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);
	fwupd_codec_string_append(str, idt, "SmbusDevFile", self->smbus_dev_file);
	fwupd_codec_string_append_bool(str, idt, "SmbusBlockEnabled", self->smbus_block_enabled);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "FgUpdateModeEnabled",
				       self->fg_update_mode_enabled);
}

static gboolean
fu_realtek_edpscaler_device_open(FuDevice *device, GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);

	/* We intentionally do *not* chain up to FuUdevDevice->open: that would
	 * open /dev/dri/cardX (the DRM device file) which we do not need.  Our
	 * channel is the separate PCH SMBus adapter, opened below. */

	g_info("%s ->open", G_OBJECT_TYPE_NAME(device));
	if (!fu_realtek_edpscaler_device_smbus_open(self, error))
		return FALSE;
	/* the DDCCI target (0x6E/8-bit) is used to enable SMBus block mode, which
	 * every session needs for its command/data transfers.  The enable
	 * command only works in normal mode: once the scaler is in foreground
	 * (0x3A) mode the 0x6E DDCCI target goes unresponsive, so skip it and
	 * rely on the block mode already being enabled by the earlier normal-mode
	 * session (->detach()/->setup()). */
	if (!self->fg_update_mode_enabled && !self->smbus_block_enabled) {
		if (!fu_realtek_edpscaler_device_set_smbus_block_enabled(self, TRUE, error))
			return FALSE;
	}
	g_info("%s ->open done", G_OBJECT_TYPE_NAME(device));
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_close(FuDevice *device, GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);

	/* release SMBus block mode (best-effort: after ->attach() the device may
	 * already be resetting into the new firmware).  Only send the disable
	 * in normal mode: while the scaler is in foreground (0x3A) mode the 0x6E
	 * DDCCI target is unresponsive, so leave block mode armed and let the
	 * session that returns to normal mode (->attach()) tear it down. */
	if (self->smbus_block_enabled && !self->fg_update_mode_enabled)
		(void)fu_realtek_edpscaler_device_set_smbus_block_enabled(self, FALSE, NULL);
	if (!fu_realtek_edpscaler_device_smbus_close(self, error))
		return FALSE;
	/* success -- no chain up: FuUdevDevice->close would close the DRM fd
	 * that ->open() deliberately never opened */
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_probe(FuDevice *device, GError **error)
{
	/* FuDrmDevice->probe: builds the DRM\VEN_&DEV_ instance ID from the EDID,
	 * and flags the connector as internal when it is an eDP one */
	if (!FU_DEVICE_CLASS(fu_realtek_edpscaler_device_parent_class)->probe(device, error))
		return FALSE;

	/* the scaler only drives the built-in panel: reject any external DP/HDMI
	 * connector that happens to report the same EDID identity */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INTERNAL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not an internal eDP panel");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* the version can only be read in foreground mode: enter FG mode, read the
 * version, then leave FG mode again so the panel runs normally; the locker
 * runs ->open()/->close() around this */
static gboolean
fu_realtek_edpscaler_device_read_version(FuRealtekEdpscalerDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(FU_DEVICE(self), error);
	if (locker == NULL) {
		g_prefix_error_literal(error, "cannot open SMBus device: ");
		return FALSE;
	}
	if (!fu_realtek_edpscaler_device_change_mode(self, error)) {
		g_prefix_error_literal(error, "cannot enter foreground mode: ");
		return FALSE;
	}
	if (!fu_realtek_edpscaler_device_get_version(self, error)) {
		g_autoptr(GError) error_exit = NULL;
		/* do not leave the panel stuck in foreground mode on failure */
		if (!fu_realtek_edpscaler_device_exit_mode(self, &error_exit))
			g_warning("failed to exit foreground mode: %s", error_exit->message);
		g_prefix_error_literal(error, "cannot read version: ");
		return FALSE;
	}
	if (!fu_realtek_edpscaler_device_exit_mode(self, error)) {
		g_prefix_error_literal(error, "cannot exit foreground mode: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_setup(FuDevice *device, GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);

	/* FuDrmDevice->setup */
	if (!FU_DEVICE_CLASS(fu_realtek_edpscaler_device_parent_class)->setup(device, error))
		return FALSE;
	if (!fu_realtek_edpscaler_device_read_version(self, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	return TRUE;
}

/* the DRM connector does not necessarily re-enumerate after the scaler resets,
 * so read the version again to show the new firmware after an update */
static gboolean
fu_realtek_edpscaler_device_reload(FuDevice *device, GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);
	return fu_realtek_edpscaler_device_read_version(self, error);
}

static gboolean
fu_realtek_edpscaler_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open the SMBus channel and enable SMBus block mode (the engine does
	 * not keep the device open between the detach/write/attach phases) */
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;

	/* enter foreground ISP mode for the update */
	if (!fu_realtek_edpscaler_device_change_mode(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);
	guint8 cmd = FU_REALTEK_EDPSCALER_FG_OPCODE_FW_UPDATE_RESET;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open the SMBus channel and enable SMBus block mode (the engine does
	 * not keep the device open between the detach/write/attach phases) */
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;

	/* exit ISP mode and reset into the newly written firmware (the
	 * firmware commit was already verified by ->write_firmware()) */
	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   &cmd,
							   sizeof(cmd),
							   error)) {
		g_prefix_error_literal(error, "failed to exit foreground mode: ");
		return FALSE;
	}
	/* device needs time to restart with the new firmware */
	fu_device_sleep_full(device, REALTEK_EDPSCALER_ATTACH_RESET_WAIT_MS, progress);
	/* the scaler has been reset: it is out of foreground mode, and its block
	 * mode state is gone.  The DDCCI target (0x6E) does not answer for a while
	 * after the reset (the block-mode disable times out), so do not send it
	 * from ->close(); the next session enables block mode again. */
	self->fg_update_mode_enabled = FALSE;
	self->smbus_block_enabled = FALSE;
	return TRUE;
}

static gboolean
fu_realtek_edpscaler_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(device);
	guint8 buf[REALTEK_EDPSCALER_SMBUS_BLOCK_MAXSZ] = {0x00};
	guint8 project_addr[6] = {0};
	guint32 project_id_addr = 0;
	guint8 project_id_count = 0;
	guint8 req = 0;
	guint8 status = 0;
	gsize blocksz = REALTEK_EDPSCALER_SMBUS_BLOCK_MAXSZ;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "setup");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "finish");

	/* open the SMBus channel and enable SMBus block mode (the engine does
	 * not keep the device open between the detach/write/attach phases) */
	locker = fu_device_locker_new(FU_DEVICE(device), error);
	if (locker == NULL)
		return FALSE;

	/* simple image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* enable ISP high priority */
	buf[0] = FU_REALTEK_EDPSCALER_FG_OPCODE_ENTER_HIGH_PRI;
	buf[1] = 0x01;
	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   buf,
							   2,
							   error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS);
	if (!fu_realtek_edpscaler_device_poll_status(self, &status, error)) {
		g_prefix_error_literal(error, "failed to enter ISP high priority mode: ");
		return FALSE;
	}

	/* get project ID address */
	req = FU_REALTEK_EDPSCALER_FG_OPCODE_GET_PROJECT_ID_ADDR;
	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   &req,
							   sizeof(req),
							   error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_CHECK_PROJECT_ID_WAIT_MS);
	if (!fu_realtek_edpscaler_device_smbus_read_block(self,
							  FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							  FU_REALTEK_EDPSCALER_FG_SUB_ADDR_STATUS,
							  project_addr,
							  sizeof(project_addr),
							  error))
		return FALSE;
	if (project_addr[0] != FU_REALTEK_EDPSCALER_STATUS_IDLE_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to read project ID address, status 0x%02x",
			    project_addr[0]);
		return FALSE;
	}
	project_id_addr = fu_memread_uint32(project_addr + 1, G_BIG_ENDIAN);
	project_id_count = project_addr[5];

	/* verify project ID (read from the firmware image at the reported offset) */
	buf[0] = FU_REALTEK_EDPSCALER_FG_OPCODE_VERIFY_PROJECT_ID;
	if (!fu_input_stream_read_safe(stream,
				       buf,
				       sizeof(buf),
				       0x1,		/* dst */
				       project_id_addr, /* src */
				       project_id_count,
				       error)) {
		g_prefix_error(error,
			       "failed to read project ID from image @0x%04x: ",
			       project_id_addr);
		return FALSE;
	}
	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   buf,
							   project_id_count + 1,
							   error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS);
	if (!fu_realtek_edpscaler_device_poll_status(self, &status, error)) {
		g_prefix_error_literal(error, "failed to verify project ID: ");
		return FALSE;
	}

	/* start command with packet length (the SMBus block size) */
	buf[0] = FU_REALTEK_EDPSCALER_FG_OPCODE_START;
	fu_memwrite_uint16(buf + 1, (guint16)blocksz, G_BIG_ENDIAN);
	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   buf,
							   3,
							   error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS);
	fu_progress_step_done(progress);

	/* write data in SMBus blocks */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						blocksz,
						error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		/* poll-then-write handshake for each data block: wait until the
		 * device is ready for the next packet, then send it */
		if (!fu_realtek_edpscaler_device_poll_status(self, &status, error)) {
			g_prefix_error(error,
				       "failed to write @0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		if (!fu_realtek_edpscaler_device_smbus_write_block(
			self,
			FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
			FU_REALTEK_EDPSCALER_FG_SUB_ADDR_DATA,
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			error)) {
			g_prefix_error(error,
				       "failed to write @0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks));
	}
	fu_progress_step_done(progress);

	/* finish: wait for the last data block to complete */
	if (!fu_realtek_edpscaler_device_poll_status(self, &status, error)) {
		g_prefix_error_literal(error, "write did not complete: ");
		return FALSE;
	}
	buf[0] = FU_REALTEK_EDPSCALER_FG_OPCODE_FINISH;
	if (!fu_realtek_edpscaler_device_smbus_write_block(self,
							   FU_REALTEK_EDPSCALER_TARGET_ADDR_FG,
							   FU_REALTEK_EDPSCALER_FG_SUB_ADDR_CMD,
							   buf,
							   sizeof(guint8),
							   error)) {
		g_prefix_error_literal(error, "failed to send finish command: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), REALTEK_EDPSCALER_RECEIVE_CMD_DELAY_BASE_MS);
	/* wait for the finish command to complete; this is the decisive check
	 * that the firmware was committed successfully.  The last byte of the
	 * image is a CRC which the scaler verifies itself once the write has
	 * finished, so no further image validation is done on the host. */
	if (!fu_realtek_edpscaler_device_poll_status(self, &status, error)) {
		g_prefix_error_literal(error, "firmware commit failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success -- the device is reset into the new firmware by ->attach(), and
	 * the SMBus fd is closed by ->close() when the update finishes */
	return TRUE;
}

static void
fu_realtek_edpscaler_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 88, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 11, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_realtek_edpscaler_device_init(FuRealtekEdpscalerDevice *self)
{
	/* keep the DDC proxy created by the udev backend: without these two the quirk GType
	 * switch drops it and the framework creates a bogus one that fails probe. The SMBus
	 * transfers themselves do not use the proxy */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_I2C_DEVICE);
	self->fg_update_mode_enabled = FALSE;
	/* the ISP status register is polled every 5ms while it is busy */
	fu_device_retry_set_delay(FU_DEVICE(self), REALTEK_EDPSCALER_RETRY_DELAY_MS);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.edp-scaler");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), "computer");
}

static void
fu_realtek_edpscaler_device_finalize(GObject *object)
{
	FuRealtekEdpscalerDevice *self = FU_REALTEK_EDPSCALER_DEVICE(object);
	g_clear_object(&self->smbus);
	g_free(self->smbus_dev_file);
	G_OBJECT_CLASS(fu_realtek_edpscaler_device_parent_class)->finalize(object);
}

static void
fu_realtek_edpscaler_device_class_init(FuRealtekEdpscalerDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_realtek_edpscaler_device_finalize;
	device_class->to_string = fu_realtek_edpscaler_device_to_string;
	device_class->probe = fu_realtek_edpscaler_device_probe;
	device_class->setup = fu_realtek_edpscaler_device_setup;
	device_class->reload = fu_realtek_edpscaler_device_reload;
	device_class->open = fu_realtek_edpscaler_device_open;
	device_class->close = fu_realtek_edpscaler_device_close;
	device_class->detach = fu_realtek_edpscaler_device_detach;
	device_class->attach = fu_realtek_edpscaler_device_attach;
	device_class->write_firmware = fu_realtek_edpscaler_device_write_firmware;
	device_class->set_progress = fu_realtek_edpscaler_device_set_progress;
}

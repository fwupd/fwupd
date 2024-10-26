/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 * Copyright 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright 2018 Ryan Chang <ryan.chang@synaptics.com>
 * Copyright 2021 Apollo Ling <apollo.ling@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>

#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-device.h"
#include "fu-synaptics-mst-firmware.h"
#include "fu-synaptics-mst-struct.h"

#define FU_SYNAPTICS_MST_ID_CTRL_SIZE	 0x1000
#define SYNAPTICS_UPDATE_ENUMERATE_TRIES 3

#define BIT(n)		       (1 << (n))
#define FLASH_SECTOR_ERASE_4K  0x1000
#define FLASH_SECTOR_ERASE_32K 0x2000
#define FLASH_SECTOR_ERASE_64K 0x3000
#define EEPROM_TAG_OFFSET      0x1FFF0
#define EEPROM_BANK_OFFSET     0x20000
#define EEPROM_ESM_OFFSET      0x40000
#define ESM_CODE_SIZE	       0x40000
#define PAYLOAD_SIZE_512K      0x80000
#define PAYLOAD_SIZE_64K       0x10000
#define MAX_RETRY_COUNTS       10
#define BLOCK_UNIT	       64
#define BANKTAG_0	       0
#define BANKTAG_1	       1
#define REG_ESM_DISABLE	       0x2000fc
#define REG_QUAD_DISABLE       0x200fc0
#define REG_HDCP22_DISABLE     0x200f90

#define FLASH_SETTLE_TIME 5000 /* ms */

#define FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT 2000 /* ms */

#define CARRERA_FIRMWARE_SIZE  0x7F000 /* bytes */
#define CAYENNE_FIRMWARE_SIZE  0x50000 /* bytes */
#define PANAMERA_FIRMWARE_SIZE 0x1A000 /* bytes */

#define UNIT_SIZE 32

#define ADDR_MEMORY_CUSTOMER_ID_CAYENNE 0x9000024E
#define ADDR_MEMORY_BOARD_ID_CAYENNE	0x9000024F
#define ADDR_MEMORY_CUSTOMER_ID_SPYDER	0x9000020E
#define ADDR_MEMORY_BOARD_ID_SPYDER	0x9000020F
#define ADDR_MEMORY_CUSTOMER_ID_CARRERA 0x9000014E
#define ADDR_MEMORY_BOARD_ID_CARRERA	0x9000014F
#define ADDR_MEMORY_CUSTOMER_ID		0x170E
#define ADDR_MEMORY_BOARD_ID		0x170F

#define REG_CHIP_ID	     0x507
#define REG_FIRMWARE_VERSION 0x50A

#define FU_SYNAPTICS_MST_DEVICE_FLAG_IGNORE_BOARD_ID	     "ignore-board-id"
#define FU_SYNAPTICS_MST_DEVICE_FLAG_MANUAL_RESTART_REQUIRED "manual-restart-required"

struct _FuSynapticsMstDevice {
	FuDpauxDevice parent_instance;
	gchar *device_kind;
	guint64 write_block_size;
	FuSynapticsMstFamily family;
	guint8 active_bank;
	guint16 board_id;
	guint16 chip_id;
};

G_DEFINE_TYPE(FuSynapticsMstDevice, fu_synaptics_mst_device, FU_TYPE_DPAUX_DEVICE)

static void
fu_synaptics_mst_device_finalize(GObject *object)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(object);

	g_free(self->device_kind);

	G_OBJECT_CLASS(fu_synaptics_mst_device_parent_class)->finalize(object);
}

static void
fu_synaptics_mst_device_udev_device_notify_cb(FuUdevDevice *udev_device,
					      GParamSpec *pspec,
					      gpointer user_data)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(user_data);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	if (!fu_device_has_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_MST_DEVICE_FLAG_IS_SOMEWHAT_EMULATED)) {
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	}
}

static void
fu_synaptics_mst_device_init(FuSynapticsMstDevice *self)
{
	self->family = FU_SYNAPTICS_MST_FAMILY_UNKNOWN;
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.mst");
	fu_device_set_vendor(FU_DEVICE(self), "Synaptics");
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "DRM_DP_AUX_DEV", 0x06CB);
	fu_device_set_summary(FU_DEVICE(self), "Multi-stream transport device");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_MST_DEVICE_FLAG_IGNORE_BOARD_ID);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_MST_DEVICE_FLAG_MANUAL_RESTART_REQUIRED);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_MST_DEVICE_FLAG_IS_SOMEWHAT_EMULATED);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);

	/* this is set from ->incorporate() */
	g_signal_connect(FU_UDEV_DEVICE(self),
			 "notify::udev-device",
			 G_CALLBACK(fu_synaptics_mst_device_udev_device_notify_cb),
			 self);
}

static void
fu_synaptics_mst_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	fwupd_codec_string_append(str, idt, "DeviceKind", self->device_kind);
	fwupd_codec_string_append_hex(str, idt, "ActiveBank", self->active_bank);
	fwupd_codec_string_append_hex(str, idt, "BoardId", self->board_id);
	fwupd_codec_string_append_hex(str, idt, "ChipId", self->chip_id);
}

static gboolean
fu_synaptics_mst_device_rc_to_error(FuSynapticsMstUpdcRc rc, GError **error)
{
	gint code = FWUPD_ERROR_INTERNAL;

	/* yay */
	if (rc == FU_SYNAPTICS_MST_UPDC_RC_SUCCESS)
		return TRUE;

	/* map */
	switch (rc) {
	case FU_SYNAPTICS_MST_UPDC_RC_INVALID:
		code = FWUPD_ERROR_INVALID_DATA;
		break;
	case FU_SYNAPTICS_MST_UPDC_RC_UNSUPPORTED:
		code = FWUPD_ERROR_NOT_SUPPORTED;
		break;
	case FU_SYNAPTICS_MST_UPDC_RC_FAILED:
		code = FWUPD_ERROR_INTERNAL;
		break;
	case FU_SYNAPTICS_MST_UPDC_RC_DISABLED:
		code = FWUPD_ERROR_NOT_FOUND;
		break;
	case FU_SYNAPTICS_MST_UPDC_RC_CONFIGURE_SIGN_FAILED:
	case FU_SYNAPTICS_MST_UPDC_RC_FIRMWARE_SIGN_FAILED:
	case FU_SYNAPTICS_MST_UPDC_RC_ROLLBACK_FAILED:
		code = FWUPD_ERROR_INVALID_DATA;
		break;
	default:
		break;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    code,
		    "%s [%u]",
		    fu_synaptics_mst_updc_rc_to_string(rc),
		    rc);
	return FALSE;
}

typedef struct {
	FuSynapticsMstUpdcRc rc;
	FuSynapticsMstUpdcCmd rc_cmd;
} FuSynapticsMstUpdcRcHelper;

static gboolean
fu_synaptics_mst_device_rc_send_command_and_wait_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuSynapticsMstUpdcRcHelper *helper = (FuSynapticsMstUpdcRcHelper *)user_data;
	guint8 buf[2] = {0};

	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  FU_SYNAPTICS_MST_REG_RC_CMD,
				  buf,
				  sizeof(buf),
				  FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read command: ");
		return FALSE;
	}
	if (buf[0] & 0x80) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "command in active state");
		return FALSE;
	}

	/* success */
	helper->rc = buf[1];
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_rc_send_command_and_wait(FuSynapticsMstDevice *self,
						 FuSynapticsMstUpdcCmd rc_cmd,
						 GError **error)
{
	guint8 buf[1] = {rc_cmd | 0x80};
	FuSynapticsMstUpdcRcHelper helper = {
	    .rc = FU_SYNAPTICS_MST_UPDC_RC_SUCCESS,
	    .rc_cmd = rc_cmd,
	};

	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   FU_SYNAPTICS_MST_REG_RC_CMD,
				   buf,
				   sizeof(buf),
				   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
				   error)) {
		g_prefix_error(error, "failed to write command: ");
		return FALSE;
	}

	/* wait command complete */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_synaptics_mst_device_rc_send_command_and_wait_cb,
				  30,
				  100, /* ms */
				  &helper,
				  error)) {
		g_prefix_error(error, "remote command failed: ");
		return FALSE;
	}
	if (helper.rc != FU_SYNAPTICS_MST_UPDC_RC_SUCCESS) {
		if (!fu_synaptics_mst_device_rc_to_error(helper.rc, error)) {
			g_prefix_error(error, "remote command failed: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_rc_set_command(FuSynapticsMstDevice *self,
				       FuSynapticsMstUpdcCmd rc_cmd,
				       guint32 offset,
				       const guint8 *buf,
				       gsize bufsz,
				       GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new(buf, bufsz, offset, 0x0, UNIT_SIZE);

	/* just sent command */
	if (chunks->len == 0) {
		g_debug("no data, just sending command %s [0x%x]",
			fu_synaptics_mst_updc_cmd_to_string(rc_cmd),
			rc_cmd);
		return fu_synaptics_mst_device_rc_send_command_and_wait(self, rc_cmd, error);
	}

	/* read each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf2[4] = {0};

		g_debug("writing chunk of 0x%x bytes at offset 0x%x",
			(guint)fu_chunk_get_data_sz(chk),
			(guint)fu_chunk_get_address(chk));

		/* write data */
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_SYNAPTICS_MST_REG_RC_DATA,
					   fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
					   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failure writing data register: ");
			return FALSE;
		}

		/* write offset */
		fu_memwrite_uint32(buf2, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_SYNAPTICS_MST_REG_RC_OFFSET,
					   buf2,
					   sizeof(buf2),
					   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failure writing offset register: ");
			return FALSE;
		}

		/* write length */
		fu_memwrite_uint32(buf2, fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_SYNAPTICS_MST_REG_RC_LEN,
					   buf2,
					   sizeof(buf2),
					   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failure writing length register: ");
			return FALSE;
		}

		/* send command */
		g_debug("data, sending command %s [0x%x]",
			fu_synaptics_mst_updc_cmd_to_string(rc_cmd),
			rc_cmd);
		if (!fu_synaptics_mst_device_rc_send_command_and_wait(self, rc_cmd, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_rc_get_command(FuSynapticsMstDevice *self,
				       FuSynapticsMstUpdcCmd rc_cmd,
				       guint32 offset,
				       guint8 *buf,
				       gsize bufsz,
				       GError **error)
{
	g_autoptr(GPtrArray) chunks =
	    fu_chunk_array_mutable_new(buf, bufsz, offset, 0x0, UNIT_SIZE);

	/* just sent command */
	if (chunks->len == 0) {
		g_debug("no data, just sending command %s [0x%x]",
			fu_synaptics_mst_updc_cmd_to_string(rc_cmd),
			rc_cmd);
		return fu_synaptics_mst_device_rc_send_command_and_wait(self, rc_cmd, error);
	}

	/* read each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf2[4] = {0};

		g_debug("reading chunk of 0x%x bytes at offset 0x%x",
			(guint)fu_chunk_get_data_sz(chk),
			(guint)fu_chunk_get_address(chk));

		/* write offset */
		fu_memwrite_uint32(buf2, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_SYNAPTICS_MST_REG_RC_OFFSET,
					   buf2,
					   sizeof(buf2),
					   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failed to write offset: ");
			return FALSE;
		}

		/* write length */
		fu_memwrite_uint32(buf2, fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_SYNAPTICS_MST_REG_RC_LEN,
					   buf2,
					   sizeof(buf2),
					   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failed to write length: ");
			return FALSE;
		}

		/* send command */
		g_debug("data, sending command %s [0x%x]",
			fu_synaptics_mst_updc_cmd_to_string(rc_cmd),
			rc_cmd);
		if (!fu_synaptics_mst_device_rc_send_command_and_wait(self, rc_cmd, error))
			return FALSE;

		/* read data */
		if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
					  FU_SYNAPTICS_MST_REG_RC_DATA,
					  fu_chunk_get_data_out(chk),
					  fu_chunk_get_data_sz(chk),
					  FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					  error)) {
			g_prefix_error(error, "failed to read data: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_disable_rc(FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* in test mode */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_SYNAPTICS_MST_DEVICE_FLAG_IS_SOMEWHAT_EMULATED))
		return TRUE;

	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_DISABLE_RC,
						    0,
						    NULL,
						    0,
						    &error_local)) {
		/* ignore disabled */
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to disable remote control: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_enable_rc(FuSynapticsMstDevice *self, GError **error)
{
	const gchar *sc = "PRIUS";

	/* in test mode */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_SYNAPTICS_MST_DEVICE_FLAG_IS_SOMEWHAT_EMULATED))
		return TRUE;

	if (!fu_synaptics_mst_device_disable_rc(self, error)) {
		g_prefix_error(error, "failed to disable-to-enable: ");
		return FALSE;
	}
	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_ENABLE_RC,
						    0,
						    (guint8 *)sc,
						    strlen(sc),
						    error)) {
		g_prefix_error(error, "failed to enable remote control: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_rc_special_get_command(FuSynapticsMstDevice *self,
					       FuSynapticsMstUpdcCmd rc_cmd,
					       guint32 cmd_offset,
					       guint8 *cmd_data,
					       gsize cmd_datasz,
					       guint8 *buf,
					       gsize bufsz,
					       GError **error)
{
	if (cmd_datasz > 0) {
		guint8 buf2[4] = {0};

		/* write cmd data */
		if (cmd_data != NULL) {
			if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
						   FU_SYNAPTICS_MST_REG_RC_DATA,
						   cmd_data,
						   cmd_datasz,
						   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
						   error)) {
				g_prefix_error(error, "Failed to write command data: ");
				return FALSE;
			}
		}

		/* write offset */
		fu_memwrite_uint32(buf2, cmd_offset, G_LITTLE_ENDIAN);
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_SYNAPTICS_MST_REG_RC_OFFSET,
					   buf2,
					   sizeof(buf2),
					   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failed to write offset: ");
			return FALSE;
		}

		/* write length */
		fu_memwrite_uint32(buf2, cmd_datasz, G_LITTLE_ENDIAN);
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_SYNAPTICS_MST_REG_RC_LEN,
					   buf2,
					   sizeof(buf2),
					   FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					   error)) {
			g_prefix_error(error, "failed to write length: ");
			return FALSE;
		}
	}

	/* send command */
	g_debug("sending command 0x%x", rc_cmd);
	if (!fu_synaptics_mst_device_rc_send_command_and_wait(self, rc_cmd, error))
		return FALSE;

	if (bufsz > 0) {
		if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
					  FU_SYNAPTICS_MST_REG_RC_DATA,
					  buf,
					  bufsz,
					  FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
					  error)) {
			g_prefix_error(error, "failed to read length: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_get_flash_checksum(FuSynapticsMstDevice *self,
					   guint32 offset,
					   guint32 length,
					   guint32 *checksum,
					   GError **error)
{
	guint8 buf[4] = {0};
	g_return_val_if_fail(length > 0, FALSE);

	if (!fu_synaptics_mst_device_rc_special_get_command(
		self,
		FU_SYNAPTICS_MST_UPDC_CMD_CAL_EEPROM_CHECKSUM,
		offset,
		NULL,
		length,
		buf,
		sizeof(buf),
		error)) {
		g_prefix_error(error, "failed to get flash checksum: ");
		return FALSE;
	}

	*checksum = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_set_flash_sector_erase(FuSynapticsMstDevice *self,
					       guint16 rc_cmd,
					       guint16 offset,
					       GError **error)
{
	guint8 buf[2] = {0};

	/* Need to add Wp control ? */
	fu_memwrite_uint16(buf, rc_cmd + offset, G_LITTLE_ENDIAN);
	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_FLASH_ERASE,
						    0,
						    buf,
						    sizeof(buf),
						    error)) {
		g_prefix_error(error, "can't sector erase flash at offset %x: ", offset);
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	GBytes *fw;
	FuChunkArray *chunks;
	FuProgress *progress;
	guint8 bank_to_update;
	guint32 checksum;
} FuSynapticsMstDeviceHelper;

static void
fu_synaptics_mst_device_helper_free(FuSynapticsMstDeviceHelper *helper)
{
	if (helper->chunks != NULL)
		g_object_unref(helper->chunks);
	if (helper->fw != NULL)
		g_bytes_unref(helper->fw);
	if (helper->progress != NULL)
		g_object_unref(helper->progress);
	g_free(helper);
}

static FuSynapticsMstDeviceHelper *
fu_synaptics_mst_device_helper_new(void)
{
	FuSynapticsMstDeviceHelper *helper = g_new0(FuSynapticsMstDeviceHelper, 1);
	helper->bank_to_update = BANKTAG_1;
	return helper;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuSynapticsMstDeviceHelper, fu_synaptics_mst_device_helper_free)

static gboolean
fu_synaptics_mst_device_update_esm_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuSynapticsMstDeviceHelper *helper = (FuSynapticsMstDeviceHelper *)user_data;
	guint32 flash_checksum = 0;

	/* erase ESM firmware; erase failure is fatal */
	for (guint32 j = 0; j < 4; j++) {
		if (!fu_synaptics_mst_device_set_flash_sector_erase(self,
								    FLASH_SECTOR_ERASE_64K,
								    j + 4,
								    error)) {
			g_prefix_error(error, "failed to erase sector %u: ", j);
			return FALSE;
		}
	}

	g_debug("waiting for flash clear to settle");
	fu_device_sleep(FU_DEVICE(self), FLASH_SETTLE_TIME);

	/* write firmware */
	fu_progress_set_id(helper->progress, G_STRLOC);
	fu_progress_set_steps(helper->progress, fu_chunk_array_length(helper->chunks));
	for (guint i = 0; i < fu_chunk_array_length(helper->chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GError) error_local = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(helper->chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_synaptics_mst_device_rc_set_command(
			self,
			FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
			fu_chunk_get_address(chk),
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			&error_local)) {
			g_warning("failed to write ESM: %s", error_local->message);
			break;
		}
		fu_progress_step_done(helper->progress);
	}

	/* check ESM checksum */
	if (!fu_synaptics_mst_device_get_flash_checksum(self,
							EEPROM_ESM_OFFSET,
							ESM_CODE_SIZE,
							&flash_checksum,
							error))
		return FALSE;

	/* ESM update done */
	if (helper->checksum != flash_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum 0x%x mismatched 0x%x",
			    flash_checksum,
			    helper->checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_esm(FuSynapticsMstDevice *self,
				   GBytes *fw,
				   FuProgress *progress,
				   GError **error)
{
	guint32 flash_checksum = 0;
	g_autoptr(FuSynapticsMstDeviceHelper) helper = fu_synaptics_mst_device_helper_new();

	/* ESM checksum same */
	helper->fw = fu_bytes_new_offset(fw, EEPROM_ESM_OFFSET, ESM_CODE_SIZE, error);
	if (helper->fw == NULL)
		return FALSE;
	helper->checksum = fu_sum32_bytes(helper->fw);
	if (!fu_synaptics_mst_device_get_flash_checksum(self,
							EEPROM_ESM_OFFSET,
							ESM_CODE_SIZE,
							&flash_checksum,
							error)) {
		return FALSE;
	}
	if (helper->checksum == flash_checksum) {
		g_debug("ESM checksum already matches");
		return TRUE;
	}
	g_debug("ESM checksum %x doesn't match expected %x", flash_checksum, helper->checksum);

	helper->progress = g_object_ref(progress);
	helper->chunks = fu_chunk_array_new_from_bytes(helper->fw,
						       EEPROM_ESM_OFFSET,
						       FU_CHUNK_PAGESZ_NONE,
						       BLOCK_UNIT);
	return fu_device_retry(FU_DEVICE(self),
			       fu_synaptics_mst_device_update_esm_cb,
			       MAX_RETRY_COUNTS,
			       helper,
			       error);
}

static gboolean
fu_synaptics_mst_device_update_tesla_leaf_firmware_cb(FuDevice *device,
						      gpointer user_data,
						      GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuSynapticsMstDeviceHelper *helper = (FuSynapticsMstDeviceHelper *)user_data;
	guint32 flash_checksum = 0;

	if (!fu_synaptics_mst_device_set_flash_sector_erase(self, 0xffff, 0, error))
		return FALSE;
	g_debug("waiting for flash clear to settle");
	fu_device_sleep(FU_DEVICE(self), FLASH_SETTLE_TIME);

	fu_progress_set_steps(helper->progress, fu_chunk_array_length(helper->chunks));
	for (guint i = 0; i < fu_chunk_array_length(helper->chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GError) error_local = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(helper->chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_synaptics_mst_device_rc_set_command(
			self,
			FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
			fu_chunk_get_address(chk),
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			&error_local)) {
			g_warning("failed to write flash offset 0x%04x: %s, retrying",
				  (guint)fu_chunk_get_address(chk),
				  error_local->message);
			/* repeat once */
			if (!fu_synaptics_mst_device_rc_set_command(
				self,
				FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
				fu_chunk_get_address(chk),
				fu_chunk_get_data(chk),
				fu_chunk_get_data_sz(chk),
				error)) {
				g_prefix_error(error,
					       "can't write flash offset 0x%04x: ",
					       (guint)fu_chunk_get_address(chk));
				return FALSE;
			}
		}
		fu_progress_step_done(helper->progress);
	}

	/* check data just written */
	if (!fu_synaptics_mst_device_get_flash_checksum(self,
							0,
							g_bytes_get_size(helper->fw),
							&flash_checksum,
							error))
		return FALSE;
	if (helper->checksum != flash_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum 0x%x mismatched 0x%x",
			    flash_checksum,
			    helper->checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_tesla_leaf_firmware(FuSynapticsMstDevice *self,
						   GBytes *fw,
						   FuProgress *progress,
						   GError **error)
{
	g_autoptr(FuSynapticsMstDeviceHelper) helper = fu_synaptics_mst_device_helper_new();
	helper->fw = g_bytes_ref(fw);
	helper->checksum = fu_sum32_bytes(fw);
	helper->progress = g_object_ref(progress);
	helper->chunks = fu_chunk_array_new_from_bytes(fw,
						       FU_CHUNK_ADDR_OFFSET_NONE,
						       FU_CHUNK_PAGESZ_NONE,
						       BLOCK_UNIT);
	return fu_device_retry(FU_DEVICE(self),
			       fu_synaptics_mst_device_update_tesla_leaf_firmware_cb,
			       MAX_RETRY_COUNTS,
			       helper,
			       error);
}

static gboolean
fu_synaptics_mst_device_get_active_bank_panamera(FuSynapticsMstDevice *self, GError **error)
{
	guint32 buf[16];

	/* get used bank */
	if (!fu_synaptics_mst_device_rc_get_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_MEMORY,
						    (gint)0x20010c,
						    (guint8 *)buf,
						    ((sizeof(buf) / sizeof(buf[0])) * 4),
						    error)) {
		g_prefix_error(error, "get active bank failed: ");
		return FALSE;
	}
	if ((buf[0] & BIT(7)) || (buf[0] & BIT(30)))
		self->active_bank = BANKTAG_1;
	else
		self->active_bank = BANKTAG_0;
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_panamera_firmware_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuSynapticsMstDeviceHelper *helper = (FuSynapticsMstDeviceHelper *)user_data;
	guint32 erase_offset = helper->bank_to_update * 2;
	guint32 flash_checksum;

	/* erase storage */
	if (!fu_synaptics_mst_device_set_flash_sector_erase(self,
							    FLASH_SECTOR_ERASE_64K,
							    erase_offset++,
							    error))
		return FALSE;
	if (!fu_synaptics_mst_device_set_flash_sector_erase(self,
							    FLASH_SECTOR_ERASE_64K,
							    erase_offset,
							    error))
		return FALSE;
	g_debug("waiting for flash clear to settle");
	fu_device_sleep(FU_DEVICE(self), FLASH_SETTLE_TIME);

	/* write */
	fu_progress_set_steps(helper->progress, fu_chunk_array_length(helper->chunks));
	for (guint i = 0; i < fu_chunk_array_length(helper->chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GError) error_local = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(helper->chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_synaptics_mst_device_rc_set_command(
			self,
			FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
			fu_chunk_get_address(chk),
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			&error_local)) {
			g_warning("write failed: %s, retrying", error_local->message);
			/* repeat once */
			if (!fu_synaptics_mst_device_rc_set_command(
				self,
				FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
				fu_chunk_get_address(chk),
				fu_chunk_get_data(chk),
				fu_chunk_get_data_sz(chk),
				error)) {
				g_prefix_error(error, "firmware write failed: ");
				return FALSE;
			}
		}
		fu_progress_step_done(helper->progress);
	}

	/* verify CRC */
	for (guint32 i = 0; i < 4; i++) {
		guint8 buf[4] = {0};
		fu_device_sleep(FU_DEVICE(self), 1); /* wait crc calculation */
		if (!fu_synaptics_mst_device_rc_special_get_command(
			self,
			FU_SYNAPTICS_MST_UPDC_CMD_CAL_EEPROM_CHECK_CRC16,
			(EEPROM_BANK_OFFSET * helper->bank_to_update),
			NULL,
			g_bytes_get_size(helper->fw),
			buf,
			sizeof(buf),
			error)) {
			g_prefix_error(error, "failed to get flash checksum: ");
			return FALSE;
		}
		flash_checksum = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	}
	if (helper->checksum != flash_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum 0x%x mismatched 0x%x",
			    flash_checksum,
			    helper->checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_panamera_set_new_valid_cb(FuDevice *device,
							 gpointer user_data,
							 GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuSynapticsMstDeviceHelper *helper = (FuSynapticsMstDeviceHelper *)user_data;
	guint8 buf[16] = {0x0};
	guint8 buf_verify[16] = {0x0};
	g_autoptr(GDateTime) dt = g_date_time_new_now_utc();

	buf[0] = helper->bank_to_update;
	buf[1] = g_date_time_get_month(dt);
	buf[2] = g_date_time_get_day_of_month(dt);
	buf[3] = g_date_time_get_year(dt) - 2000;
	buf[4] = (helper->checksum >> 8) & 0xff;
	buf[5] = helper->checksum & 0xff;
	buf[15] = fu_synaptics_mst_calculate_crc8(0, buf, sizeof(buf) - 1);
	g_debug("tag date %x %x %x crc %x %x %x %x",
		buf[1],
		buf[2],
		buf[3],
		buf[0],
		buf[4],
		buf[5],
		buf[15]);

	if (!fu_synaptics_mst_device_rc_set_command(
		self,
		FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
		(EEPROM_BANK_OFFSET * helper->bank_to_update + EEPROM_TAG_OFFSET),
		buf,
		sizeof(buf),
		error)) {
		g_prefix_error(error, "failed to write tag: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 1); /* ms */
	if (!fu_synaptics_mst_device_rc_get_command(
		self,
		FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_EEPROM,
		(EEPROM_BANK_OFFSET * helper->bank_to_update + EEPROM_TAG_OFFSET),
		buf_verify,
		sizeof(buf_verify),
		error)) {
		g_prefix_error(error, "failed to read tag: ");
		return FALSE;
	}
	if (memcmp(buf, buf_verify, sizeof(buf)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "set tag valid fail");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_panamera_set_old_invalid_cb(FuDevice *device,
							   gpointer user_data,
							   GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuSynapticsMstDeviceHelper *helper = (FuSynapticsMstDeviceHelper *)user_data;
	guint8 checksum_tmp = 0;
	guint8 checksum_nul = 0;

	/* CRC8 is not 0xff, erase last 4k of bank# */
	if (helper->checksum != 0xff) {
		guint32 erase_offset =
		    (EEPROM_BANK_OFFSET * self->active_bank + EEPROM_BANK_OFFSET - 0x1000) / 0x1000;
		g_debug("erasing offset 0x%x", erase_offset);
		if (!fu_synaptics_mst_device_set_flash_sector_erase(self,
								    FLASH_SECTOR_ERASE_4K,
								    erase_offset,
								    error))
			return FALSE;
	}

	/* set CRC8 to 0x00 */
	if (!fu_synaptics_mst_device_rc_set_command(
		self,
		FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
		(EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
		&checksum_nul,
		sizeof(checksum_nul),
		error)) {
		g_prefix_error(error, "failed to clear CRC: ");
		return FALSE;
	}
	if (!fu_synaptics_mst_device_rc_get_command(
		self,
		FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_EEPROM,
		(EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
		&checksum_tmp,
		sizeof(checksum_tmp),
		error)) {
		g_prefix_error(error, "failed to read CRC from flash: ");
		return FALSE;
	}
	if (checksum_tmp != checksum_nul) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "set tag invalid fail, got 0x%x and expected 0x%x",
			    checksum_tmp,
			    checksum_nul);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_panamera_firmware(FuSynapticsMstDevice *self,
						 GBytes *fw,
						 FuProgress *progress,
						 GError **error)
{
	guint32 fw_size = 0;
	guint8 checksum8 = 0;
	g_autoptr(FuSynapticsMstDeviceHelper) helper = fu_synaptics_mst_device_helper_new();

	/* get used bank */
	if (!fu_synaptics_mst_device_get_active_bank_panamera(self, error))
		return FALSE;
	if (self->active_bank == BANKTAG_1)
		helper->bank_to_update = BANKTAG_0;
	g_debug("bank to update:%x", helper->bank_to_update);

	/* get firmware size */
	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    0x400,
				    &fw_size,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	fw_size += 0x410;
	if (fw_size > PANAMERA_FIRMWARE_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid firmware size 0x%x",
			    fw_size);
		return FALSE;
	}

	/* current max firmware size is 104K */
	if (fw_size < g_bytes_get_size(fw))
		fw_size = PANAMERA_FIRMWARE_SIZE;
	g_debug("calculated fw size as %u", fw_size);

	helper->fw = fu_bytes_new_offset(fw, 0x0, fw_size, error);
	if (helper->fw == NULL)
		return FALSE;
	helper->checksum = fu_synaptics_mst_calculate_crc16(0,
							    g_bytes_get_data(helper->fw, NULL),
							    g_bytes_get_size(helper->fw));
	helper->progress = g_object_ref(progress);
	helper->chunks = fu_chunk_array_new_from_bytes(helper->fw,
						       EEPROM_BANK_OFFSET * helper->bank_to_update,
						       FU_CHUNK_PAGESZ_NONE,
						       BLOCK_UNIT);
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_synaptics_mst_device_update_panamera_firmware_cb,
				  MAX_RETRY_COUNTS,
				  2, /* ms */
				  helper,
				  error))
		return FALSE;

	/* set bank_to_update tag valid */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_synaptics_mst_device_update_panamera_set_new_valid_cb,
			     MAX_RETRY_COUNTS,
			     helper,
			     error))
		return FALSE;

	/* set active_bank tag invalid */
	if (!fu_synaptics_mst_device_rc_get_command(
		self,
		FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_EEPROM,
		(EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
		&checksum8,
		sizeof(checksum8),
		error)) {
		g_prefix_error(error, "failed to read tag from flash: ");
		return FALSE;
	}
	helper->checksum = checksum8;
	return fu_device_retry(FU_DEVICE(self),
			       fu_synaptics_mst_device_update_panamera_set_old_invalid_cb,
			       MAX_RETRY_COUNTS,
			       helper,
			       error);
}

static gboolean
fu_synaptics_mst_device_panamera_prepare_write(FuSynapticsMstDevice *self, GError **error)
{
	guint32 buf[4] = {0};

	/* Need to detect flash mode and ESM first ? */
	/* disable flash Quad mode and ESM/HDCP2.2*/

	/* disable ESM first */
	buf[0] = 0x21;
	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_MEMORY,
						    (gint)REG_ESM_DISABLE,
						    (guint8 *)buf,
						    4,
						    error)) {
		g_prefix_error(error, "ESM disable failed: ");
		return FALSE;
	}

	/* wait for ESM exit */
	fu_device_sleep(FU_DEVICE(self), 1); /* ms */

	/* disable QUAD mode */
	if (!fu_synaptics_mst_device_rc_get_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_MEMORY,
						    (gint)REG_QUAD_DISABLE,
						    (guint8 *)buf,
						    ((sizeof(buf) / sizeof(buf[0])) * 4),
						    error)) {
		g_prefix_error(error, "quad query failed: ");
		return FALSE;
	}

	buf[0] = 0x00;
	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_MEMORY,
						    (gint)REG_QUAD_DISABLE,
						    (guint8 *)buf,
						    4,
						    error)) {
		g_prefix_error(error, "quad disable failed: ");
		return FALSE;
	}

	/* disable HDCP2.2 */
	if (!fu_synaptics_mst_device_rc_get_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_MEMORY,
						    (gint)REG_HDCP22_DISABLE,
						    (guint8 *)buf,
						    4,
						    error)) {
		g_prefix_error(error, "HDCP query failed: ");
		return FALSE;
	}

	buf[0] = buf[0] & (~BIT(2));
	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_MEMORY,
						    (gint)REG_HDCP22_DISABLE,
						    (guint8 *)buf,
						    4,
						    error)) {
		g_prefix_error(error, "HDCP disable failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_firmware_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuSynapticsMstDeviceHelper *helper = (FuSynapticsMstDeviceHelper *)user_data;
	guint32 flash_checksum;
	guint8 buf[4] = {0};

	if (!fu_synaptics_mst_device_set_flash_sector_erase(self, 0xffff, 0, error))
		return FALSE;
	g_debug("waiting for flash clear to settle");
	fu_device_sleep(FU_DEVICE(self), FLASH_SETTLE_TIME);

	fu_progress_set_steps(helper->progress, fu_chunk_array_length(helper->chunks));
	for (guint i = 0; i < fu_chunk_array_length(helper->chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GError) error_local = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(helper->chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_synaptics_mst_device_rc_set_command(
			self,
			FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
			fu_chunk_get_address(chk),
			fu_chunk_get_data(chk),
			fu_chunk_get_data_sz(chk),
			&error_local)) {
			g_warning("failed to write flash offset 0x%04x: %s, retrying",
				  (guint)fu_chunk_get_address(chk),
				  error_local->message);
			/* repeat once */
			if (!fu_synaptics_mst_device_rc_set_command(
				self,
				FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_EEPROM,
				fu_chunk_get_address(chk),
				fu_chunk_get_data(chk),
				fu_chunk_get_data_sz(chk),
				error)) {
				g_prefix_error(error,
					       "can't write flash offset 0x%04x: ",
					       (guint)fu_chunk_get_address(chk));
				return FALSE;
			}
		}
		fu_progress_step_done(helper->progress);
	}

	/* no need to check CRC here, it will be done in Activate Firmware command */
	if (self->family == FU_SYNAPTICS_MST_FAMILY_CARRERA)
		return TRUE;

	/* verify CRC */
	if (!fu_synaptics_mst_device_rc_special_get_command(
		self,
		FU_SYNAPTICS_MST_UPDC_CMD_CAL_EEPROM_CHECK_CRC16,
		0,
		NULL,
		g_bytes_get_size(helper->fw),
		buf,
		sizeof(buf),
		error)) {
		g_prefix_error(error, "Failed to get flash checksum: ");
		return FALSE;
	}
	flash_checksum = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	if (helper->checksum != flash_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum 0x%x mismatched 0x%x",
			    flash_checksum,
			    helper->checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_firmware(FuSynapticsMstDevice *self,
					GBytes *fw,
					FuProgress *progress,
					GError **error)
{
	g_autoptr(FuSynapticsMstDeviceHelper) helper = fu_synaptics_mst_device_helper_new();

	guint32 fw_size = 0;
	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		fw_size = PANAMERA_FIRMWARE_SIZE;
		break;
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
		fw_size = CAYENNE_FIRMWARE_SIZE;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CARRERA:
		fw_size = CARRERA_FIRMWARE_SIZE;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unsupported chip family");
		return FALSE;
	}

	helper->fw = fu_bytes_new_offset(fw, 0x0, fw_size, error);
	if (helper->fw == NULL)
		return FALSE;
	helper->checksum = fu_synaptics_mst_calculate_crc16(0,
							    g_bytes_get_data(helper->fw, NULL),
							    g_bytes_get_size(helper->fw));
	helper->progress = g_object_ref(progress);
	helper->chunks = fu_chunk_array_new_from_bytes(helper->fw,
						       FU_CHUNK_ADDR_OFFSET_NONE,
						       FU_CHUNK_PAGESZ_NONE,
						       BLOCK_UNIT);
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_synaptics_mst_device_update_firmware_cb,
			     MAX_RETRY_COUNTS,
			     helper,
			     error))
		return FALSE;

	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_ACTIVATE_FIRMWARE,
						    0,
						    NULL,
						    0,
						    error)) {
		g_prefix_error(error, "active firmware failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_restart(FuSynapticsMstDevice *self, GError **error)
{
	guint8 buf[4] = {0xF5, 0, 0, 0};
	gint offset;
	g_autoptr(GError) error_local = NULL;

	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		offset = 0x2000FC;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
		offset = 0x2020021C;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CARRERA:
		offset = 0x2020A024;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unsupported chip family");
		return FALSE;
	}
	/* issue the reboot command, ignore return code (triggers before returning) */
	if (!fu_synaptics_mst_device_rc_set_command(self,
						    FU_SYNAPTICS_MST_UPDC_CMD_WRITE_TO_MEMORY,
						    offset,
						    buf,
						    sizeof(buf),
						    &error_local))
		g_debug("failed to restart: %s", error_local->message);

	return TRUE;
}

static FuFirmware *
fu_synaptics_mst_device_prepare_firmware(FuDevice *device,
					 GInputStream *stream,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_synaptics_mst_firmware_new();

	/* set chip family to use correct board ID offset */
	fu_synaptics_mst_firmware_set_family(FU_SYNAPTICS_MST_FIRMWARE(firmware), self->family);

	/* check firmware and board ID match */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0 &&
	    !fu_device_has_private_flag(device, FU_SYNAPTICS_MST_DEVICE_FLAG_IGNORE_BOARD_ID)) {
		guint16 board_id =
		    fu_synaptics_mst_firmware_get_board_id(FU_SYNAPTICS_MST_FIRMWARE(firmware));
		if (board_id != self->board_id) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "board ID mismatch, got 0x%04x, expected 0x%04x",
				    board_id,
				    self->board_id);
			return NULL;
		}
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_mst_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* some devices do not use a GPIO to reset the chip */
	if (fu_device_has_private_flag(device,
				       FU_SYNAPTICS_MST_DEVICE_FLAG_MANUAL_RESTART_REQUIRED)) {
		g_autoptr(FwupdRequest) request = fwupd_request_new();
		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REPLUG_POWER);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		if (!fu_device_emit_request(device, request, progress, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, NULL);

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* enable remote control and disable on exit */
	if (!fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART)) {
		locker =
		    fu_device_locker_new_full(self,
					      (FuDeviceLockerFunc)fu_synaptics_mst_device_enable_rc,
					      (FuDeviceLockerFunc)fu_synaptics_mst_device_restart,
					      error);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_device_set_remove_delay(FU_DEVICE(self), 10000); /* a long time */
	} else {
		locker = fu_device_locker_new_full(
		    self,
		    (FuDeviceLockerFunc)fu_synaptics_mst_device_enable_rc,
		    (FuDeviceLockerFunc)fu_synaptics_mst_device_disable_rc,
		    error);
	}
	if (locker == NULL)
		return FALSE;

	/* update firmware */
	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
		if (!fu_synaptics_mst_device_update_tesla_leaf_firmware(self,
									fw,
									progress,
									error)) {
			g_prefix_error(error, "Firmware update failed: ");
			return FALSE;
		}
		break;
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		if (!fu_synaptics_mst_device_panamera_prepare_write(self, error)) {
			g_prefix_error(error, "Failed to prepare for write: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_esm(self, fw, progress, error)) {
			g_prefix_error(error, "ESM update failed: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_panamera_firmware(self, fw, progress, error)) {
			g_prefix_error(error, "Firmware update failed: ");
			return FALSE;
		}
		break;
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
	case FU_SYNAPTICS_MST_FAMILY_CARRERA:
		if (!fu_synaptics_mst_device_update_firmware(self, fw, progress, error)) {
			g_prefix_error(error, "Firmware update failed: ");
			return FALSE;
		}
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unsupported chip family");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* wait for flash clear to settle */
	fu_device_sleep_full(device, 2000, fu_progress_get_child(progress)); /* ms */
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_ensure_board_id(FuSynapticsMstDevice *self, GError **error)
{
	gint offset;
	guint8 buf[4] = {0x0};

	/* in test mode we need to open a different file node instead */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_SYNAPTICS_MST_DEVICE_FLAG_IS_SOMEWHAT_EMULATED)) {
		g_autofree gchar *filename = NULL;
		g_autofree gchar *dirname = NULL;
		gboolean exists_eeprom = FALSE;
		gint fd;
		dirname = g_path_get_dirname(fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)));
		filename = g_strdup_printf("%s/remote/%s_eeprom",
					   dirname,
					   fu_device_get_logical_id(FU_DEVICE(self)));
		if (!fu_device_query_file_exists(FU_DEVICE(self), filename, &exists_eeprom, error))
			return FALSE;
		if (!exists_eeprom) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no device exists %s",
				    filename);
			return FALSE;
		}
		fd = open(filename, O_RDONLY);
		if (fd == -1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_PERMISSION_DENIED,
				    "cannot open device %s",
				    filename);
			return FALSE;
		}
		if (read(fd, buf, 2) != 2) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "error reading EEPROM file %s",
				    filename);
			close(fd);
			return FALSE;
		}
		self->board_id = fu_memread_uint16(buf, G_BIG_ENDIAN);
		close(fd);
		return TRUE;
	}

	if (self->family == FU_SYNAPTICS_MST_FAMILY_CARRERA) {
		/* get ID via RC command */
		if (!fu_synaptics_mst_device_rc_get_command(self,
							    FU_SYNAPTICS_MST_UPDC_CMD_GET_ID,
							    0,
							    buf,
							    sizeof(buf),
							    error)) {
			g_prefix_error(error, "RC command failed: ");
			return FALSE;
		}
		if (!fu_memread_uint16_safe(buf,
					    sizeof(buf),
					    0x2,
					    &self->board_id,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
	} else {
		/* older chip reads customer&board ID from memory */
		switch (self->family) {
		case FU_SYNAPTICS_MST_FAMILY_TESLA:
		case FU_SYNAPTICS_MST_FAMILY_LEAF:
		case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
			offset = (gint)ADDR_MEMORY_CUSTOMER_ID;
			break;
		case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
			offset = (gint)ADDR_MEMORY_CUSTOMER_ID_CAYENNE;
			break;
		case FU_SYNAPTICS_MST_FAMILY_SPYDER:
			offset = (gint)ADDR_MEMORY_CUSTOMER_ID_SPYDER;
			break;
		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Unsupported chip family");
			return FALSE;
		}

		if (!fu_synaptics_mst_device_rc_get_command(
			self,
			FU_SYNAPTICS_MST_UPDC_CMD_READ_FROM_MEMORY,
			offset,
			buf,
			sizeof(buf),
			error)) {
			g_prefix_error(error, "memory query failed: ");
			return FALSE;
		}
		if (!fu_memread_uint16_safe(buf,
					    sizeof(buf),
					    0x0,
					    &self->board_id,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_setup(FuDevice *device, GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	FuDevice *parent;
	const gchar *name_family;
	const gchar *name_parent = NULL;
	guint8 buf_ver[3] = {0x0};
	guint8 buf_cid[2] = {0x0};
	guint8 rc_cap = 0x0;
	g_autofree gchar *guid0 = NULL;
	g_autofree gchar *guid1 = NULL;
	g_autofree gchar *guid2 = NULL;
	g_autofree gchar *guid3 = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* read support for remote control */
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  FU_SYNAPTICS_MST_REG_RC_CAP,
				  &rc_cap,
				  sizeof(rc_cap),
				  FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read remote control capabilities: ");
		return FALSE;
	}
	if ((rc_cap & 0x04) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no support for remote control");
		return FALSE;
	}

	/* enable remote control and disable on exit */
	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_synaptics_mst_device_enable_rc,
					   (FuDeviceLockerFunc)fu_synaptics_mst_device_disable_rc,
					   &error_local);
	if (locker == NULL) {
		if (g_strcmp0(fu_device_get_name(device), "DPMST") == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "downstream endpoint not supported");
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
		}
		return FALSE;
	}

	/* read firmware version: the third byte is vendor-specific usage */
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  REG_FIRMWARE_VERSION,
				  buf_ver,
				  sizeof(buf_ver),
				  FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read firmware version: ");
		return FALSE;
	}

	version = g_strdup_printf("%1d.%02d.%02d", buf_ver[0], buf_ver[1], buf_ver[2]);
	fu_device_set_version(FU_DEVICE(self), version);

	/* read board chip_id */
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  REG_CHIP_ID,
				  buf_cid,
				  sizeof(buf_cid),
				  FU_SYNAPTICS_MST_DEVICE_READ_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed to read chip id: ");
		return FALSE;
	}
	self->chip_id = fu_memread_uint16(buf_cid, G_BIG_ENDIAN);
	if (self->chip_id == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid chip ID");
		return FALSE;
	}
	self->family = fu_synaptics_mst_family_from_chip_id(self->chip_id);

	/* VMM >= 6 use RSA2048 */
	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		break;
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
	case FU_SYNAPTICS_MST_FAMILY_CARRERA:
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
		break;
	default:
		g_warning("family 0x%02x does not indicate unsigned/signed payload", self->family);
		break;
	}

	/* check the active bank for debugging */
	if (self->family == FU_SYNAPTICS_MST_FAMILY_PANAMERA) {
		if (!fu_synaptics_mst_device_get_active_bank_panamera(self, error))
			return FALSE;
	}

	/* read board ID */
	if (!fu_synaptics_mst_device_ensure_board_id(self, error))
		return FALSE;

	parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent != NULL)
		name_parent = fu_device_get_name(parent);
	if (name_parent != NULL) {
		name = g_strdup_printf("VMM%04x inside %s", self->chip_id, name_parent);
	} else {
		name = g_strdup_printf("VMM%04x", self->chip_id);
	}
	fu_device_set_name(FU_DEVICE(self), name);

	/* set up the device name and kind via quirks */
	guid0 = g_strdup_printf("MST-%u", self->board_id);
	fu_device_add_instance_id(FU_DEVICE(self), guid0);

	/* requires user to do something */
	if (fu_device_has_private_flag(device,
				       FU_SYNAPTICS_MST_DEVICE_FLAG_MANUAL_RESTART_REQUIRED)) {
		fu_device_set_remove_delay(device, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	}

	/* this is a host system, use system ID */
	name_family = fu_synaptics_mst_family_to_string(self->family);
	if (g_strcmp0(self->device_kind, "system") == 0) {
		FuContext *ctx = fu_device_get_context(device);
		const gchar *system_type = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_SKU);
		g_autofree gchar *guid =
		    g_strdup_printf("MST-%s-%s-%u", name_family, system_type, self->board_id);
		fu_device_add_instance_id(FU_DEVICE(self), guid);

		/* docks or something else */
	} else if (self->device_kind != NULL) {
		g_auto(GStrv) templates = NULL;
		templates = g_strsplit(self->device_kind, ",", -1);
		for (guint i = 0; templates[i] != NULL; i++) {
			g_autofree gchar *dock_id1 = NULL;
			g_autofree gchar *dock_id2 = NULL;
			dock_id1 = g_strdup_printf("MST-%s-%u", templates[i], self->board_id);
			fu_device_add_instance_id(FU_DEVICE(self), dock_id1);
			dock_id2 = g_strdup_printf("MST-%s-vmm%04x-%u",
						   templates[i],
						   self->chip_id,
						   self->board_id);
			fu_device_add_instance_id(FU_DEVICE(self), dock_id2);
		}
	} else {
		/* devices are explicit opt-in */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "ignoring %s device with no SynapticsMstDeviceKind quirk",
			    guid0);
		return FALSE;
	}

	/* detect chip family */
	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
	case FU_SYNAPTICS_MST_FAMILY_CARRERA:
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	default:
		break;
	}

	/* add non-standard GUIDs */
	guid1 = g_strdup_printf("MST-%s-vmm%04x-%u", name_family, self->chip_id, self->board_id);
	fu_device_add_instance_id(FU_DEVICE(self), guid1);
	guid2 = g_strdup_printf("MST-%s-%u", name_family, self->board_id);
	fu_device_add_instance_id(FU_DEVICE(self), guid2);
	guid3 = g_strdup_printf("MST-%s", name_family);
	fu_device_add_instance_id_full(FU_DEVICE(self), guid3, FU_DEVICE_INSTANCE_FLAG_QUIRKS);

	/* whitebox customers */
	if ((self->board_id >> 8) == 0x0)
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_set_quirk_kv(FuDevice *device,
				     const gchar *key,
				     const gchar *value,
				     GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	if (g_strcmp0(key, "SynapticsMstDeviceKind") == 0) {
		self->device_kind = g_strdup(value);
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_synaptics_mst_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 45, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 54, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_synaptics_mst_device_class_init(FuSynapticsMstDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_synaptics_mst_device_finalize;
	device_class->to_string = fu_synaptics_mst_device_to_string;
	device_class->set_quirk_kv = fu_synaptics_mst_device_set_quirk_kv;
	device_class->setup = fu_synaptics_mst_device_setup;
	device_class->write_firmware = fu_synaptics_mst_device_write_firmware;
	device_class->attach = fu_synaptics_mst_device_attach;
	device_class->prepare_firmware = fu_synaptics_mst_device_prepare_firmware;
	device_class->set_progress = fu_synaptics_mst_device_set_progress;
}

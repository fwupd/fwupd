/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2018 Ryan Chang <ryan.chang@synaptics.com>
 * Copyright (C) 2021 Apollo Ling <apollo.ling@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>

#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-connection.h"
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

#define CAYENNE_FIRMWARE_SIZE 0x50000 /* bytes */
#define PANAMERA_FIRMWARE_SIZE 0x1A000 /* bytes */

/**
 * FU_SYNAPTICS_MST_DEVICE_FLAG_IGNORE_BOARD_ID:
 *
 * Ignore board ID firmware mismatch.
 */
#define FU_SYNAPTICS_MST_DEVICE_FLAG_IGNORE_BOARD_ID (1 << 0)

struct _FuSynapticsMstDevice {
	FuUdevDevice parent_instance;
	gchar *device_kind;
	gchar *system_type;
	guint64 write_block_size;
	FuSynapticsMstFamily family;
	FuSynapticsMstMode mode;
	guint8 active_bank;
	guint8 layer;
	guint16 rad; /* relative address */
	guint32 board_id;
	guint16 chip_id;
};

G_DEFINE_TYPE(FuSynapticsMstDevice, fu_synaptics_mst_device, FU_TYPE_UDEV_DEVICE)

static void
fu_synaptics_mst_device_finalize(GObject *object)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(object);

	g_free(self->device_kind);
	g_free(self->system_type);

	G_OBJECT_CLASS(fu_synaptics_mst_device_parent_class)->finalize(object);
}

static void
fu_synaptics_mst_device_udev_device_notify_cb(FuUdevDevice *udev_device,
					      GParamSpec *pspec,
					      gpointer user_data)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(user_data);
	if (fu_udev_device_get_dev(FU_UDEV_DEVICE(self)) != NULL) {
		fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
					 FU_UDEV_DEVICE_FLAG_OPEN_READ |
					     FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
					     FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
	} else {
		fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
					 FU_UDEV_DEVICE_FLAG_OPEN_READ |
					     FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
	}
}

static void
fu_synaptics_mst_device_init(FuSynapticsMstDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.mst");
	fu_device_set_vendor(FU_DEVICE(self), "Synaptics");
	fu_device_add_vendor_id(FU_DEVICE(self), "DRM_DP_AUX_DEV:0x06CB");
	fu_device_set_summary(FU_DEVICE(self), "Multi-stream transport device");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_SYNAPTICS_MST_DEVICE_FLAG_IGNORE_BOARD_ID,
					"ignore-board-id");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_PROBE_COMPLETE);

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

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_synaptics_mst_device_parent_class)->to_string(device, idt, str);

	fu_string_append(str, idt, "DeviceKind", self->device_kind);
	if (self->mode != FU_SYNAPTICS_MST_MODE_UNKNOWN) {
		fu_string_append(str, idt, "Mode", fu_synaptics_mst_mode_to_string(self->mode));
	}
	if (self->family == FU_SYNAPTICS_MST_FAMILY_PANAMERA)
		fu_string_append_kx(str, idt, "ActiveBank", self->active_bank);
	fu_string_append_kx(str, idt, "Layer", self->layer);
	fu_string_append_kx(str, idt, "Rad", self->rad);
	if (self->board_id != 0x0)
		fu_string_append_ku(str, idt, "BoardId", self->board_id);
	if (self->chip_id != 0x0)
		fu_string_append_kx(str, idt, "ChipId", self->chip_id);
}

static gboolean
fu_synaptics_mst_device_enable_rc(FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	/* in test mode */
	if (fu_udev_device_get_dev(FU_UDEV_DEVICE(self)) == NULL)
		return TRUE;

	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						     self->layer,
						     self->rad);
	return fu_synaptics_mst_connection_enable_rc(connection, error);
}

static gboolean
fu_synaptics_mst_device_disable_rc(FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	/* in test mode */
	if (fu_udev_device_get_dev(FU_UDEV_DEVICE(self)) == NULL)
		return TRUE;

	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						     self->layer,
						     self->rad);
	return fu_synaptics_mst_connection_disable_rc(connection, error);
}

static gboolean
fu_synaptics_mst_device_probe(FuDevice *device, GError **error)
{
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_synaptics_mst_device_parent_class)->probe(device, error))
		return FALSE;

	/* get from sysfs if not set from tests */
	if (fu_device_get_logical_id(device) == NULL) {
		g_autofree gchar *logical_id = NULL;
		logical_id =
		    g_path_get_basename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
		fu_device_set_logical_id(device, logical_id);
	}
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "pci,drm_dp_aux_dev", error);
}

static gboolean
fu_synaptics_mst_device_get_flash_checksum(FuSynapticsMstDevice *self,
					   guint32 offset,
					   guint32 length,
					   guint32 *checksum,
					   GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	g_return_val_if_fail(length > 0, FALSE);

	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						     self->layer,
						     self->rad);
	if (!fu_synaptics_mst_connection_rc_special_get_command(connection,
								UPDC_CAL_EEPROM_CHECKSUM,
								offset,
								NULL,
								length,
								(guint8 *)checksum,
								4,
								error)) {
		g_prefix_error(error, "failed to get flash checksum: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_set_flash_sector_erase(FuSynapticsMstDevice *self,
					       guint16 rc_cmd,
					       guint16 offset,
					       GError **error)
{
	guint16 us_data;
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						     self->layer,
						     self->rad);
	/* Need to add Wp control ? */
	us_data = rc_cmd + offset;

	if (!fu_synaptics_mst_connection_rc_set_command(connection,
							UPDC_FLASH_ERASE,
							0,
							(guint8 *)&us_data,
							2,
							error)) {
		g_prefix_error(error, "can't sector erase flash at offset %x: ", offset);
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	FuSynapticsMstConnection *connection;
	GBytes *fw;
	GPtrArray *chunks;
	FuProgress *progress;
	guint8 bank_to_update;
	guint32 checksum;
} FuSynapticsMstDeviceHelper;

static void
fu_synaptics_mst_device_helper_free(FuSynapticsMstDeviceHelper *helper)
{
	if (helper->chunks != NULL)
		g_ptr_array_unref(helper->chunks);
	if (helper->fw != NULL)
		g_bytes_unref(helper->fw);
	if (helper->connection != NULL)
		g_object_unref(helper->connection);
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
	fu_progress_set_steps(helper->progress, helper->chunks->len);
	for (guint i = 0; i < helper->chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(helper->chunks, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
								UPDC_WRITE_TO_EEPROM,
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
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
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

	helper->connection =
	    fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
					    self->layer,
					    self->rad);
	helper->progress = g_object_ref(progress);
	helper->chunks =
	    fu_chunk_array_new_from_bytes(helper->fw, EEPROM_ESM_OFFSET, 0x0, BLOCK_UNIT);
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

	fu_progress_set_steps(helper->progress, helper->chunks->len);
	for (guint i = 0; i < helper->chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(helper->chunks, i);
		g_autoptr(GError) error_local = NULL;

		if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
								UPDC_WRITE_TO_EEPROM,
								fu_chunk_get_address(chk),
								fu_chunk_get_data(chk),
								fu_chunk_get_data_sz(chk),
								&error_local)) {
			g_warning("Failed to write flash offset 0x%04x: %s, retrying",
				  fu_chunk_get_address(chk),
				  error_local->message);
			/* repeat once */
			if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
									UPDC_WRITE_TO_EEPROM,
									fu_chunk_get_address(chk),
									fu_chunk_get_data(chk),
									fu_chunk_get_data_sz(chk),
									error)) {
				g_prefix_error(error,
					       "can't write flash offset 0x%04x: ",
					       fu_chunk_get_address(chk));
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
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
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

	helper->connection =
	    fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
					    self->layer,
					    self->rad);
	helper->fw = g_bytes_ref(fw);
	helper->checksum = fu_sum32_bytes(fw);
	helper->progress = g_object_ref(progress);
	helper->chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, BLOCK_UNIT);
	return fu_device_retry(FU_DEVICE(self),
			       fu_synaptics_mst_device_update_tesla_leaf_firmware_cb,
			       MAX_RETRY_COUNTS,
			       helper,
			       error);
}

static gboolean
fu_synaptics_mst_device_get_active_bank_panamera(FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;
	guint32 buf[16];

	/* get used bank */
	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						     self->layer,
						     self->rad);
	if (!fu_synaptics_mst_connection_rc_get_command(connection,
							UPDC_READ_FROM_MEMORY,
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
	guint32 flash_checksum = 0;

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
	fu_progress_set_steps(helper->progress, helper->chunks->len);
	for (guint i = 0; i < helper->chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(helper->chunks, i);
		g_autoptr(GError) error_local = NULL;
		if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
								UPDC_WRITE_TO_EEPROM,
								fu_chunk_get_address(chk),
								fu_chunk_get_data(chk),
								fu_chunk_get_data_sz(chk),
								&error_local)) {
			g_warning("write failed: %s, retrying", error_local->message);
			/* repeat once */
			if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
									UPDC_WRITE_TO_EEPROM,
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
		fu_device_sleep(FU_DEVICE(self), 1); /* wait crc calculation */
		if (!fu_synaptics_mst_connection_rc_special_get_command(
			helper->connection,
			UPDC_CAL_EEPROM_CHECK_CRC16,
			(EEPROM_BANK_OFFSET * helper->bank_to_update),
			NULL,
			g_bytes_get_size(helper->fw),
			(guint8 *)(&flash_checksum),
			sizeof(flash_checksum),
			error)) {
			g_prefix_error(error, "failed to get flash checksum: ");
			return FALSE;
		}
	}
	if (helper->checksum != flash_checksum) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
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

	if (!fu_synaptics_mst_connection_rc_set_command(
		helper->connection,
		UPDC_WRITE_TO_EEPROM,
		(EEPROM_BANK_OFFSET * helper->bank_to_update + EEPROM_TAG_OFFSET),
		buf,
		sizeof(buf),
		error)) {
		g_prefix_error(error, "failed to write tag: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 1); /* ms */
	if (!fu_synaptics_mst_connection_rc_get_command(
		helper->connection,
		UPDC_READ_FROM_EEPROM,
		(EEPROM_BANK_OFFSET * helper->bank_to_update + EEPROM_TAG_OFFSET),
		buf_verify,
		sizeof(buf_verify),
		error)) {
		g_prefix_error(error, "failed to read tag: ");
		return FALSE;
	}
	if (memcmp(buf, buf_verify, sizeof(buf)) != 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
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
	if (!fu_synaptics_mst_connection_rc_set_command(
		helper->connection,
		UPDC_WRITE_TO_EEPROM,
		(EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
		&checksum_nul,
		sizeof(checksum_nul),
		error)) {
		g_prefix_error(error, "failed to clear CRC: ");
		return FALSE;
	}
	if (!fu_synaptics_mst_connection_rc_get_command(
		helper->connection,
		UPDC_READ_FROM_EEPROM,
		(EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
		&checksum_tmp,
		sizeof(checksum_tmp),
		error)) {
		g_prefix_error(error, "failed to read CRC from flash: ");
		return FALSE;
	}
	if (checksum_tmp != checksum_nul) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
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
	g_autoptr(GDateTime) dt = g_date_time_new_now_utc();

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
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid firmware size 0x%x",
			    fw_size);
		return FALSE;
	}

	/* current max firmware size is 104K */
	if (fw_size < g_bytes_get_size(fw))
		fw_size = PANAMERA_FIRMWARE_SIZE;
	g_debug("calculated fw size as %u", fw_size);

	helper->connection =
	    fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
					    self->layer,
					    self->rad);
	helper->fw = fu_bytes_new_offset(fw, 0x0, fw_size, error);
	if (helper->fw == NULL)
		return FALSE;
	helper->checksum = fu_synaptics_mst_calculate_crc16(0,
							    g_bytes_get_data(helper->fw, NULL),
							    g_bytes_get_size(helper->fw));
	helper->progress = g_object_ref(progress);
	helper->chunks = fu_chunk_array_new_from_bytes(helper->fw,
						       EEPROM_BANK_OFFSET * helper->bank_to_update,
						       0x0,
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
	if (!fu_synaptics_mst_connection_rc_get_command(
		helper->connection,
		UPDC_READ_FROM_EEPROM,
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
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	/* Need to detect flash mode and ESM first ? */
	/* disable flash Quad mode and ESM/HDCP2.2*/
	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						     self->layer,
						     self->rad);

	/* disable ESM first */
	buf[0] = 0x21;
	if (!fu_synaptics_mst_connection_rc_set_command(connection,
							UPDC_WRITE_TO_MEMORY,
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
	if (!fu_synaptics_mst_connection_rc_get_command(connection,
							UPDC_READ_FROM_MEMORY,
							(gint)REG_QUAD_DISABLE,
							(guint8 *)buf,
							((sizeof(buf) / sizeof(buf[0])) * 4),
							error)) {
		g_prefix_error(error, "quad query failed: ");
		return FALSE;
	}

	buf[0] = 0x00;
	if (!fu_synaptics_mst_connection_rc_set_command(connection,
							UPDC_WRITE_TO_MEMORY,
							(gint)REG_QUAD_DISABLE,
							(guint8 *)buf,
							4,
							error)) {
		g_prefix_error(error, "quad disable failed: ");
		return FALSE;
	}

	/* disable HDCP2.2 */
	if (!fu_synaptics_mst_connection_rc_get_command(connection,
							UPDC_READ_FROM_MEMORY,
							(gint)REG_HDCP22_DISABLE,
							(guint8 *)buf,
							4,
							error)) {
		g_prefix_error(error, "HDCP query failed: ");
		return FALSE;
	}

	buf[0] = buf[0] & (~BIT(2));
	if (!fu_synaptics_mst_connection_rc_set_command(connection,
							UPDC_WRITE_TO_MEMORY,
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
fu_synaptics_mst_device_update_cayenne_firmware_cb(FuDevice *device,
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

	fu_progress_set_steps(helper->progress, helper->chunks->len);
	for (guint i = 0; i < helper->chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(helper->chunks, i);
		g_autoptr(GError) error_local = NULL;

		if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
								UPDC_WRITE_TO_EEPROM,
								fu_chunk_get_address(chk),
								fu_chunk_get_data(chk),
								fu_chunk_get_data_sz(chk),
								&error_local)) {
			g_warning("Failed to write flash offset 0x%04x: %s, retrying",
				  fu_chunk_get_address(chk),
				  error_local->message);
			/* repeat once */
			if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
									UPDC_WRITE_TO_EEPROM,
									fu_chunk_get_address(chk),
									fu_chunk_get_data(chk),
									fu_chunk_get_data_sz(chk),
									error)) {
				g_prefix_error(error,
					       "can't write flash offset 0x%04x: ",
					       fu_chunk_get_address(chk));
				return FALSE;
			}
		}
		fu_progress_step_done(helper->progress);
	}

	/* verify CRC */
	if (!fu_synaptics_mst_connection_rc_special_get_command(helper->connection,
								UPDC_CAL_EEPROM_CHECK_CRC16,
								0,
								NULL,
								g_bytes_get_size(helper->fw),
								(guint8 *)(&flash_checksum),
								sizeof(flash_checksum),
								error)) {
		g_prefix_error(error, "Failed to get flash checksum: ");
		return FALSE;
	}
	if (helper->checksum != flash_checksum) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "checksum 0x%x mismatched 0x%x",
			    flash_checksum,
			    helper->checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_cayenne_firmware(FuSynapticsMstDevice *self,
						GBytes *fw,
						FuProgress *progress,
						GError **error)
{
	g_autoptr(FuSynapticsMstDeviceHelper) helper = fu_synaptics_mst_device_helper_new();

	helper->connection =
	    fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
					    self->layer,
					    self->rad);
	helper->fw = fu_bytes_new_offset(fw, 0x0, CAYENNE_FIRMWARE_SIZE, error);
	if (helper->fw == NULL)
		return FALSE;
	helper->checksum = fu_synaptics_mst_calculate_crc16(0,
							    g_bytes_get_data(helper->fw, NULL),
							    g_bytes_get_size(helper->fw));
	helper->progress = g_object_ref(progress);
	helper->chunks = fu_chunk_array_new_from_bytes(helper->fw, 0x0, 0x0, BLOCK_UNIT);
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_synaptics_mst_device_update_cayenne_firmware_cb,
			     MAX_RETRY_COUNTS,
			     helper,
			     error))
		return FALSE;

	if (!fu_synaptics_mst_connection_rc_set_command(helper->connection,
							UPDC_ACTIVATE_FIRMWARE,
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
	g_autoptr(FuSynapticsMstConnection) connection = NULL;
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
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unsupported chip family");
		return FALSE;
	}
	/* issue the reboot command, ignore return code (triggers before returning) */
	connection = fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						     self->layer,
						     self->rad);
	if (!fu_synaptics_mst_connection_rc_set_command(connection,
							UPDC_WRITE_TO_MEMORY,
							offset,
							(guint8 *)&buf,
							4,
							&error_local))
		g_debug("failed to restart: %s", error_local->message);

	return TRUE;
}

static FuFirmware *
fu_synaptics_mst_device_prepare_firmware(FuDevice *device,
					 GBytes *fw,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_synaptics_mst_firmware_new();

	/* check firmware and board ID match */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0 &&
	    !fu_device_has_private_flag(device, FU_SYNAPTICS_MST_DEVICE_FLAG_IGNORE_BOARD_ID)) {
		guint16 board_id =
		    fu_synaptics_mst_firmware_get_board_id(FU_SYNAPTICS_MST_FIRMWARE(firmware));
		if (board_id != self->board_id) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "board ID mismatch, got 0x%04x, expected 0x%04x",
				    board_id,
				    self->board_id);
			return NULL;
		}
	}
	return fu_firmware_new_from_bytes(fw);
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
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART)) {
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
		if (!fu_synaptics_mst_device_update_cayenne_firmware(self, fw, progress, error)) {
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

FuSynapticsMstDevice *
fu_synaptics_mst_device_new(FuUdevDevice *device)
{
	FuSynapticsMstDevice *self = g_object_new(FU_TYPE_SYNAPTICS_MST_DEVICE, NULL);
	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device));
	return self;
}

static gboolean
fu_synaptics_mst_device_read_board_id(FuSynapticsMstDevice *self,
				      FuSynapticsMstConnection *connection,
				      guint8 *byte,
				      GError **error)
{
	gint offset;

	/* in test mode we need to open a different file node instead */
	if (fu_udev_device_get_dev(FU_UDEV_DEVICE(self)) == NULL) {
		g_autofree gchar *filename = NULL;
		g_autofree gchar *dirname = NULL;
		gint fd;
		dirname = g_path_get_dirname(fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)));
		filename = g_strdup_printf("%s/remote/%s_eeprom",
					   dirname,
					   fu_device_get_logical_id(FU_DEVICE(self)));
		if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "no device exists %s",
				    filename);
			return FALSE;
		}
		fd = open(filename, O_RDONLY);
		if (fd == -1) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_PERMISSION_DENIED,
				    "cannot open device %s",
				    filename);
			return FALSE;
		}
		if (read(fd, byte, 2) != 2) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "error reading EEPROM file %s",
				    filename);
			close(fd);
			return FALSE;
		}
		close(fd);
		return TRUE;
	}

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

	/* get board ID via MCU address 0x170E instead of flash access due to HDCP2.2 running */
	if (!fu_synaptics_mst_connection_rc_get_command(connection,
							UPDC_READ_FROM_MEMORY,
							offset,
							byte,
							2,
							error)) {
		g_prefix_error(error, "Memory query failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_scan_cascade(FuSynapticsMstDevice *self, guint8 layer, GError **error)
{
	/* in test mode we skip this */
	if (fu_udev_device_get_dev(FU_UDEV_DEVICE(self)) == NULL)
		return TRUE;

	/* test each relative address in this layer */
	for (guint16 rad = 0; rad <= 2; rad++) {
		guint8 byte[4];
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(FuSynapticsMstConnection) connection = NULL;
		g_autoptr(FuSynapticsMstDevice) device_tmp = NULL;
		g_autoptr(GError) error_local = NULL;

		/* enable remote control and disable on exit */
		device_tmp = fu_synaptics_mst_device_new(FU_UDEV_DEVICE(self));
		device_tmp->layer = layer;
		device_tmp->rad = rad;
		locker = fu_device_locker_new_full(
		    device_tmp,
		    (FuDeviceLockerFunc)fu_synaptics_mst_device_enable_rc,
		    (FuDeviceLockerFunc)fu_synaptics_mst_device_disable_rc,
		    &error_local);
		if (locker == NULL) {
			g_debug("no cascade device found: %s", error_local->message);
			continue;
		}
		connection =
		    fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)),
						    layer + 1,
						    rad);
		if (!fu_synaptics_mst_connection_read(connection,
						      REG_RC_CAP,
						      byte,
						      1,
						      &error_local)) {
			g_debug("no valid cascade device: %s", error_local->message);
			continue;
		}

		/* check recursively for more devices */
		if (!fu_device_locker_close(locker, &error_local)) {
			g_debug("failed to close parent: %s", error_local->message);
			continue;
		}
		self->mode = FU_SYNAPTICS_MST_MODE_REMOTE;
		self->layer = layer + 1;
		self->rad = rad;
		if (!fu_synaptics_mst_device_scan_cascade(self, layer + 1, error))
			return FALSE;
	}
	return TRUE;
}

void
fu_synaptics_mst_device_set_system_type(FuSynapticsMstDevice *self, const gchar *system_type)
{
	g_return_if_fail(FU_IS_SYNAPTICS_MST_DEVICE(self));
	self->system_type = g_strdup(system_type);
}

static gboolean
fu_synaptics_mst_device_rescan(FuDevice *device, GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE(device);
	guint8 buf_vid[4];
	g_autoptr(FuSynapticsMstConnection) connection = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *guid0 = NULL;
	g_autofree gchar *guid1 = NULL;
	g_autofree gchar *guid2 = NULL;
	g_autofree gchar *guid3 = NULL;
	g_autofree gchar *name = NULL;
	const gchar *name_parent = NULL;
	const gchar *name_family;
	guint8 buf_ver[16];
	FuDevice *parent;

	/* read vendor ID */
	connection =
	    fu_synaptics_mst_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)), 0, 0);
	if (!fu_synaptics_mst_connection_read(connection, REG_RC_CAP, buf_vid, 1, error)) {
		g_prefix_error(error, "failed to read device: ");
		return FALSE;
	}
	if (buf_vid[0] & 0x04) {
		if (!fu_synaptics_mst_connection_read(connection,
						      REG_VENDOR_ID,
						      buf_vid,
						      3,
						      error)) {
			g_prefix_error(error, "failed to read vendor ID: ");
			return FALSE;
		}
		/* not a correct device */
		if (buf_vid[0] != 0x90 || buf_vid[1] != 0xCC || buf_vid[2] != 0x24) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "no device");
			return FALSE;
		}
	}

	/* direct */
	self->mode = FU_SYNAPTICS_MST_MODE_DIRECT;
	self->layer = 0;
	self->rad = 0;

	/* enable remote control and disable on exit */
	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_synaptics_mst_device_enable_rc,
					   (FuDeviceLockerFunc)fu_synaptics_mst_device_disable_rc,
					   error);
	if (locker == NULL)
		return FALSE;

	/* read firmware version */
	if (!fu_synaptics_mst_connection_read(connection, REG_FIRMWARE_VERSION, buf_ver, 3, error))
		return FALSE;

	version = g_strdup_printf("%1d.%02d.%02d", buf_ver[0], buf_ver[1], buf_ver[2]);
	fu_device_set_version(FU_DEVICE(self), version);

	/* read board chip_id */
	if (!fu_synaptics_mst_connection_read(connection, REG_CHIP_ID, buf_ver, 2, error)) {
		g_prefix_error(error, "failed to read chip id: ");
		return FALSE;
	}
	self->chip_id = (buf_ver[0] << 8) | (buf_ver[1]);
	if (self->chip_id == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid chip ID");
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
	if (!fu_synaptics_mst_device_read_board_id(self, connection, buf_ver, error))
		return FALSE;
	self->board_id = fu_memread_uint16(buf_ver, G_BIG_ENDIAN);

	/* recursively look for cascade devices */
	if (!fu_device_locker_close(locker, error)) {
		g_prefix_error(error, "failed to close parent: ");
		return FALSE;
	}
	if (!fu_synaptics_mst_device_scan_cascade(self, 0, error))
		return FALSE;

	/* set up the device name and kind via quirks */
	guid0 = g_strdup_printf("MST-%u", self->board_id);
	fu_device_add_instance_id(FU_DEVICE(self), guid0);
	parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent != NULL)
		name_parent = fu_device_get_name(parent);
	if (name_parent != NULL) {
		name = g_strdup_printf("VMM%04x inside %s", self->chip_id, name_parent);
	} else {
		name = g_strdup_printf("VMM%04x", self->chip_id);
	}
	fu_device_set_name(FU_DEVICE(self), name);

	/* this is a host system, use system ID */
	name_family = fu_synaptics_mst_family_to_string(self->family);
	if (g_strcmp0(self->device_kind, "system") == 0) {
		g_autofree gchar *guid = NULL;
		guid =
		    g_strdup_printf("MST-%s-%s-%u", name_family, self->system_type, self->board_id);
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
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
		fu_device_set_firmware_size_max(device, 0x10000);
		fu_device_add_instance_id_full(device, "MST-tesla", FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		break;
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
		fu_device_set_firmware_size_max(device, 0x10000);
		fu_device_add_instance_id_full(device, "MST-leaf", FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		break;
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		fu_device_set_firmware_size_max(device, 0x80000);
		fu_device_add_instance_id_full(device,
					       "MST-panamera",
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		break;
	default:
		break;
	}

	/* add non-standard GUIDs */
	guid1 = g_strdup_printf("MST-%s-vmm%04x-%u", name_family, self->chip_id, self->board_id);
	fu_device_add_instance_id(FU_DEVICE(self), guid1);
	guid2 = g_strdup_printf("MST-%s-%u", name_family, self->board_id);
	fu_device_add_instance_id(FU_DEVICE(self), guid2);
	guid3 = g_strdup_printf("MST-%s", name_family);
	fu_device_add_instance_id(FU_DEVICE(self), guid3);

	/* this is not a valid customer ID */
	if ((self->board_id >> 8) == 0x0) {
		fu_device_inhibit(device,
				  "invalid-customer-id",
				  "cannot update as CustomerID is unset");
	}
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_synaptics_mst_device_finalize;
	klass_device->to_string = fu_synaptics_mst_device_to_string;
	klass_device->set_quirk_kv = fu_synaptics_mst_device_set_quirk_kv;
	klass_device->rescan = fu_synaptics_mst_device_rescan;
	klass_device->write_firmware = fu_synaptics_mst_device_write_firmware;
	klass_device->prepare_firmware = fu_synaptics_mst_device_prepare_firmware;
	klass_device->probe = fu_synaptics_mst_device_probe;
	klass_device->set_progress = fu_synaptics_mst_device_set_progress;
}

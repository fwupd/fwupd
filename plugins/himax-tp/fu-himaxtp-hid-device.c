/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-himaxtp-firmware.h"
#include "fu-himaxtp-hid-device.h"
#include "fu-himaxtp-struct.h"

#define HIMAX_VID (0x3558)

#define HID_CFG_ID		     (0x05)
#define HID_REG_RW_ID		     (0x06)
#define HID_FW_UPDATE_ID	     (0x0A)
#define HID_FW_UPDATE_HANDSHAKING_ID (0x0B)
#define HID_SELF_TEST_ID	     (0x0C)

#define HID_UPDATE_MAIN_CMD   (0x55)
#define HID_UPDATE_BL_CMD     (0x77)
#define HID_UPDATE_COMMIT_RET (0xB1)

#define HID_READY_TIMEOUT_S	(7)
#define HID_UPDATE_TIMEOUT_S	(3)
#define HID_POLLING_INTERVAL_MS (400)

union FuHxVal {
	guint16 word;
	/* nocheck:zero-init */
	guint8 byte[2];
};

#pragma pack(1)

struct FuHxRegRw {
	guint8 rw_flag;
	guint32 reg_addr;
	guint32 reg_value;
};

#pragma pack()

struct FuHxIdSizeTable {
	guint32 id;
	guint32 size;
	const gchar name[32];
};

struct FuHxFlashInfo {
	guint32 id;
	gint16 write_delay;
	guint8 block_protect_mask;
};

struct _FuHimaxtpHidDevice {
	FuHidrawDevice parent_instance;
	guint16 pid;
	FuHimaxtpHidInfo *dev_info;
	struct FuHxIdSizeTable *id_size_table;
	guint8 id_size_table_num;
};

G_DEFINE_TYPE(FuHimaxtpHidDevice, fu_himaxtp_hid_device, FU_TYPE_HIDRAW_DEVICE)
static struct FuHxIdSizeTable g_id_size_table[] = {
    {HID_CFG_ID, 0, "HID_CFG_ID"},
    {HID_REG_RW_ID, 0, "HID_REG_RW_ID"},
    {HID_FW_UPDATE_ID, 0, "HID_FW_UPDATE_ID"},
    {HID_FW_UPDATE_HANDSHAKING_ID, 0, "HID_FW_UPDATE_HANDSHAKING_ID"},
    {HID_SELF_TEST_ID, 0, "HID_SELF_TEST_ID"},
};

#define FLASH_ID_P25Q40SL	    (0x00136085)
#define P25Q40SL_BLOCK_PROTECT_MASK (0x7c)
#define FLASH_ID_DEFAULT	    (0x00000000)
#define NONE_BLOCK_PROTECT_MASK	    (0x00)

static struct FuHxFlashInfo g_flash_info_table[] = {
    {.id = FLASH_ID_P25Q40SL, .write_delay = 8, .block_protect_mask = P25Q40SL_BLOCK_PROTECT_MASK},
    {.id = FLASH_ID_DEFAULT, .write_delay = 1, .block_protect_mask = NONE_BLOCK_PROTECT_MASK},
};

#define FLASH_TABLE_SIZE	  (sizeof(g_flash_info_table) / sizeof(struct FuHxFlashInfo))
#define BLOCK_PROTECT_BASE_ADDR	  (0x80000000U)
#define BLOCK_PROTECT_CMD1_ADDR	  (BLOCK_PROTECT_BASE_ADDR + 0x10)
#define BLOCK_PROTECT_CMD2_ADDR	  (BLOCK_PROTECT_BASE_ADDR + 0x20)
#define BLOCK_PROTECT_CMD3_ADDR	  (BLOCK_PROTECT_BASE_ADDR + 0x24)
#define BLOCK_PROTECT_STATUS_ADDR (BLOCK_PROTECT_BASE_ADDR + 0x2C)
#define WRITE_PROTECT_PIN_ADDR	  (0x900880BCU)

static gsize
fu_himaxtp_hid_device_size_lookup(FuHimaxtpHidDevice *self, guint8 id)
{
	for (guint32 i = 0; i < self->id_size_table_num; i++) {
		if (self->id_size_table[i].id == id)
			return self->id_size_table[i].size;
	}

	return 0;
}

static gboolean
fu_himaxtp_hid_device_set_feature(FuHimaxtpHidDevice *self,
				  guint8 id,
				  const guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autofree guint8 *data = NULL;
	g_autoptr(GPtrArray) chunks =
	    fu_chunk_array_new(buf, bufsz, 0, 0, fu_himaxtp_hid_device_size_lookup(self, id));
	gsize unit_sz;
	guint32 chunk_count = 0;

	g_return_val_if_fail(FU_IS_HIMAXTP_HID_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz > 0, FALSE);
	g_return_val_if_fail(self->id_size_table != NULL, FALSE);

	unit_sz = fu_himaxtp_hid_device_size_lookup(self, id);
	/* allocate buffer */
	data = g_malloc0(unit_sz + 1);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		memset(data, 0, unit_sz + 1);
		data[0] = id; /* report number */
		if (!fu_memcpy_safe(data,
				    unit_sz + 1,
				    1, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		/* SetFeature */
		if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
						  data,
						  unit_sz + 1,
						  FU_IOCTL_FLAG_NONE,
						  error))
			return FALSE;

		chunk_count++;
		fu_device_sleep(FU_DEVICE(self), 1);
	}
	g_debug("SetFeature called %u times for id %u, %zu written",
		chunk_count,
		id,
		chunk_count * (unit_sz));

	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_get_feature(FuHimaxtpHidDevice *self, guint8 id, guint8 *buf, gsize bufsz)
{
	g_autofree guint8 *data = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean ret;
	gsize datasz;

	g_return_val_if_fail(FU_IS_HIMAXTP_HID_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz > 0, FALSE);

	/* allocate buffer */
	datasz = bufsz + 1;
	data = g_malloc0(datasz);
	data[0] = id; /* report number */

	/* GetFeature */
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  data,
					  datasz,
					  FU_IOCTL_FLAG_NONE,
					  &error_local)) {
		g_debug("GetFeature failed for id %u: %s", id, error_local->message);
		return FALSE;
	}

	/* copy out */
	ret = fu_memcpy_safe(buf,
			     bufsz,
			     0,
			     data,
			     datasz,
			     1, /* src */
			     bufsz,
			     &error_local);
	if (!ret) {
		g_debug("failed to copy data for id %u: %s", id, error_local->message);
		return FALSE;
	}

	return TRUE;
}

static long
fu_himaxtp_hid_device_get_time_diff_ms(struct timespec *start, struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_nsec - start->tv_nsec) / 1000000;
}

static gboolean
fu_himaxtp_hid_device_polling_for_result(FuHimaxtpHidDevice *self,
					 guint8 feature_id,
					 guint8 *expected_data,
					 gsize expected_data_size,
					 guint interval_ms,
					 guint timeout_ms,
					 guint8 *received_data,
					 gsize *received_data_size)
{
	struct timespec start, current;
	guint interval_ms_sum = 0;
	g_autofree guint8 *data = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean ret = FALSE;

	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
		g_debug("Failed to get start time");
		return FALSE;
	}
	*received_data_size = 0;

	g_return_val_if_fail(FU_IS_HIMAXTP_HID_DEVICE(self), FALSE);
	g_return_val_if_fail(expected_data != NULL, FALSE);
	g_return_val_if_fail(expected_data_size > 0, FALSE);
	g_return_val_if_fail(received_data != NULL, FALSE);
	g_return_val_if_fail(received_data_size != NULL, FALSE);

	data = g_malloc0(expected_data_size);
	while (interval_ms_sum < timeout_ms) {
		fu_device_sleep(FU_DEVICE(self), interval_ms);
		interval_ms_sum += interval_ms;
		ret = fu_himaxtp_hid_device_get_feature(self, feature_id, data, expected_data_size);
		if (!ret) {
			*received_data_size = expected_data_size;
			/* return when failed to copy failed reason */
			if (!fu_memcpy_safe(received_data,
					    expected_data_size,
					    0,
					    data,
					    expected_data_size,
					    0,
					    expected_data_size,
					    &error_local)) {
				g_debug("failed to copy received data for id %u: %s",
					feature_id,
					error_local->message);
				return FALSE;
			}
			/* continue to polling until timeout */
		}

		if (ret && memcmp(data, expected_data, expected_data_size) == 0)
			return TRUE;

		if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
			g_debug("Failed to get current time");
			return FALSE;
		}
		if (fu_himaxtp_hid_device_get_time_diff_ms(&start, &current) >= timeout_ms) {
			g_debug("Polling for expected data timeout for id %u", feature_id);
			return FALSE;
		}
	}

	return FALSE;
}

static gboolean
fu_himaxtp_hid_device_register_operate(FuHimaxtpHidDevice *self,
				       gboolean is_write,
				       guint32 reg_addr,
				       guint32 *reg_value,
				       GError **error)
{
	struct FuHxRegRw reg_and_data = {0};

	reg_and_data.reg_addr = reg_addr;

	if (is_write) {
		reg_and_data.rw_flag = 0x01;
		reg_and_data.reg_value = *reg_value;
		if (!fu_himaxtp_hid_device_set_feature(self,
						       HID_REG_RW_ID,
						       (guint8 *)&reg_and_data,
						       sizeof(struct FuHxRegRw),
						       error)) {
			g_prefix_error(error, "Failed to write register 0x%08X: ", reg_addr);
			return FALSE;
		}
		return TRUE;
	}

	if (!fu_himaxtp_hid_device_set_feature(self,
					       HID_REG_RW_ID,
					       (guint8 *)&reg_and_data,
					       sizeof(struct FuHxRegRw),
					       error)) {
		g_prefix_error(error, "Failed to initiate register read for 0x%08X: ", reg_addr);
		return FALSE;
	}

	if (!fu_himaxtp_hid_device_get_feature(self,
					       HID_REG_RW_ID,
					       (guint8 *)&reg_and_data,
					       sizeof(struct FuHxRegRw))) {
		g_debug("GetFeature failed for register 0x%08X", reg_addr);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "Failed to read register");
		return FALSE;
	}

	*reg_value = reg_and_data.reg_value;

	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_get_size_by_id(FuHidReport *report, guint8 id, gsize *size, GError **error)
{
	g_autoptr(FuFirmware) item_count = NULL;
	g_autoptr(FuFirmware) item_size = NULL;
	gsize final;

	g_return_val_if_fail(FU_IS_HID_REPORT(report), FALSE);
	g_return_val_if_fail(size != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	item_count = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-count", error);
	if (item_count == NULL)
		return FALSE;

	item_size = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-size", error);
	if (item_size == NULL)
		return FALSE;

	final = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_size)) / 8;
	final *= fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_count));
	*size = final;

	/* success */
	return TRUE;
}

static guint32
fu_himaxtp_hid_device_calculate_mapping_entries(FuHimaxtpHidInfo *info, gsize max_table_size)
{
	guint32 entries = 0;

	for (gsize i = 0; i < max_table_size; i++) {
		FuHimaxtpHidFwUnit *entry = fu_himaxtp_hid_info_get_main_mapping(info, i);
		if (fu_himaxtp_hid_fw_unit_get_bin_size(entry) != 0)
			entries++;
		else
			break;
	}

	return entries;
}

static gboolean
fu_himaxtp_hid_device_polling_error_handler(FuHimaxtpHidDevice *self,
					    struct timespec *start,
					    gint timeout_s,
					    guint8 received_code,
					    FuHimaxtpUpdateErrorCode *status,
					    GError **error)
{
	struct timespec now;

	switch (received_code) {
	case FU_HIMAXTP_UPDATE_ERROR_CODE_MCU_E0:
	case FU_HIMAXTP_UPDATE_ERROR_CODE_MCU_E1:
		if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
			*status = FU_HIMAXTP_UPDATE_ERROR_CODE_POLLING_TIMEOUT;
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "Failed to get current time");
			return FALSE;
		}
		if (fu_himaxtp_hid_device_get_time_diff_ms(start, &now) >= timeout_s * 1000) {
			*status = FU_HIMAXTP_UPDATE_ERROR_CODE_POLLING_TIMEOUT;
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_TIMED_OUT,
					    "Polling for ready state timeout");
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 10);
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_POLLING_AGAIN;
		return TRUE;
	case FU_HIMAXTP_UPDATE_ERROR_CODE_NO_BL:
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_NO_BL;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "No bootloader found");
		return FALSE;
	case FU_HIMAXTP_UPDATE_ERROR_CODE_NO_MAIN:
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_NO_MAIN;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "No main firmware found");
		return FALSE;
	default:
		*status = (FuHimaxtpUpdateErrorCode)received_code;
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Unknown error code received: 0x%02X",
			    received_code);
		return FALSE;
	}
}

static gboolean
fu_himaxtp_hid_device_write_register_sequence(FuHimaxtpHidDevice *self,
					      guint32 *sequence,
					      gsize seq_count,
					      GError **error)
{
	for (guint i = 0; i < seq_count; i++) {
		if (!fu_himaxtp_hid_device_register_operate(self,
							    TRUE,
							    sequence[i * 2],
							    &sequence[i * 2 + 1],
							    error)) {
			g_prefix_error(
			    error,
			    "Failed to write register sequence step %u: 0x%08X<-0x%08X: ",
			    i,
			    sequence[i * 2],
			    sequence[i * 2 + 1]);
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 1);
	}

	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_get_flash_id(FuHimaxtpHidDevice *self, gint32 *flash_id, GError **error)
{
	guint32 reg_value = 0;
	guint32 flash_id_read_seq[] = {
	    BLOCK_PROTECT_CMD1_ADDR,
	    0x00020780U,
	    BLOCK_PROTECT_CMD2_ADDR,
	    0x42000002U,
	    BLOCK_PROTECT_CMD3_ADDR,
	    0x0000009FU,
	};

	if (!fu_himaxtp_hid_device_write_register_sequence(self,
							   flash_id_read_seq,
							   sizeof(flash_id_read_seq) /
							       (2 * sizeof(guint32)),
							   error)) {
		/* nocheck:error */
		g_prefix_error(error, "cannot write flash id read sequence: ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 1);

	if (!fu_himaxtp_hid_device_register_operate(self,
						    FALSE,
						    BLOCK_PROTECT_STATUS_ADDR,
						    &reg_value,
						    error)) {
		/* nocheck:error */
		g_prefix_error(error, "cannot read flash id: ");
		return FALSE;
	}

	if (reg_value == 0 || reg_value >= 0x00FFFFFF) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "invalid flash id read");
		return FALSE;
	}

	*flash_id = (gint32)reg_value;

	return TRUE;
}

static gint16
fu_himaxtp_hid_device_get_block_protect_mask(guint32 flash_id)
{
	for (gsize i = 0; i < FLASH_TABLE_SIZE; i++) {
		if (g_flash_info_table[i].id == flash_id)
			return g_flash_info_table[i].block_protect_mask;
	}

	return -1;
}

static gint16
fu_himaxtp_hid_device_get_write_delay(guint32 flash_id)
{
	for (gsize i = 0; i < FLASH_TABLE_SIZE; i++) {
		if (g_flash_info_table[i].id == flash_id)
			return g_flash_info_table[i].write_delay;
	}

	return -1;
}

static gboolean
fu_himaxtp_hid_device_get_flash_status(FuHimaxtpHidDevice *self,
				       gint16 block_protect_mask,
				       gint32 *flash_status,
				       FuHimaxtpUpdateErrorCode *status,
				       GError **error)
{
	guint32 reg_value = 0;
	guint32 get_status_seq[] = {
	    BLOCK_PROTECT_CMD1_ADDR,
	    0x00020780U,
	    BLOCK_PROTECT_CMD2_ADDR,
	    0x42000000U,
	    BLOCK_PROTECT_CMD3_ADDR,
	    0x00000005U,
	};

	if (!fu_himaxtp_hid_device_write_register_sequence(self,
							   get_status_seq,
							   sizeof(get_status_seq) /
							       (2 * sizeof(guint32)),
							   error)) {
		*flash_status = -1;
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot write flash status get sequence: ");
		return FALSE;
	}

	if (!fu_himaxtp_hid_device_register_operate(self,
						    FALSE,
						    BLOCK_PROTECT_STATUS_ADDR,
						    &reg_value,
						    error)) {
		*flash_status = -1;
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot read flash status: ");
		return FALSE;
	}

	if (((guint16)reg_value & (guint16)block_protect_mask) != 0) {
		g_debug("Flash is write protected, status: 0x%08X, mask: 0x%02X",
			reg_value,
			(guint16)block_protect_mask);
		*flash_status = 1;
		return TRUE;
	} else {
		g_debug("Flash is not write protected, status: 0x%08X, mask: 0x%02X",
			reg_value,
			(guint16)block_protect_mask);
	}
	*flash_status = 0;

	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_switch_write_protect(FuHimaxtpHidDevice *self,
					   gboolean enable,
					   FuHimaxtpUpdateErrorCode *status,
					   GError **error)
{
	guint32 reg_value = 0;

	if (!fu_himaxtp_hid_device_register_operate(self,
						    FALSE,
						    WRITE_PROTECT_PIN_ADDR,
						    &reg_value,
						    error)) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot read write protect pin status: ");
		return FALSE;
	}

	if (enable)
		FU_BIT_SET(reg_value, 0);
	else
		FU_BIT_CLEAR(reg_value, 0);

	if (!fu_himaxtp_hid_device_register_operate(self,
						    TRUE,
						    WRITE_PROTECT_PIN_ADDR,
						    &reg_value,
						    error)) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot write write protect pin status: ");
		return FALSE;
	}

	return TRUE;
}

/* nocheck:name */
static gboolean
fu_himaxtp_hid_device_switch_block_protect_retry_func(FuDevice *device,
						      gpointer user_data,
						      GError **error)
{
	FuHimaxtpHidDevice *self = FU_HIMAXTP_HID_DEVICE(device);
	FuHimaxtpUpdateErrorCode *status = (FuHimaxtpUpdateErrorCode *)user_data;
	guint32 reg_value;

	reg_value = 0x00000005U;
	if (!fu_himaxtp_hid_device_register_operate(self,
						    TRUE,
						    BLOCK_PROTECT_CMD3_ADDR,
						    &reg_value,
						    error)) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot write flash block protect status: ");
		return FALSE;
	}

	reg_value = 0;
	if (!fu_himaxtp_hid_device_register_operate(self,
						    FALSE,
						    BLOCK_PROTECT_STATUS_ADDR,
						    &reg_value,
						    error)) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot read flash block protect status: ");
		return FALSE;
	}

	if ((reg_value & 0x03) == 0)
		return TRUE;

	return FALSE;
}

static gboolean
fu_himaxtp_hid_device_switch_block_protect(FuHimaxtpHidDevice *self,
					   gint16 block_protect_mask,
					   guint16 write_delay,
					   gboolean enable,
					   FuHimaxtpUpdateErrorCode *status,
					   GError **error)
{
	guint32 reg_value = 0;
	const gint max_retry = 100;
	guint32 switch_seq[] = {
	    BLOCK_PROTECT_CMD1_ADDR,
	    0x00020780U,
	    BLOCK_PROTECT_CMD2_ADDR,
	    0x47000000U,
	    BLOCK_PROTECT_CMD3_ADDR,
	    0x00000006U,
	    BLOCK_PROTECT_CMD2_ADDR,
	    0x41000000U,
	    BLOCK_PROTECT_STATUS_ADDR,
	    0x00000000U,
	    BLOCK_PROTECT_CMD3_ADDR,
	    0x00000001U,
	};
	guint32 *block_protect_param = &switch_seq[9];

	if (!enable)
		block_protect_mask = 0;

	if (block_protect_mask < 0) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "unknown flash id, cannot get block protect mask");
		return FALSE;
	}

	*block_protect_param = (guint32)block_protect_mask;

	if (!fu_himaxtp_hid_device_write_register_sequence(self,
							   switch_seq,
							   sizeof(switch_seq) /
							       (2 * sizeof(guint32)),
							   error)) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot write flash block protect switch sequence: ");

		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), write_delay);

	reg_value = 0x42000000U;
	if (!fu_himaxtp_hid_device_register_operate(self,
						    TRUE,
						    BLOCK_PROTECT_CMD2_ADDR,
						    &reg_value,
						    error)) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		/* nocheck:error */
		g_prefix_error(error, "cannot write flash block protect switch cmd: ");
		return FALSE;
	}

	fu_device_retry_set_delay(FU_DEVICE(self), 1);
	if (fu_device_retry(FU_DEVICE(self),
			    fu_himaxtp_hid_device_switch_block_protect_retry_func,
			    max_retry,
			    status,
			    error) == FALSE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "flash block protect switch timeout");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_unlock_flash(FuHimaxtpHidDevice *self,
				   gint32 flash_id,
				   FuHimaxtpUpdateErrorCode *status,
				   GError **error)
{
	gint32 flash_status;
	gint16 write_delay;
	gint16 block_protect_mask;

	block_protect_mask = fu_himaxtp_hid_device_get_block_protect_mask(flash_id);
	write_delay = fu_himaxtp_hid_device_get_write_delay(flash_id);
	if (write_delay < 0 || block_protect_mask < 0) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "unknown flash id, cannot get write delay or block protect mask");
		return FALSE;
	}

	if (fu_himaxtp_hid_device_get_flash_status(self,
						   block_protect_mask,
						   &flash_status,
						   status,
						   error)) {
		if (flash_status > 0) {
			if (!fu_himaxtp_hid_device_switch_write_protect(self,
									FALSE,
									status,
									error)) {
				/* Unable to disable write protect pin */
				return FALSE;
			}

			if (!fu_himaxtp_hid_device_switch_block_protect(self,
									block_protect_mask,
									write_delay,
									FALSE,
									status,
									error)) {
				/* Unable to disable block protect */
				return FALSE;
			}

			if (!fu_himaxtp_hid_device_get_flash_status(self,
								    block_protect_mask,
								    &flash_status,
								    status,
								    error)) {
				/* Error while getting flash status */
				return FALSE;
			}

			if (flash_status > 0) {
				/* Flash is still write protected after disabling write protect pin
				and block protect */
				*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROTECT;
				g_set_error_literal(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "flash is still write protected after disabling write protect "
				    "pin and block protect");
				return FALSE;
			}

			return TRUE;
		}

		return TRUE;
	}

	return FALSE;
}

static gboolean
fu_himaxtp_hid_device_update_process(FuHimaxtpHidDevice *self,
				     gint update_type,
				     guint fw_entry_count,
				     guint8 start_cmd,
				     guint8 commit_cmd,
				     const guint8 *firmware,
				     gsize fw_size,
				     FuHimaxtpUpdateErrorCode *status,
				     GError **error)
{
	struct timespec start;
	const guint8 ready_timeout_s = HID_READY_TIMEOUT_S;
	const guint8 update_timeout_s = HID_UPDATE_TIMEOUT_S;
	const FuHimaxtpHidInfo *hid_info = self->dev_info;
	FuHimaxtpHidFwUnit *fw_entry = NULL;
	guint8 cmd = 0;
	guint8 received[2] = {0};
	guint32 tmp_offset;
	guint32 tmp_size;
	gint32 flash_id = -1;
	gsize n_received = 0;

	if (fu_himaxtp_hid_device_get_feature(self, HID_FW_UPDATE_HANDSHAKING_ID, &cmd, 1) ==
	    FALSE) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_INITIAL;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "Failed to get initial handshake status from device");
		return FALSE;
	}

	/* unlock flash if flash id exists, otherwise do nothing */
	if (fu_himaxtp_hid_device_get_flash_id(self, &flash_id, error))
		if (!fu_himaxtp_hid_device_unlock_flash(self, flash_id, status, error))
			return FALSE;

	/* restart in bootloader mode */
	cmd = start_cmd;
	if (!fu_himaxtp_hid_device_set_feature(self,
					       HID_FW_UPDATE_HANDSHAKING_ID,
					       &cmd,
					       1,
					       error)) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_INITIAL;
		/* nocheck:error */
		g_prefix_error(error, "Failed to send command to start firmware update: ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 100);
	/* unlock again after reset if locked before */
	if (flash_id > 0)
		if (!fu_himaxtp_hid_device_unlock_flash(self, flash_id, status, error))
			return FALSE;

	for (guint i = 0; i < fw_entry_count; i++) {
		if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
			*status = FU_HIMAXTP_UPDATE_ERROR_CODE_POLLING_TIMEOUT;
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "Failed to get current time");
			return FALSE;
		}
		while (TRUE) {
			if (update_type == FU_HIMAXTP_UPDATE_TYPE_BL)
				fw_entry = fu_himaxtp_hid_info_get_bl_mapping(hid_info);
			else
				fw_entry = fu_himaxtp_hid_info_get_main_mapping(hid_info, i);

			cmd = fu_himaxtp_hid_fw_unit_get_cmd(fw_entry);
			if (!fu_himaxtp_hid_device_polling_for_result(self,
								      HID_FW_UPDATE_HANDSHAKING_ID,
								      &cmd,
								      sizeof(cmd),
								      HID_POLLING_INTERVAL_MS,
								      ready_timeout_s * 1000,
								      received,
								      &n_received)) {
				if (n_received > 0) {
					if (!fu_himaxtp_hid_device_polling_error_handler(
						self,
						&start,
						ready_timeout_s,
						received[0],
						status,
						error))
						return FALSE;

					continue;
				}
				*status = FU_HIMAXTP_UPDATE_ERROR_CODE_POLLING_TIMEOUT;
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_TIMED_OUT,
						    "Polling for result timeout");
				return FALSE;
			} else {
				/* Ready to send firmware data */
				break;
			}
		}

		tmp_offset = fu_himaxtp_hid_fw_unit_get_bin_start_offset(fw_entry) * 1024;
		tmp_size = fu_himaxtp_hid_fw_unit_get_bin_size(fw_entry) * 1024;
		if ((tmp_offset + tmp_size) > fw_size) {
			*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FW_ENTRY_INVALID;
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "Firmware entry exceeds firmware size");
			return FALSE;
		}

		if (!fu_himaxtp_hid_device_set_feature(self,
						       HID_FW_UPDATE_ID,
						       &(firmware[tmp_offset]),
						       tmp_size,
						       error)) {
			*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FW_TRANSFER;
			/* nocheck:error */
			g_prefix_error(error, "Sending firmware data failed: ");

			return FALSE;
		}
	}
	cmd = commit_cmd;
	if (!fu_himaxtp_hid_device_polling_for_result(self,
						      HID_FW_UPDATE_HANDSHAKING_ID,
						      &cmd,
						      sizeof(cmd),
						      HID_POLLING_INTERVAL_MS,
						      update_timeout_s * 1000,
						      received,
						      &n_received)) {
		if (n_received > 0) {
			/* received[0] == FU_HIMAXTP_UPDATE_ERROR_CODE_BL: Bootloader error during
			   update
			   received[0] == FU_HIMAXTP_UPDATE_ERROR_CODE_PW: Password error
			   during update
			   received[0] == FU_HIMAXTP_UPDATE_ERROR_CODE_ERASE_FLASH:
			   Flash erase error during update
			   received[0] ==
			   FU_HIMAXTP_UPDATE_ERROR_CODE_FLASH_PROGRAMMING: Flash programming error
			   during update
			 */

			*status = (FuHimaxtpUpdateErrorCode)received[0];
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Firmware update failed with error code: 0x%02X",
				    received[0]);
			return FALSE;
		}
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_POLLING_TIMEOUT;
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "Update commit polling for result timeout");

		return FALSE;
	} else {
		/* Update successfully completed */
		fu_device_sleep(FU_DEVICE(self), 500);
	}

	*status = FU_HIMAXTP_UPDATE_ERROR_CODE_NO_ERROR;
	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_bootloader_update(FuHimaxtpHidDevice *self,
					const guint8 *firmware,
					gsize fw_size,
					FuHimaxtpUpdateErrorCode *status,
					GError **error)
{
	const guint8 bl_update_cmd = HID_UPDATE_BL_CMD;
	const guint8 bl_update_commit = HID_UPDATE_COMMIT_RET;
	FuHimaxtpHidFwUnit *fw_entry = NULL;

	fw_entry = fu_himaxtp_hid_info_get_bl_mapping(self->dev_info);
	if ((fu_himaxtp_hid_fw_unit_get_bin_start_offset(fw_entry) * 1024 +
	     fu_himaxtp_hid_fw_unit_get_bin_size(fw_entry) * 1024) > fw_size) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FW_ENTRY_INVALID;
		return FALSE;
	}

	return fu_himaxtp_hid_device_update_process(self,
						    FU_HIMAXTP_UPDATE_TYPE_BL,
						    FU_HIMAXTP_MAP_COUNT_BL,
						    bl_update_cmd,
						    bl_update_commit,
						    firmware,
						    fw_size,
						    status,
						    error);
}

static gboolean
fu_himaxtp_hid_device_main_update(FuHimaxtpHidDevice *self,
				  const guint8 *firmware,
				  gsize fw_size,
				  FuHimaxtpUpdateErrorCode *status,
				  GError **error)
{
	const guint8 main_update_cmd = HID_UPDATE_MAIN_CMD;
	const guint8 main_update_commit = HID_UPDATE_COMMIT_RET;
	guint fw_entry_count = 0;
	FuHimaxtpHidFwUnit *fw_entry = NULL;

	fw_entry_count = fu_himaxtp_hid_device_calculate_mapping_entries(self->dev_info,
									 FU_HIMAXTP_MAP_COUNT_MAIN);
	if (fw_entry_count == 0) {
		*status = FU_HIMAXTP_UPDATE_ERROR_CODE_NO_ERROR;
		return TRUE;
	}

	for (guint i = 0; i < fw_entry_count; i++) {
		fw_entry = fu_himaxtp_hid_info_get_main_mapping(self->dev_info, i);
		if ((fu_himaxtp_hid_fw_unit_get_bin_start_offset(fw_entry) * 1024 +
		     fu_himaxtp_hid_fw_unit_get_bin_size(fw_entry) * 1024) > fw_size) {
			*status = FU_HIMAXTP_UPDATE_ERROR_CODE_FW_ENTRY_INVALID;
			return FALSE;
		}
	}

	return fu_himaxtp_hid_device_update_process(self,
						    FU_HIMAXTP_UPDATE_TYPE_MAIN,
						    fw_entry_count,
						    main_update_cmd,
						    main_update_commit,
						    firmware,
						    fw_size,
						    status,
						    error);
}

static gboolean
fu_himaxtp_hid_device_probe(FuDevice *device, GError **error)
{
	guint16 device_id = fu_device_get_pid(device);
	FuHimaxtpHidDevice *self = NULL;

	/* check if interface valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	self = FU_HIMAXTP_HID_DEVICE(device);
	self->pid = device_id;
	self->id_size_table = g_id_size_table;
	self->id_size_table_num = sizeof(g_id_size_table) / sizeof(struct FuHxIdSizeTable);

	/* success */
	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_setup(FuDevice *device, GError **error)
{
	FuHimaxtpHidDevice *self = FU_HIMAXTP_HID_DEVICE(device);
	gsize tmp;
	gboolean ret;
	g_autoptr(FuHidDescriptor) hid_desc = NULL;
	g_autoptr(GPtrArray) reports = NULL;
	g_autoptr(FuFirmware) item = NULL;
	g_autofree guint8 *hid_info = NULL;
	guint8 report_id;
	FuHidReport *report = NULL;
	guint64 version;

	hid_desc = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(self), error);
	if (hid_desc == NULL)
		return FALSE;

	reports = fu_firmware_get_images(FU_FIRMWARE(hid_desc));
	for (guint i = 0; i < reports->len; i++) {
		report = g_ptr_array_index(reports, i);
		item = fu_firmware_get_image_by_id(FU_FIRMWARE(report), "report-id", NULL);
		if (item == NULL)
			continue;

		report_id = fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item));
		for (guint j = 0; j < self->id_size_table_num; j++) {
			if (self->id_size_table[j].id == report_id) {
				if (fu_himaxtp_hid_device_get_size_by_id(report,
									 report_id,
									 &tmp,
									 error) == TRUE)
					self->id_size_table[j].size = tmp;
				break;
			}
		}
	}

	if (fu_himaxtp_hid_device_size_lookup(self, HID_CFG_ID) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Necessary id-size lookup failed");
		return FALSE;
	}

	hid_info = g_malloc0(fu_himaxtp_hid_device_size_lookup(self, HID_CFG_ID));
	ret =
	    fu_himaxtp_hid_device_get_feature(self,
					      HID_CFG_ID,
					      (guint8 *)hid_info,
					      fu_himaxtp_hid_device_size_lookup(self, HID_CFG_ID));
	if (ret == FALSE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "Failed to get initial handshake status from device");
		return FALSE;
	}

	self->dev_info =
	    fu_himaxtp_hid_info_parse((guint8 *)hid_info,
				      fu_himaxtp_hid_device_size_lookup(self, HID_CFG_ID),
				      0,
				      error);

	/* define the extra instance IDs */
	fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(device));
	fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(device));
	fu_device_add_instance_u8(device,
				  "CID",
				  (fu_himaxtp_hid_info_get_cid(self->dev_info) & 0xFF00) >> 8);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "CID", NULL))
		return FALSE;

	/* version format : pid.cid (decimal) */
	version = fu_himaxtp_hid_info_get_pid(self->dev_info);
	version <<= 16;
	version = version | fu_himaxtp_hid_info_get_cid(self->dev_info);
	fu_device_set_version_raw(device, version);
	fu_device_set_vid(device, fu_himaxtp_hid_info_get_vid(self->dev_info));
	fu_device_set_pid(device, fu_himaxtp_hid_info_get_pid(self->dev_info));

	/* success */
	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuHimaxtpHidDevice *self = FU_HIMAXTP_HID_DEVICE(device);
	const guint8 cmd = 0x01;

	/* check if reset function available */
	if (fu_himaxtp_hid_device_size_lookup(self, HID_SELF_TEST_ID) == 0) {
		fu_device_sleep(device, 500);

		return TRUE;
	}

	/* reset the device */
	if (!fu_himaxtp_hid_device_set_feature(self, HID_SELF_TEST_ID, &cmd, sizeof(cmd), error)) {
		/* nocheck:error */
		g_prefix_error(error, "cannot reset device, and no fallback method available: ");
		return FALSE;
	}

	fu_device_sleep(device, 500);

	/* success */
	return TRUE;
}

static gboolean
fu_himaxtp_hid_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuHimaxtpHidDevice *self = FU_HIMAXTP_HID_DEVICE(device);
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(GBytes) fw = NULL;
	FuHimaxtpUpdateErrorCode result;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "main");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 15, "bootloader");

	self->id_size_table = g_id_size_table;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_himaxtp_hid_device_main_update(self, buf, bufsz, &result, error)) {
		if (result == FU_HIMAXTP_UPDATE_ERROR_CODE_NO_BL) {
			if (!fu_himaxtp_hid_device_bootloader_update(self,
								     buf,
								     bufsz,
								     &result,
								     error)) {
				g_prefix_error_literal(error,
						       "failed to update firmware bootloader: ");
				return FALSE;
			}
			fu_progress_step_done(progress);
			if (!fu_himaxtp_hid_device_main_update(self, buf, bufsz, &result, error)) {
				g_prefix_error_literal(error,
						       "failed to update firmware main code: ");
				return FALSE;
			}
			fu_progress_step_done(progress);
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "firmware update failed");
			return FALSE;
		}
	} else {
		fu_progress_step_done(progress);
		fu_device_sleep(device, 100);
		if (!fu_himaxtp_hid_device_bootloader_update(self, buf, bufsz, &result, error)) {
			g_prefix_error_literal(error, "failed to update firmware bootloader: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static void
fu_himaxtp_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_himaxtp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static FuFirmware *
fu_himaxtp_hid_device_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	FuHimaxtpHidDevice *self = FU_HIMAXTP_HID_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_himaxtp_firmware_new();

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* check is compatible with hardware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* check VID */
	if (fu_himaxtp_hid_info_get_vid(self->dev_info) !=
	    fu_himaxtp_firmware_get_vid(FU_HIMAXTP_FIRMWARE(firmware))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware incompatible, VID is not the same");

		return NULL;
	}

	/* check PID match */
	if (fu_himaxtp_hid_info_get_pid(self->dev_info) !=
	    fu_himaxtp_firmware_get_pid(FU_HIMAXTP_FIRMWARE(firmware))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware incompatible, PID is not the same");
		return NULL;
	}

	/* check CID high byte match */
	if ((fu_himaxtp_hid_info_get_cid(self->dev_info) & 0xFF00) >> 8 !=
	    fu_himaxtp_firmware_get_cid(FU_HIMAXTP_FIRMWARE(firmware)) >> 8) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware incompatible, CID high byte is not the same");

		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static void
fu_himaxtp_hid_device_init(FuHimaxtpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_summary(FU_DEVICE(self), "Touchscreen");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.himax.tp");
	fu_device_set_name(FU_DEVICE(self), "Touchscreen Controller");
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x3FC00);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_himaxtp_hid_device_class_init(FuHimaxtpHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_himaxtp_hid_device_attach;
	device_class->setup = fu_himaxtp_hid_device_setup;
	device_class->reload = fu_himaxtp_hid_device_setup;
	device_class->write_firmware = fu_himaxtp_hid_device_write_firmware;
	device_class->prepare_firmware = fu_himaxtp_hid_device_prepare_firmware;
	device_class->probe = fu_himaxtp_hid_device_probe;
	device_class->set_progress = fu_himaxtp_hid_device_set_progress;
	device_class->convert_version = fu_himaxtp_hid_device_convert_version;
}

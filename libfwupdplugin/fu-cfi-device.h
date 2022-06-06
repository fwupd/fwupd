/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_CFI_DEVICE (fu_cfi_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCfiDevice, fu_cfi_device, FU, CFI_DEVICE, FuDevice)

struct _FuCfiDeviceClass {
	FuDeviceClass parent_class;
	gboolean (*chip_select)(FuCfiDevice *self, gboolean value, GError **error);
	gpointer __reserved[30];
};

/**
 * FuCfiDeviceCmd:
 * @FU_CFI_DEVICE_CMD_READ_ID:		Read the chip ID
 * @FU_CFI_DEVICE_CMD_PAGE_PROG:	Page program
 * @FU_CFI_DEVICE_CMD_CHIP_ERASE:	Whole chip erase
 * @FU_CFI_DEVICE_CMD_READ_DATA:	Read data
 * @FU_CFI_DEVICE_CMD_READ_STATUS:	Read status
 * @FU_CFI_DEVICE_CMD_SECTOR_ERASE:	Sector erase
 * @FU_CFI_DEVICE_CMD_WRITE_EN:		Write enable
 * @FU_CFI_DEVICE_CMD_WRITE_STATUS:	Write status
 * @FU_CFI_DEVICE_CMD_BLOCK_ERASE:	Block erase
 *
 * Commands used when calling fu_cfi_device_get_cmd().
 **/
typedef enum {
	FU_CFI_DEVICE_CMD_READ_ID,
	FU_CFI_DEVICE_CMD_PAGE_PROG,
	FU_CFI_DEVICE_CMD_CHIP_ERASE,
	FU_CFI_DEVICE_CMD_READ_DATA,
	FU_CFI_DEVICE_CMD_READ_STATUS,
	FU_CFI_DEVICE_CMD_SECTOR_ERASE,
	FU_CFI_DEVICE_CMD_WRITE_EN,
	FU_CFI_DEVICE_CMD_WRITE_STATUS,
	FU_CFI_DEVICE_CMD_BLOCK_ERASE,
	/*< private >*/
	FU_CFI_DEVICE_CMD_LAST
} FuCfiDeviceCmd;

FuCfiDevice *
fu_cfi_device_new(FuContext *ctx, const gchar *flash_id);
const gchar *
fu_cfi_device_get_flash_id(FuCfiDevice *self);
void
fu_cfi_device_set_flash_id(FuCfiDevice *self, const gchar *flash_id);
guint64
fu_cfi_device_get_size(FuCfiDevice *self);
void
fu_cfi_device_set_size(FuCfiDevice *self, guint64 size);
guint32
fu_cfi_device_get_page_size(FuCfiDevice *self);
void
fu_cfi_device_set_page_size(FuCfiDevice *self, guint32 page_size);
guint32
fu_cfi_device_get_sector_size(FuCfiDevice *self);
void
fu_cfi_device_set_sector_size(FuCfiDevice *self, guint32 sector_size);
guint32
fu_cfi_device_get_block_size(FuCfiDevice *self);
void
fu_cfi_device_set_block_size(FuCfiDevice *self, guint32 block_size);
gboolean
fu_cfi_device_get_cmd(FuCfiDevice *self, FuCfiDeviceCmd cmd, guint8 *value, GError **error);

gboolean
fu_cfi_device_chip_select(FuCfiDevice *self, gboolean value, GError **error);
FuDeviceLocker *
fu_cfi_device_chip_select_locker_new(FuCfiDevice *self, GError **error);

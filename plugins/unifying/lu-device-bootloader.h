/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_DEVICE_BOOTLOADER_H
#define __LU_DEVICE_BOOTLOADER_H

#include <glib-object.h>

#include "lu-device.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE_BOOTLOADER (lu_device_bootloader_get_type ())
G_DECLARE_DERIVABLE_TYPE (LuDeviceBootloader, lu_device_bootloader, LU, DEVICE_BOOTLOADER, LuDevice)

struct _LuDeviceBootloaderClass
{
	LuDeviceClass	parent_class;
	gboolean	 (*probe)			(LuDevice		*device,
							 GError			**error);
};

typedef enum {
	LU_DEVICE_BOOTLOADER_CMD_GENERAL_ERROR			= 0x01,
	LU_DEVICE_BOOTLOADER_CMD_READ				= 0x10,
	LU_DEVICE_BOOTLOADER_CMD_WRITE				= 0x20,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_INVALID_ADDR		= 0x21,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_VERIFY_FAIL		= 0x22,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_NONZERO_START		= 0x23,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_INVALID_CRC		= 0x24,
	LU_DEVICE_BOOTLOADER_CMD_ERASE_PAGE			= 0x30,
	LU_DEVICE_BOOTLOADER_CMD_ERASE_PAGE_INVALID_ADDR	= 0x31,
	LU_DEVICE_BOOTLOADER_CMD_ERASE_PAGE_NONZERO_START	= 0x33,
	LU_DEVICE_BOOTLOADER_CMD_GET_HW_PLATFORM_ID		= 0x40,
	LU_DEVICE_BOOTLOADER_CMD_GET_FW_VERSION			= 0x50,
	LU_DEVICE_BOOTLOADER_CMD_GET_CHECKSUM			= 0x60,
	LU_DEVICE_BOOTLOADER_CMD_REBOOT				= 0x70,
	LU_DEVICE_BOOTLOADER_CMD_GET_MEMINFO			= 0x80,
	LU_DEVICE_BOOTLOADER_CMD_GET_BL_VERSION			= 0x90,
	LU_DEVICE_BOOTLOADER_CMD_GET_INIT_FW_VERSION		= 0xa0,
	LU_DEVICE_BOOTLOADER_CMD_READ_SIGNATURE			= 0xb0,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_RAM_BUFFER		= 0xc0,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR	= 0xc1,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_RAM_BUFFER_OVERFLOW	= 0xc2,
	LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM			= 0xd0,
	LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_INVALID_ADDR		= 0xd1,
	LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_WRONG_CRC		= 0xd2,
	LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_PAGE0_INVALID	= 0xd3,
	LU_DEVICE_BOOTLOADER_CMD_FLASH_RAM_INVALID_ORDER	= 0xd4,
	LU_DEVICE_BOOTLOADER_CMD_WRITE_SIGNATURE		= 0xe0,
	LU_DEVICE_BOOTLOADER_CMD_LAST
} LuDeviceBootloaderCmd;

/* packet to and from device */
typedef struct __attribute__((packed)) {
	guint8		 cmd;
	guint16		 addr;
	guint8		 len;
	guint8		 data[28];
} LuDeviceBootloaderRequest;

LuDeviceBootloaderRequest	*lu_device_bootloader_request_new	(void);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(LuDeviceBootloaderRequest, g_free);
#pragma clang diagnostic pop

GPtrArray	*lu_device_bootloader_parse_requests	(LuDevice	*device,
							 GBytes		*fw,
							 GError		**error);
gboolean	 lu_device_bootloader_request		(LuDevice	*device,
							 LuDeviceBootloaderRequest *req,
							 GError		**error);

guint16		 lu_device_bootloader_get_addr_lo	(LuDevice	*device);
guint16		 lu_device_bootloader_get_addr_hi	(LuDevice	*device);
void		 lu_device_bootloader_set_addr_lo	(LuDevice	*device,
							 guint16	 addr);
void		 lu_device_bootloader_set_addr_hi	(LuDevice	*device,
							 guint16	 addr);
guint16		 lu_device_bootloader_get_blocksize	(LuDevice	*device);

G_END_DECLS

#endif /* __LU_DEVICE_BOOTLOADER_H */

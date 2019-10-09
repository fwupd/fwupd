/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-usb-device.h"

#define FU_TYPE_UNIFYING_BOOTLOADER (fu_unifying_bootloader_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuUnifyingBootloader, fu_unifying_bootloader, FU, UNIFYING_BOOTLOADER, FuUsbDevice)

struct _FuUnifyingBootloaderClass
{
	FuUsbDeviceClass	parent_class;
	gboolean		 (*setup)		(FuUnifyingBootloader	*self,
							 GError			**error);
};

typedef enum {
	FU_UNIFYING_BOOTLOADER_CMD_GENERAL_ERROR		= 0x01,
	FU_UNIFYING_BOOTLOADER_CMD_READ				= 0x10,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE			= 0x20,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_INVALID_ADDR		= 0x21,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_VERIFY_FAIL		= 0x22,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_NONZERO_START		= 0x23,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_INVALID_CRC		= 0x24,
	FU_UNIFYING_BOOTLOADER_CMD_ERASE_PAGE			= 0x30,
	FU_UNIFYING_BOOTLOADER_CMD_ERASE_PAGE_INVALID_ADDR	= 0x31,
	FU_UNIFYING_BOOTLOADER_CMD_ERASE_PAGE_NONZERO_START	= 0x33,
	FU_UNIFYING_BOOTLOADER_CMD_GET_HW_PLATFORM_ID		= 0x40,
	FU_UNIFYING_BOOTLOADER_CMD_GET_FW_VERSION		= 0x50,
	FU_UNIFYING_BOOTLOADER_CMD_GET_CHECKSUM			= 0x60,
	FU_UNIFYING_BOOTLOADER_CMD_REBOOT			= 0x70,
	FU_UNIFYING_BOOTLOADER_CMD_GET_MEMINFO			= 0x80,
	FU_UNIFYING_BOOTLOADER_CMD_GET_BL_VERSION		= 0x90,
	FU_UNIFYING_BOOTLOADER_CMD_GET_INIT_FW_VERSION		= 0xa0,
	FU_UNIFYING_BOOTLOADER_CMD_READ_SIGNATURE		= 0xb0,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_RAM_BUFFER		= 0xc0,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_RAM_BUFFER_INVALID_ADDR= 0xc1,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_RAM_BUFFER_OVERFLOW	= 0xc2,
	FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM			= 0xd0,
	FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_INVALID_ADDR	= 0xd1,
	FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_WRONG_CRC		= 0xd2,
	FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_PAGE0_INVALID	= 0xd3,
	FU_UNIFYING_BOOTLOADER_CMD_FLASH_RAM_INVALID_ORDER	= 0xd4,
	FU_UNIFYING_BOOTLOADER_CMD_WRITE_SIGNATURE		= 0xe0,
	FU_UNIFYING_BOOTLOADER_CMD_LAST
} FuUnifyingBootloaderCmd;

/* packet to and from device */
typedef struct __attribute__((packed)) {
	guint8		 cmd;
	guint16		 addr;
	guint8		 len;
	guint8		 data[28];
} FuUnifyingBootloaderRequest;

FuUnifyingBootloaderRequest	*fu_unifying_bootloader_request_new	(void);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUnifyingBootloaderRequest, g_free);
#pragma clang diagnostic pop

GPtrArray	*fu_unifying_bootloader_parse_requests	(FuUnifyingBootloader	*self,
							 GBytes			*fw,
							 GError			**error);
gboolean	 fu_unifying_bootloader_request		(FuUnifyingBootloader	*self,
							 FuUnifyingBootloaderRequest *req,
							 GError			**error);

guint16		 fu_unifying_bootloader_get_addr_lo	(FuUnifyingBootloader	*self);
guint16		 fu_unifying_bootloader_get_addr_hi	(FuUnifyingBootloader	*self);
guint16		 fu_unifying_bootloader_get_blocksize	(FuUnifyingBootloader	*self);

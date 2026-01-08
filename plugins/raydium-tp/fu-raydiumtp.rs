// Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuRaydiumtpBootMode {
    TS_MAIN,
    TS_BLDR,
    TS_NONE,
}

#[repr(u8)]
enum FuRaydiumtpFtRecordInfo {
    ASCII_I,
    ASCII_D,
    PID_L,
    PID_H,
    VID_L,
    VID_H,
    SLAVE_ID,
    RESERVE,
}

#[repr(u8)]
enum FuRaydiumtpDescRecordInfo {
    VID_L = 20,
    VID_H,
    PID_L,
    PID_H,
    REV_L,
    REV_H,
    RESERVE,
}

#[repr(u8)]
enum FuRaydiumtpCmd {
	BL_CMD_ERASEFLASH				= 0x02,
	BL_CMD_WRITEFLASH				= 0x03,
	BL_CMD_SELECTFLASH				= 0x06,
	BL_CMD_WRITEREGISTER	    	= 0x08,
	BL_CMD_READ_ADDRESS_MEMORY		= 0x09,
	BL_CMD_WATCHDOG_FUNCTION_SET	= 0x0A,
	BL_CMD_WRITERAMFALSH			= 0x0B,
	BL_CMD_WRITEHIDI2CFALSH			= 0x0C,
	BL_CMD_READFLASHADDR			= 0x0D,
	BL_CMD_TRIGGER_WRITE_FLASH 		= 0xA3,
	BL_CMD_SOFTWARERESET 			= 0xA4,	
	BL_CMD_IDLE						= 0xFF,

	ADDR_JUMP_TO_BOOTLOADER        	= 0x52,
    ADDR_MEM_ADDRESS_SET			= 0x65,
    ADDR_MEM_READ					= 0x66,
    ADDR_MEM_WRITE					= 0x67,
	ADDR_SYSTEM_INFO_MODE_WRITE 	= 0x80,
	ADDR_SYSTEM_INFO_MODE_READ 		= 0x81,
	ADDR_SYSTEM_STATUS_WRITE 		= 0x90,
	ADDR_SYSTEM_STATUS_READ 		= 0x91,

	BL_AP_ADDR						= 0x00,	
	BL_WATCHDOG_ENABLE     			= 0x33,	
	BL_ERASEFLASH_MODE1				= 0xA1,
	BL_ERASEFLASH_MODE4				= 0xA4,
	BL_FLASH_CRC					= 0xA5,
	BL_WATCHDOG_DISABLE    			= 0xAA,
}

#[repr(u8)]
enum FuRaydiumtpCmd2 {
	RID								= 0x02,    
	WID								= 0x08,
	WRT								= 0x11,
	ACK								= 0x22,
	READ							= 0x33,
	CHK								= 0x44,
}

#[repr(u8)]
enum FuRaydiumtpProtect {
	ALLOCK							= 0x18,
	FWUNLOCK						= 0x68,
	GDUNLOCK						= 0x24,
	BLUNLOCK						= 0x04,
}

#[repr(u32)]
enum FuRaydiumtpFlash {
	FLASH_ADDR						= 0x00000000,
	FIRMCRC_ADDR					= 0x00015DFC,
	FT_RECORD_ADDR					= 0x0001E030,
	DESC_ADDR						= 0x0001F000,
	DESC_RECORD_ADDR				= 0x0001F004,
	DESCCRC_ADDR					= 0x0001FFFC,
}

#[repr(u32)]
enum FuRaydiumtpRam {
	BOOT_BASE						= 0x00000000,
	FIRM_BASE						= 0x00002000,
}

#[repr(u32)]
enum FuRaydiumtpFlashCtrl {
	PRAM_LOCK						= 0x50000900,
    ADDR							= 0x50000910,
    LENGTH							= 0x5000091C,
    ISPCTL							= 0x50000914,
    CHKADS							= 0x50000920,
    DATA							= 0x5000093C,
	DMA_SADDR						= 0x50000A20,
	DMA_EADDR						= 0x50000A24,
	DMA_IER							= 0x50000A14,
	DMA_CR							= 0x50000A00,
	DMA_RES							= 0x50000A28,
}

#[repr(u32)]
enum FuRaydiumtpKey {
	DISABLE_FLASH_PROTECTION		= 0x50000624,
	DISABLE							= 0x00000000,
	UNLOCK_PRAM						= 0x50000900,
	FLASH_FLKEY1					= 0x50000934,
	FLASH_FLKEY2					= 0x50000938,
	FLKEY1_KEY						= 0x000000A5,
	FLKEY2_KEY						= 0x000000C3,
	FLKEY3_KEY						= 0x000000D7,
	FLREAD_STATUS					= 0x00000105,
	FLWRITE_EN						= 0x00010006,
	FLWRITE_STATUS					= 0x00014001,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructRaydiumtpFwHdr {
    vendor_id: u16le,
	product_id: u16le,
    fw_base: u32le,
    desc_base: u32le,
    _unknown: [u8; 4],
    start: u32le,
    length: u32le,
    desc_start: u32le,
    desc_length: u32le,
}

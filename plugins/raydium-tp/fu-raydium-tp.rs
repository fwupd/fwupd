// Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[repr(u8)]
enum FuRaydiumTpBootMode {
    TsMain,
    TsBldr,
    TsNone,
}

#[repr(u8)]
enum FuRaydiumTpFtRecordInfo {
    AsciiI,
    AsciiD,
    PidL,
    PidH,
    VidL,
    VidH,
    SlaveId,
    Reserve,
}

#[repr(u8)]
enum FuRaydiumTpDescRecordInfo {
    VidL = 20,
    VidH,
    PidL,
    PidH,
    RevL,
    RevH,
    Reserve,
}

#[repr(u8)]
enum FuRaydiumTpCmd {
	BlCmdEraseFlash					= 0x02,
	BlCmdWriteFlash					= 0x03,
	BlCmdSelectFlash				= 0x06,
	BlCmdWriteRegister	    		= 0x08,
	BlCmdReadAddressMemory			= 0x09,
	BlCmdWatchdogFunctionSet		= 0x0A,
	BlCmdWriteRamFlash				= 0x0B,
	BlCmdWriteHidI2cFlash			= 0x0C,
	BlCmdReadFlashAddr				= 0x0D,
	BlCmdTriggerWriteFlash 			= 0xA3,
	BlCmdSoftwareReset 				= 0xA4,	
	BlCmdIdle						= 0xFF,

	AddrJumpToBootloader        	= 0x52,
    AddrMemAddressSet				= 0x65,
    AddrMemRead						= 0x66,
    AddrMemWrite					= 0x67,
	AddrSystemInfoModeWrite 		= 0x80,
	AddrSystemInfoModeRead 			= 0x81,
	AddrSystemStatusWrite 			= 0x90,
	AddrSystemStatusRead 			= 0x91,

	BlApAddr						= 0x00,	
	BlWatchdogEnable     			= 0x33,	
	BlEraseFlashMode1				= 0xA1,
	BlEraseFlashMode4				= 0xA4,
	BlFlashCrc						= 0xA5,
	BlWatchdogDisable    			= 0xAA,
}

#[repr(u8)]
enum FuRaydiumTpCmd2 {
	Rid								= 0x02,    
	Wid								= 0x08,
	Wrt								= 0x11,
	Ack								= 0x22,
	Read							= 0x33,
	Chk								= 0x44,
}

#[repr(u8)]
enum FuRaydiumTpProtect {
	AllLock							= 0x18,
	FwUnlock						= 0x68,
	GdUnlock						= 0x24,
	BlUnlock						= 0x04,
}

#[repr(u32)]
enum FuRaydiumTpFlash {
	FlashAddr						= 0x00000000,
	FirmCrcAddr						= 0x00015DFC,
	FtRecordAddr					= 0x0001E030,
	DescAddr						= 0x0001F000,
	DescRecordAddr					= 0x0001F004,
	DescCrcAddr						= 0x0001FFFC,
}

#[repr(u32)]
enum FuRaydiumTpRam {
	BootBase						= 0x00000000,
	FirmBase						= 0x00002000,
}

#[repr(u32)]
enum FuRaydiumTpFlashCtrl {
	PramLock						= 0x50000900,
    Addr							= 0x50000910,
    Length							= 0x5000091C,
    Ispctl							= 0x50000914,
    Chkads							= 0x50000920,
    Data							= 0x5000093C,
	DmaSaddr						= 0x50000A20,
	DmaEaddr						= 0x50000A24,
	DmaIer							= 0x50000A14,
	DmaCr							= 0x50000A00,
	DmaRes							= 0x50000A28,
}

#[repr(u32)]
enum FuRaydiumTpKey {
	DisableFlashProtection			= 0x50000624,
	Disable							= 0x00000000,
	UnlockPram						= 0x50000900,
	FlashFlkey1						= 0x50000934,
	FlashFlkey2						= 0x50000938,
	Flkey1Key						= 0x000000A5,
	Flkey2Key						= 0x000000C3,
	Flkey3Key						= 0x000000D7,
	FlreadStatus					= 0x00000105,
	FlwriteEn						= 0x00010006,
	FlwriteStatus					= 0x00014001,
	ResetReg                                        = 0x40000004,
	ResetValue                                      = 0x00000001,
}

#[repr(u8)]
enum FuRaydiumTpHidData {
	HeaderEmpty						= 0x00,
	Header1							= 0x04,
	Header3Wr						= 0x21,
	Header3Rd						= 0x12,
	Header4Wr						= 0x03,
	Header4Rd						= 0x02,	
	Header5							= 0x05,	
	Header10						= 0x3C,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructRaydiumTpFwHdr {
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

// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ParseBytes, Default)]
#[repr(C, packed)]
struct FuStructCcgxMetadataHdr {
    fw_checksum: u8,
    fw_entry: u32le,
    last_boot_row: u16le,   // last flash row of bootloader or previous firmware
    _reserved1: [u8; 2],
    fw_size: u32le,
    _reserved2: [u8; 9],
    metadata_valid: u16le = 0x4359, // "CY"
    _reserved3: [u8; 4],
    boot_seq: u32le,
}

#[derive(ToString, FromString)]
enum FuCcgxImageType {
    Unknown,
    Single,
    DualSymmetric,          // A/B runtime
    DualAsymmetric,         // A=bootloader (fixed) B=runtime
    DualAsymmetricVariable, // A=bootloader (variable) B=runtime
}

#[derive(ToString)]
enum FuCcgxFwMode {
    Boot,
    Fw1,
    Fw2,
}

#[derive(ToString)]
enum FuCcgxPdResp {
    // responses
    NoResponse,
    Success = 0x02,
    FlashDataAvailable,
    InvalidCommand = 0x05,
    CollisionDetected,
    FlashUpdateFailed,
    InvalidFw,
    InvalidArguments,
    NotSupported,
    TransactionFailed = 0x0C,
    PdCommandFailed,
    Undefined,
    RaDetect = 0x10,
    RaRemoved,

    // device specific events
    ResetComplete = 0x80,
    MessageQueueOverflow,

    // type-c specific events
    OverCurrentDetected,
    OverVoltageDetected,
    TypeCConnected,
    TypeCDisconnected,

    // pd specific events and asynchronous messages
    PdContractEstablished,
    DrSwap,
    PrSwap,
    VconSwap,
    PsRdy,
    Gotomin,
    AcceptMessage,
    RejectMessage,
    WaitMessage,
    HardReset,
    VdmReceived,
    SrcCapRcvd,
    SinkCapRcvd,
    DpAlternateMode,
    DpDeviceNonnected,
    DpDeviceNotConnected,
    DpSidNotFound,
    MultipleSvidDiscovered,
    DpFunctionNotSupported,
    DpPortConfigNotSupported,

    // not a response?
    HardResetSent,
    SoftResetSent,
    CableResetSent,
    SourceDisabledStateEntered,
    SenderResponseTimerTimeout,
    NoVdmResponseReceived,
}

enum FuCcgxHpiVendorCmd {
    GetVersion = 0xB0,      // get the version of the boot-loader
                            // value = 0, index = 0, length = 4;
                            // data_in = 32 bit version
    GetSignature = 0xBD,    // gsupposes to be 'CYUS' for normal firmware
                            // and 'CYBL' for Bootloader
    UartGetConfig = 0xC0,   // retrieve the 16 byte UART configuration information
                            // MS bit of value indicates the SCB index
                            // length = 16, data_in = 16 byte configuration
    UartSetConfig,          // update the 16 byte UART configuration information
                            // MS bit of value indicates the SCB index.
                            // length = 16, data_out = 16 byte configuration information
    SpiGetConfig,           // retrieve the 16 byte SPI configuration information
                            // MS bit of value indicates the SCB index
                            // length = 16, data_in = 16 byte configuration
    SpiSetConfig,           // update the 16 byte SPI configuration  information
                            // MS bit of value indicates the SCB index
                            // length = 16, data_out = 16 byte configuration information
    I2cGetConfig,           // retrieve the 16 byte I2C configuration information
                            // MS bit of value indicates the SCB index
                            // length = 16, data_in = 16 byte configuration
    I2cSetConfig = 0xc5,    // update the 16 byte I2C configuration information
                            // MS bit of value indicates the SCB index
                            // length = 16, data_out = 16 byte configuration information
    I2cWrite,               // perform I2C write operation
                            // value = bit0 - start, bit1 - stop, bit3 - start on idle,
                            // bits[14:8] - target address, bit15 - scbIndex. length = 0 the
                            // data  is provided over the bulk endpoints
    I2cRead,                // value = bit0 - start, bit1 - stop, bit2 - Nak last byte,
                            // bit3 - start on idle, bits[14:8] - target address, bit15 - scbIndex,
                            // length = 0. The data is provided over the bulk endpoints
    I2cGetStatus,           // value = bit0 - 0: TX 1: RX, bit15 - scbIndex, length = 3,
                            // data_in = byte0: bit0 - flag, bit1 -  bus_state, bit2 - SDA state,
                            // bit3 - TX underflow, bit4 - arbitration error, bit5 - NAK
                            // bit6 - bus error,
                            // byte[2:1] Data count remaining
    I2cReset,               // the command cleans up the I2C state machine and frees the bus
                            // value = bit0 - 0: TX path, 1: RX path; bit15 - scbIndex,
                            // length = 0
    SpiReadWrite = 0xCA,    // the command starts a read / write operation at SPI
                            // value = bit 0 - RX enable, bit 1 - TX enable, bit 15 -
                            // scbIndex; index = length of transfer
    SpiReset,               // the command resets the SPI pipes and allows it to receive new
                            // request
                            // value = bit 15 - scbIndex
    SpiGetStatus,           // the command returns the current transfer status
                            // the count will match the TX pipe status at SPI end
                            // for completion of read, read all data
                            // at the USB end signifies the end of transfer
                            // value = bit 15 - scbIndex
    JtagEnable = 0xD0,
    JtagDisable,
    JtagRead,
    JtagWrite,
    GpioGetConfig = 0xD8,
    GpioSetConfig,
    GpioGetValue,
    GpioSetValue,
    ProgUserFlash = 0xE0,   // The total space available is 512
                            // bytes this can be accessed by the user from USB. The flash
                            // area address offset is from 0x0000 to 0x00200 and can be
                            // written to page wise (128 byte)
    ReadUserFlash,
    DeviceReset = 0xE3,
}

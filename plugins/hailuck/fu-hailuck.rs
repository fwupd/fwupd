#[derive(ToString)]
enum HailuckCmd {
    Erase             = 0x45,
    ReadBlockStart    = 0x52,
    Attach            = 0x55, // guessed
    WriteBlockStart   = 0x57,
    ReadBlock         = 0x72,
    Detach            = 0x75, // guessed
    WriteBlock        = 0x77,
    GetStatus         = 0xA1,
    WriteTp           = 0xD0, // guessed
    I2cCheckChecksum  = 0xF0,
    I2cEnterBl        = 0xF1,
    I2cErase          = 0xF2,
    I2cProgram        = 0xF3,
    I2cVerifyBlock    = 0xF4,
    I2cVerifyChecksum = 0xF5,
    I2cProgrampass    = 0xF6,
    I2cEndProgram     = 0xF7,
}

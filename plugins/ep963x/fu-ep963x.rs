
// byte = 0x07
#[derive(ToString)]
enum Ep963xSmbusError {
    None = 0x00
    Address = 0x01
    NoAck = 0x02
    Arbitration = 0x04
    Command = 0x08
    Timeout = 0x10
    Busy = 0x20
}

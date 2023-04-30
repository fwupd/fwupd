#[derive(New, Validate, Parse)]
struct {{Vendor}}{{Example}} {
    signature: u8: const=0xDE,
    address: u16le,
}
#[derive(ToString)]
enum {{Vendor}}{{Example}}Status {
    Unknown,
    Failed,
}

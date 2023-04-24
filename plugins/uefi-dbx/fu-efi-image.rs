#[derive(Getters)]
struct EfiImageDataDirEntry {
    addr: u32le
    size: u32le
}

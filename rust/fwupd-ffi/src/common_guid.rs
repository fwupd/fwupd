//! C-compatible FFI binding matching the `fu-common-guid.h` API.

#[unsafe(no_mangle)]
pub unsafe extern "C" fn fu_common_guid_is_plausible(buf: *const u8) -> i32 {
    debug_assert!(!buf.is_null(), "buf must not be null");
    if buf.is_null() { return 0 }
    let data: &[u8; 16] = unsafe { &*(buf as *const [u8; 16]) };
    fwupd::common_guid::guid_is_plausible(data) as i32
}

//! GUID plausibility check.
//!
//! # Example
//!
//! ```
//! use fwupd::common_guid::guid_is_plausible;
//!
//! assert!(!guid_is_plausible(&[0u8; 16]));     // all zeros
//! assert!(guid_is_plausible(&[0xFFu8; 16]));   // all 0xFF
//! ```


/// Check whether a 16-byte buffer looks like it could be a GUID.
///
/// Returns `false` if the buffer is all zeros or if the sum of all bytes
/// is less than 0xFF (suggesting a mostly-empty buffer that is unlikely to
/// be a real GUID).
///
/// The buffer must be exactly 16 bytes.
pub fn guid_is_plausible(buf: &[u8; 16]) -> bool {
    let sum: u32 = buf.iter().map(|b| *b as u32).sum();
    sum >= 0xFF
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_zeros() {
        assert!(!guid_is_plausible(&[0u8; 16]));
    }

    #[test]
    fn low_sum() {
        let mut buf = [0u8; 16];
        buf[0] = 0x05;
        assert!(!guid_is_plausible(&buf));
    }

    #[test]
    fn all_ff() {
        assert!(guid_is_plausible(&[0xFFu8; 16]));
    }

    #[test]
    fn just_below_threshold() {
        // Sum = 0xFE (254), should return false
        let mut buf = [0u8; 16];
        buf[0] = 0xFE;
        assert!(!guid_is_plausible(&buf));
    }

    #[test]
    fn at_threshold() {
        // Sum = 0xFF (255), should return true
        let mut buf = [0u8; 16];
        buf[0] = 0xFF;
        assert!(guid_is_plausible(&buf));
    }
}

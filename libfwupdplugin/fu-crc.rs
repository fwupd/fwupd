// Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
//
// SPDX-License-Identifier: LGPL-2.1+

use std::slice;

fn crc8_full(buf: &[u8], crc_init: u8, polynomial: u8) -> u8 {
    let mut crc = crc_init as u32;
    for tmp in buf.iter() {
        crc ^= (*tmp as u32) << 8;
        for _ in 0..8 {
            if crc & 0x8000 > 0 {
                crc ^= ((polynomial as u32 | 0x100) << 7) as u32;
            }
            crc <<= 1;
        }
    }
    !(crc >> 8) as u8
}

fn crc16_full(buf: &[u8], crc_init: u16, polynomial: u16) -> u16 {
    let mut crc = crc_init;
    for tmp in buf.iter() {
        crc = crc ^ *tmp as u16;
        for _ in 0..8 {
            if crc & 0x1 > 0 {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
    }
    !crc
}

fn crc32_full(buf: &[u8], crc_init: u32, polynomial: u32) -> u32 {
    let mut crc = crc_init;
    for tmp in buf.iter() {
        crc = crc ^ *tmp as u32;
        for _ in 0..8 {
            let mask = {
                if crc & 0b1 > 0 {
                    0xFFFFFFFF
                } else {
                    0x0
                }
            };
            crc = (crc >> 1) ^ (polynomial & mask);
        }
    }
    !crc
}

#[no_mangle]
pub extern "C" fn fu_crc8_full(buf: *mut u8, bufsz: usize, crc_init: u8, polynomial: u8) -> u8 {
    let slice = unsafe { slice::from_raw_parts(buf, bufsz) };
    crc8_full(&slice, crc_init, polynomial)
}

#[no_mangle]
pub extern "C" fn fu_crc16_full(buf: *mut u8, bufsz: usize, crc_init: u16, polynomial: u16) -> u16 {
    let slice = unsafe { slice::from_raw_parts(buf, bufsz) };
    crc16_full(&slice, crc_init, polynomial)
}

#[no_mangle]
pub extern "C" fn fu_crc32_full(buf: *mut u8, bufsz: usize, crc_init: u32, polynomial: u32) -> u32 {
    let slice = unsafe { slice::from_raw_parts(buf, bufsz) };
    crc32_full(&slice, crc_init, polynomial)
}

#[test]
fn fu_crc() {
    let buf = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09];
    assert_eq!(crc8_full(&buf, 0x0, 0x07), 0x7A);
    assert_eq!(crc16_full(&buf, 0xFFFF, 0xA001), 0x4DF1);
    assert_eq!(crc32_full(&buf, 0xFFFFFFFF, 0xEDB88320), 0x40EFAB9E);
}

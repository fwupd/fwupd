/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Hughski ColorHug fwupd plugin (Rust port).
//!
//! Supports firmware updates for ColorHug, ColorHug2, and ColorHugALS
//! devices via a USB HID-like interrupt transfer protocol.

use glib::prelude::*;

use fwupdplugin::fu_debug;
use fwupdplugin::prelude::*;

// USB constants
const CH_USB_HID_EP_IN: u8 = 0x81;
const CH_USB_HID_EP_OUT: u8 = 0x01;
const CH_USB_HID_EP_SIZE: usize = 64;
const CH_USB_CONFIG: i32 = 1;
const CH_USB_INTERFACE: u8 = 0;
const CH_DEVICE_USB_TIMEOUT: u32 = 5000;

// vendor-specific USB class for custom descriptors
const FU_USB_CLASS_VENDOR_SPECIFIC: u8 = 0xff;

// commands
const CH_CMD_GET_FIRMWARE_VERSION: u8 = 0x07;
const CH_CMD_RESET: u8 = 0x24;
const CH_CMD_READ_FLASH: u8 = 0x25;
const CH_CMD_WRITE_FLASH: u8 = 0x26;
const CH_CMD_BOOT_FLASH: u8 = 0x27;
const CH_CMD_SET_FLASH_SUCCESS: u8 = 0x28;
const CH_CMD_ERASE_FLASH: u8 = 0x29;

// error codes from device
const CH_ERROR_NONE: u8 = 0;

// flags from fwupd_sys
const FWUPD_DEVICE_FLAG_IS_BOOTLOADER: u64 =
    fwupdplugin::fwupd_sys::FWUPD_DEVICE_FLAG_IS_BOOTLOADER;
const FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG: u64 =
    fwupdplugin::fwupd_sys::FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG;
const FWUPD_DEVICE_FLAG_UPDATABLE: u64 = fwupdplugin::fwupd_sys::FWUPD_DEVICE_FLAG_UPDATABLE;
const FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD: u64 =
    fwupdplugin::fwupd_sys::FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD;

const FWUPD_VERSION_FORMAT_BCD: fwupdplugin::fwupd_sys::FwupdVersionFormat =
    fwupdplugin::fwupd_sys::FWUPD_VERSION_FORMAT_BCD;
const FWUPD_VERSION_FORMAT_TRIPLET: fwupdplugin::fwupd_sys::FwupdVersionFormat =
    fwupdplugin::fwupd_sys::FWUPD_VERSION_FORMAT_TRIPLET;

const FWUPD_STATUS_DEVICE_BUSY: fwupdplugin::fwupd_sys::FwupdStatus =
    fwupdplugin::fwupd_sys::FWUPD_STATUS_DEVICE_BUSY;
const FWUPD_STATUS_DEVICE_ERASE: fwupdplugin::fwupd_sys::FwupdStatus =
    fwupdplugin::fwupd_sys::FWUPD_STATUS_DEVICE_ERASE;
const FWUPD_STATUS_DEVICE_WRITE: fwupdplugin::fwupd_sys::FwupdStatus =
    fwupdplugin::fwupd_sys::FWUPD_STATUS_DEVICE_WRITE;
const FWUPD_STATUS_DEVICE_VERIFY: fwupdplugin::fwupd_sys::FwupdStatus =
    fwupdplugin::fwupd_sys::FWUPD_STATUS_DEVICE_VERIFY;
const FWUPD_STATUS_DEVICE_RESTART: fwupdplugin::fwupd_sys::FwupdStatus =
    fwupdplugin::fwupd_sys::FWUPD_STATUS_DEVICE_RESTART;
const FWUPD_STATUS_DECOMPRESSING: fwupdplugin::fwupd_sys::FwupdStatus =
    fwupdplugin::fwupd_sys::FWUPD_STATUS_DECOMPRESSING;

// device private flag name (registered in quirk files as "halfsize")
const FLAG_HALFSIZE: &str = "halfsize";

// ---------------------------------------------------------------------------
// Device implementation
// ---------------------------------------------------------------------------

mod device_imp {
    use super::*;
    use glib::subclass::prelude::*;

    pub struct ColorhugDevice {
        pub start_addr: std::cell::Cell<u16>,
    }

    impl Default for ColorhugDevice {
        fn default() -> Self {
            Self {
                start_addr: std::cell::Cell::new(0x4000),
            }
        }
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ColorhugDevice {
        const NAME: &'static str = "FuHughskiColorhugRsDevice";
        type Type = super::ColorhugDevice;
        type ParentType = fwupdplugin::UsbDevice;
    }

    impl ObjectImpl for ColorhugDevice {
        fn constructed(&self) {
            self.parent_constructed();
            let device = self.obj();
            device.add_protocol("com.hughski.colorhug");
            device.set_remove_delay(fwupdplugin::ffi::FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE as u32);
            device.add_private_flag("add-counterpart-guids");
            device.add_private_flag("replug-match-guid");
            device.add_flag(FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
            device.register_private_flag(FLAG_HALFSIZE);
            device.set_usb_configuration(CH_USB_CONFIG);
            device.add_interface(CH_USB_INTERFACE);
        }
    }

    impl DeviceImpl for ColorhugDevice {
        fn probe(&self) -> Result<(), glib::Error> {
            let device = self.obj();
            if device.has_private_flag(FLAG_HALFSIZE) {
                self.start_addr.set(0x2000);
            }
            device.add_flag(FWUPD_DEVICE_FLAG_UPDATABLE);
            Ok(())
        }

        fn setup(&self) -> Result<(), glib::Error> {
            self.parent_setup()?;
            let device = self.obj();

            // try to get the firmware version from USB descriptors
            if let Ok(idx) = device.get_custom_index(FU_USB_CLASS_VENDOR_SPECIFIC, b'F', b'W') {
                if let Ok(version) = device.get_string_descriptor(idx) {
                    let fmt = unsafe {
                        fwupdplugin::ffi::fu_version_guess_format(version.as_ptr() as *const _)
                    };
                    device.set_version_format(fmt);
                    device.set_version(&version);
                }
            }

            // try to get the GUID from USB descriptors
            if let Ok(idx) = device.get_custom_index(FU_USB_CLASS_VENDOR_SPECIFIC, b'G', b'U') {
                if let Ok(guid) = device.get_string_descriptor(idx) {
                    device.add_instance_id(&guid);
                }
            }

            // if version format is still BCD, get version via HID command
            if device.version_format() == FWUPD_VERSION_FORMAT_BCD {
                if let Ok(version) =
                    super::get_firmware_version(device.upcast_ref::<fwupdplugin::UsbDevice>())
                {
                    device.set_version_format(FWUPD_VERSION_FORMAT_TRIPLET);
                    device.set_version(&version);
                }
            }

            Ok(())
        }

        fn detach(&self, _progress: &Progress) -> Result<(), glib::Error> {
            let device = self.obj();
            if device.has_flag(FWUPD_DEVICE_FLAG_IS_BOOTLOADER) {
                fu_debug!("FuHughskiColorhugRs", "already in bootloader mode");
                return Ok(());
            }
            // send RESET to enter bootloader
            super::send_cmd(
                device.upcast_ref::<fwupdplugin::UsbDevice>(),
                CH_CMD_RESET,
                &[],
            )?;
            device.add_flag(FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
            Ok(())
        }

        fn attach(&self, _progress: &Progress) -> Result<(), glib::Error> {
            let device = self.obj();
            if !device.has_flag(FWUPD_DEVICE_FLAG_IS_BOOTLOADER) {
                fu_debug!("FuHughskiColorhugRs", "already in runtime mode");
                return Ok(());
            }
            // send BOOT_FLASH to leave bootloader
            super::send_cmd(
                device.upcast_ref::<fwupdplugin::UsbDevice>(),
                CH_CMD_BOOT_FLASH,
                &[],
            )?;
            device.add_flag(FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
            Ok(())
        }

        fn reload(&self) -> Result<(), glib::Error> {
            let device = self.obj();
            super::set_flash_success(device.upcast_ref::<fwupdplugin::UsbDevice>(), true)
        }

        fn write_firmware(
            &self,
            firmware: &Firmware,
            progress: &Progress,
            _flags: fwupdplugin::fwupd_sys::FwupdInstallFlags,
        ) -> Result<(), glib::Error> {
            let device = self.obj();
            let start_addr = self.start_addr.get();

            progress.set_id(module_path!());
            progress.add_step(FWUPD_STATUS_DEVICE_BUSY, 1, Some("set-flash-success"));
            progress.add_step(FWUPD_STATUS_DEVICE_ERASE, 19, Some("erase"));
            progress.add_step(FWUPD_STATUS_DEVICE_WRITE, 44, Some("write"));
            progress.add_step(FWUPD_STATUS_DEVICE_VERIFY, 35, Some("verify"));

            // step 1: mark flash as not-yet-successful
            super::set_flash_success(device.upcast_ref::<fwupdplugin::UsbDevice>(), false)?;
            progress.step_done();

            // step 2: erase flash region
            let stream = firmware.stream()?;
            let fw_size = firmware.size();
            super::erase_flash(
                device.upcast_ref::<fwupdplugin::UsbDevice>(),
                start_addr,
                fw_size as u16,
            )?;
            progress.step_done();

            // step 3: write firmware in 32-byte blocks
            let chunks = ChunkArray::from_stream(&stream, start_addr as usize, 0, 32)?;
            super::write_blocks(
                device.upcast_ref::<fwupdplugin::UsbDevice>(),
                &chunks,
                &progress.child(),
            )?;
            progress.step_done();

            // step 4: verify by reading back
            super::verify_blocks(
                device.upcast_ref::<fwupdplugin::UsbDevice>(),
                &chunks,
                &progress.child(),
            )?;
            progress.step_done();

            Ok(())
        }

        fn set_progress(&self, progress: &Progress) {
            progress.set_id(module_path!());
            progress.add_step(FWUPD_STATUS_DECOMPRESSING, 0, Some("prepare-fw"));
            progress.add_step(FWUPD_STATUS_DEVICE_RESTART, 0, Some("detach"));
            progress.add_step(FWUPD_STATUS_DEVICE_WRITE, 57, Some("write"));
            progress.add_step(FWUPD_STATUS_DEVICE_RESTART, 0, Some("attach"));
            progress.add_step(FWUPD_STATUS_DEVICE_BUSY, 43, Some("reload"));
        }
    }

    impl UdevDeviceImpl for ColorhugDevice {}
    impl UsbDeviceImpl for ColorhugDevice {}
}

glib::wrapper! {
    pub struct ColorhugDevice(ObjectSubclass<device_imp::ColorhugDevice>)
        @extends fwupdplugin::UsbDevice, fwupdplugin::UdevDevice, fwupdplugin::Device;
}

// ---------------------------------------------------------------------------
// USB HID protocol helpers
// ---------------------------------------------------------------------------

/// Send a command to the device over the interrupt OUT endpoint.
fn send_cmd(device: &fwupdplugin::UsbDevice, cmd: u8, data: &[u8]) -> Result<(), glib::Error> {
    let mut buf = [0u8; CH_USB_HID_EP_SIZE];
    buf[0] = cmd;
    let len = data.len().min(CH_USB_HID_EP_SIZE - 1);
    buf[1..1 + len].copy_from_slice(&data[..len]);

    fu_debug!(
        "FuHughskiColorhugRs",
        "sending cmd 0x{:02x} with {} bytes",
        cmd,
        len
    );

    match device.interrupt_transfer(CH_USB_HID_EP_OUT, &mut buf, CH_DEVICE_USB_TIMEOUT) {
        Ok(_) => Ok(()),
        Err(e) => {
            // for RESET command, NOT_FOUND errors are expected (device disconnects)
            if cmd == CH_CMD_RESET {
                fu_debug!("FuHughskiColorhugRs", "ignoring error for reset: {}", e);
                Ok(())
            } else {
                Err(e)
            }
        }
    }
}

/// Receive a response from the device over the interrupt IN endpoint.
fn recv_cmd(device: &fwupdplugin::UsbDevice, cmd: u8, data: &mut [u8]) -> Result<(), glib::Error> {
    let mut buf = [0u8; CH_USB_HID_EP_SIZE];

    let actual = match device.interrupt_transfer(CH_USB_HID_EP_IN, &mut buf, CH_DEVICE_USB_TIMEOUT)
    {
        Ok(n) => n,
        Err(e) => {
            // for RESET command, NOT_FOUND errors are expected
            if cmd == CH_CMD_RESET {
                fu_debug!(
                    "FuHughskiColorhugRs",
                    "ignoring recv error for reset: {}",
                    e
                );
                return Ok(());
            }
            return Err(e);
        }
    };

    if actual != CH_USB_HID_EP_SIZE {
        return Err(glib::Error::new(
            glib::FileError::Failed,
            &format!("expected {} bytes, got {}", CH_USB_HID_EP_SIZE, actual),
        ));
    }

    // check error code (byte 0)
    let error_code = buf[0];
    if error_code != CH_ERROR_NONE {
        return Err(glib::Error::new(
            glib::FileError::Failed,
            &format!("device returned error code {}", error_code),
        ));
    }

    // check command echo (byte 1)
    if buf[1] != cmd {
        return Err(glib::Error::new(
            glib::FileError::Failed,
            &format!("expected cmd 0x{:02x}, got 0x{:02x}", cmd, buf[1]),
        ));
    }

    // copy payload
    let payload_len = data.len().min(CH_USB_HID_EP_SIZE - 2);
    data[..payload_len].copy_from_slice(&buf[2..2 + payload_len]);

    Ok(())
}

/// Send a command and receive its response.
fn msg(
    device: &fwupdplugin::UsbDevice,
    cmd: u8,
    send_data: &[u8],
    recv_data: &mut [u8],
) -> Result<(), glib::Error> {
    send_cmd(device, cmd, send_data)?;
    recv_cmd(device, cmd, recv_data)
}

/// Get the firmware version via the HID protocol.
fn get_firmware_version(device: &fwupdplugin::UsbDevice) -> Result<String, glib::Error> {
    let mut buf = [0u8; 6];
    msg(device, CH_CMD_GET_FIRMWARE_VERSION, &[], &mut buf)?;

    let major = u16::from_le_bytes([buf[0], buf[1]]);
    let minor = u16::from_le_bytes([buf[2], buf[3]]);
    let micro = u16::from_le_bytes([buf[4], buf[5]]);

    Ok(format!("{}.{}.{}", major, minor, micro))
}

/// Set the flash success flag on the device.
fn set_flash_success(device: &fwupdplugin::UsbDevice, val: bool) -> Result<(), glib::Error> {
    let data = [if val { 0x01u8 } else { 0x00u8 }];
    send_cmd(device, CH_CMD_SET_FLASH_SUCCESS, &data)?;
    recv_cmd(device, CH_CMD_SET_FLASH_SUCCESS, &mut [])
}

/// Erase a flash region.
fn erase_flash(device: &fwupdplugin::UsbDevice, addr: u16, size: u16) -> Result<(), glib::Error> {
    let mut data = [0u8; 4];
    data[0..2].copy_from_slice(&addr.to_le_bytes());
    data[2..4].copy_from_slice(&size.to_le_bytes());
    send_cmd(device, CH_CMD_ERASE_FLASH, &data)?;
    recv_cmd(device, CH_CMD_ERASE_FLASH, &mut [])
}

/// Write firmware in 32-byte blocks.
fn write_blocks(
    device: &fwupdplugin::UsbDevice,
    chunks: &ChunkArray,
    progress: &Progress,
) -> Result<(), glib::Error> {
    progress.set_id(module_path!());
    progress.set_steps(chunks.len());

    for i in 0..chunks.len() {
        let chunk = chunks.index(i)?;
        let addr = chunk.address() as u16;
        let chunk_data = chunk.data();
        let chunk_len = chunk.data_sz() as u8;

        // build write buffer: addr(2) + len(1) + checksum(1) + data(32)
        let mut buf = [0u8; 36];
        buf[0..2].copy_from_slice(&addr.to_le_bytes());
        buf[2] = chunk_len;
        buf[3] = chunk_data.iter().fold(0u8, |acc, &b| acc ^ b); // XOR checksum
        let copy_len = chunk_data.len().min(32);
        buf[4..4 + copy_len].copy_from_slice(&chunk_data[..copy_len]);

        send_cmd(device, CH_CMD_WRITE_FLASH, &buf[..4 + copy_len])?;
        recv_cmd(device, CH_CMD_WRITE_FLASH, &mut [])?;
        progress.step_done();
    }
    Ok(())
}

/// Verify firmware by reading back and comparing.
fn verify_blocks(
    device: &fwupdplugin::UsbDevice,
    chunks: &ChunkArray,
    progress: &Progress,
) -> Result<(), glib::Error> {
    progress.set_id(module_path!());
    progress.set_steps(chunks.len());

    for i in 0..chunks.len() {
        let chunk = chunks.index(i)?;
        let addr = chunk.address() as u16;
        let chunk_data = chunk.data();
        let chunk_len = chunk.data_sz() as u8;

        // send read request: addr(2) + len(1)
        let mut req = [0u8; 3];
        req[0..2].copy_from_slice(&addr.to_le_bytes());
        req[2] = chunk_len;

        let mut read_buf = [0u8; 33];
        msg(device, CH_CMD_READ_FLASH, &req, &mut read_buf)?;

        // compare
        let read_data = &read_buf[..chunk_data.len()];
        if read_data != chunk_data {
            return Err(glib::Error::new(
                glib::FileError::Failed,
                &format!("verify failed at address 0x{:04x}: data mismatch", addr),
            ));
        }

        progress.step_done();
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// Plugin implementation
// ---------------------------------------------------------------------------

#[derive(Default)]
struct ColorhugPlugin;

impl PluginImpl for ColorhugPlugin {
    fn constructed(&self, plugin: &Plugin) {
        plugin.add_udev_subsystem("usb");
        plugin.add_device_gtype(ColorhugDevice::static_type());
    }
}

fwupdplugin::export_plugin!(ColorhugPlugin);

Altos Support
=============

Introduction
------------

Altos is a 8051 operating system for Altus-Metrum projects.
The ChaosKey is a hardware random number generator that attaches via USB.

When the ChaosKey when inserted it appears as a device handled by the kernel
with VID 0x1d50 and PID 0x60c6. If pins 1 and 5 are shorted as the device is
connected then the bootloader is run, which presents VID 0xfffe and PID 0x000a.

The bootloader communication is not handled in the kernel, and a tty device is
created so userspace can communicate with the hardware. Commands the bootloader
accept are as follows:

### List Information

Command:    `l\n`
Several lines of text about the device are transferred to the host, e.g.

    altos-loader
    manufacturer     altusmetrum.org
    product          AltosFlash
    flash-range      08001000 08008000
    software-version 1.6.8

There doesn't appear to be any kind of end-of-message signal.

### Read Flash

Command:    `R $addr\n` where `$addr` is a memory address `0x8001000->0x8008000`.
256 bytes of raw data are then transferred to the host.

### Write Flash

Command:    `W $addr\n` where `$addr` is a memory address `0x8001000->0x8008000`.
256 bytes of raw data are then transferred to the device.

### Application Mode

Command:    `v\n`
The device will reboot into application mode. This is typically performed after
flashing firmware completes successfully.

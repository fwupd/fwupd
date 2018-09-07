Unifying Support
================

Introduction
------------

This plugin can flash the firmware on Logitech Unifying dongles, both the
Nordic (U0007) device and the Texas Instruments (U0008) version.

This plugin will not work with the different "Nano" dongle (U0010) as it does
not use the Unifying protocol.

Some bootloader protocol infomation was taken from the Mousejack[1] project,
specifically logitech-usb-restore.py and unifying.py. Other documentation was
supplied by Logitech.

Additional constants were taken from the Solaar[2] project.

Design Notes
------------

When a dongle is detected in bootloader mode we detach the hidraw driver from
the kernel and use raw control transfers. This ensures that we don't accidentally
corrupt the uploading firmware. For application firmware we use hidraw which
means the hardware keeps working while probing, and also allows us to detect
paired devices.

[1] https://www.mousejack.com/
[2] https://pwr.github.io/Solaar/

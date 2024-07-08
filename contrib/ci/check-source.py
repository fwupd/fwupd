#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import sys


def test_files() -> int:
    # test all C and H files
    rc: int = 0
    for fn in sorted(glob.glob("**/*.[c|h]", recursive=True)):
        if fn.startswith("subprojects"):
            continue
        if fn.startswith("build"):
            continue
        if fn.startswith("dist"):
            continue
        if fn.startswith("contrib/ci"):
            continue
        if fn.startswith("venv"):
            continue
        with open(fn, "rb") as f:
            linecnt_g_set_error: int = 0
            for linecnt, line in enumerate(f.read().decode().split("\n")):
                if line.find("nocheck") != -1:
                    continue
                for token, msg in {
                    "cbor_get_uint8(": "Use cbor_get_int() instead",
                    "cbor_get_uint16(": "Use cbor_get_int() instead",
                    "cbor_get_uint32(": "Use cbor_get_int() instead",
                    "g_error(": "Use GError instead",
                    "g_byte_array_free_to_bytes(": "Use g_bytes_new() instead",
                    "g_usb_device_bulk_transfer(": "Use fu_usb_device_bulk_transfer() instead",
                    "g_usb_device_claim_interface(": "Use fu_usb_device_claim_interface() instead",
                    "g_usb_device_control_transfer(": "Use fu_usb_device_control_transfer() instead",
                    "g_usb_device_get_configuration_index(": "Use fu_usb_device_get_configuration_index() instead",
                    "g_usb_device_get_custom_index(": "Use fu_usb_device_get_custom_index() instead",
                    "g_usb_device_get_device_class(": "Use fu_usb_device_get_class() instead",
                    "g_usb_device_get_interface(": "Use fu_usb_device_get_interface() instead",
                    "g_usb_device_get_interfaces(": "Use fu_usb_device_get_interfaces() instead",
                    "g_usb_device_get_release(": "Use fu_usb_device_get_release() instead",
                    "g_usb_device_get_serial_number_index(": "Use fu_usb_device_get_serial_number_index() instead",
                    "g_usb_device_get_string_descriptor_bytes_full(": "Use fu_usb_device_get_string_descriptor_bytes_full() instead",
                    "g_usb_device_get_string_descriptor desc_index(": "Use fu_usb_device_get_string_descriptor desc_index() instead",
                    "g_usb_device_interrupt_transfer(": "Use fu_usb_device_interrupt_transfer() instead",
                    "g_usb_device_release_interface(": "Use fu_usb_device_release_interface() instead",
                    "g_usb_device_reset error(": "Use fu_usb_device_reset error() instead",
                    "g_usb_device_set_interface_alt(": "Use fu_usb_device_set_interface_alt() instead",
                    "g_ascii_strtoull(": "Use fu_strtoull() instead",
                    "g_ascii_strtoll(": "Use fu_strtoll() instead",
                }.items():
                    if line.find(token) != -1:
                        print(
                            f"{fn}:{linecnt} contains blocked token {token}: {msg} -- "
                            "use a nocheck comment to ignore"
                        )
                        rc = 1
                        break

                # do not use G_IO_ERROR internally
                if line.find("g_set_error") != -1:
                    linecnt_g_set_error = linecnt
                if linecnt - linecnt_g_set_error < 5:
                    for error_domain in ["G_IO_ERROR", "G_FILE_ERROR"]:
                        if line.find(error_domain) != -1:
                            print(
                                f"{fn}:{linecnt} uses g_set_error() without using FWUPD_ERROR: -- "
                                "use a nocheck comment to ignore"
                            )
                            rc = 1
                            break

    return rc


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())

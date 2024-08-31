#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import sys
import os
from typing import List, Optional


class SourceFailure:
    def __init__(self, fn=None, linecnt=None, message=None, nocheck=True):
        self.fn: Optional[str] = fn
        self.linecnt: Optional[int] = linecnt
        self.message: Optional[str] = message
        self.nocheck: bool = nocheck


class Checker:
    def __init__(self):
        self.failures: List[SourceFailure] = []
        self._current_fn: Optional[str] = None
        self._current_linecnt: Optional[int] = None
        self._current_nocheck: bool = False

    def add_failure(self, message=None):
        self.failures.append(
            SourceFailure(
                fn=self._current_fn,
                linecnt=self._current_linecnt,
                message=message,
                nocheck=self._current_nocheck,
            )
        )

    def _test_line_debug_fns(self, line: str) -> None:
        # no console output expected
        if os.path.basename(self._current_fn) in [
            "fu-console.c",
            "fu-daemon.c",
            "fu-dbxtool.c",
            "fu-debug.c",
            "fu-fuzzer-main.c",
            "fu-gcab.c",
            "fu-main.c",
            "fu-main-windows.c",
            "fu-offline.c",
            "fu-self-test.c",
            "fu-tpm-eventlog.c",
            "fwupd-self-test.c",
        ]:
            return
        for token, msg in {
            "g_print(": "Use g_debug() instead",
            "g_printerr(": "Use g_debug() instead",
        }.items():
            if line.find(token) != -1:
                self.add_failure(f"contains blocked token {token}: {msg}")

    def _test_line_blocked_fns(self, line: str) -> None:
        self._current_nocheck = True
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
            "g_assert(": "Use g_set_error() or g_return_val_if_fail() instead",
            "g_udev_device_get_sysfs_attr(": "Use fu_udev_device_read_sysfs() instead",
            "g_udev_device_get_property(": "Use fu_udev_device_read_property() instead",
        }.items():
            if line.find(token) != -1:
                self.add_failure("contains blocked token {token}: {msg}")

    def _test_lines_gerror(self, lines: List[str]) -> None:
        self._current_nocheck = True
        linecnt_g_set_error: int = 0
        for linecnt, line in enumerate(lines):
            if line.find("nocheck") != -1:
                continue
            self._current_linecnt = linecnt

            # do not use G_IO_ERROR internally
            if line.find("g_set_error") != -1:
                linecnt_g_set_error = linecnt
            if linecnt - linecnt_g_set_error < 5:
                for error_domain in ["G_IO_ERROR", "G_FILE_ERROR"]:
                    if line.find(error_domain) != -1:
                        self.add_failure("uses g_set_error() without using FWUPD_ERROR")
                        break

    def _test_lines_depth(self, lines: List[str]) -> None:
        self._current_nocheck = True

        # check depth
        depth: int = 0
        for linecnt, line in enumerate(lines):
            if line.find("nocheck") != -1:
                continue
            self._current_linecnt = linecnt
            for char in line:
                if char == "{":
                    depth += 1
                    if depth > 5:
                        self.add_failure("is nested too deep")
                        success = False
                    continue
                if char == "}":
                    if depth == 0:
                        self.add_failure("has unequal nesting")
                        success = False
                        continue
                    depth -= 1

        # sanity check
        self._current_linecnt = None
        if depth != 0:
            self.add_failure("nesting was weird")
            success = False

    def _test_lines(self, lines: List[str]) -> None:
        lines_nocheck: List[str] = []

        # tests we can do line by line
        self._current_nocheck = True
        for linecnt, line in enumerate(lines):
            if line.find("nocheck") != -1:
                continue
            self._current_linecnt = linecnt

            # test for blocked functions
            self._test_line_blocked_fns(line)

            # test for debug lines
            self._test_line_debug_fns(line)

        # using FUWPD_ERROR domains
        self._test_lines_gerror(lines)

        # not nesting too deep
        self._test_lines_depth(lines)

    def test_file(self, fn: str) -> None:
        self._current_fn = fn
        with open(fn, "rb") as f:
            self._test_lines(f.read().decode().split("\n"))


def test_files() -> int:
    # test all C and H files
    rc: int = 0

    checker = Checker()
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
        checker.test_file(fn)

    # show issues
    for failure in checker.failures:
        line: str = failure.fn
        if failure.linecnt:
            line += f":{failure.linecnt}"
        line += f": {failure.message}"
        if failure.nocheck:
            line += " -- use a nocheck comment to ignore"
        print(line)
    return 1 if checker.failures else 0


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())

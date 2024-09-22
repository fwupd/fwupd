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
    def __init__(self, fn=None, linecnt=None, message=None, nocheck=None):
        self.fn: Optional[str] = fn
        self.linecnt: Optional[int] = linecnt
        self.message: Optional[str] = message
        self.nocheck: Optional[str] = nocheck


class Checker:
    MAX_FUNCTION_LINES: int = 400

    def __init__(self):
        self.failures: List[SourceFailure] = []
        self._current_fn: Optional[str] = None
        self._current_linecnt: Optional[int] = None
        self._current_nocheck: Optional[str] = None

    def add_failure(self, message=None):
        self.failures.append(
            SourceFailure(
                fn=self._current_fn,
                linecnt=self._current_linecnt,
                message=message,
                nocheck=self._current_nocheck,
            )
        )

    def _test_line_function_names_private(self, func_name: str) -> None:
        valid_prefixes = ["_fwupd_", "_fu_", "_g_", "_xb_"]
        for prefix in valid_prefixes:
            if func_name.startswith(prefix):
                return
        self.add_failure(
            f"invalid function name {func_name} should have {'|'.join(valid_prefixes)} prefix"
        )

    def _test_line_function_names_valid(self, func_name: str) -> None:
        # ignore headers
        if self._current_fn.endswith(".h"):
            return

        # ignore all self tests FIXME: 335 failures
        if self._current_fn.endswith("-test.c"):
            return

        # namespacing is strange here
        if self._current_fn.endswith("fwupd-enums.c"):
            return

        # doh
        if func_name in ["main", "fu_plugin_init_vfuncs"]:
            return

        # this is stuff that should move to GLib
        if func_name.startswith("_g_"):
            return

        # remove some suffixes we do not care about
        prefix = os.path.basename(self._current_fn).split(".")[0].replace("-", "_")
        for suffix in [
            "_common",
            "_darwin",
            "_freebsd",
            "_helper",
            "_impl",
            "_linux",
            "_sync",
            "_windows",
        ]:
            if prefix.endswith(suffix):
                prefix = prefix[: -len(suffix)]

        # allowed truncations
        valid_prefixes = []
        valid_prefixes.append(prefix)
        for key, value in {
            "fu_crc": "fu_misr",  # FIXME: split out to fu-misr.[c|h]
            "fu_darwin_efivars": "fu_efivars",
            "fu_dbus_daemon": "fu_daemon",
            "fu_dbxtool": "fu_util",
            "fu_freebsd_efivars": "fu_efivars",
            "fu_linux_efivars": "fu_efivars",
            "fu_logitech_hidpp_hidpp": "fu_logitech_hidpp",
            "fu_logitech_hidpp_hidpp_msg": "fu_logitech_hidpp",
            "fu_self_test": "fu_test",
            "fu_string": "fu_str,fu_utf",
            "fu_tool": "fu_util",
            "fu_windows_efivars": "fu_efivars",
        }.items():
            if prefix == key:
                valid_prefixes.extend(value.split(","))
        for prefix in valid_prefixes:
            if func_name.startswith(prefix):
                return

        self.add_failure(
            f"invalid function name {func_name} should have {'|'.join(valid_prefixes)} prefix"
        )

    def _test_line_function_names(self, line: str) -> None:
        # empty line
        if not line:
            return

        # skip!
        self._current_nocheck = "nocheck:name"
        if line.find(self._current_nocheck) != -1:
            return

        # these are not functions
        for prefix in ["\t", " ", "typedef", "__attribute__", "G_DEFINE_"]:
            if line.startswith(prefix):
                return

        # ignore prototypes
        if line.endswith(";"):
            return

        # strip to function name then test for validity
        idx: int = line.find("(")
        if idx == -1:
            return
        func_name = line[:idx]
        if func_name.find(" ") != -1:
            return
        if func_name.startswith("_"):
            self._test_line_function_names_private(func_name)
            return
        self._test_line_function_names_valid(func_name)

    def _test_line_debug_fns(self, line: str) -> None:
        # no console output expected
        self._current_nocheck = "nocheck:print"
        if line.find(self._current_nocheck) != -1:
            return
        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-console.c",
            "fu-daemon.c",
            "fu-dbxtool.c",
            "fu-debug.c",
            "fu-fuzzer-main.c",
            "fu-gcab.c",
            "fu-main.c",
            "fu-main-windows.c",
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
        self._current_nocheck = "nocheck:blocked"
        if line.find(self._current_nocheck) != -1:
            return
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
            "g_udev_client_new(": "Use fu_backend_create_device() instead",
            "HIDIOCSFEATURE": "Use fu_hidraw_device_set_feature() instead",
            "HIDIOCGFEATURE": "Use fu_hidraw_device_get_feature() instead",
            "|= 1 <<": "Use FU_BIT_SET() instead",
            "|= 1u <<": "Use FU_BIT_SET() instead",
            "|= 1ull <<": "Use FU_BIT_SET() instead",
            "|= (1 <<": "Use FU_BIT_SET() instead",
            "|= (1u <<": "Use FU_BIT_SET() instead",
            "|= (1ull <<": "Use FU_BIT_SET() instead",
            "&= ~(1 <<": "Use FU_BIT_CLEAR() instead",
            "&= ~(1u <<": "Use FU_BIT_CLEAR() instead",
            "&= ~(1ull <<": "Use FU_BIT_CLEAR() instead",
            "__attribute__((packed))": "Use rustgen instead",
            "memcpy(": "Use fu_memcpy_safe or rustgen instead",
            " ioctl(": "Use fu_udev_device_ioctl() instead",
        }.items():
            if line.find(token) != -1:
                self.add_failure(f"contains blocked token {token}: {msg}")

    def _test_lines_gerror(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:error"
        linecnt_g_set_error: int = 0
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
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

    def _test_lines_function_length(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:lines"
        func_begin: int = 0
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
                func_begin = 0
                continue
            if line == "{":
                func_begin = linecnt
                continue
            if func_begin > 0 and line == "}":
                self._current_linecnt = func_begin
                if linecnt - func_begin > self.MAX_FUNCTION_LINES:
                    self.add_failure(
                        f"function is too long, was {linecnt - func_begin} of {self.MAX_FUNCTION_LINES}"
                    )
                func_begin = 0

    def _test_lines_firmware_convert_version(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:set-version"

        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-firmware.c",
            "fu-firmware.h",
        ]:
            return

        # contains fu_firmware_set_version_raw()
        _set_version_raw: bool = False
        for line in lines:
            if line.find("fu_firmware_set_version_raw(") != -1:
                _set_version_raw = True
                break
        if not _set_version_raw:
            return

        # also contains fu_firmware_set_version()
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
                continue
            self._current_linecnt = linecnt
            if line.find("fu_firmware_set_version(") != -1:
                self.add_failure(
                    "Use FuFirmwareClass->convert_version rather than fu_firmware_set_version()"
                )

    def _test_lines_device_convert_version(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:set-version"

        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-device.c",
            "fu-device.h",
            "fu-self-test.c",
        ]:
            return

        # contains fu_firmware_set_version_raw()
        _set_version_raw: bool = False
        for line in lines:
            if line.find("fu_device_set_version_raw(") != -1:
                _set_version_raw = True
                break
        if not _set_version_raw:
            return

        # also contains fu_firmware_set_version()
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
                continue
            self._current_linecnt = linecnt
            if line.find("fu_device_set_version(") != -1:
                self.add_failure(
                    "Use FuDeviceClass->convert_version rather than fu_device_set_version()"
                )

    def _test_lines_depth(self, lines: List[str]) -> None:
        # check depth
        self._current_nocheck = "nocheck:depth"
        depth: int = 0
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
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
        for linecnt, line in enumerate(lines):
            self._current_linecnt = linecnt

            # test for blocked functions
            self._test_line_blocked_fns(line)

            # test for debug lines
            self._test_line_debug_fns(line)

            # test for function names
            self._test_line_function_names(line)

        # using FUWPD_ERROR domains
        self._test_lines_gerror(lines)

        # not nesting too deep
        self._test_lines_depth(lines)

        # functions too long
        self._test_lines_function_length(lines)

        # should use FuFirmwareClass->convert_version
        self._test_lines_firmware_convert_version(lines)

        # should use FuDeviceClass->convert_version
        self._test_lines_device_convert_version(lines)

    def test_file(self, fn: str) -> None:
        self._current_fn = fn
        with open(fn, "rb") as f:
            self._test_lines(f.read().decode().split("\n"))


def test_files() -> int:
    # test all C and H files
    rc: int = 0

    checker = Checker()
    for fn in (
        glob.glob("libfwupd/*.[c|h]")
        + glob.glob("libfwupdplugin/*.[c|h]")
        + glob.glob("plugins/*/*.[c|h]")
        + glob.glob("src/*.[c|h]")
    ):
        checker.test_file(fn)

    # show issues
    for failure in checker.failures:
        line: str = ""
        if failure.fn:
            line += failure.fn
        if failure.linecnt:
            line += f":{failure.linecnt}"
        line += f": {failure.message}"
        if failure.nocheck:
            line += f" -- use a {failure.nocheck} comment to ignore"
        print(line)
    if checker.failures:
        print(f"{len(checker.failures)} failures!")
    return 1 if checker.failures else 0


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())

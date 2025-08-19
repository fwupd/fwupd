#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import sys
import os
import re
from typing import List, Optional


def _find_func_name(line: str) -> Optional[str]:
    # these are not functions
    for prefix in ["\t", " ", "typedef", "__attribute__", "G_DEFINE_"]:
        if line.startswith(prefix):
            return None

    # ignore prototypes
    if line.endswith(";"):
        return None

    # strip to function name then test for validity
    idx: int = line.find("(")
    if idx == -1:
        return None
    func_name = line[:idx]
    if func_name.find(" ") != -1:
        return None

    # success!
    return func_name


def _split_args(line: str) -> list[str]:
    is_quoted: bool = False
    split = []
    last_idx = 0
    for idx, char in enumerate(line):
        if is_quoted and char == '"':
            is_quoted = False
            continue
        if char == '"':
            is_quoted = True
            continue
        if not is_quoted and char == ",":
            split.append(line[last_idx:idx].strip())
            last_idx = idx + 1
    split.append(line[last_idx:].strip())
    return split


# make lines long again
def _fix_newlines(lines: list[str]) -> list[str]:
    lines_fixed = []
    padding: int = 0
    for line in lines:
        # push back onto previous line
        if lines_fixed and lines_fixed[-1].endswith(","):
            lines_fixed[-1] += " " + line.lstrip()
            padding += 1
            continue

        # push back onto previous line
        if (
            line
            and line.lstrip()[0] == '"'
            and lines_fixed
            and lines_fixed[-1].endswith('"')
        ):
            lines_fixed[-1] = lines_fixed[-1][:-1] + line.lstrip()[1:]
            padding += 1
            continue

        # this means the line numbers match
        for _ in range(padding):
            lines_fixed.append("\n")
        padding = 0
        lines_fixed.append(line)
    return lines_fixed


# convert a CamelCase name into snake_case
def _camel_to_snake(name: str) -> str:
    name_snake: str = ""
    for char in name:
        if char.islower() or char.isnumeric():
            name_snake += char
            continue
        if char == "_":
            name_snake += char
            continue
        if name_snake:  # and not char.isnumeric():
            name_snake += "_"
        name_snake += char.lower()
    return name_snake


class SourceFailure:
    def __init__(self, fn=None, linecnt=None, message=None, nocheck=None):
        self.fn: Optional[str] = fn
        self.linecnt: Optional[int] = linecnt
        self.message: Optional[str] = message
        self.nocheck: Optional[str] = nocheck


class Checker:
    MAX_FUNCTION_LINES: int = 400
    MAX_FUNCTION_SWITCH: int = 2
    MAX_FILE_MAGIC_DEFINES: int = 15
    MAX_FILE_MAGIC_INLINES: int = 80

    def __init__(self):
        self.failures: List[SourceFailure] = []
        self._current_fn: Optional[str] = None
        self._current_linecnt: Optional[int] = None
        self._current_nocheck: Optional[str] = None
        self._gtype_parents: dict[str, str] = {}

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
        # sanity check
        if not self._current_fn:
            return

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

        # parse
        func_name: str = _find_func_name(line)
        if not func_name:
            return
        if func_name.startswith("_"):
            self._test_line_function_names_private(func_name)
            return
        self._test_line_function_names_valid(func_name)

    def _test_line_missing_literal_task_return_new(self, line: str) -> None:
        # skip!
        self._current_nocheck = "nocheck:error"
        if line.find(self._current_nocheck) != -1:
            return
        idx = line.find("g_task_return_new_error(")
        if idx == -1:
            return
        sections = _split_args(line)
        if len(sections) == 4:
            self.add_failure(f"missing literal, use g_task_return_new_error() instead")

    def _test_line_missing_literal_prefix_error(self, line: str) -> None:
        # skip!
        self._current_nocheck = "nocheck:error"
        if line.find(self._current_nocheck) != -1:
            return
        idx = line.find("g_prefix_error(")
        if idx == -1:
            return
        sections = _split_args(line)
        if len(sections) == 2:
            self.add_failure(f"missing literal, use g_prefix_error_literal() instead")

    def _test_line_missing_literal_set_error(self, line: str) -> None:
        # skip!
        self._current_nocheck = "nocheck:error"
        if line.find(self._current_nocheck) != -1:
            return
        idx = line.find("g_set_error(")
        if idx == -1:
            return
        if line.find("%m") != -1:
            return
        if line.find("TRANSLATORS") != -1:
            return
        sections = _split_args(line)
        if len(sections) == 4:
            self.add_failure(f"missing literal, use g_set_error_literal() instead")

    def _test_line_enums(self, line: str) -> None:
        # skip!
        self._current_nocheck = "nocheck:prefix"
        if line.find(self._current_nocheck) != -1:
            return

        # needs Fu prefix
        enum_name = None
        valid_prefixes = ["Fwupd", "Fu"]
        if line.startswith("enum ") and line.endswith("{"):
            enum_name = line[5:-2]
        elif line.startswith("typedef enum ") and line.endswith("{"):
            enum_name = line[13:-2]
        if not enum_name:
            return
        for prefix in valid_prefixes:
            if enum_name.startswith(prefix):
                return
        self.add_failure(
            f"invalid enum name {enum_name} should have {'|'.join(valid_prefixes)} prefix"
        )

    def _test_static_vars(self, line: str) -> None:
        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-main-windows.c",
            "fu-self-test.c",
        ]:
            return
        self._current_nocheck = "nocheck:static"
        if line.find(self._current_nocheck) != -1:
            return
        if line.find(" = ") == -1:
            return
        tokens = line.split(" ")
        for cnt, token in enumerate(tokens):
            if token != "static":
                continue
            varname = tokens[cnt + 2].split("[")[0]
            if varname not in ["signals", "quarks"]:
                self.add_failure(f"static variable {varname} not allowed")
            break

    def _test_zero_init(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:zero-init"
        in_struct: bool = False
        for linecnt, line in enumerate(lines):
            self._current_linecnt = linecnt + 1
            if line.find(self._current_nocheck) != -1:
                continue
            if line.find("struct ") != -1:
                in_struct = True
                continue
            if in_struct and line == "}":
                in_struct = False
                continue
            if in_struct:
                continue
            if line.find(" = ") != -1:
                continue
            if not line.lstrip().startswith("guint"):
                continue
            if line.endswith("];"):
                self.add_failure(f"buffer not zero init, use ` = {{0}}`")

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
            "g_ascii_strtoull(": "Use fu_strtoull() instead",
            "g_ascii_strtoll(": "Use fu_strtoll() instead",
            "g_strerror(": "Use fwupd_strerror() instead",
            "g_random_int_range(": "Use a predicatable token instead",
            "g_assert(": "Use g_set_error() or g_return_val_if_fail() instead",
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
            "GUINT16_FROM_BE(": "Use fu_memread_uint16_safe() or rustgen instead",
            "GUINT16_FROM_LE(": "Use fu_memread_uint16_safe() or rustgen instead",
            "GUINT16_TO_BE(": "Use fu_memwrite_uint16_safe() or rustgen instead",
            "GUINT16_TO_LE(": "Use fu_memwrite_uint16_safe() or rustgen instead",
            "GUINT32_FROM_BE(": "Use fu_memread_uint32_safe() or rustgen instead",
            "GUINT32_FROM_LE(": "Use fu_memread_uint32_safe() or rustgen instead",
            "GUINT32_TO_BE(": "Use fu_memwrite_uint32_safe() or rustgen instead",
            "GUINT32_TO_LE(": "Use fu_memwrite_uint32_safe() or rustgen instead",
            "GUINT64_FROM_BE(": "Use fu_memread_uint64_safe() or rustgen instead",
            "GUINT64_FROM_LE(": "Use fu_memread_uint64_safe() or rustgen instead",
            "GUINT64_TO_BE(": "Use fu_memwrite_uint64_safe() or rustgen instead",
            "GUINT64_TO_LE(": "Use fu_memwrite_uint64_safe() or rustgen instead",
            " ioctl(": "Use fu_udev_device_ioctl() instead",
        }.items():
            if line.find(token) != -1:
                self.add_failure(f"contains blocked token {token}: {msg}")

    def _test_lines_magic_numbers(self, lines: List[str]) -> None:
        if os.path.basename(self._current_fn) == "fu-self-test.c":
            return

        self._current_nocheck = "nocheck:magic"
        magic_defines: list[int] = []
        magic_inlines: list[int] = []
        magic_defines_limit = self.MAX_FILE_MAGIC_DEFINES
        magic_inlines_limit = self.MAX_FILE_MAGIC_INLINES
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
                idx = line.find("nocheck:magic-defines=")
                if idx != -1:
                    magic_defines_limit = int(line[idx + 22 :].split(" ")[0])
                    continue
                idx = line.find("nocheck:magic-inlines=")
                if idx != -1:
                    magic_inlines_limit = int(line[idx + 22 :].split(" ")[0])
                    continue
                continue
            tokens = re.split("(\\W+)", line)
            for token in tokens:
                if token in ["0x0", "0x00"]:
                    continue
                if len(token) >= 3 and token.startswith("0x"):
                    if line.startswith("#define"):
                        magic_defines.append(linecnt)
                    else:
                        magic_inlines.append(linecnt)
                    break
        if len(magic_defines) > magic_defines_limit:
            self.add_failure(
                f"file has too many #defined magic values ({len(magic_defines)}), "
                f"limit of {magic_defines_limit}"
            )
        if len(magic_inlines) > magic_inlines_limit:
            self.add_failure(
                f"file has too many inline magic values ({len(magic_inlines)}), "
                f"limit of {magic_inlines_limit}"
            )

    def _test_lines_gerror_false_returns(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:error-false-return"

        func_begin: int = 0
        for linecnt, line in enumerate(lines):
            self._current_linecnt = linecnt + 1
            if (
                line.find("g_set_error_literal(") != -1
                or line.find("g_set_error(") != -1
            ):
                func_begin = linecnt
                continue
            if line.find(self._current_nocheck) != -1:
                func_begin = 0
                continue
            if not func_begin:
                continue
            if line.find("return") != -1 or line.find("break;") != -1:
                func_begin = 0
                continue
            if line.find("}") != -1:
                func_begin = 0
                self.add_failure("uses g_set_error() without returning FALSE")
                continue

    def _test_lines_gerror(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:error"
        linecnt_g_set_error: int = 0
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
                continue
            self._current_linecnt = linecnt + 1

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
        func_n_switch: int = 0
        func_begin: int = 0
        func_name: Optional[str] = None
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
                func_begin = 0
                continue
            if line.find("switch (") != -1:
                func_n_switch += 1
            if line == "{":
                func_begin = linecnt
                continue
            if func_begin > 0 and line == "}":
                self._current_linecnt = func_begin
                if func_n_switch > self.MAX_FUNCTION_SWITCH:
                    self.add_failure(
                        f"{func_name} has too many switches ({func_n_switch}), limit of {self.MAX_FUNCTION_SWITCH}"
                    )
                if linecnt - func_begin > self.MAX_FUNCTION_LINES:
                    if func_name:
                        self.add_failure(
                            f"{func_name} is too long, was {linecnt - func_begin} of {self.MAX_FUNCTION_LINES}"
                        )
                    else:
                        self.add_failure(
                            f"function is too long, was {linecnt - func_begin} of {self.MAX_FUNCTION_LINES}"
                        )
                if func_name and linecnt - func_begin < 3:
                    if func_name.endswith("_finalize"):
                        self.add_failure(f"{func_name} is redundant and can be removed")
                func_begin = 0
                func_n_switch = 0
                func_name = None
                continue

            # is a function?
            func_name_tmp: Optional[str] = _find_func_name(line)
            if func_name_tmp:
                func_name = func_name_tmp

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
            self._current_linecnt = linecnt + 1
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
            self._current_linecnt = linecnt + 1
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
            self._current_linecnt = linecnt + 1
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

    def _test_gobject_parents(self, lines: List[str]) -> None:
        self._current_nocheck = "nocheck:name"

        gtype: Optional[str] = None
        for linecnt, line in enumerate(lines):
            if line.find(self._current_nocheck) != -1:
                continue
            self._current_linecnt = linecnt + 1
            if line.find("G_DECLARE_FINAL_TYPE") != -1:
                gtype, _, _, _, gtypeparent = line[21:-1].replace(" ", "").split(",")
                self._gtype_parents[gtype] = gtypeparent
                continue
            if line.find("G_DECLARE_DERIVABLE_TYPE") != -1:
                gtype, _, _, _, gtypeparent = line[25:-1].replace(" ", "").split(",")
                self._gtype_parents[gtype] = gtypeparent
                continue
            if line.find("G_DEFINE_TYPE") != -1:
                gtype, prefix, parent = (
                    line.split("(")[1].split(")")[0].replace(" ", "").split(",")[0:3]
                )
                # verify prefix is correct
                if gtype not in ["FuIOChannel"]:
                    if prefix != _camel_to_snake(gtype):
                        self.add_failure(
                            f"Weird prefix for GType {gtype}, "
                            f"got {prefix} and expected {_camel_to_snake(gtype)}"
                        )

                # verify the correct _get_type() define is being used
                if gtype not in self._gtype_parents:
                    continue
                parentgtype_sections = _camel_to_snake(
                    self._gtype_parents[gtype]
                ).split("_")
                parentgtype_sections.insert(1, "type")
                expected_parentgtype = "_".join(parentgtype_sections).upper()
                if parent != expected_parentgtype:
                    self.add_failure(
                        f"Invalid GType parent type for {gtype}, "
                        f"got {parent} and expected {expected_parentgtype}"
                    )
                continue

            # verify that the correct thing is included in the class struct
            if line.startswith("struct _"):
                gtype = line[8:].split(" ")[0]
                continue
            if line.endswith("parent_instance;"):
                parentgtype = line[1:-17]
                if gtype not in self._gtype_parents:
                    continue
                expected_parentgtype = self._gtype_parents[gtype]
                if expected_parentgtype != parentgtype:
                    self.add_failure(
                        f"Invalid GType parent type for {gtype}, "
                        f"got {parentgtype} and expected {expected_parentgtype}"
                    )

    def _test_lines(self, lines: List[str]) -> None:
        lines_nocheck: List[str] = []

        # tests we can do line by line
        for linecnt, line in enumerate(lines):
            self._current_linecnt = linecnt + 1

            # test for blocked functions
            self._test_line_blocked_fns(line)

            # test for debug lines
            self._test_line_debug_fns(line)

            # test for function names
            self._test_line_function_names(line)

            # test for invalid enum names
            self._test_line_enums(line)

            # test for missing literals
            self._test_line_missing_literal_task_return_new(line)
            self._test_line_missing_literal_prefix_error(line)
            self._test_line_missing_literal_set_error(line)

            # test for static variables
            self._test_static_vars(line)

        # using too many hardcoded constants
        self._test_gobject_parents(lines)

        # using too many hardcoded constants
        self._test_lines_magic_numbers(lines)

        # using FUWPD_ERROR domains
        self._test_lines_gerror(lines)

        # setting GError, not returning
        self._test_lines_gerror_false_returns(lines)

        # not nesting too deep
        self._test_lines_depth(lines)

        # functions too long
        self._test_lines_function_length(lines)

        # should use FuFirmwareClass->convert_version
        self._test_lines_firmware_convert_version(lines)

        # should use FuDeviceClass->convert_version
        self._test_lines_device_convert_version(lines)

        # test for non-zero'd init
        self._test_zero_init(lines)

    def test_file(self, fn: str) -> None:
        self._current_fn = fn
        with open(fn, "rb") as f:
            try:
                self._test_lines(_fix_newlines(f.read().decode().split("\n")))
            except UnicodeDecodeError as e:
                print(f"failed to read {fn}: {e}")


def test_files() -> int:
    # test all C and H files
    rc: int = 0

    checker = Checker()

    # use any file specified in argv, falling back to scanning the entire tree
    fns: List[str] = []
    if len(sys.argv) > 1:
        for fn in sys.argv[1:]:
            try:
                ext: str = fn.rsplit(".", maxsplit=1)[1]
            except IndexError:
                continue
            if ext in ["c", "h"]:
                fns.append(fn)
    else:
        fns.extend(glob.glob("libfwupd/*.[c|h]"))
        fns.extend(glob.glob("libfwupdplugin/*.[c|h]"))
        fns.extend(glob.glob("plugins/*/*.[c|h]"))
        fns.extend(glob.glob("src/*.[c|h]"))
    for fn in sorted(fns, reverse=True):
        if os.path.basename(fn) == "check-source.py":
            continue
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

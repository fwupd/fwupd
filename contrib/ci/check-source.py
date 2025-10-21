#!/usr/bin/env python3
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
# pylint: disable=too-many-lines,too-many-return-statements,missing-class-docstring
# pylint: disable=too-many-arguments,too-many-positional-arguments,too-many-statements
# pylint: disable=too-few-public-methods,too-many-branches,protected-access

import glob
import sys
import os
import argparse
from typing import List, Optional
from ctokenizer import Tokenizer, Node, NodeHint, Token, TokenHint


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


def _value_relaxed(data: str) -> str:
    if data in ["0x0", "0x00", "0x0000"]:
        return "0"
    if data in ["G_SOURCE_REMOVE"]:
        return "FALSE"
    if data in ["G_SOURCE_CONTINUE"]:
        return "TRUE"
    return data


class SourceFailure:
    def __init__(
        self, fn=None, linecnt=None, message=None, nocheck=None, expected=False
    ) -> None:
        self.fn: Optional[str] = fn
        self.linecnt: Optional[int] = linecnt
        self.message: Optional[str] = message
        self.nocheck: Optional[str] = nocheck
        self.expected: bool = expected


class Checker:

    def __init__(self, verbose: bool = False) -> None:
        self.verbose: bool = verbose
        self.failures: List[SourceFailure] = []
        self._current_fn: Optional[str] = None
        self._current_nocheck: Optional[str] = None
        self._gtype_parents: dict[str, str] = {}
        self._klass_funcs: list[str] = []
        self._expected_failure_prefixes: list[str] = []

    def add_expected_failure(self, message_prefix: str) -> None:
        self._expected_failure_prefixes.append(message_prefix)

    def _should_process_node(self, node: Node) -> bool:

        if self._current_nocheck:
            if node.tokens_pre.count_fuzzy([f"~/* {self._current_nocheck} */"]) > 0:
                return False
            if node.tokens.count_fuzzy([f"~/* {self._current_nocheck} */"]) > 0:
                return False
        return True

    def add_failure(self, message=None, linecnt: Optional[int] = None) -> None:

        # we were expecting this
        expected: bool = False
        for message_prefix in self._expected_failure_prefixes:
            if message.startswith(message_prefix):
                expected = True
                break

        # add
        self.failures.append(
            SourceFailure(
                expected=expected,
                fn=self._current_fn,
                linecnt=linecnt,
                message=message,
                nocheck=self._current_nocheck,
            )
        )

    def has_failure(self, message_prefix: str) -> bool:
        for failure in self.failures:
            if failure.message and failure.message.startswith(message_prefix):
                return True
        return False

    def _test_function_names_prefix_private(self, func_name: str, token: Token) -> None:
        valid_prefixes = ["_fwupd_", "_fu_", "_g_", "_xb_"]
        for prefix in valid_prefixes:
            if func_name.startswith(prefix):
                return
        self.add_failure(
            f"invalid function name {func_name} should have {'|'.join(valid_prefixes)} prefix",
            linecnt=token.linecnt,
        )

    def _test_function_names_prefix_valid(self, func_name: str, token: Token) -> None:
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
        if func_name in ["main", "fu_plugin_init_vfuncs", "fu_plugin_get_data"]:
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
            "fu_crc": "fu_misr",  # split out to fu-misr.[c|h]?
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
            f"invalid function name {func_name} should have {'|'.join(valid_prefixes)} prefix",
            linecnt=token.linecnt,
        )

    def _discover_klass_functions(self, node: Node) -> None:
        """discover all the device_class-> functions"""

        if node.depth != 0:
            return
        idx = node.tokens_pre.find_fuzzy(["~fu_*_class_init@FUNCTION"])
        if idx == -1:
            return

        idx = 0
        while True:
            idx = node.tokens.find_fuzzy(["-", ">", "~*", "="], offset=idx + 1)
            if idx == -1:
                break
            self._klass_funcs.append(node.tokens[idx + 4].data)

    def _test_variable_case(self, node: Node) -> None:
        """disallow non-lowercase variables"""

        if node.hint == NodeHint.ENUM:
            return
        idx = node.tokens.find_fuzzy(["="])
        if idx == -1:
            return
        token = node.tokens[idx - 1]
        if token.data.find(".") != -1:
            return
        if token.data.lower() != token.data:
            self.add_failure(
                f"mixed case variable {token.data}",
                linecnt=token.linecnt,
            )

    def _test_struct_member_case(self, node: Node) -> None:
        """disallow non-lowercase struct members"""

        if node.hint not in [NodeHint.STRUCT, NodeHint.STRUCT_TYPEDEF]:
            return
        idx: int = 0
        while True:
            idx = node.tokens.find_fuzzy([";"], offset=idx + 1)
            if idx == -1:
                return
            token = node.tokens[idx - 1]
            if token.data.lower() != token.data:
                self.add_failure(
                    f"mixed case struct member {token.data}",
                    linecnt=token.linecnt,
                )

    def _test_param_self_native(self, node: Node) -> None:
        """use @self for native internal functions, not the basetype"""

        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-device.c",
            "fu-device-locker.c",
            "fu-device-progress.c",
        ]:
            return
        if self._current_fn and os.path.basename(self._current_fn).endswith(".h"):
            return
        if node.depth != 0:
            return
        idx = node.tokens_pre.find_fuzzy(["@FUNCTION", "(", "FuDevice", "~*", "device"])
        if idx == -1:
            return
        token = node.tokens_pre[idx]
        if token.data.endswith("_cb"):
            return
        if token.data in self._klass_funcs:
            return
        self.add_failure(
            "native device functions should use self as the first parameter not device",
            linecnt=token.linecnt,
        )

    def _test_param_self_device(self, node: Node) -> None:
        """only use @self for device GTypes, not the basetype"""

        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-device.c",
            "fu-device.h",
            "fu-device-poll-locker.h",
            "fu-device-private.h",
        ]:
            return
        if node.depth != 0:
            return
        idx = node.tokens_pre.find_fuzzy(["@FUNCTION", "(", "FuDevice", "*", "self"])
        if idx == -1:
            return
        token = node.tokens_pre[idx + 4]
        self.add_failure(
            f"invalid parameter name {token.data} should be called 'device'",
            linecnt=token.linecnt,
        )

    def _test_param_self_firmware(self, node: Node) -> None:
        """only use @self for firmware GTypes, not the basetype"""

        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-firmware.c",
        ]:
            return
        if node.depth != 0:
            return
        idx = node.tokens_pre.find_fuzzy(["@FUNCTION", "(", "FuFirmware", "*", "self"])
        if idx == -1:
            return
        token = node.tokens_pre[idx + 4]
        self.add_failure(
            f"invalid parameter name {token.data} should be called 'firmware'",
            linecnt=token.linecnt,
        )

    def _test_function_names_ensure(self, node: Node) -> None:
        """setting internal state should be 'ensuring' the data, not setting it"""

        if node.depth != 0:
            return

        idx = node.tokens_pre.find_fuzzy(
            [
                "gboolean",
                "~fu_*_set_*@FUNCTION",
                "(",
                "~*",
                "*",
                "self",
                ",",
                "GError",
                "*",
                "*",
                "error",
                ")",
            ]
        )
        if idx != -1:
            token = node.tokens_pre[idx]
            self.add_failure(
                "function should be called ensure, not set",
                linecnt=token.linecnt,
            )

    def _test_function_names_prefix(self, node: Node) -> None:

        if node.depth != 0:
            return

        idx: int = 0
        func_name: str = ""
        while True:
            idx = node.tokens_pre.find_fuzzy(["@FUNCTION", "("], offset=idx + 1)
            if idx == -1:
                return

            # sanity check
            token = node.tokens_pre[idx]
            func_name = token.data
            if len(func_name) < 10:
                continue
            if func_name in ["__attribute__"]:
                continue
            if self.verbose:
                print("func_name", func_name)
            if func_name.upper() != func_name and func_name.lower() != func_name:
                self.add_failure(
                    "mixed case function name",
                    linecnt=token.linecnt,
                )
            if func_name.startswith("_"):
                self._test_function_names_prefix_private(func_name, token)
            else:
                self._test_function_names_prefix_valid(func_name, token)

    def _test_missing_literal(self, node: Node) -> None:
        """test for missing literals"""
        idx = node.tokens.find_fuzzy(
            ["g_task_return_new_error", "(", "~*", ",", "~*", ",", "~*", ",", "~*", ")"]
        )
        if idx != -1:
            token = node.tokens[idx]
            self.add_failure(
                "missing literal, use g_task_return_new_error_literal() instead",
                linecnt=token.linecnt,
            )
        idx = node.tokens.find_fuzzy(["g_prefix_error", "(", "~*", ",", "~*", ")"])
        if idx != -1:
            token = node.tokens[idx]
            self.add_failure(
                "missing literal, use g_prefix_error_literal() instead",
                linecnt=token.linecnt,
            )

        idx = node.tokens.find_fuzzy(
            ["g_set_error", "(", "~*", ",", "~*", ",", "~*", ",", "~*", ")"]
        )
        if idx != -1:
            if node.tokens[idx + 8].data.find("%m") == -1:
                self.add_failure(
                    "missing literal, use g_set_error_literal() instead",
                    linecnt=node.linecnt,
                )

    def _test_missing_error_suffixes(self, node: Node) -> None:
        """test for missing : suffixes"""
        idx = node.tokens.find_fuzzy(["~g_prefix_error*", "(", "~*", ","])
        if idx == -1:
            return
        token = node.tokens[idx + 4]
        if not token.data.endswith(': "'):
            self.add_failure("missing ': ' suffix", linecnt=token.linecnt)

    def _test_extra_error_suffixes(self, node: Node) -> None:
        """test for extra : suffixes"""
        idx = node.tokens.find_fuzzy(["~g_set_error*", "(", "~*[", ","])
        if idx == -1:
            return
        token = node.tokens[idx + 8]
        if token.data.endswith(': "'):
            self.add_failure("extraneous ': ' suffix", linecnt=token.linecnt)

    def _test_enums(self, node: Node) -> None:
        if node.depth != 0:
            return

        # only consider the last token
        idx = node.tokens_pre.find_fuzzy(["@ENUM"], offset=len(node.tokens_pre) - 1)
        if idx == -1:
            return
        name = node.tokens_pre[idx].data
        if self.verbose:
            print("enum_name", name)

        # needs Fu prefix
        valid_prefixes = ["Fwupd", "Fu"]
        for prefix in valid_prefixes:
            if name.startswith(prefix):
                return
        self.add_failure(
            f"invalid enum name {name} should have {'|'.join(valid_prefixes)} prefix",
            linecnt=node.linecnt,
        )

    def _test_struct(self, node: Node) -> None:

        if node.depth != 0:
            return

        # no limit on name
        if node.hint == NodeHint.STRUCT_CONST:
            return

        # only consider the last token
        name: Optional[str] = None
        if node.hint == NodeHint.STRUCT_TYPEDEF:
            name = node.tokens_pre[-1].data
        else:
            idx = node.tokens_pre.find_fuzzy(
                ["@STRUCT"], offset=len(node.tokens_pre) - 1
            )
            if idx != -1:
                name = node.tokens_pre[idx].data
        if not name:
            return
        if name.startswith("_"):
            name = name[1:]
        if self.verbose:
            print("struct_name", name)

        # only for Rust code
        if name.startswith("FuStruct"):
            self.add_failure(
                f"incorrect struct name {name} -- FuStruct is reserved for Rust code",
                linecnt=node.linecnt,
            )

        # needs Fu prefix
        valid_prefixes = ["Fwupd", "Fu"]
        for prefix in valid_prefixes:
            if name.startswith(prefix):
                return
        self.add_failure(
            f"invalid struct name {name} should have {'|'.join(valid_prefixes)} prefix",
            linecnt=node.linecnt,
        )

    def _test_static_vars(self, node: Node) -> None:

        if node.depth != 0:
            return

        idx = node.tokens_pre.find_fuzzy(["static", "~*", "~*", "="])
        if idx == -1:
            return

        token = node.tokens_pre[idx + 2]
        if token.data in ["signals", "quarks"]:
            return
        self.add_failure(
            f"static variable {token.data} not allowed", linecnt=token.linecnt
        )

    def _test_rustgen_bitshifts(self, node: Node) -> None:

        if node.hint == NodeHint.ENUM:
            return
        idx = node.tokens.find_fuzzy(["]", "<", "<", "16"])
        if idx != -1 and node.tokens.find_fuzzy(["]", "<", "<", "8"]) != -1:
            token = node.tokens[idx + 3]
            self.add_failure(
                "endian unsafe construction; perhaps use fu_memread_uint32 or rustgen",
                linecnt=token.linecnt,
            )

    def _test_rustgen_vars(self, node: Node) -> None:

        idx = node.tokens.find_fuzzy(["g_autoptr", "(", "~FuStruct*", ")", "~*", "="])
        if idx == -1:
            return
        token = node.tokens[idx + 4]
        if not token.data.startswith("st"):
            self.add_failure(
                f"rustgen structure '{token.data}' has to have 'st' prefix",
                linecnt=token.linecnt,
            )

    def _test_zero_init(self, node: Node) -> None:
        if node.hint in [
            NodeHint.UNION,
            NodeHint.STRUCT,
            NodeHint.STRUCT_CONST,
            NodeHint.STRUCT_TYPEDEF,
        ]:
            return
        if node.tokens_pre.count_fuzzy(["struct"]) > 0:
            return
        idx = node.tokens.find_fuzzy(["~guint*", "~*", "[", "~*", "]", ";"])
        if idx != -1:
            token = node.tokens[idx]
            self.add_failure(
                "buffer not zero init, use ` = {0}`", linecnt=token.linecnt
            )

    def _test_debug_newlines(self, node: Node) -> None:

        for func in ["g_info", "g_debug", "g_message"]:
            idx: int = 0
            while True:
                idx = node.tokens.find_fuzzy([func, "(", "~*\\n*"], offset=idx + 1)
                if idx == -1:
                    break
                token = node.tokens[idx]
                self.add_failure(
                    f"{func} should not contain newlines", linecnt=token.linecnt
                )

    def _test_debug_fullstops(self, node: Node) -> None:

        for func in ["g_error", "g_info", "g_debug", "g_message"]:
            idx: int = 0
            while True:
                idx = node.tokens.find_fuzzy([func, "(", '~*."'], offset=idx + 1)
                if idx == -1:
                    break
                token = node.tokens[idx]
                self.add_failure(
                    f"{func} should not end with a full stop", linecnt=token.linecnt
                )

    def _test_debug_sentence_case(self, node: Node) -> None:

        for func in ["g_error", "g_info", "g_debug", "g_message"]:
            idx: int = 0
            while True:
                idx = node.tokens.find_fuzzy([func], offset=idx + 1)
                if idx == -1:
                    break
                token = node.tokens[idx + 2]
                first_word = token.data[1:].split(" ")[0]
                if first_word and first_word[0].isupper() and first_word[1:].islower():
                    self.add_failure(
                        f"{func} should not use sentence case", linecnt=token.linecnt
                    )

    def _test_comment_lower_case(self, node: Node) -> None:
        """single line comments are supposed to be lowercase"""
        idx: int = 0
        while True:
            idx = node.tokens.find_fuzzy(["@COMMENT"], offset=idx + 1)
            if idx == -1:
                break
            token = node.tokens[idx]
            first_word = token.data[2:-2].strip().split(" ")[0]
            if (
                first_word not in ["Windows", "Microsoft", "Thunderbolt", "Dell"]
                and token.linecnt == token.linecnt_end
                and first_word
                and first_word[0].isupper()
                and first_word[1:].islower()
            ):
                self.add_failure(
                    f"single line comments should not use sentence case",
                    linecnt=token.linecnt,
                )

    def _test_device_display(self, node: Node) -> None:
        """use fu_device_get_id_display rather than the two different commands"""

        idx1 = node.tokens.find_fuzzy(["fu_device_get_name", "(", "~*", ")"])
        idx2 = node.tokens.find_fuzzy(["fu_device_get_id", "(", "~*", ")"])
        if idx1 != -1 and idx2 != -1:

            # check this isn't an assert
            if node.tokens[idx1 - 2].data == "g_assert_cmpstr":
                return

            # same FuDevice within a limited number of lines?
            token1 = node.tokens[idx1 + 2]
            token2 = node.tokens[idx2 + 2]
            limit: int = 3
            if token1.data == token2.data:
                if abs(token1.linecnt - token2.linecnt) < limit:
                    self.add_failure(
                        "use fu_device_get_id_display() rather than "
                        "fu_device_get_name()+fu_device_get_id()",
                        linecnt=token1.linecnt,
                    )

    def _test_debug_fns(self, node: Node) -> None:
        # no console output expected
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
            "~g_print*": "Use g_debug() instead",
        }.items():
            idx = node.tokens.find_fuzzy([token])
            if idx != -1:
                token = node.tokens[idx]
                self.add_failure(
                    f"contains blocked token {token.data}: {msg}", linecnt=token.linecnt
                )

    def _test_gobject_finalize(self, node: Node) -> None:

        if node.tokens_pre.endswith_fuzzy(
            ["void", "~*_finalize", "(", "GObject", "*", "~*", ")"]
        ):
            token = node.tokens_pre[-1]
            idx = node.tokens.find_fuzzy(
                ["G_OBJECT_CLASS", "(", "~*_parent_class", ")", "-", ">", "finalize"]
            )
            if idx == -1:
                self.add_failure(
                    "did not have parent ->finalize()", linecnt=token.linecnt
                )

    def _test_blocked_funcs(self, node: Node) -> None:

        for token, msg in {
            "~cbor_get_uint?": "Use cbor_get_int() instead",
            "~cbor_get_uint??": "Use cbor_get_int() instead",
            "g_error": "Use GError instead",
            "g_byte_array_free_to_bytes": "Use g_bytes_new() instead",
            "g_ascii_strtoull": "Use fu_strtoull() instead",
            "g_ascii_strtoll": "Use fu_strtoll() instead",
            "g_strerror": "Use fwupd_strerror() instead",
            "g_random_int_range": "Use a predicatable token instead",
            "g_assert": "Use g_set_error() or g_return_val_if_fail() instead",
            "HIDIOCSFEATURE": "Use fu_hidraw_device_set_feature() instead",
            "HIDIOCGFEATURE": "Use fu_hidraw_device_get_feature() instead",
            "memcpy": "Use fu_memcpy_safe or rustgen instead",
            "~GUINT??_FROM_?E": "Use fu_memread_uintXX_safe() or rustgen instead",
            "~GUINT??_TO_?E": "Use fu_memwrite_uintXX_safe() or rustgen instead",
            "ioctl": "Use fu_udev_device_ioctl() instead",
        }.items():
            idx = node.tokens.find_fuzzy([token, "("])
            if idx != -1:
                token = node.tokens[idx]
                self.add_failure(
                    f"contains blocked token {token.data}: {msg}", linecnt=token.linecnt
                )

        idx = node.tokens.find_fuzzy(["|=", "~1*", "<", "<"])
        if idx != -1:
            token = node.tokens[idx]
            self.add_failure("Use FU_BIT_SET() instead", linecnt=token.linecnt)
        idx = node.tokens.find_fuzzy(["|=", "(", "~1*", "<", "<"])
        if idx != -1:
            token = node.tokens[idx]
            self.add_failure("Use FU_BIT_SET() instead", linecnt=token.linecnt)
        idx = node.tokens.find_fuzzy(["&", "=", "~", "(", "~1*", "<", "<"])
        if idx != -1:
            token = node.tokens[idx]
            self.add_failure("Use FU_BIT_CLEAR() instead", linecnt=token.linecnt)
        idx = node.tokens_pre.find_fuzzy(
            ["__attribute__", "(", "(", "packed", ")", ")"]
        )
        if idx != -1:
            token = node.tokens_pre[idx]
            self.add_failure("use rustgen instead", linecnt=token.linecnt)

    def _test_magic_numbers_defined(self, nodes: list[Node]) -> None:

        cnt: int = 0
        limit: int = 15
        linecnt: int = 0
        for node in nodes:
            if node.depth != 0:
                continue

            # overridden per-file
            idx = node.tokens_pre.find_fuzzy([f"~*{self._current_nocheck}=*"])
            if idx != -1:
                limit = int(node.tokens_pre[idx].data.split("=")[1].split(" ")[0])
            cnt_tmp = node.tokens_pre.count_fuzzy(["#define", "~*", "~0x*"])
            if cnt_tmp:
                linecnt = node.linecnt
                cnt += cnt_tmp
        if cnt > limit:
            self.add_failure(
                f"file has too many #defined magic values ({cnt}), limit of {limit}",
                linecnt=linecnt,
            )

    def _test_magic_numbers_inline(self, nodes: list[Node]) -> None:

        cnt: int = 0
        limit: int = 80
        linecnt: int = 0
        for node in nodes:
            # overridden per-file
            idx = node.tokens_pre.find_fuzzy([f"~*{self._current_nocheck}=*"])
            if idx != -1:
                for word in node.tokens_pre[idx].data.split(" "):
                    if word.startswith("nocheck:magic-inlines="):
                        limit = int(word[22:])
                        break
            for token in node.tokens:
                if token.data in ["0x0", "0x00", "0x0000"]:
                    continue
                if len(token.data) >= 3 and token.data.startswith("0x"):
                    linecnt = node.linecnt
                    cnt += 1
        if cnt > limit:
            self.add_failure(
                f"file has too many inline magic values ({cnt}), limit of {limit}",
                linecnt=linecnt,
            )

    def _test_gerror_false_returns(self, nodes: list[Node]) -> None:

        for node in nodes:
            if node.depth == 0:
                continue
            if not self._should_process_node(node):
                return
            idx = node.tokens.find_fuzzy(["~g_set_error*", "("])
            if idx != -1:
                token = node.tokens[idx]
                idx = node.tokens.find_fuzzy(["return", "~*", ";"], offset=idx)
                if idx == -1:
                    idx = node.tokens.find_fuzzy(["break", ";"], offset=idx)
                if idx == -1:
                    self.add_failure(
                        "uses g_set_error() without returning a value",
                        linecnt=token.linecnt,
                    )
                    break

    def _test_gerror_not_set(self, nodes: list[Node]) -> None:

        limit: int = 10
        for node in nodes:
            if node.depth == 0:
                continue
            if not self._should_process_node(node):
                continue
            idx = node.tokens.find_fuzzy(["~g_prefix_error*", "("])
            if idx != -1:
                linecnt = node.tokens[idx].linecnt
                if self.verbose:
                    print(f"GError required @{linecnt}")

                found_linecnt: list[int] = []

                # set error inner
                idx_found = node.tokens.find_fuzzy(
                    ["~error*", ")"], reverse=True, offset=idx
                )
                if idx_found != -1:
                    found_linecnt.append(node.tokens[idx_found].linecnt_end)
                idx_found = node.tokens.find_fuzzy(
                    ["~g_set_error*", "(", "~error*"], reverse=True, offset=idx
                )
                if idx_found != -1:
                    found_linecnt.append(node.tokens[idx_found].linecnt_end)

                # set error prior
                idx_found = node.tokens_pre.find_fuzzy(["~error*", ")"], reverse=True)
                if idx_found != -1:
                    found_linecnt.append(node.tokens_pre[idx_found].linecnt_end)
                idx_found = node.tokens_pre.find_fuzzy(
                    ["~g_set_error*", "(", "~error*"], reverse=True
                )
                if idx_found != -1:
                    found_linecnt.append(node.tokens_pre[idx_found].linecnt_end)
                idx_found = node.tokens_pre.find_fuzzy(["~error*", ","], reverse=True)
                if idx_found != -1:
                    found_linecnt.append(node.tokens_pre[idx_found].linecnt_end)

                # find the closest error set
                linecnt_closest: int = 0
                if found_linecnt:
                    linecnt_closest = max(found_linecnt)
                    if self.verbose:
                        print(f"GError set @{linecnt_closest}")

                linecnt_delta = linecnt - linecnt_closest
                if linecnt_delta > limit:
                    self.add_failure(
                        "uses g_prefix_error() without setting GError for "
                        f"{linecnt_delta}/{limit} previous lines",
                        linecnt=linecnt,
                    )
                    break

    def _test_gerror_domain(self, node: Node) -> None:
        """must use FUWPD_ERROR domains"""
        idx = node.tokens.find_fuzzy(
            ["~g_set_error*", "(", "~*", ",", "~G_*", ",", "~G_*"]
        )
        if idx == -1:
            return
        token = node.tokens[idx]
        self.add_failure(
            "uses g_set_error() without using FWUPD_ERROR", linecnt=token.linecnt
        )

    def _test_gerror_deref(self, node: Node) -> None:
        """using (*error)->message"""
        idx = node.tokens.find_fuzzy(["(", "*", "~*error*", ")", "-", ">"])
        if idx == -1:
            return
        token = node.tokens[idx]
        self.add_failure(
            "dereferences GError; use error_local instead", linecnt=token.linecnt
        )

    def _test_switch(self, nodes: list[Node]) -> None:

        limit: int = 2
        cnt: int = 0
        for node in nodes:
            # restrict only per-top-level=function, not per-file
            if node.depth == 0:
                cnt = 0
            idx = node.tokens_pre.find_fuzzy(["switch", "(", "~*", ")"])
            if idx != -1:
                cnt += 1
                if cnt > limit:
                    self.add_failure(
                        f"has too many switches ({cnt}), limit of {limit}",
                        linecnt=node.linecnt,
                    )
                    break

    def _test_null_false_returns(self, nodes: list[Node]) -> None:

        # allowed values from g_return_val_if_fail()
        types_rvif = {
            "*": ["NULL"],
            "GQuark": ["0"],
            "GType": ["G_TYPE_INVALID"],
            "gpointer": ["NULL"],
            "gboolean": ["TRUE", "FALSE"],
            "guint32": ["0", "G_MAXUINT32"],
            "guint64": ["0", "G_MAXUINT64"],
            "guint16": ["0", "G_MAXUINT16"],
            "guint8": ["0", "G_MAXUINT8"],
            "gint64": ["0", "-1", "G_MAXINT64"],
            "gint32": ["0", "-1", "G_MAXINT32"],
            "gint16": ["0", "-1", "G_MAXINT16"],
            "gint8": ["0", "-1", "G_MAXINT8"],
            "gint": ["0", "-1", "G_MAXINT"],
            "guint": ["0", "G_MAXUINT"],
            "gulong": ["0", "G_MAXLONG"],
            "gsize": ["0", "G_MAXSIZE"],
            "gssize": ["0", "-1", "G_MAXSSIZE"],
        }

        # disallowed values from return
        types_nret = {
            "*": ["FALSE"],
            "GQuark": ["NULL", "TRUE", "FALSE"],
            "GType": ["NULL", "TRUE", "FALSE"],
            "gpointer": ["FALSE"],
            "gboolean": ["NULL", "0"],
            "guint32": ["NULL", "TRUE", "FALSE"],
            "guint64": ["NULL", "TRUE", "FALSE"],
            "guint16": ["NULL", "TRUE", "FALSE"],
            "guint8": ["NULL", "TRUE", "FALSE"],
            "gint64": ["NULL", "TRUE", "FALSE"],
            "gint32": ["NULL", "TRUE", "FALSE"],
            "gint16": ["NULL", "TRUE", "FALSE"],
            "gint8": ["NULL", "TRUE", "FALSE"],
            "gint": ["NULL", "TRUE", "FALSE"],
            "guint": ["NULL", "TRUE", "FALSE"],
            "gulong": ["NULL", "TRUE", "FALSE"],
            "gsize": ["NULL", "TRUE", "FALSE"],
            "gssize": ["NULL", "TRUE", "FALSE"],
        }

        current_type: str = ""
        for node in nodes:
            if not self._should_process_node(node):
                continue

            # new function
            if node.depth == 0:
                idx = node.tokens_pre.find_fuzzy(["~*", "("])
                if idx == -1:
                    continue
                token_type = node.tokens_pre[idx - 1]
                token_func = node.tokens_pre[idx]
                if self.verbose:
                    print("TYPE", token_func.data, token_type.data)
                current_type = token_type.data

            # look for g_return_val_if_fail
            idx = node.tokens.find_fuzzy(["g_return_val_if_fail", "("])
            if idx != -1:
                # advance to the return
                idx = node.tokens.find_fuzzy([",", "~*", ")", ";"], offset=idx)
                if idx != -1:
                    token = node.tokens[idx + 1]
                    try:
                        rvif = types_rvif[current_type]
                    except KeyError:
                        if self.verbose:
                            print(f"missing type {current_type}")
                        continue
                    if _value_relaxed(token.data) not in rvif:
                        self.add_failure(
                            "g_return_val_if_fail() return type invalid, "
                            f"expected {'|'.join(rvif)} for {current_type}, not {token.data}",
                            linecnt=token.linecnt,
                        )

            # look for return values
            idx = node.tokens.find_fuzzy(["return", "~*", ";"])
            if idx == -1:
                continue
            token = node.tokens[idx + 1]
            if token.hint == TokenHint.FUNCTION:
                continue
            try:
                nret = types_nret[current_type]
            except KeyError:
                continue
            if _value_relaxed(token.data) in nret:
                self.add_failure(
                    f"return type invalid for {current_type}: {token.data}",
                    linecnt=token.linecnt,
                )

    def _test_gerror_void_return(self, node: Node) -> None:
        """takes GError but returns void"""

        if node.depth != 0:
            return

        idx_start = node.tokens_pre.find_fuzzy(["static", "void", "@FUNCTION"])
        if idx_start == -1:
            return
        idx = node.tokens_pre.find_fuzzy(
            ["GError", "*", "*", "error", ")"], offset=idx_start
        )
        if idx == -1:
            return
        if idx - idx_start < 10:
            token = node.tokens_pre[idx]
            self.add_failure(
                "void return type not expected for GError",
                linecnt=token.linecnt,
            )

    def _test_function_length(self, node: Node) -> None:

        if node.depth != 0:
            return
        limit: int = 400
        if node.linecnt_end - node.linecnt > limit:
            self.add_failure(
                f"function is too long, was {node.linecnt_end - node.linecnt} of {limit}",
                linecnt=node.linecnt,
            )

    def _test_firmware_convert_version(self, nodes: list[Node]) -> None:

        # contains fu_firmware_set_version_raw()
        _set_version_raw: bool = False
        for node in nodes:
            idx = node.tokens.find_fuzzy(
                ["fu_firmware_set_version_raw", "(", "~*", ",", "~*", ")", ";"]
            )
            if idx != -1:
                _set_version_raw = True
        if not _set_version_raw:
            return

        # also contains fu_firmware_set_version()
        for node in nodes:
            if not self._should_process_node(node):
                continue
            idx = node.tokens.find_fuzzy(
                ["fu_firmware_set_version", "(", "~*", ",", "~*", ")", ";"]
            )
            if idx != -1:
                token = node.tokens[idx]
                self.add_failure(
                    "Use FuFirmwareClass->convert_version rather than fu_firmware_set_version()",
                    linecnt=token.linecnt,
                )

    def _test_device_convert_version(self, nodes: list[Node]) -> None:

        if self._current_fn and os.path.basename(self._current_fn) in [
            "fu-self-test.c",
        ]:
            return

        # contains fu_device_set_version_raw()
        _set_version_raw: bool = False
        for node in nodes:
            idx = node.tokens.find_fuzzy(
                ["fu_device_set_version_raw", "(", "~*", ",", "~*", ")", ";"]
            )
            if idx != -1:
                _set_version_raw = True
        if not _set_version_raw:
            return

        # also contains fu_device_set_version()
        for node in nodes:
            if not self._should_process_node(node):
                continue
            idx = node.tokens.find_fuzzy(
                ["fu_device_set_version", "(", "~*", ",", "~*", ")", ";"]
            )
            if idx != -1:
                token = node.tokens[idx]
                self.add_failure(
                    "Use FuDeviceClass->convert_version rather than fu_device_set_version()",
                    linecnt=token.linecnt,
                )

    def _test_nesting_depth(self, node: Node) -> None:

        limit: int = 5
        if node.depth >= limit:
            self.add_failure(
                f"is nested too deep {node.depth}/{limit}", linecnt=node.linecnt
            )

    def _test_memread(self, node: Node) -> None:

        limit: int = 7
        cnt = node.tokens.count_fuzzy(["~fu_memread_uint*"])
        if cnt >= limit:
            self.add_failure(
                f"too many calls to fu_memread_uintXX() ({cnt}/{limit}), use rustgen",
                linecnt=node.linecnt,
            )
        cnt = node.tokens.count_fuzzy(["~fu_memwrite_uint*"])
        if cnt >= limit:
            self.add_failure(
                f"too many calls to fu_memwrite_uintXX() ({cnt}/{limit}), use rustgen",
                linecnt=node.linecnt,
            )

    def _test_gobject_parents(self, nodes: list[Node]) -> None:

        gtype: str = ""
        gtypeparent: str = ""
        for node in nodes:
            if node.depth != 0:
                continue
            if not self._should_process_node(node):
                return

            # .h
            idx = node.tokens_pre.find_fuzzy(["~G_DECLARE_*_TYPE", "("])
            if idx != -1:
                gtype = node.tokens_pre[idx + 2].data
                gtypeparent = node.tokens_pre[idx + 10].data
                self._gtype_parents[gtype] = gtypeparent

            # check the class def is correct
            if gtype:
                idx = node.tokens.find_fuzzy(["~*Class", "parent_class"])
                if idx != -1:
                    gtypeparentclass_found = node.tokens[idx].data
                    gtypeparentclass_expected = f"{gtypeparent}Class"
                    if gtypeparentclass_found != gtypeparentclass_expected:
                        self.add_failure(
                            f"wrong parent_class for {gtype}, "
                            f"got {gtypeparentclass_found} and "
                            f"expected {gtypeparentclass_expected}",
                            linecnt=node.linecnt,
                        )

            # .c
            idx = node.tokens_pre.find_fuzzy(["~G_DEFINE_TYPE*", "("])
            if idx != -1:
                gtype = node.tokens_pre[idx + 2].data
                if self.verbose:
                    print("GTYPE", gtype, self._gtype_parents)
                if not gtype in self._gtype_parents:
                    continue
                gtypeparent_found: str = node.tokens_pre[idx + 6].data
                gtype_snake = _camel_to_snake(self._gtype_parents[gtype]).split(
                    "_", maxsplit=1
                )
                gtypeparent_expected = f"{gtype_snake[0]}_type_{gtype_snake[1]}".upper()
                if gtypeparent_found != gtypeparent_expected:
                    self.add_failure(
                        f"wrong parent GType for {gtype}, "
                        f"got {gtypeparent_found} and "
                        f"expected {gtypeparent_expected}",
                        linecnt=node.linecnt,
                    )

    def _test_nodes(self, nodes: list[Node]) -> None:

        # preroll
        self._klass_funcs.clear()
        for node in nodes:
            self._discover_klass_functions(node)
        if self.verbose:
            print("KLASS FUNCS", self._klass_funcs)

        # tests we can do node by node
        for node in nodes:

            # test for missing ->finalize()
            self._current_nocheck = "nocheck:finalize"
            if self._should_process_node(node):
                self._test_gobject_finalize(node)

            # test for blocked functions
            self._current_nocheck = "nocheck:blocked"
            if self._should_process_node(node):
                self._test_blocked_funcs(node)
                self._test_device_display(node)

            # test for debug lines
            self._current_nocheck = "nocheck:print"
            if self._should_process_node(node):
                self._test_debug_fns(node)
                self._test_debug_newlines(node)
                self._test_debug_fullstops(node)
                self._test_debug_sentence_case(node)
                self._test_comment_lower_case(node)

            # not nesting too deep
            self._current_nocheck = "nocheck:depth"
            if self._should_process_node(node):
                self._test_nesting_depth(node)

            # test for function names
            self._current_nocheck = "nocheck:name"
            if self._should_process_node(node):
                self._test_function_names_prefix(node)
                self._test_function_names_ensure(node)
                self._test_param_self_device(node)
                self._test_param_self_firmware(node)
                self._test_param_self_native(node)
                self._test_variable_case(node)
                self._test_struct_member_case(node)

            # test for invalid struct and enum names
            self._current_nocheck = "nocheck:prefix"
            if self._should_process_node(node):
                self._test_struct(node)
                self._test_enums(node)

            # test for static variables
            self._current_nocheck = "nocheck:static"
            if self._should_process_node(node):
                self._test_static_vars(node)

            # test for rustgen variables
            self._current_nocheck = "nocheck:rustgen"
            if self._should_process_node(node):
                self._test_rustgen_vars(node)

            # test for endian safety
            self._current_nocheck = "nocheck:endian"
            if self._should_process_node(node):
                self._test_rustgen_bitshifts(node)

            # GError
            self._current_nocheck = "nocheck:error"
            if self._should_process_node(node):
                self._test_gerror_domain(node)
                self._test_gerror_void_return(node)
                self._test_extra_error_suffixes(node)
                self._test_missing_error_suffixes(node)
                self._test_missing_literal(node)
                self._test_gerror_deref(node)

            # using too many memory reads/writes
            self._current_nocheck = "nocheck:memread"
            if self._should_process_node(node):
                self._test_memread(node)

            # test for non-zero'd init
            self._current_nocheck = "nocheck:zero-init"
            if self._should_process_node(node):
                self._test_zero_init(node)

            # functions too long
            self._current_nocheck = "nocheck:lines"
            if self._should_process_node(node):
                self._test_function_length(node)

        # NULL != FALSE
        self._current_nocheck = "nocheck:lines"
        self._test_null_false_returns(nodes)

        # using too many switch()
        self._current_nocheck = None
        self._test_switch(nodes)

        # should use FuFirmwareClass->convert_version or FuDeviceClass->convert_version
        self._current_nocheck = "nocheck:set-version"
        self._test_firmware_convert_version(nodes)
        self._test_device_convert_version(nodes)

        # using too many hardcoded constants
        self._current_nocheck = "nocheck:magic-defines"
        self._test_magic_numbers_defined(nodes)
        self._current_nocheck = "nocheck:magic-inlines"
        self._test_magic_numbers_inline(nodes)

        # setting GError, not returning
        self._current_nocheck = "nocheck:error-false-return"
        self._test_gerror_false_returns(nodes)

        # prefix with no set
        self._current_nocheck = "nocheck:error"
        self._test_gerror_not_set(nodes)

        # test GObject parent types
        self._current_nocheck = "nocheck:name"
        self._test_gobject_parents(nodes)

    def test_file(self, fn: str) -> None:
        self._current_fn = fn
        with open(fn, "rb") as f:
            try:
                data = f.read().decode()
            except UnicodeDecodeError as e:
                print(f"failed to read {fn}: {e}")
        tokenizer = Tokenizer(data)
        nodes = tokenizer.nodes
        if self.verbose:
            print(nodes)
        self._test_nodes(nodes)


def test_files(fns_optional: list[str], verbose: bool = False) -> int:
    # test all C and H files

    checker = Checker(verbose=verbose)

    # use any file specified in argv, falling back to scanning the entire tree
    fns: List[str] = []
    if fns_optional:
        for fn in fns_optional:
            if fn.startswith("contrib/ci/tests"):
                continue
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
        print(f"checking {fn}…", file=sys.stderr)
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


def unit_test(fn: str, verbose: bool = False) -> int:

    # load test file with any expected failures
    rc: int = 0
    checker = Checker(verbose=verbose)
    with open(fn, "rb") as f:
        lines = f.read().decode().split("\n")
    for line in lines:
        if line.startswith(" * nocheck:expect:"):
            checker.add_expected_failure(line[19:])
    checker.test_file(fn)

    # any unexpected failures
    for failure in checker.failures:
        if not failure.expected:
            print(f"{failure.fn} did not expect to see: {failure.message}")
            rc += 1

    # check we got the ones we wanted
    for expect in checker._expected_failure_prefixes:
        if not checker.has_failure(expect):
            print(f"{fn} expected to see: {expect}")
            rc += 1

    # print what we did get
    if rc:
        for failure in checker.failures:
            line_tmp: str = ""
            if failure.fn:
                line_tmp += failure.fn
            if failure.linecnt:
                line_tmp += f":{failure.linecnt}"
            line_tmp += f": {failure.message}"
            print(line_tmp)

    return rc


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="Check source files")
    parser.add_argument("--test", type=str, help="Run self tests")
    parser.add_argument("--verbose", action="store_true", help="Run in verbose mode")
    args, argv = parser.parse_known_args()

    if args.test:
        _rc: int = 0
        for _fn in glob.glob(f"{args.test}/*.[c|h]"):
            _rc += unit_test(_fn, args.verbose)
        sys.exit(_rc)

    # all done!
    sys.exit(test_files(argv, args.verbose))

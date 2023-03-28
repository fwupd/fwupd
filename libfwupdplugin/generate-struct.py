#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import os
import sys
import argparse

from enum import Enum
from typing import Optional, List, Tuple

# This script builds source files that describe a structure.
#
# This is a very smart structure that supports endian conversion, arrays, GUIDs, strings, default
# and constant data of variable size.
#
# In most cases the smart structure will be defined in a `.struct` file:
#
#    struct UswidHdr {
#        magic: guid
#        hdrver: u8
#        hdrsz: u16le: $struct_size
#        payloadsz: u32le
#        flags: u8
#    }
#
# The types currently supported are:
#
# - `u8`: a #guint8
# - `u16`: a #guint16
# - `u24`: a 24 bit number represented as a #guint32
# - `u32`:  #guint32
# - `u64`:  #guint64
# - `s`: a string
# - `guid`: a packed GUID
#
# Additionally, default values can be auto-populated:
#
# - `$struct_size`: the struct size, e.g. the value of `fu_struct_size()`
# - `$struct_offset`: the internal offset in the struct
# - string values
# - integer values, specified with a `0x` prefix for base-16 and with no prefix for base-10
#
# Any default value prefixed with an additional `:` is set as the default, and is **also**
# verified during unpacking.
# This is suitable for constant signature fields where there is no other valid value.


class Endian(Enum):
    NATIVE = "native"
    LITTLE = "le"
    BIG = "be"


class Type(Enum):
    U8 = "u8"
    U16 = "u16"
    U24 = "u24"
    U32 = "u32"
    U64 = "u64"
    STRING = "s"
    GUID = "guid"


class Item:
    def __init__(self):
        self.element_id: str = ""
        self.type: Type = Type.U8
        self.default: Optional[str] = None
        self.constant: Optional[str] = None
        self.endian: Endian = Endian.NATIVE
        self.multiplier: int = 0
        self.offset: int = 0

    @property
    def size(self) -> int:
        multiplier = self.multiplier
        if not multiplier:
            multiplier = 1
        if self.type in [Type.U8, Type.STRING, Type.GUID]:
            return multiplier
        if self.type == Type.U16:
            return multiplier * 2
        if self.type == Type.U24:
            return multiplier * 3
        if self.type == Type.U32:
            return multiplier * 4
        if self.type == Type.U64:
            return multiplier * 8
        return 0

    @property
    def enabled(self) -> bool:
        if self.element_id.startswith("_"):
            return False
        if self.element_id == "reserved":
            return False
        return True

    @property
    def endian_glib(self) -> str:
        if self.endian == Endian.LITTLE:
            return "G_LITTLE_ENDIAN"
        if self.endian == Endian.BIG:
            return "G_BIG_ENDIAN"
        return "G_BYTE_ORDER"

    @property
    def type_glib(self) -> str:
        if self.type == Type.U8:
            return "guint8"
        elif self.type == Type.U16:
            return "guint16"
        elif self.type == Type.U24:
            return "guint32"
        elif self.type == Type.U32:
            return "guint32"
        elif self.type == Type.U64:
            return "guint64"
        elif self.type == Type.STRING:
            return "gchar"
        if self.type == Type.GUID:
            return "fwupd_guid_t"
        return "void"

    @property
    def type_mem(self) -> str:
        if self.type == Type.U16:
            return "uint16"
        elif self.type == Type.U24:
            return "uint24"
        elif self.type == Type.U32:
            return "uint32"
        elif self.type == Type.U64:
            return "uint64"
        return ""

    def generate_h_glib(self, name_snake: str) -> str:

        # constants do not need getters and setters as they're, well, constant
        if self.constant:
            return ""

        # string
        if self.type == Type.STRING:
            return f"""
gchar *{name_snake}_get_{self.element_id}(GByteArray *st);
gboolean {name_snake}_set_{self.element_id}(GByteArray *st, const gchar *value, GError **error);
"""

        # data blob
        if self.type == Type.U8 and self.multiplier:
            return f"""
const guint8 *{name_snake}_get_{self.element_id}(GByteArray *st, gsize *bufsz);
gboolean {name_snake}_set_{self.element_id}(GByteArray *st, const guint8 *buf, gsize bufsz, GError **error);
"""

        # GUID
        if self.type == Type.GUID:
            return f"""
const fwupd_guid_t *{name_snake}_get_{self.element_id}(GByteArray *st);
void {name_snake}_set_{self.element_id}(GByteArray *st, const fwupd_guid_t *value);
"""

        # uint
        if not self.multiplier and self.type in [
            Type.U8,
            Type.U16,
            Type.U24,
            Type.U32,
            Type.U64,
        ]:
            return f"""
{self.type_glib} {name_snake}_get_{self.element_id}(GByteArray *st);
void {name_snake}_set_{self.element_id}(GByteArray *st, {self.type_glib} value);
"""

        # fallback
        return ""

    def generate_c_glib(self, name_snake: str) -> str:

        # string
        if self.type == Type.STRING:
            return f"""
{"G_GNUC_UNUSED static " if self.constant else ""}gchar *
{name_snake}_get_{self.element_id}(GByteArray *st)
{{
    g_return_val_if_fail(st != NULL, NULL);
    return fu_strsafe((const gchar *) (st->data + {self.offset}), {self.size});
}}
{"static " if self.constant else ""}gboolean
{name_snake}_set_{self.element_id}(GByteArray *st, const gchar *value, GError **error)
{{
    gsize len = strlen(value);
    g_return_val_if_fail(st != NULL, FALSE);
    return fu_memcpy_safe(st->data, st->len, {self.offset}, (const guint8 *)value, len, 0x0, len, error);
}}
"""

        # data blob
        if self.type == Type.U8 and self.multiplier:
            return f"""
{"static " if self.constant else ""}const guint8 *
{name_snake}_get_{self.element_id}(GByteArray *st, gsize *bufsz)
{{
    g_return_val_if_fail(st != NULL, NULL);
    if (bufsz != NULL)
        *bufsz = st->len;
    return st->data + {self.offset};
}}
{"static " if self.constant else ""}gboolean
{name_snake}_set_{self.element_id}(GByteArray *st, const guint8 *buf, gsize bufsz, GError **error)
{{
    g_return_val_if_fail(st != NULL, FALSE);
    g_return_val_if_fail(buf != NULL, FALSE);
    return fu_memcpy_safe(st->data, st->len, {self.offset}, buf, bufsz, 0x0, bufsz, error);
}}
"""

        # GUID
        if self.type == Type.GUID:
            return f"""
{"static " if self.constant else ""}const fwupd_guid_t *
{name_snake}_get_{self.element_id}(GByteArray *st)
{{
    g_return_val_if_fail(st != NULL, NULL);
    return (const fwupd_guid_t *) (st->data + {self.offset});
}}
{"static " if self.constant else ""}void
{name_snake}_set_{self.element_id}(GByteArray *st, const fwupd_guid_t *value)
{{
    g_return_if_fail(st != NULL);
    g_return_if_fail(value != NULL);
    memcpy(st->data + {self.offset}, value, sizeof(*value));
}}
"""

        # U8
        if self.type == Type.U8:
            return f"""
{"static " if self.constant else ""}guint8
{name_snake}_get_{self.element_id}(GByteArray *st)
{{
    g_return_val_if_fail(st != NULL, 0x0);
    return st->data[{self.offset}];
}}
{"static " if self.constant else ""}void
{name_snake}_set_{self.element_id}(GByteArray *st, guint8 value)
{{
    g_return_if_fail(st != NULL);
    st->data[{self.offset}] = value;
}}
"""
        # uint
        if not self.multiplier and self.type in [
            Type.U16,
            Type.U24,
            Type.U32,
            Type.U64,
        ]:
            return f"""
{"static " if self.constant else ""}{self.type_glib}
{name_snake}_get_{self.element_id}(GByteArray *st)
{{
    g_return_val_if_fail(st != NULL, 0x0);
    return fu_memread_{self.type_mem}(st->data + {self.offset}, {self.endian_glib});
}}
{"static " if self.constant else ""}void
{name_snake}_set_{self.element_id}(GByteArray *st, {self.type_glib} value)
{{
    g_return_if_fail(st != NULL);
    fu_memwrite_{self.type_mem}(st->data + {self.offset}, value, {self.endian_glib});
}}
"""

        # fallback
        return ""

    def _parse_default(self, val: str) -> str:

        if self.type == Type.STRING:
            return val
        if self.type == Type.GUID or (self.type == Type.U8 and self.multiplier):
            if not val.startswith("0x"):
                raise ValueError(f"0x prefix for hex number expected, got: {val}")
            if len(val) != (self.size * 2) + 2:
                raise ValueError(f"data has to be {self.size} bytes exactly")
            val_hex = ""
            for idx in range(2, len(val), 2):
                val_hex += f"\\x{val[idx:idx+2]}"
            return val_hex
        if self.type in [
            Type.U8,
            Type.U16,
            Type.U24,
            Type.U32,
            Type.U64,
        ]:
            return val.replace("$struct_offset", str(self.offset))
        raise ValueError(f"do not know how to parse value for type: {self.type}")

    def parse_default(self, val: str) -> None:

        self.default = self._parse_default(val)

    def parse_constant(self, val: str) -> None:

        self.default = self._parse_default(val)
        self.constant = self.default

    def parse_type(self, val: str) -> None:

        # get the integer multiplier
        split_index = 0
        for char in val:
            if not char.isnumeric():
                break
            split_index += 1
        if split_index:
            self.multiplier = int(val[:split_index])

        # find the type
        try:
            val2 = val[split_index:]
            if val2.endswith("be"):
                self.endian = Endian.BIG
                val2 = val2[:-2]
            elif val2.endswith("le"):
                self.endian = Endian.LITTLE
                val2 = val2[:-2]
            self.type = Type(val2)
            if self.type == Type.GUID:
                self.multiplier = 16
        except ValueError as e:
            raise ValueError(f"invalid type: {val2}") from e

    def __str__(self) -> str:
        tmp = f"{self.element_id}: "
        if self.multiplier:
            tmp += str(self.multiplier)
        tmp += str(self.type)
        if self.endian != Endian.NATIVE:
            tmp += str(self.endian)
        if self.default:
            tmp += f": {self.default}"
        elif self.constant:
            tmp += f":: {self.constant}"
        return tmp


# convert a CamelCase name into snake_case
def _camel_to_snake(name: str) -> str:

    name_snake: str = ""
    for char in name:
        if char.islower() or char.isnumeric():
            name_snake += char
            continue
        if name_snake:
            name_snake += "_"
        name_snake += char.lower()
    return name_snake


class Generator:
    def __init__(self, basename):
        self.basename: str = basename

    def _process_items(self, name: str, items: List[Item]) -> Tuple[str, str]:

        # useful constants
        name_snake = f"fu_struct_{_camel_to_snake(name)}"
        size: int = 0
        for item in items:
            size += item.size
        has_constant: bool = False
        for item in items:
            if item.constant:
                has_constant = True
                break

        # header
        str_h = ""
        str_h += f"GByteArray* {name_snake}_new(void);\n"
        str_h += f"GByteArray* {name_snake}_parse(const guint8 *buf, gsize bufsz, gsize offset, GError **error);\n"
        str_h += f"gboolean {name_snake}_validate(const guint8 *buf, gsize bufsz, gsize offset, GError **error);\n"
        for item in items:
            if not item.enabled:
                continue
            str_h += item.generate_h_glib(name_snake)
        for item in items:
            str_h += f"#define {name_snake.upper()}_OFFSET_{item.element_id.upper()} 0x{item.offset:x}\n"
        str_h += f"#define {name_snake.upper()}_SIZE 0x{size:x}\n"
        # print(str_h)

        # code
        str_c = ""
        for item in items:
            if not item.enabled:
                continue
            str_c += item.generate_c_glib(name_snake)

        # _new()
        str_c += f"GByteArray *\n"
        str_c += f"{name_snake}_new(void)\n"
        str_c += f"{{\n"
        str_c += f"    GByteArray *st = g_byte_array_new();\n"
        str_c += f"    fu_byte_array_set_size(st, {size}, 0x0);\n"
        for item in items:
            if not item.default:
                continue
            if item.type == Type.STRING:
                str_c += f'    {name_snake}_set_{item.element_id}(st, "{item.default}", NULL);\n'
            elif item.type == Type.GUID:
                str_c += f'    {name_snake}_set_{item.element_id}(st, (fwupd_guid_t *) "{item.default}");\n'
            else:
                str_c += (
                    f"    {name_snake}_set_{item.element_id}(st, {item.default});\n"
                )
        str_c += f"    return st;\n"
        str_c += f"}}\n"

        # _parse()
        str_c += f"GByteArray *\n"
        str_c += f"{name_snake}_parse(const guint8 *buf, gsize bufsz, gsize offset, GError **error)\n"
        str_c += f"{{\n"
        str_c += f"    g_autoptr(GByteArray) st = g_byte_array_new();\n"
        str_c += f"    g_return_val_if_fail(buf != NULL, NULL);\n"
        str_c += f"    g_return_val_if_fail(error == NULL || *error == NULL, NULL);\n"
        str_c += f"    if (offset + {size} > bufsz) {{\n"
        str_c += f"            g_set_error(error,\n"
        str_c += f"                        G_IO_ERROR,\n"
        str_c += f"                        G_IO_ERROR_INVALID_DATA,\n"
        str_c += f'                        "cannot parse buffer of size 0x%x at offset 0x%x for struct {name} of size 0x%x",\n'
        str_c += f"                        (guint) bufsz,\n"
        str_c += f"                        (guint) offset,\n"
        str_c += f"                        (guint) {size});\n"
        str_c += f"            return NULL;\n"
        str_c += f"    }}\n"
        str_c += f"    g_byte_array_append(st, buf + offset, {size});\n"
        for item in items:
            if item.constant:
                if item.type == Type.STRING:
                    str_c += f'    if (strncmp((const gchar *) (st->data + {item.offset}), "{item.constant}", {item.size}) != 0) {{\n'
                elif item.type == Type.GUID:
                    str_c += f'    if (memcmp(st->data + {item.offset}, "{item.constant}", {item.size}) != 0) {{\n'
                else:
                    str_c += f"    if ({name_snake}_get_{item.element_id}(st) != {item.constant}) {{\n"
                str_c += "            g_set_error_literal(error,\n"
                str_c += "                                G_IO_ERROR,\n"
                str_c += "                                G_IO_ERROR_INVALID_DATA,\n"
                str_c += f'                                "constant {name}.{item.element_id} was not valid, expected {item.constant}");\n'
                str_c += "            return NULL;\n"
                str_c += "    }\n"
        str_c += "    return g_steal_pointer(&st);\n"
        str_c += "}\n"

        # _validate()
        if has_constant:
            str_c += f"gboolean\n"
            str_c += f"{name_snake}_validate(const guint8 *buf, gsize bufsz, gsize offset, GError **error)\n"
            str_c += f"{{\n"
            str_c += f"    GByteArray st = {{.data = (guint8 *) buf + offset, .len = bufsz - offset, }};\n"
            str_c += f"    g_return_val_if_fail(buf != NULL, FALSE);\n"
            str_c += (
                f"    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);\n"
            )
            str_c += f"    if (offset + {size} > bufsz) {{\n"
            str_c += f"            g_set_error(error,\n"
            str_c += f"                        G_IO_ERROR,\n"
            str_c += f"                        G_IO_ERROR_INVALID_DATA,\n"
            str_c += f'                        "cannot parse buffer of size 0x%x at offset 0x%x for struct {name} of size 0x%x",\n'
            str_c += f"                        (guint) bufsz,\n"
            str_c += f"                        (guint) offset,\n"
            str_c += f"                        (guint) {size});\n"
            str_c += f"            return FALSE;\n"
            str_c += f"    }}\n"
            for item in items:
                if not item.constant:
                    continue
                if item.type == Type.STRING:
                    str_c += f'    if (strncmp((const gchar *) (st.data + {item.offset}), "{item.constant}", {item.size}) != 0) {{\n'
                elif item.type == Type.GUID or (
                    item.type == Type.U8 and item.multiplier
                ):
                    str_c += f'    if (memcmp({name_snake}_get_{item.element_id}(&st), "{item.constant}", {item.size}) != 0) {{\n'
                else:
                    str_c += f"    if ({name_snake}_get_{item.element_id}(&st) != {item.constant}) {{\n"
                str_c += f"            g_set_error_literal(error,\n"
                str_c += f"                                G_IO_ERROR,\n"
                str_c += f"                                G_IO_ERROR_INVALID_DATA,\n"
                str_c += f'                                "constant {name}.{item.element_id} was not valid");\n'
                str_c += f"            return FALSE;\n"
                str_c += f"    }}\n"
            str_c += f"    return TRUE;\n"
            str_c += f"}}\n"

        # success
        return str_c, str_h

    def process_input(self, contents: str) -> Tuple[str, str]:
        name = None
        items: List[Item] = []
        offset: int = 0

        # header
        dst_h = """/* auto-generated, do not modify */
#pragma once
#include <glib.h>
typedef guint8 fwupd_guid_t[16];
"""

        # body
        dst_c = f"""/* auto-generated, do not modify */
#include "{self.basename}"
#include "fu-byte-array.h"
#include "fu-mem.h"
#include "fu-string.h"
"""

        for line in contents.split("\n"):

            # remove comments and indent
            line = line.split("//")[0].strip()
            if not line:
                continue

            # start of structure
            if line.startswith("struct ") and line.endswith("{"):
                name = line[6:-1].strip()
                continue

            # not in the structure
            if not name:
                continue

            # end of structure
            if line.startswith("}"):
                for item in items:
                    if item.default == "$struct_size":
                        item.default = str(offset)
                    if item.constant == "$struct_size":
                        item.constant = str(offset)
                str_c, str_h = self._process_items(name, items)
                dst_c += str_c
                dst_h += str_h
                name = None
                items = []
                offset = 0
                continue

            # split into sections
            parts = line.replace(" ", "").split(":")
            if len(parts) == 1:
                raise ValueError(f"invalid struct line: {line}")

            # parse one element
            item = Item()
            item.offset = offset
            item.element_id = parts[0]
            item.parse_type(parts[1])
            if len(parts) >= 3 and parts[2]:
                item.parse_default(parts[2])
            if len(parts) >= 4 and parts[3]:
                item.parse_constant(parts[3])
            offset += item.size
            items.append(item)

        # success
        return dst_c, dst_h


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("src", action="store", type=str, help="source")
    parser.add_argument("dst_c", action="store", type=str, help="destination .c")
    parser.add_argument("dst_h", action="store", type=str, help="destination .h")
    args = parser.parse_args()

    g = Generator(basename=os.path.basename(args.dst_h))
    with open(args.src, "rb") as f:
        try:
            dst_c, dst_h = g.process_input(f.read().decode())
        except ValueError as e:
            sys.exit(f"cannot process {args.src}: {str(e)}")
    with open(args.dst_c, "wb") as f:  # type: ignore
        f.write(dst_c.encode())
    with open(args.dst_h, "wb") as f:  # type: ignore
        f.write(dst_h.encode())

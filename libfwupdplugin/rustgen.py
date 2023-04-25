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

from jinja2 import Environment, FileSystemLoader, select_autoescape


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


# convert a CamelCase name into snake_case
def _camel_to_snake(name: str) -> str:

    # specified as all caps
    if name.upper() == name:
        return name.lower()

    name_snake: str = ""
    for char in name:
        if char.islower() or char.isnumeric():
            name_snake += char
            continue
        if char == "_":
            name_snake += char
            continue
        if name_snake:
            name_snake += "_"
        name_snake += char.lower()
    return name_snake


class EnumItem:
    def __init__(self) -> None:
        self.name: str = ""
        self.default: Optional[str] = None

    def c_define(self, name: str) -> str:
        name_snake = _camel_to_snake(name)
        return "{}_{}".format(
            name_snake.upper(), _camel_to_snake(self.name).replace("-", "_").upper()
        )

    @property
    def value(self) -> str:
        return _camel_to_snake(self.name).replace("_", "-")


class StructItem:
    def __init__(self) -> None:
        self.element_id: str = ""
        self.type: Type = Type.U8
        self.default: Optional[str] = None
        self.constant: Optional[str] = None
        self.padding: Optional[str] = None
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
        if self.type == Type.U16:
            return "guint16"
        if self.type == Type.U24:
            return "guint32"
        if self.type == Type.U32:
            return "guint32"
        if self.type == Type.U64:
            return "guint64"
        if self.type == Type.STRING:
            return "gchar"
        if self.type == Type.GUID:
            return "fwupd_guid_t"
        return "void"

    @property
    def type_mem(self) -> str:
        if self.type == Type.U16:
            return "uint16"
        if self.type == Type.U24:
            return "uint24"
        if self.type == Type.U32:
            return "uint32"
        if self.type == Type.U64:
            return "uint64"
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

    def parse_padding(self, val: str) -> None:

        if self.type == Type.U8 and self.multiplier:
            if not val.startswith("0x"):
                raise ValueError(f"0x prefix for hex number expected, got: {val}")
            if len(val) != 4:
                raise ValueError("data has to be one byte only")
            self.padding = val
            return
        raise ValueError(f"do not know how to parse value for type: {self.type}")

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
            tmp += f": default={self.default}"
        elif self.constant:
            tmp += f": const={self.constant}"
        elif self.padding:
            tmp += f": padding={self.padding}"
        return tmp


class Generator:
    def __init__(self, basename) -> None:
        self.basename: str = basename
        self._env = Environment(
            loader=FileSystemLoader(os.path.dirname(__file__)),
            autoescape=select_autoescape(),
            keep_trailing_newline=True,
        )

    def _process_enums(
        self, name: str, items: List[EnumItem], derives: List[str]
    ) -> Tuple[str, str]:

        # render
        subst = {
            "Type": Type,
            "name": f"Fu{name}",
            "name_snake": f"fu_{_camel_to_snake(name)}",
            "items": items,
            "derives": derives,
        }
        template_h = self._env.get_template(os.path.basename("fu-rustgen-enum.h.in"))
        template_c = self._env.get_template(os.path.basename("fu-rustgen-enum.c.in"))
        return template_c.render(subst), template_h.render(subst)

    def _process_structs(
        self, name: str, items: List[StructItem], derives: List[str]
    ) -> Tuple[str, str]:

        # useful constants
        size: int = 0
        for item in items:
            size += item.size
        has_constant: bool = False
        for item in items:
            if item.constant:
                has_constant = True
                break

        # render
        subst = {
            "Type": Type,
            "name": f"Fu{name}",
            "name_snake": f"fu_struct_{_camel_to_snake(name)}",
            "items": items,
            "derives": derives,
            "size": size,
            "has_constant": has_constant,
        }
        template_h = self._env.get_template(os.path.basename("fu-rustgen-struct.h.in"))
        template_c = self._env.get_template(os.path.basename("fu-rustgen-struct.c.in"))
        return template_c.render(subst), template_h.render(subst)

    def process_input(self, contents: str) -> Tuple[str, str]:
        name = None
        enum_items: List[EnumItem] = []
        struct_items: List[StructItem] = []
        derives: List[str] = []
        offset: int = 0

        # header
        subst = {
            "basename": self.basename,
        }
        template_h = self._env.get_template(os.path.basename("fu-rustgen.h.in"))
        template_c = self._env.get_template(os.path.basename("fu-rustgen.c.in"))
        dst_h = template_h.render(subst)
        dst_c = template_c.render(subst)

        mode: Optional[str] = None
        for line in contents.split("\n"):

            # remove comments and indent
            line = line.split("//")[0].strip()
            if not line:
                continue

            # start of structure
            if line.startswith("struct ") and line.endswith("{"):
                mode = "struct"
                name = line[6:-1].strip()
                continue
            if line.startswith("enum ") and line.endswith("{"):
                mode = "enum"
                name = line[4:-1].strip()
                continue

            # what should we build
            if line.startswith("#[derive("):
                for derive in line[9:-2].replace(" ", "").split(","):
                    if derive == "Parse":
                        derives.append("Getters")
                    if derive == "New":
                        derives.append("Setters")
                    derives.append(derive)
                continue

            # not in object
            if not mode:
                continue

            # end of structure
            if line.startswith("}"):
                if name and struct_items:
                    for item in struct_items:
                        if item.default == "$struct_size":
                            item.default = str(offset)
                        if item.constant == "$struct_size":
                            item.constant = str(offset)
                    str_c, str_h = self._process_structs(name, struct_items, derives)
                if name and enum_items:
                    str_c, str_h = self._process_enums(name, enum_items, derives)
                dst_c += str_c
                dst_h += str_h
                mode = None
                name = None
                struct_items.clear()
                enum_items.clear()
                derives.clear()
                offset = 0
                continue

            # split enumeration into sections
            if mode == "enum":
                enum_item = EnumItem()
                parts = line.replace(" ", "").replace(",", "").split("=", maxsplit=2)
                enum_item.name = parts[0]
                if len(parts) > 1:
                    enum_item.default = parts[1]
                enum_items.append(enum_item)

            # split structure into sections
            if mode == "struct":
                parts = line.replace(" ", "").split(":", maxsplit=3)
                if len(parts) == 1:
                    raise ValueError(f"invalid struct line: {line}")

                # parse one element
                item = StructItem()
                item.offset = offset
                item.element_id = parts[0]
                item.parse_type(parts[1])
                for part in parts[2:]:
                    try:
                        key, value = tuple(part.split("=", maxsplit=1))
                    except ValueError as e:
                        raise ValueError(f"invalid struct line: {line}") from e
                    if key == "const":
                        item.parse_constant(value)
                    elif key == "default":
                        item.parse_default(value)
                    elif key == "padding":
                        item.parse_padding(value)
                    else:
                        raise ValueError(f"invalid struct line: {line}")
                offset += item.size
                struct_items.append(item)

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

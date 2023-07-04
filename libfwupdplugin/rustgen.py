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
from typing import Optional, List, Tuple, Dict

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
    STRING = "char"
    GUID = "Guid"


class Export(Enum):
    NONE = "none"
    PRIVATE = "static "
    PUBLIC = ""


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


class EnumObj:
    def __init__(self, name: str) -> None:
        self.name: str = name
        self.repr_type: Optional[str] = None
        self.items: List[EnumItem] = []
        self._exports: Dict[str, Export] = {
            "ToString": Export.NONE,
            "ToBitString": Export.NONE,
            "FromString": Export.NONE,
        }

    def c_method(self, suffix: str):
        return f"fu_{_camel_to_snake(self.name)}_{_camel_to_snake(suffix)}"

    @property
    def c_type(self):
        return f"Fu{self.name}"

    def add_private_export(self, derive: str) -> None:
        if self._exports[derive] == Export.PUBLIC:
            return
        self._exports[derive] = Export.PRIVATE

    def add_public_export(self, derive: str) -> None:
        self.add_private_export(derive)
        self._exports[derive] = Export.PUBLIC

    def export(self, derive: str) -> Export:
        return self._exports[derive]

    def __str__(self) -> str:
        return f"EnumObj({self.name})"


class EnumItem:
    def __init__(self, obj: EnumObj) -> None:
        self.obj: EnumObj = obj
        self.name: str = ""
        self.default: Optional[str] = None

    @property
    def c_define(self) -> str:
        name_snake = _camel_to_snake(self.obj.name)
        return f"FU_{name_snake.upper()}_{_camel_to_snake(self.name).replace('-', '_').upper()}"

    @property
    def value(self) -> str:
        return _camel_to_snake(self.name).replace("_", "-")


class StructObj:
    def __init__(self, name: str) -> None:
        self.name: str = name
        self.items: List[StructItem] = []
        self._exports: Dict[str, Export] = {
            "Validate": Export.NONE,
            "Parse": Export.NONE,
            "New": Export.NONE,
            "ToString": Export.NONE,
        }

    def c_method(self, suffix: str):
        return f"fu_struct_{_camel_to_snake(self.name)}_{_camel_to_snake(suffix)}"

    def c_define(self, suffix: str):
        return f"FU_STRUCT_{_camel_to_snake(self.name).upper()}_{suffix.upper()}"

    @property
    def size(self) -> int:
        size: int = 0
        for item in self.items:
            size += item.size
        return size

    @property
    def has_constant(self) -> bool:
        for item in self.items:
            if item.constant:
                return True
        return False

    def add_private_export(self, derive: str) -> None:
        if self._exports[derive] == Export.PUBLIC:
            return
        self._exports[derive] = Export.PRIVATE
        if derive == "Validate":
            for item in self.items:
                if item.constant and item.type != Type.STRING:
                    item.add_private_export("Getters")
                if item.struct_obj:
                    item.struct_obj.add_private_export("Validate")
        if derive == "ToString":
            for item in self.items:
                if item.enum_obj:
                    item.enum_obj.add_private_export("ToString")
        if derive == "Parse":
            self.add_private_export("ToString")
            for item in self.items:
                if item.constant and item.type != Type.STRING:
                    item.add_private_export("Getters")
        if derive == "New":
            for item in self.items:
                if item.constant:
                    item.add_private_export("Setters")

    def add_public_export(self, derive: str) -> None:

        # Getters and Setters are special as we do not want public exports of const
        if derive in ["Getters", "Setters"]:
            for item in self.items:
                if not item.constant:
                    item.add_public_export(derive)
        else:
            self.add_private_export(derive)
            self._exports[derive] = Export.PUBLIC

        # for convenience
        if derive == "Parse":
            self.add_public_export("Getters")
        if derive == "New":
            self.add_public_export("Setters")

    def export(self, derive: str) -> Export:
        return self._exports[derive]

    def __str__(self) -> str:
        return f"StructObj({self.name})"


class StructItem:
    def __init__(self, obj: StructObj) -> None:
        self.obj: StructObj = obj
        self.element_id: str = ""
        self.type: Type = Type.U8
        self.enum_obj: Optional[EnumObj] = None
        self.struct_obj: Optional[StructObj] = None
        self.default: Optional[str] = None
        self.constant: Optional[str] = None
        self.padding: Optional[str] = None
        self.endian: Endian = Endian.NATIVE
        self.multiplier: int = 0
        self.offset: int = 0
        self._exports: Dict[str, Export] = {
            "Getters": Export.NONE,
            "Setters": Export.NONE,
        }

    def add_private_export(self, derive: str) -> None:
        if self._exports[derive] == Export.PUBLIC:
            return
        self._exports[derive] = Export.PRIVATE

    def add_public_export(self, derive: str) -> None:
        self.add_private_export(derive)
        self._exports[derive] = Export.PUBLIC

    def export(self, derive: str) -> Export:
        return self._exports[derive]

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

    def c_define(self, suffix: str):
        return self.obj.c_define(suffix.upper() + "_" + self.element_id.upper())

    @property
    def c_getter(self):
        return self.obj.c_method("get_" + self.element_id)

    @property
    def c_setter(self):
        return self.obj.c_method("set_" + self.element_id)

    @property
    def type_glib(self) -> str:
        if self.enum_obj:
            return self.enum_obj.c_type
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
            if val.startswith('"') and val.endswith('"'):
                return val[1:-1]
            raise ValueError(f"string default {val} needs double quotes")
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

        if (
            self.type == Type.U8
            and self.multiplier
            and val.startswith("0x")
            and len(val) == 4
        ):
            self.padding = val
            return
        self.default = self._parse_default(val)

    def parse_constant(self, val: str) -> None:

        self.default = self._parse_default(val)
        self.constant = self.default

    def parse_type(
        self, val: str, enum_objs: Dict[str, EnumObj], struct_objs: Dict[str, StructObj]
    ) -> None:

        # is array
        if val.startswith("[") and val.endswith("]"):
            typestr, multiplier = val[1:-1].split(";", maxsplit=1)
            self.multiplier = int(multiplier)
        else:
            typestr = val

        # nested struct
        if typestr in struct_objs:
            self.struct_obj = struct_objs[typestr]
            self.multiplier = self.struct_obj.size
            self.type = Type.U8
            return

        # find the type
        if typestr in enum_objs:
            self.enum_obj = enum_objs[typestr]
            typestr_maybe: Optional[str] = enum_objs[typestr].repr_type
            if not typestr_maybe:
                raise ValueError(f"no repr for: {typestr}")
            typestr = typestr_maybe
        try:
            if typestr.endswith("be"):
                self.endian = Endian.BIG
                typestr = typestr[:-2]
            elif typestr.endswith("le"):
                self.endian = Endian.LITTLE
                typestr = typestr[:-2]
            self.type = Type(typestr)
            if self.type == Type.GUID:
                self.multiplier = 16
        except ValueError as e:
            raise ValueError(f"invalid type: {typestr}") from e

    def __str__(self) -> str:
        tmp = f"{self.element_id}: "
        if self.multiplier:
            tmp += str(self.multiplier)
        tmp += self.type.value
        if self.endian != Endian.NATIVE:
            tmp += self.endian.value
        if self.default:
            tmp += f" = {self.default}"
        elif self.constant:
            tmp += f" == {self.constant}"
        elif self.padding:
            tmp += f" = {self.padding}"
        return tmp


class Generator:
    def __init__(self, basename) -> None:
        self.basename: str = basename
        self.struct_objs: Dict[str, StructObj] = {}
        self.enum_objs: Dict[str, EnumObj] = {}
        self._env = Environment(
            loader=FileSystemLoader(os.path.dirname(__file__)),
            autoescape=select_autoescape(),
            keep_trailing_newline=True,
        )

    def _process_enums(self, enum_obj: EnumObj) -> Tuple[str, str]:

        # render
        subst = {
            "Type": Type,
            "Export": Export,
            "obj": enum_obj,
        }
        template_h = self._env.get_template(os.path.basename("fu-rustgen-enum.h.in"))
        template_c = self._env.get_template(os.path.basename("fu-rustgen-enum.c.in"))
        return template_c.render(subst), template_h.render(subst)

    def _process_structs(self, struct_obj: StructObj) -> Tuple[str, str]:

        # render
        subst = {
            "Type": Type,
            "Export": Export,
            "obj": struct_obj,
        }
        template_h = self._env.get_template(os.path.basename("fu-rustgen-struct.h.in"))
        template_c = self._env.get_template(os.path.basename("fu-rustgen-struct.c.in"))
        return template_c.render(subst), template_h.render(subst)

    def process_input(self, contents: str) -> Tuple[str, str]:
        name = None
        repr_type: Optional[str] = None
        derives: List[str] = []
        offset: int = 0
        struct_cur: Optional[StructObj] = None
        enum_cur: Optional[EnumObj] = None

        for line in contents.split("\n"):

            # remove comments and indent
            line = line.split("//")[0].strip()
            if not line:
                continue

            # start of structure
            if line.startswith("struct ") and line.endswith("{"):
                name = line[6:-1].strip()
                if name in self.struct_objs:
                    raise ValueError(f"struct {name} already defined")
                struct_cur = StructObj(name)
                self.struct_objs[name] = struct_cur
                continue
            if line.startswith("enum ") and line.endswith("{"):
                name = line[4:-1].strip()
                if name in self.enum_objs:
                    raise ValueError(f"enum {name} already defined")
                enum_cur = EnumObj(name)
                enum_cur.repr_type = repr_type
                self.enum_objs[name] = enum_cur
                continue

            # the enum type
            if line.startswith("#[repr(") and line.endswith(")]"):
                repr_type = line[7:-2]
                continue

            # what should we build
            if line.startswith("#[derive("):
                for derive in line[9:-2].replace(" ", "").split(","):
                    derives.append(derive)
                continue

            # not in object
            if not struct_cur and not enum_cur:
                continue

            # end of structure
            if line.startswith("}"):
                if struct_cur:
                    for derive in derives:
                        struct_cur.add_public_export(derive)
                    for item in struct_cur.items:
                        if item.default == "$struct_size":
                            item.default = str(offset)
                        if item.constant == "$struct_size":
                            item.constant = str(offset)
                if enum_cur:
                    for derive in derives:
                        enum_cur.add_public_export(derive)
                struct_cur = None
                enum_cur = None
                repr_type = None
                derives.clear()
                offset = 0
                continue

            # check for trailing comma
            if not line.endswith(","):
                raise ValueError(f"invalid struct line: {line} -- needs trailing comma")
            line = line[:-1]

            # split enumeration into sections
            if enum_cur:
                enum_item = EnumItem(enum_cur)
                parts = line.replace(" ", "").split("=", maxsplit=2)
                enum_item.name = parts[0]
                if len(parts) > 1:
                    enum_item.default = parts[1]
                enum_cur.items.append(enum_item)

            # split structure into sections
            if struct_cur:

                # parse "signature: u32be == 0x12345678"
                parts = line.replace(" ", "").split(":", maxsplit=2)
                if len(parts) == 1:
                    raise ValueError(f"invalid struct line: {line}")

                # parse one element
                item = StructItem(struct_cur)
                item.offset = offset
                item.element_id = parts[0]

                type_parts = parts[1].split("=", maxsplit=3)
                item.parse_type(
                    type_parts[0],
                    enum_objs=self.enum_objs,
                    struct_objs=self.struct_objs,
                )
                if len(type_parts) == 3:
                    item.parse_constant(type_parts[2])
                elif len(type_parts) == 2:
                    item.parse_default(type_parts[1])
                offset += item.size
                struct_cur.items.append(item)

        # process the templates here
        subst = {
            "basename": self.basename,
        }
        template_h = self._env.get_template(os.path.basename("fu-rustgen.h.in"))
        template_c = self._env.get_template(os.path.basename("fu-rustgen.c.in"))
        dst_h = template_h.render(subst)
        dst_c = template_c.render(subst)
        for enum_obj in self.enum_objs.values():
            str_c, str_h = self._process_enums(enum_obj)
            dst_c += str_c
            dst_h += str_h
        for struct_obj in self.struct_objs.values():
            str_c, str_h = self._process_structs(struct_obj)
            dst_c += str_c
            dst_h += str_h

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

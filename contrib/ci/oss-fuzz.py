#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=too-many-instance-attributes,no-self-use

import os
import sys
import subprocess
import glob
from typing import Dict, Optional, List, Union

DEFAULT_BUILDDIR = ".ossfuzz"


class Builder:
    def __init__(self) -> None:
        self.cc = self._ensure_environ("CC", "gcc")
        self.cxx = self._ensure_environ("CXX", "g++")
        self.builddir = self._ensure_environ("WORK", os.path.realpath(DEFAULT_BUILDDIR))
        self.installdir = self._ensure_environ(
            "OUT", os.path.realpath(os.path.join(DEFAULT_BUILDDIR, "out"))
        )
        self.srcdir = self._ensure_environ("SRC", os.path.realpath(".."))
        self.ldflags = [
            "-lpthread",
            "-lresolv",
            "-ldl",
            "-lffi",
            "-lz",
            "-llzma",
            "-lzstd",
        ]

        # defined in env
        self.cflags = ["-Wno-deprecated-declarations", "-g"]
        if "CFLAGS" in os.environ:
            self.cflags += os.environ["CFLAGS"].split(" ")
        self.cxxflags = []
        if "CXXFLAGS" in os.environ:
            self.cxxflags += os.environ["CXXFLAGS"].split(" ")

        # set up shared / static
        os.environ["PKG_CONFIG"] = "pkg-config --static"
        if "PATH" in os.environ:
            os.environ["PATH"] = "{}:{}".format(
                os.environ["PATH"], os.path.join(self.builddir, "bin")
            )
        else:
            os.environ["PATH"] = os.path.join(self.builddir, "bin")
        os.environ["PKG_CONFIG_PATH"] = os.path.join(self.builddir, "lib", "pkgconfig")

        # writable
        os.makedirs(self.builddir, exist_ok=True)
        os.makedirs(self.installdir, exist_ok=True)

    def _ensure_environ(self, key: str, value: str) -> str:
        """set the environment unless already set"""
        if key not in os.environ:
            os.environ[key] = value
        return os.environ[key]

    def checkout_source(
        self,
        name: str,
        url: str,
        commit: Optional[str] = None,
        patches: Optional[List[str]] = None,
    ) -> str:
        """checkout source tree, optionally to a specific commit"""
        srcdir_name = os.path.join(self.srcdir, name)
        if os.path.exists(srcdir_name):
            return srcdir_name
        subprocess.run(["git", "clone", url], cwd=self.srcdir, check=True)
        if commit:
            subprocess.run(["git", "checkout", commit], cwd=srcdir_name, check=True)
        if patches:
            for fn in patches:
                with open(os.path.join(self.srcdir, "fwupd", fn), "rb") as f:
                    subprocess.run(
                        ["patch", "-p1"], cwd=srcdir_name, check=True, input=f.read()
                    )
        return srcdir_name

    def build_meson_project(self, srcdir: str, argv) -> None:
        """configure and build the meson project"""
        srcdir_build = os.path.join(srcdir, DEFAULT_BUILDDIR)
        if not os.path.exists(srcdir_build):
            subprocess.run(
                [
                    "meson",
                    "--prefix",
                    self.builddir,
                    "--libdir",
                    "lib",
                    "--default-library",
                    "static",
                ]
                + argv
                + [DEFAULT_BUILDDIR],
                cwd=srcdir,
                check=True,
            )
            subprocess.run(["ninja", "install"], cwd=srcdir_build, check=True)

    def build_cmake_project(self, srcdir: str, argv=None) -> None:
        """configure and build the meson project"""
        if not argv:
            argv = []
        srcdir_build = os.path.join(srcdir, DEFAULT_BUILDDIR)
        if not os.path.exists(srcdir_build):
            os.makedirs(srcdir_build, exist_ok=True)
            subprocess.run(
                [
                    "cmake",
                    f"-DCMAKE_INSTALL_PREFIX:PATH={self.builddir}",
                    "-DCMAKE_INSTALL_LIBDIR=lib",
                ]
                + argv
                + [".."],
                cwd=srcdir_build,
                check=True,
            )
            subprocess.run(["make", "all", "install"], cwd=srcdir_build, check=True)

    def add_work_includedir(self, value: str) -> None:
        """add a CFLAG"""
        self.cflags.append(f"-I{self.builddir}/{value}")

    def add_src_includedir(self, value: str) -> None:
        """add a CFLAG"""
        self.cflags.append(f"-I{self.srcdir}/{value}")

    def add_build_ldflag(self, value: str) -> None:
        """add a LDFLAG"""
        self.ldflags.append(os.path.join(self.builddir, value))

    def substitute(self, src: str, replacements: Dict[str, str]) -> str:
        """map changes"""

        dst = os.path.basename(src).replace(".in", "")
        with open(os.path.join(self.srcdir, src), "r") as f:
            blob = f.read()
            for key in replacements:
                blob = blob.replace(key, replacements[key])
            with open(os.path.join(self.builddir, dst), "w") as out:
                out.write(blob)
        return dst

    def compile(self, src: str) -> str:
        """compile a specific source file"""
        argv = [self.cc]
        argv.extend(self.cflags)
        fullsrc = os.path.join(self.srcdir, src)
        if not os.path.exists(fullsrc):
            fullsrc = os.path.join(self.builddir, src)
        dst = os.path.basename(src).replace(".c", ".o")
        argv.extend(["-c", fullsrc, "-o", os.path.join(self.builddir, dst)])
        print(f"building {src} into {dst}")
        try:
            subprocess.run(argv, cwd=self.srcdir, check=True)
        except subprocess.CalledProcessError as e:
            print(e)
            sys.exit(1)
        return os.path.join(self.builddir, f"{dst}")

    def rustgen(self, src: str) -> str:
        fn_root = os.path.basename(src).replace(".rs", "")
        fulldst_c = os.path.join(self.builddir, f"{fn_root}-struct.c")
        fulldst_h = os.path.join(self.builddir, f"{fn_root}-struct.h")
        try:
            subprocess.run(
                [
                    "python",
                    "fwupd/libfwupdplugin/rustgen.py",
                    src,
                    fulldst_c,
                    fulldst_h,
                ],
                cwd=self.srcdir,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print(e)
            sys.exit(1)
        return fulldst_c

    def link(self, objs: List[str], dst: str) -> str:
        """link multiple objects into a binary"""
        argv = [self.cxx] + self.cxxflags
        for obj in objs:
            if obj.startswith("-"):
                argv.append(obj)
            else:
                argv.append(os.path.join(self.builddir, obj))
        argv += ["-o", os.path.join(self.installdir, dst)]
        argv += self.ldflags
        print(f"building {','.join(objs)} into {dst}")
        subprocess.run(argv, cwd=self.srcdir, check=True)
        return os.path.join(self.installdir, dst)

    def mkfuzztargets(self, exe: str, globstr: str) -> List[str]:
        """make binary fuzzing targets from builder.xml files"""
        builder_xmls = glob.glob(globstr)
        corpus: List[str] = []
        if not builder_xmls:
            print(f"failed to find {globstr}")
        for fn_src in builder_xmls:
            fn_dst = os.path.join(
                self.builddir, os.path.basename(fn_src).replace(".builder.xml", ".bin")
            )
            print(f"building {fn_src} into {fn_dst}")
            try:
                argv = [exe, fn_src, fn_dst]
                subprocess.run(argv, check=True)
            except subprocess.CalledProcessError as e:
                print(f"tried to run: `{' '.join(argv)}` and got {str(e)}")
                sys.exit(1)
            corpus.append(fn_dst)
        return corpus

    def write_header(
        self, dst: str, defines: Dict[str, Optional[Union[str, int]]]
    ) -> None:
        """write a header file"""
        dstdir = os.path.join(self.builddir, os.path.dirname(dst))
        os.makedirs(dstdir, exist_ok=True)
        print(f"writing {dst}")
        with open(os.path.join(dstdir, os.path.basename(dst)), "w") as f:
            for key in defines:
                value = defines[key]
                if value is not None:
                    if isinstance(value, int):
                        f.write(f"#define {key} {value}\n")
                    else:
                        f.write(f'#define {key} "{value}"\n')
                else:
                    f.write(f"#define {key}\n")
        self.add_work_includedir(os.path.dirname(dst))

    def makezip(self, dst: str, corpus: List[str]) -> None:
        """create a zip file archive from a glob"""
        if not corpus:
            return
        argv = ["zip", "--junk-paths", os.path.join(self.installdir, dst)] + corpus
        print(f"assembling {dst}")
        subprocess.run(argv, cwd=self.srcdir, check=True)

    def grep_meson(self, src: str, token: str = "fuzzing") -> List[str]:
        """find source files tagged with a specific comment"""
        srcs = []
        with open(os.path.join(self.srcdir, src, "meson.build"), "r") as f:
            for line in f.read().split("\n"):
                if line.find(token) == -1:
                    continue

                # get rid of token
                line = line.split("#")[0]

                # get rid of variable
                try:
                    line = line.split("=")[1]
                except IndexError:
                    pass

                # get rid of whitespace
                for char in ["'", ",", " "]:
                    line = line.replace(char, "")

                # all done
                srcs.append(os.path.join(src, line))
        return srcs


class Fuzzer:
    def __init__(self, name, srcdir=None, pattern=None) -> None:
        self.name = name
        self.srcdir = srcdir or name
        self.globstr = f"{name}*.bin"
        self.pattern = pattern or f"{name}-firmware"

    @property
    def new_gtype(self) -> str:
        return f"g_object_new(FU_TYPE_{self.pattern.replace('-', '_').upper()}, NULL)"

    @property
    def header(self) -> str:
        return f"fu-{self.pattern}.h"


def _build(bld: Builder) -> None:
    # CBOR
    src = bld.checkout_source(
        "libcbor",
        url="https://github.com/PJK/libcbor.git",
        commit="b223daaaa34dcb83f9c25576f05e4f1646f44bf9",
    )
    bld.build_cmake_project(src, argv=["-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF"])
    bld.add_build_ldflag("lib/libcbor.a")

    # GLib
    src = bld.checkout_source(
        "glib", url="https://gitlab.gnome.org/GNOME/glib.git", commit="glib-2-68"
    )
    bld.build_meson_project(
        src,
        [
            "-Dlibmount=disabled",
            "-Dselinux=disabled",
            "-Dnls=disabled",
            "-Dlibelf=disabled",
            "-Dbsymbolic_functions=false",
            "-Dtests=false",
            "-Dinternal_pcre=true",
            "--force-fallback-for=libpcre",
        ],
    )
    bld.add_work_includedir("include/glib-2.0")
    bld.add_work_includedir("lib/glib-2.0/include")
    bld.add_build_ldflag("lib/libgio-2.0.a")
    bld.add_build_ldflag("lib/libgmodule-2.0.a")
    bld.add_build_ldflag("lib/libgobject-2.0.a")
    bld.add_build_ldflag("lib/libglib-2.0.a")
    bld.add_build_ldflag("lib/libgthread-2.0.a")

    # JSON-GLib
    src = bld.checkout_source(
        "json-glib",
        url="https://gitlab.gnome.org/GNOME/json-glib.git",
        commit="1.8.0-actual",
    )
    bld.build_meson_project(
        src, ["-Dgtk_doc=disabled", "-Dtests=false", "-Dintrospection=disabled"]
    )
    bld.add_work_includedir("include/json-glib-1.0/json-glib")
    bld.add_work_includedir("include/json-glib-1.0")
    bld.add_build_ldflag("lib/libjson-glib-1.0.a")

    # libxmlb
    src = bld.checkout_source("libxmlb", url="https://github.com/hughsie/libxmlb.git")
    bld.build_meson_project(
        src, ["-Dgtkdoc=false", "-Dintrospection=false", "-Dtests=false"]
    )
    bld.add_work_includedir("include/libxmlb-2")
    bld.add_work_includedir("include/libxmlb-2/libxmlb")
    bld.add_build_ldflag("lib/libxmlb.a")

    # write required headers
    bld.write_header(
        "libfwupd/fwupd-version.h",
        {
            "FWUPD_MAJOR_VERSION": 0,
            "FWUPD_MINOR_VERSION": 0,
            "FWUPD_MICRO_VERSION": 0,
        },
    )
    bld.write_header(
        "config.h",
        {
            "FWUPD_DATADIR": "/tmp",
            "FWUPD_LOCALSTATEDIR": "/tmp",
            "FWUPD_LIBDIR_PKG": "/tmp",
            "FWUPD_SYSCONFDIR": "/tmp",
            "FWUPD_LIBEXECDIR": "/tmp",
            "HAVE_CBOR": None,
            "HAVE_CBOR_SET_ALLOCS": None,
            "HAVE_REALPATH": None,
            "PACKAGE_NAME": "fwupd",
            "PACKAGE_VERSION": "0.0.0",
        },
    )

    # libfwupd + libfwupdplugin
    built_objs: List[str] = []
    fuzzing_objs: List[str] = []
    bld.add_src_includedir("fwupd")
    for path in ["fwupd/libfwupd", "fwupd/libfwupdplugin"]:
        bld.add_src_includedir(path)
        for src in bld.grep_meson(path):
            if src.endswith(".c"):
                built_objs.append(bld.compile(src))
            elif src.endswith(".rs"):
                built_objs.append(bld.compile(bld.rustgen(src)))

    # dummy binary entrypoint
    if "LIB_FUZZING_ENGINE" in os.environ:
        fuzzing_objs.append(os.environ["LIB_FUZZING_ENGINE"])
    else:
        fuzzing_objs.append(bld.compile("fwupd/libfwupdplugin/fu-fuzzer-main.c"))

    # built in formats
    for fzr in [
        Fuzzer("efi-lz77", pattern="efi-lz77-decompressor"),
        Fuzzer("csv"),
        Fuzzer("cab"),
        Fuzzer("dfuse"),
        Fuzzer("edid", pattern="edid"),
        Fuzzer("fdt"),
        Fuzzer("fit"),
        Fuzzer("fmap"),
        Fuzzer("hid-descriptor", pattern="hid-descriptor"),
        Fuzzer("ihex"),
        Fuzzer("srec"),
        Fuzzer("intel-thunderbolt"),
        Fuzzer("ifwi-cpd"),
        Fuzzer("ifwi-fpt"),
        Fuzzer("oprom"),
        Fuzzer("uswid"),
        Fuzzer("efi-filesystem", pattern="efi-filesystem"),
        Fuzzer("efi-volume", pattern="efi-volume"),
        Fuzzer("efi-load-option", pattern="efi-load-option"),
        Fuzzer("ifd"),
    ]:
        src = bld.substitute(
            "fwupd/libfwupdplugin/fu-fuzzer-firmware.c.in",
            {
                "@FIRMWARENEW@": fzr.new_gtype,
                "@INCLUDE@": os.path.join("libfwupdplugin", fzr.header),
            },
        )
        exe = bld.link(
            [bld.compile(src)] + fuzzing_objs + built_objs, f"{fzr.name}_fuzzer"
        )

        src_generator = bld.substitute(
            "fwupd/libfwupdplugin/fu-fuzzer-generate.c.in",
            {
                "@FIRMWARENEW@": fzr.new_gtype,
                "@INCLUDE@": os.path.join("libfwupdplugin", fzr.header),
            },
        )
        exe_generator = bld.link(
            [bld.compile(src_generator)] + built_objs, f"{fzr.name}_generator"
        )
        corpus = bld.mkfuzztargets(
            exe_generator,
            os.path.join(
                bld.srcdir,
                "fwupd",
                "libfwupdplugin",
                "tests",
                f"{fzr.name}*.builder.xml",
            ),
        )
        bld.makezip(
            f"{fzr.name}_fuzzer_seed_corpus.zip",
            corpus,
        )

    # plugins
    for fzr in [
        Fuzzer("acpi-phat", pattern="acpi-phat"),
        Fuzzer("bcm57xx"),
        Fuzzer("ccgx"),
        Fuzzer("ccgx-dmc"),
        Fuzzer("cros-ec"),
        Fuzzer("ebitdo"),
        Fuzzer("elanfp"),
        Fuzzer("elantp"),
        Fuzzer("genesys-scaler", srcdir="genesys", pattern="genesys-scaler-firmware"),
        Fuzzer("genesys-usbhub", srcdir="genesys", pattern="genesys-usbhub-firmware"),
        Fuzzer("pixart", srcdir="pixart-rf", pattern="pxi-firmware"),
        Fuzzer("redfish-smbios", srcdir="redfish", pattern="redfish-smbios"),
        Fuzzer("synaptics-prometheus", pattern="synaprom-firmware"),
        Fuzzer("synaptics-cape"),
        Fuzzer("synaptics-mst"),
        Fuzzer("synaptics-rmi"),
        Fuzzer("uf2"),
        Fuzzer("wacom-usb", pattern="wac-firmware"),
    ]:
        fuzz_objs = []
        for obj in bld.grep_meson(f"fwupd/plugins/{fzr.srcdir}"):
            if obj.endswith(".c"):
                fuzz_objs.append(bld.compile(obj))
            elif obj.endswith(".rs"):
                fuzz_objs.append(bld.compile(bld.rustgen(obj)))
        src = bld.substitute(
            "fwupd/libfwupdplugin/fu-fuzzer-firmware.c.in",
            {
                "@FIRMWARENEW@": fzr.new_gtype,
                "@INCLUDE@": os.path.join("plugins", fzr.srcdir, fzr.header),
            },
        )
        exe = bld.link(
            fuzz_objs + built_objs + fuzzing_objs + [bld.compile(src)],
            f"{fzr.name}_fuzzer",
        )

        # generate the corpus
        src_generator = bld.substitute(
            "fwupd/libfwupdplugin/fu-fuzzer-generate.c.in",
            {
                "@FIRMWARENEW@": fzr.new_gtype,
                "@INCLUDE@": os.path.join("plugins", fzr.srcdir, fzr.header),
            },
        )
        exe_generator = bld.link(
            fuzz_objs + built_objs + [bld.compile(src_generator)],
            f"{fzr.name}_generator",
        )
        corpus = bld.mkfuzztargets(
            exe_generator,
            os.path.join(
                bld.srcdir,
                "fwupd",
                "plugins",
                fzr.srcdir,
                "tests",
                f"{fzr.name}*.builder.xml",
            ),
        )
        bld.makezip(
            f"{fzr.name}_fuzzer_seed_corpus.zip",
            corpus,
        )


if __name__ == "__main__":
    # install missing deps here rather than patching the Dockerfile in oss-fuzz
    try:
        subprocess.check_call(
            [
                "apt-get",
                "install",
                "-y",
                "liblzma-dev",
                "libzstd-dev",
                "libcbor-dev",
                "python3",
                "python3-jinja2",
                "python3-packaging",
            ],
            stdout=open(os.devnull, "wb"),
        )
    except FileNotFoundError:
        pass
    except subprocess.CalledProcessError as e:
        print(e.output)
        sys.exit(1)

    _builder = Builder()
    _build(_builder)
    sys.exit(0)

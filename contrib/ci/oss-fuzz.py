#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
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
        self.ldflags = ["-lpthread", "-lresolv", "-ldl", "-lffi", "-lz"]

        # defined in env
        self.cflags = ["-Wno-deprecated-declarations"]
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
        """ set the environment unless already set """
        if key not in os.environ:
            os.environ[key] = value
        return os.environ[key]

    def checkout_source(self, name: str, url: str, commit: Optional[str] = None) -> str:
        """ checkout source tree, optionally to a specific commit """
        srcdir_name = os.path.join(self.srcdir, name)
        if os.path.exists(srcdir_name):
            return srcdir_name
        subprocess.run(["git", "clone", url], cwd=self.srcdir, check=True)
        if commit:
            subprocess.run(["git", "checkout", commit], cwd=srcdir_name, check=True)
        return srcdir_name

    def build_meson_project(self, srcdir: str, argv) -> None:
        """ configure and build the meson project """
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

    def add_work_includedir(self, value: str) -> None:
        """ add a CFLAG """
        self.cflags.append("-I{}/{}".format(self.builddir, value))

    def add_src_includedir(self, value: str) -> None:
        """ add a CFLAG """
        self.cflags.append("-I{}/{}".format(self.srcdir, value))

    def add_build_ldflag(self, value: str) -> None:
        """ add a LDFLAG """
        self.ldflags.append(os.path.join(self.builddir, value))

    def substitute(self, src: str, replacements: Dict[str, str]) -> str:
        """ map changes """

        dst = os.path.basename(src).replace(".in", "")
        with open(os.path.join(self.srcdir, src), "r") as f:
            blob = f.read()
            for key in replacements:
                blob = blob.replace(key, replacements[key])
            with open(os.path.join(self.builddir, dst), "w") as out:
                out.write(blob)
        return dst

    def compile(self, src: str) -> str:
        """ compile a specific source file """
        argv = [self.cc]
        argv.extend(self.cflags)
        fullsrc = os.path.join(self.srcdir, src)
        if not os.path.exists(fullsrc):
            fullsrc = os.path.join(self.builddir, src)
        dst = os.path.basename(src).replace(".c", ".o")
        argv.extend(
            [
                "-c",
                fullsrc,
                "-o",
                os.path.join(self.builddir, dst),
            ]
        )
        print("building {} into {}".format(src, dst))
        try:
            subprocess.run(argv, cwd=self.srcdir, check=True)
        except subprocess.CalledProcessError as e:
            print(e)
            sys.exit(1)
        return os.path.join(self.builddir, "{}".format(dst))

    def link(self, objs: List[str], dst: str) -> None:
        """ link multiple obects into a binary """
        argv = [self.cxx] + self.cxxflags
        for obj in objs:
            if obj.startswith("-"):
                argv.append(obj)
            else:
                argv.append(os.path.join(self.builddir, obj))
        argv += ["-o", os.path.join(self.installdir, dst)]
        argv += self.ldflags
        print("building {} into {}".format(",".join(objs), dst))
        subprocess.run(argv, cwd=self.srcdir, check=True)

    def write_header(
        self, dst: str, defines: Dict[str, Optional[Union[str, int]]]
    ) -> None:
        """ write a header file """
        dstdir = os.path.join(self.builddir, os.path.dirname(dst))
        os.makedirs(dstdir, exist_ok=True)
        print("writing {}".format(dst))
        with open(os.path.join(dstdir, os.path.basename(dst)), "w") as f:
            for key in defines:
                value = defines[key]
                if value is not None:
                    if isinstance(value, int):
                        f.write("#define {} {}\n".format(key, value))
                    else:
                        f.write('#define {} "{}"\n'.format(key, value))
                else:
                    f.write("#define {}\n".format(key))
        self.add_work_includedir(os.path.dirname(dst))

    def makezip(self, dst: str, globstr: str) -> None:
        """ create a zip file archive from a glob """
        argv = [
            "zip",
            "--junk-paths",
            os.path.join(self.installdir, dst),
        ] + glob.glob(os.path.join(self.srcdir, globstr))
        print("assembling {}".format(dst))
        subprocess.run(argv, cwd=self.srcdir, check=True)

    def grep_meson(self, src: str, token: str = "fuzzing") -> List[str]:
        """ find source files tagged with a specific comment """
        srcs = []
        with open(os.path.join(self.srcdir, src, "meson.build"), "r") as f:
            for line in f.read().split("\n"):
                if line.find(token) == -1:
                    continue
                srcs.append(
                    os.path.join(
                        src,
                        line.strip()
                        .replace("'", "")
                        .replace(",", "")
                        .replace(" ", "")
                        .split("#")[0],
                    )
                )
        return srcs


def _build(bld: Builder) -> None:

    # GLib
    src = bld.checkout_source("glib", url="https://gitlab.gnome.org/GNOME/glib.git")
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
    )
    bld.build_meson_project(
        src,
        [
            "-Dgtk_doc=disabled",
            "-Dtests=false",
            "-Dintrospection=disabled",
        ],
    )
    bld.add_work_includedir("include/json-glib-1.0/json-glib")
    bld.add_work_includedir("include/json-glib-1.0")
    bld.add_build_ldflag("lib/libjson-glib-1.0.a")

    # libxmlb
    src = bld.checkout_source("libxmlb", url="https://github.com/hughsie/libxmlb.git")
    bld.build_meson_project(
        src,
        [
            "-Dgtkdoc=false",
            "-Dintrospection=false",
            "-Dtests=false",
        ],
    )
    bld.add_work_includedir("include/libxmlb-2")
    bld.add_work_includedir("include/libxmlb-2/libxmlb")
    bld.add_build_ldflag("lib/libxmlb.a")

    # write required headers
    bld.write_header("libfwupd/fwupd-version.h", {})
    bld.write_header(
        "config.h",
        {
            "FWUPD_DATADIR": "/tmp",
            "FWUPD_LOCALSTATEDIR": "/tmp",
            "FWUPD_PLUGINDIR": "/tmp",
            "FWUPD_SYSCONFDIR": "/tmp",
            "HAVE_REALPATH": None,
            "PACKAGE_NAME": "fwupd",
            "PACKAGE_VERSION": "0.0.0",
        },
    )

    # libfwupd + libfwupdplugin
    built_objs: List[str] = []
    bld.add_src_includedir("fwupd")
    for path in ["fwupd/libfwupd", "fwupd/libfwupdplugin"]:
        bld.add_src_includedir(path)
        for src in bld.grep_meson(path):
            built_objs.append(bld.compile(src))

    # dummy binary entrypoint
    if "LIB_FUZZING_ENGINE" in os.environ:
        built_objs.append(os.environ["LIB_FUZZING_ENGINE"])
    else:
        built_objs.append(bld.compile("fwupd/libfwupdplugin/fu-fuzzer-main.c"))

    # built in formats
    for fuzzer in ["dfuse", "fmap", "ihex", "srec"]:
        src = bld.substitute(
            "fwupd/libfwupdplugin/fu-fuzzer-firmware.c.in",
            {
                "@FIRMWARENEW@": "fu_{}_firmware_new".format(fuzzer),
                "@INCLUDE@": "libfwupdplugin/fu-{}-firmware.h".format(fuzzer),
            },
        )
        bld.link([bld.compile(src)] + built_objs, "{}_fuzzer".format(fuzzer))
        bld.makezip(
            "{}_fuzzer_seed_corpus.zip".format(fuzzer),
            "fwupd/src/fuzzing/firmware/{}*".format(fuzzer),
        )

    # plugins
    for srcdir, fuzzer, globstr in [
        ("bcm57xx", "bcm57xx", "bcm57xx*"),
        ("ccgx", "ccgx", "ccgx*"),
        ("cros-ec", "cros-ec", "cros-ec*"),
        ("ebitdo", "ebitdo", "ebitdo*"),
        ("hailuck", "hailuck-kbd", "ihex*"),
        ("solokey", "solokey", "solokey*"),
        ("synaptics-prometheus", "synaprom", "synaprom*"),
        ("synaptics-rmi", "synaptics-rmi", "synaptics-rmi*"),
        ("wacom-usb", "wac", "wacom*"),
    ]:
        fuzz_objs = []
        for obj in bld.grep_meson("fwupd/plugins/{}".format(srcdir)):
            fuzz_objs.append(bld.compile(obj))
        src = bld.substitute(
            "fwupd/libfwupdplugin/fu-fuzzer-firmware.c.in",
            {
                "@FIRMWARENEW@": "fu_{}_firmware_new".format(fuzzer.replace("-", "_")),
                "@INCLUDE@": "plugins/{}/fu-{}-firmware.h".format(srcdir, fuzzer),
            },
        )
        fuzz_objs.append(bld.compile(src))
        bld.link(fuzz_objs + built_objs, "{}_fuzzer".format(fuzzer))
        bld.makezip(
            "{}_fuzzer_seed_corpus.zip".format(fuzzer),
            "fwupd/src/fuzzing/firmware/{}".format(globstr),
        )


if __name__ == "__main__":
    _builder = Builder()
    _build(_builder)
    sys.exit(0)

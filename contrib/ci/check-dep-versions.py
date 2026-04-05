#!/usr/bin/env python3
#
# Copyright 2026 Mario Limonciello
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=missing-module-docstring,missing-function-docstring,invalid-name
#
# Verify that minimum versions in meson.build and dependencies.xml
# stay in sync to prevent drift in Debian Build-Depends.

import os
import re
import sys
import xml.etree.ElementTree as etree
from typing import Dict, Optional

# Maps the dependency id in dependencies.xml to the meson dependency() name.
# Only includes packages that have versioned control entries in the XML.
# libusb-1.0-0-dev is intentionally omitted: its Debian package version (e.g. 1.0.27)
# uses a different scheme than the pkg-config version checked by meson (e.g. 0.1.27).
MESON_ID_MAP: Dict[str, str] = {
    "libglib2.0-dev": "gio-2.0",
    "gnutls-dev": "gnutls",
    "libxmlb-dev": "xmlb",
    "libjcat-dev": "jcat",
    "libcurl4-gnutls-dev": "libcurl",
    "libpolkit-gobject-1-dev": "polkit-gobject-1",
    "gi-docgen": "gi-docgen",
    "systemd": "systemd",
}


def _extract_dep_blocks(content: str) -> Dict[str, str]:
    """Parse dependency() calls from meson content, returning {name: version}."""
    versions: Dict[str, str] = {}
    pos = 0
    while True:
        start = content.find("dependency(", pos)
        if start == -1:
            break

        # Find the matching closing parenthesis for this dependency() call
        depth = 0
        end = -1
        for i in range(start, len(content)):
            if content[i] == "(":
                depth += 1
            elif content[i] == ")":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        if end == -1:
            pos = start + 1
            continue

        block = content[start : end + 1]

        # The first quoted string is the dependency name
        name_m = re.search(r"""['"]([^'"]+)['"]""", block)
        if name_m:
            dep_name = name_m.group(1)
            ver_m = re.search(r"""version:\s*['"]>=\s*([^'"]+)['"]""", block)
            if ver_m and dep_name not in versions:
                versions[dep_name] = ver_m.group(1).strip()

        pos = end + 1
    return versions


def get_meson_versions(meson_file: str) -> Dict[str, str]:
    versions: Dict[str, str] = {}

    with open(meson_file) as f:
        content = f.read()

    # Extract meson_version from project() call: meson_version: '>=0.63.0'
    m = re.search(r"""meson_version:\s*['"]>=\s*([^'"]+)['"]""", content)
    if m:
        versions["meson"] = m.group(1).strip()

    versions.update(_extract_dep_blocks(content))
    return versions


def get_xml_versions(xml_file: str) -> Dict[str, str]:
    versions: Dict[str, str] = {}

    tree = etree.parse(xml_file)
    root = tree.getroot()
    for child in root:
        dep_id = child.attrib.get("id", "")
        for distro in child:
            if distro.attrib.get("id") not in ("debian", "ubuntu"):
                continue
            for ctrl in distro.findall("control"):
                for ver in ctrl.findall("version"):
                    if ver.text and dep_id not in versions:
                        m = re.search(r">=\s*([^)]+)\)", ver.text)
                        if m:
                            versions[dep_id] = m.group(1).strip()

    return versions


def check_versions() -> int:
    rc = 0

    script_dir = os.path.dirname(os.path.realpath(__file__))
    repo_dir = os.path.normpath(os.path.join(script_dir, "..", ".."))

    meson_file = os.path.join(repo_dir, "meson.build")
    xml_file = os.path.join(script_dir, "dependencies.xml")

    meson_versions = get_meson_versions(meson_file)
    xml_versions = get_xml_versions(xml_file)

    # Check meson tool itself (version lives in project()'s meson_version:)
    if "meson" in xml_versions:
        xml_ver = xml_versions["meson"]
        meson_ver = meson_versions.get("meson", "")
        if meson_ver and xml_ver != meson_ver:
            print(
                f"ERROR: meson version mismatch: "
                f"meson.build requires '>= {meson_ver}' "
                f"but dependencies.xml has '>= {xml_ver}'"
            )
            rc = 1

    # Check all mapped library dependencies
    for xml_id, meson_id in MESON_ID_MAP.items():
        if xml_id not in xml_versions:
            continue
        xml_ver = xml_versions[xml_id]
        meson_ver = meson_versions.get(meson_id, "")
        if not meson_ver:
            continue
        if xml_ver != meson_ver:
            print(
                f"ERROR: {xml_id} version mismatch: "
                f"meson.build requires '>= {meson_ver}' (as '{meson_id}') "
                f"but dependencies.xml has '>= {xml_ver}'"
            )
            rc = 1

    return rc


if __name__ == "__main__":
    sys.exit(check_versions())

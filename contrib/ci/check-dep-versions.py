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
import json
import shutil
import subprocess
import argparse
import xml.etree.ElementTree as etree
from typing import Dict, List, Tuple, Optional

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


def _candidate_build_dirs(repo_dir: str, build_dir: Optional[str]) -> List[str]:
    candidates: List[str] = []
    if build_dir:
        candidates.append(build_dir)
    env_build_dir = os.environ.get("MESON_BUILD_DIR", "").strip()
    if env_build_dir and env_build_dir not in candidates:
        candidates.append(env_build_dir)
    for default_dir in ("venv/build", "build"):
        if default_dir not in candidates:
            candidates.append(default_dir)

    resolved: List[str] = []
    for candidate in candidates:
        if os.path.isabs(candidate):
            resolved.append(candidate)
        else:
            resolved.append(os.path.join(repo_dir, candidate))
    return resolved


def get_meson_versions_from_introspection(
    repo_dir: str, build_dir: Optional[str] = None
) -> Tuple[Dict[str, str], str]:
    """Return dependency versions from `meson introspect --dependencies`."""
    meson_cmd = shutil.which("meson")
    if meson_cmd is None:
        local_meson = os.path.join(repo_dir, "venv", "bin", "meson")
        if os.path.isfile(local_meson) and os.access(local_meson, os.X_OK):
            meson_cmd = local_meson
        else:
            raise RuntimeError("`meson` not found in PATH or at venv/bin/meson")

    errors: List[str] = []
    for candidate in _candidate_build_dirs(repo_dir, build_dir):
        if not os.path.isdir(candidate):
            continue
        cmd = [meson_cmd, "introspect", candidate, "--dependencies"]
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            stderr = proc.stderr.strip() or proc.stdout.strip() or "unknown error"
            errors.append(f"{candidate}: {stderr}")
            continue
        try:
            data = json.loads(proc.stdout)
        except json.JSONDecodeError as exc:
            errors.append(f"{candidate}: invalid JSON output ({exc})")
            continue

        versions: Dict[str, str] = {}
        for dep in data:
            name = dep.get("name")
            version = dep.get("version")
            if not name or version is None:
                continue
            versions[str(name)] = str(version).strip()
        return versions, candidate

    details = "; ".join(errors) if errors else "no usable build directory found"
    raise RuntimeError(
        "Unable to run `meson introspect --dependencies`. "
        "Pass --builddir or set MESON_BUILD_DIR. "
        f"Details: {details}"
    )


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


def _sync_version_in_distro_block(block: str, version: str) -> Tuple[str, bool]:
    changed = False

    # Expand a self-closing control tag if needed.
    control_self = re.search(
        r"(?P<indent>^[ \t]*)<control\s*/>", block, flags=re.MULTILINE
    )
    if control_self:
        indent = control_self.group("indent")
        replacement = (
            f"{indent}<control>\n"
            f"{indent}  <version>(>= {version})</version>\n"
            f"{indent}</control>"
        )
        block = re.sub(
            r"^[ \t]*<control\s*/>",
            replacement,
            block,
            count=1,
            flags=re.MULTILINE,
        )
        changed = True

    control_match = re.search(
        r"<control>(?P<body>.*?)</control>", block, flags=re.DOTALL
    )
    if not control_match:
        return block, changed

    control_body = control_match.group("body")
    version_re = r"<version>\s*\(>=\s*([^)]+)\)\s*</version>"
    existing = re.search(version_re, control_body)
    if existing:
        current = existing.group(1).strip()
        if current != version:
            control_body = re.sub(
                version_re,
                f"<version>(>= {version})</version>",
                control_body,
                count=1,
            )
            changed = True
    else:
        base_indent_match = re.search(
            r"^(?P<indent>[ \t]*)<control>", block, flags=re.MULTILINE
        )
        base_indent = base_indent_match.group("indent") if base_indent_match else ""
        control_body = (
            f"\n{base_indent}  <version>(>= {version})</version>{control_body}"
        )
        changed = True

    new_control = f"<control>{control_body}</control>"
    block = re.sub(
        r"<control>.*?</control>", new_control, block, count=1, flags=re.DOTALL
    )
    return block, changed


def _sync_version_in_dep_block(dep_block: str, version: str) -> Tuple[str, bool]:
    changed = False

    for distro_id in ("debian", "ubuntu"):
        distro_re = re.compile(
            rf'(?P<head><distro id="{distro_id}">)(?P<body>.*?)(?P<tail>\n[ \t]*</distro>)',
            flags=re.DOTALL,
        )
        match = distro_re.search(dep_block)
        if not match:
            continue

        old_block = f"{match.group('head')}{match.group('body')}{match.group('tail')}"
        new_body, body_changed = _sync_version_in_distro_block(
            match.group("body"), version
        )
        if not body_changed:
            continue

        new_block = f"{match.group('head')}{new_body}{match.group('tail')}"
        dep_block = dep_block.replace(old_block, new_block, 1)
        changed = True

    return dep_block, changed


def sync_xml_versions(xml_file: str, meson_versions: Dict[str, str]) -> List[str]:
    with open(xml_file, encoding="utf-8") as f:
        xml_content = f.read()

    targets: Dict[str, str] = {}
    for xml_id, meson_id in MESON_ID_MAP.items():
        meson_version = meson_versions.get(meson_id)
        if meson_version:
            targets[xml_id] = meson_version

    updated_ids: List[str] = []
    for dep_id, version in targets.items():
        dep_re = re.compile(
            rf'(?P<head><dependency[^>]*\sid="{re.escape(dep_id)}"[^>]*>)(?P<body>.*?)(?P<tail>\n[ \t]*</dependency>)',
            flags=re.DOTALL,
        )
        match = dep_re.search(xml_content)
        if not match:
            continue

        old_dep_block = (
            f"{match.group('head')}{match.group('body')}{match.group('tail')}"
        )
        new_body, changed = _sync_version_in_dep_block(match.group("body"), version)
        if not changed:
            continue

        new_dep_block = f"{match.group('head')}{new_body}{match.group('tail')}"
        xml_content = xml_content.replace(old_dep_block, new_dep_block, 1)
        updated_ids.append(dep_id)

    if updated_ids:
        with open(xml_file, "w", encoding="utf-8") as f:
            f.write(xml_content)

    return updated_ids


def check_versions(fix: bool = False, build_dir: Optional[str] = None) -> int:
    rc = 0

    script_dir = os.path.dirname(os.path.realpath(__file__))
    repo_dir = os.path.normpath(os.path.join(script_dir, "..", ".."))

    xml_file = os.path.join(script_dir, "dependencies.xml")
    try:
        meson_versions, used_build_dir = get_meson_versions_from_introspection(
            repo_dir, build_dir
        )
    except RuntimeError as exc:
        print(f"ERROR: {exc}")
        return 1

    if fix:
        updated = sync_xml_versions(xml_file, meson_versions)
        if updated:
            print("Updated dependency versions: " + ", ".join(sorted(updated)))
        else:
            print(f"No XML updates required (build dir: {used_build_dir})")

    xml_versions = get_xml_versions(xml_file)

    # Check mapped dependencies from meson introspection output
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
                f"meson introspect reports '{meson_ver}' (as '{meson_id}') "
                f"but dependencies.xml has '>= {xml_ver}'"
            )
            rc = 1

    return rc


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Verify or sync dependency versions between meson introspection and dependencies.xml"
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Update Debian/Ubuntu <control><version> in dependencies.xml from meson introspection versions",
    )
    parser.add_argument(
        "--builddir",
        help="Meson build directory (default: MESON_BUILD_DIR, then venv/build, then build)",
    )
    args = parser.parse_args()
    sys.exit(check_versions(fix=args.fix, build_dir=args.builddir))

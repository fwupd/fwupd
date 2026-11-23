#!/usr/bin/env python3
# Copyright 2026 Genesys Logic, Inc.
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Release-time policy lint for Lenovo P34WD-40 Hub-only CAB metainfo."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

L1_GUID = "008d820f-ffc9-502d-8f25-f9313d04e8de"
L2_GUID = "7ea0761a-9e0c-5879-b808-e8d9b8beafae"
L3_GUID = "9a0bece0-69e7-5489-a218-6645fb94f72d"

MIN_FWUPD_VERSION = (1, 9, 8)

EXPECTED_HUBS = {
    L1_GUID: {
        "role": "L1",
        "priority": 1,
        "payload": "l1.bin",
        "required_parents": {},
    },
    L2_GUID: {
        "role": "L2",
        "priority": 2,
        "payload": "l2.bin",
        "required_parents": {1: L1_GUID},
    },
    L3_GUID: {
        "role": "L3",
        "priority": 3,
        "payload": "l3.bin",
        "required_parents": {1: L2_GUID, 2: L1_GUID},
    },
}


def parse_version(ver: str) -> tuple[int, ...]:
    parts = ver.strip().split(".")
    if not parts or any(not p.isdigit() for p in parts):
        raise ValueError(f"invalid numeric version: {ver!r}")
    return tuple(int(p) for p in parts)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest().lower()


def lint_components(
    components: list[tuple[str, ET.Element]],
    payload_dir: Path | None = None,
    payload_files: set[str] | None = None,
    manifest: dict[str, dict[str, str]] | None = None,
) -> list[str]:
    errors: list[str] = []
    seen_ids: set[str] = set()
    seen_payloads: set[str] = set()
    found_roles: dict[str, tuple[str, int]] = {}

    for source, root in components:
        if root.tag != "component" or root.attrib.get("type") != "firmware":
            errors.append(f"{source}: root must be <component type=\"firmware\">")
            continue

        comp_id_el = root.find("id")
        comp_id = (comp_id_el.text or "").strip() if comp_id_el is not None else ""
        if not comp_id:
            errors.append(f"{source}: missing <id>")
        elif comp_id in seen_ids:
            errors.append(f"{source}: duplicate component <id> {comp_id!r}")
        else:
            seen_ids.add(comp_id)

        provides = [
            (el.text or "").strip().lower()
            for el in root.findall("./provides/firmware")
            if el.attrib.get("type") == "flashed" and (el.text or "").strip()
        ]
        if len(provides) != 1:
            errors.append(
                f"{source}: expected exactly one <provides><firmware type=\"flashed\">, got {len(provides)}"
            )
            continue
        guid = provides[0]
        if guid not in EXPECTED_HUBS:
            errors.append(f"{source}: unexpected P34WD-40 hub GUID {guid!r}")
            continue

        spec = EXPECTED_HUBS[guid]
        role = spec["role"]
        if role in found_roles:
            errors.append(f"{source}: duplicate component for {role} ({guid})")
            continue

        # priority
        prio_raw = root.attrib.get("priority")
        if prio_raw is None:
            errors.append(f"{source} ({role}): missing priority attribute")
            priority = -1
        else:
            try:
                priority = int(prio_raw)
            except ValueError:
                errors.append(f"{source} ({role}): non-integer priority {prio_raw!r}")
                priority = -1
        found_roles[role] = (source, priority)

        # fwupd minimum version (>= 1.9.8 for enforce-requires)
        fwupd_req = None
        for req_id in root.findall("./requires/id"):
            if (req_id.text or "").strip() == "org.freedesktop.fwupd":
                fwupd_req = req_id
                break
        if fwupd_req is None:
            errors.append(f"{source} ({role}): missing org.freedesktop.fwupd requirement")
        else:
            cmp_op = fwupd_req.attrib.get("compare")
            ver_str = fwupd_req.attrib.get("version", "")
            if cmp_op != "ge":
                errors.append(
                    f"{source} ({role}): org.freedesktop.fwupd must use compare=\"ge\", got {cmp_op!r}"
                )
            try:
                if parse_version(ver_str) < MIN_FWUPD_VERSION:
                    errors.append(
                        f"{source} ({role}): org.freedesktop.fwupd version {ver_str!r} is below "
                        f"minimum {'.'.join(map(str, MIN_FWUPD_VERSION))}"
                    )
            except ValueError as exc:
                errors.append(f"{source} ({role}): {exc}")

        # parent firmware depth requirements (CHID/<hardware> alone is NOT sufficient)
        parent_reqs: dict[int, set[str]] = {}
        for fw_req in root.findall("./requires/firmware"):
            depth_raw = fw_req.attrib.get("depth")
            if depth_raw is None:
                continue
            try:
                depth = int(depth_raw)
            except ValueError:
                errors.append(f"{source} ({role}): invalid firmware depth {depth_raw!r}")
                continue
            req_guid = (fw_req.text or "").strip().lower()
            parent_reqs.setdefault(depth, set()).add(req_guid)

        expected_parents: dict[int, str] = spec["required_parents"]
        for req_depth, req_guid in expected_parents.items():
            actual = parent_reqs.get(req_depth, set())
            if req_guid not in actual:
                errors.append(
                    f"{source} ({role}): missing required <firmware depth=\"{req_depth}\">"
                    f"{req_guid}</firmware> (found: {sorted(actual)})"
                )
        for depth, guids in parent_reqs.items():
            if depth not in expected_parents or guids - {expected_parents[depth]}:
                errors.append(
                    f"{source} ({role}): unexpected parent firmware requirement depth={depth} {sorted(guids)}"
                )

        # protocol and version format
        custom = {
            el.attrib.get("key", ""): (el.text or "").strip()
            for el in root.findall("./custom/value")
        }
        if custom.get("LVFS::UpdateProtocol") != "com.genesys.usbhub":
            errors.append(
                f"{source} ({role}): LVFS::UpdateProtocol must be 'com.genesys.usbhub', "
                f"got {custom.get('LVFS::UpdateProtocol')!r}"
            )
        if custom.get("LVFS::VersionFormat") != "pair":
            errors.append(
                f"{source} ({role}): LVFS::VersionFormat must be 'pair', "
                f"got {custom.get('LVFS::VersionFormat')!r}"
            )

        # per-release checksum and payload binding validation
        releases = root.findall("./releases/release")
        if not releases:
            errors.append(f"{source} ({role}): missing <releases><release>")
        manifest_entry = manifest.get(role) if manifest is not None else None
        if manifest is not None and manifest_entry is None:
            errors.append(f"{source} ({role}): missing role entry in release manifest")
        expected_payload = (
            manifest_entry.get("filename")
            if manifest_entry is not None
            else spec["payload"]
        )
        expected_version = manifest_entry.get("version") if manifest_entry is not None else None
        expected_sha256 = (
            manifest_entry.get("sha256", "").lower() if manifest_entry is not None else ""
        )

        for idx, rel in enumerate(releases):
            rel_ver = rel.attrib.get("version", "").strip()
            if not rel_ver:
                errors.append(f"{source} ({role}): release[{idx}] missing version attribute")
            elif expected_version and rel_ver != expected_version:
                errors.append(
                    f"{source} ({role}): release[{idx}] version {rel_ver!r} does not match "
                    f"manifest {expected_version!r}"
                )

            checksums = [
                el
                for el in rel.findall("checksum")
                if el.attrib.get("target") == "content"
            ]
            if len(checksums) != 1:
                errors.append(
                    f"{source} ({role}): release[{idx}] must have exactly one "
                    f"<checksum target=\"content\">, got {len(checksums)}"
                )
                continue

            fname = checksums[0].attrib.get("filename", "").strip()
            if not fname:
                errors.append(f"{source} ({role}): release[{idx}] empty checksum filename")
                continue
            if expected_payload and fname != expected_payload:
                errors.append(
                    f"{source} ({role}): release[{idx}] payload {fname!r} does not match "
                    f"expected {expected_payload!r}"
                )
            if fname in seen_payloads:
                errors.append(f"{source} ({role}): duplicate payload filename {fname!r}")
            seen_payloads.add(fname)
            if payload_files is not None and fname not in payload_files:
                errors.append(f"{source} ({role}): payload file {fname!r} not present in archive")
            elif payload_dir is not None and expected_sha256:
                fpath = payload_dir / fname
                if not fpath.is_file():
                    errors.append(
                        f"{source} ({role}): payload file {fname!r} not present in archive"
                    )
                else:
                    actual_sha256 = sha256_file(fpath)
                    if actual_sha256 != expected_sha256:
                        errors.append(
                            f"{source} ({role}): payload {fname!r} SHA256 mismatch "
                            f"(expected {expected_sha256}, got {actual_sha256})"
                        )

    # completeness and priority order L3 > L2 > L1
    for role in ("L1", "L2", "L3"):
        if role not in found_roles:
            errors.append(f"missing required {role} hub component")
    if all(r in found_roles for r in ("L1", "L2", "L3")):
        p1 = found_roles["L1"][1]
        p2 = found_roles["L2"][1]
        p3 = found_roles["L3"][1]
        if not (p3 > p2 > p1 > 0):
            errors.append(
                f"invalid install priority order: require L3 > L2 > L1 > 0, got L3={p3}, L2={p2}, L1={p1}"
            )

    return errors


def load_from_dir(
    directory: Path,
    check_payloads: bool,
    manifest: dict[str, dict[str, str]] | None = None,
) -> list[str]:
    xml_files = sorted(directory.glob("*.metainfo.xml"))
    if not xml_files:
        return [f"{directory}: no *.metainfo.xml files found"]
    components: list[tuple[str, ET.Element]] = []
    for path in xml_files:
        components.append((path.name, ET.parse(path).getroot()))
    payload_files = {p.name for p in directory.iterdir() if p.is_file()} if check_payloads else None
    payload_dir = directory if check_payloads else None
    return lint_components(
        components,
        payload_dir=payload_dir,
        payload_files=payload_files,
        manifest=manifest,
    )


def load_from_cab(
    cab_path: Path,
    manifest: dict[str, dict[str, str]] | None = None,
) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="p34-cab-lint-") as tmpdir:
        subprocess.run(
            ["gcab", "-x", "-C", tmpdir, str(cab_path)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        return load_from_dir(Path(tmpdir), check_payloads=True, manifest=manifest)


def run_self_test(fixture_dir: Path) -> int:
    # 1. Positive: real fixture metainfos must pass
    errs = load_from_dir(fixture_dir, check_payloads=False)
    if errs:
        print("FAIL positive fixture:", errs, file=sys.stderr)
        return 1

    base = {
        name: ET.parse(fixture_dir / f"{name}.metainfo.xml").getroot()
        for name in ("l1", "l2", "l3")
    }

    def clone_base() -> dict[str, ET.Element]:
        return {k: ET.fromstring(ET.tostring(v)) for k, v in base.items()}

    def assert_rejected(
        label: str,
        comps: dict[str, ET.Element],
        expected_substr: str,
        payload_dir: Path | None = None,
        payload_files: set[str] | None = None,
        manifest: dict[str, dict[str, str]] | None = None,
    ) -> None:
        res = lint_components(
            [(f"{k}.metainfo.xml", v) for k, v in comps.items()],
            payload_dir=payload_dir,
            payload_files=payload_files,
            manifest=manifest,
        )
        if not res or not any(expected_substr in e for e in res):
            raise AssertionError(
                f"{label}: expected rejection containing {expected_substr!r}, got {res!r}"
            )

    # 2. L2 missing depth=1
    c = clone_base()
    req = c["l2"].find("requires")
    for fw in list(req.findall("firmware")):
        req.remove(fw)
    assert_rejected("l2-missing-depth1", c, 'depth="1"')

    # 3. L3 missing depth=2
    c = clone_base()
    req = c["l3"].find("requires")
    for fw in list(req.findall("firmware")):
        if fw.attrib.get("depth") == "2":
            req.remove(fw)
    assert_rejected("l3-missing-depth2", c, 'depth="2"')

    # 4. L3 depth=2 wrong GUID (model B 1151)
    c = clone_base()
    for fw in c["l3"].findall("./requires/firmware"):
        if fw.attrib.get("depth") == "2":
            fw.text = "00000000-0000-5000-8000-000000001151"
    assert_rejected("l3-wrong-depth2-guid", c, 'depth="2"')

    # 5. L3 wrong depth (L1 GUID placed at depth=1)
    c = clone_base()
    for fw in c["l3"].findall("./requires/firmware"):
        if fw.attrib.get("depth") == "1":
            fw.text = L1_GUID
    assert_rejected("l3-wrong-depth", c, 'depth="1"')

    # 6. CHID (<hardware>) only without parent <firmware depth=...>
    c = clone_base()
    req = c["l2"].find("requires")
    for fw in list(req.findall("firmware")):
        req.remove(fw)
    hw = ET.SubElement(req, "hardware")
    hw.text = "12345678-1234-5678-90ab-cdef12345678"
    assert_rejected("chid-only-no-parent", c, 'depth="1"')

    # 7. Duplicate component ID
    c = clone_base()
    c["l2"].find("id").text = c["l1"].find("id").text
    assert_rejected("duplicate-id", c, "duplicate component <id>")

    # 8. Swapped L1/L3 payload filenames must be rejected even when both files exist
    c = clone_base()
    c["l1"].find("./releases/release/checksum").attrib["filename"] = "l3.bin"
    c["l3"].find("./releases/release/checksum").attrib["filename"] = "l1.bin"
    assert_rejected(
        "swapped-l1-l3-payloads",
        c,
        "does not match expected",
        payload_files={"l1.bin", "l2.bin", "l3.bin"},
    )

    # 9. Duplicate payload filename & missing payload in archive
    c = clone_base()
    c["l2"].find("./releases/release/checksum").attrib["filename"] = "l1.bin"
    assert_rejected("duplicate-payload", c, "duplicate payload filename")
    c = clone_base()
    assert_rejected(
        "missing-payload",
        c,
        "not present in archive",
        payload_files={"l1.bin", "l2.bin"},
    )

    # 10. Multi-release component where one release is missing <checksum target="content">
    c = clone_base()
    rels = c["l3"].find("releases")
    rel2 = ET.SubElement(rels, "release", {"version": "88.18", "date": "2026-10-29"})
    ET.SubElement(rel2, "description")
    assert_rejected("release-missing-checksum", c, "release[1] must have exactly one")

    # 11. Manifest binding: exact SHA256 + version passes, swapped binary contents or wrong
    # version fails
    with tempfile.TemporaryDirectory(prefix="p34-lint-manifest-") as tmpdir:
        tdir = Path(tmpdir)
        (tdir / "l1.bin").write_bytes(b"p34-l1-image")
        (tdir / "l2.bin").write_bytes(b"p34-l2-image")
        (tdir / "l3.bin").write_bytes(b"p34-l3-image")
        manifest = {
            "L1": {
                "version": "86.25",
                "filename": "l1.bin",
                "sha256": sha256_file(tdir / "l1.bin"),
            },
            "L2": {
                "version": "87.16",
                "filename": "l2.bin",
                "sha256": sha256_file(tdir / "l2.bin"),
            },
            "L3": {
                "version": "88.17",
                "filename": "l3.bin",
                "sha256": sha256_file(tdir / "l3.bin"),
            },
        }
        c = clone_base()
        ok_errs = lint_components(
            [(f"{k}.metainfo.xml", v) for k, v in c.items()],
            payload_dir=tdir,
            payload_files={"l1.bin", "l2.bin", "l3.bin"},
            manifest=manifest,
        )
        if ok_errs:
            raise AssertionError(f"manifest-positive: unexpected errors {ok_errs!r}")

        # swap file contents while keeping filenames unchanged
        (tdir / "l1.bin").write_bytes(b"p34-l3-image")
        (tdir / "l3.bin").write_bytes(b"p34-l1-image")
        assert_rejected(
            "manifest-swapped-content",
            clone_base(),
            "SHA256 mismatch",
            payload_dir=tdir,
            payload_files={"l1.bin", "l2.bin", "l3.bin"},
            manifest=manifest,
        )

        # version mismatch against manifest
        (tdir / "l1.bin").write_bytes(b"p34-l1-image")
        (tdir / "l3.bin").write_bytes(b"p34-l3-image")
        c = clone_base()
        c["l2"].find("./releases/release").attrib["version"] = "87.99"
        assert_rejected(
            "manifest-wrong-version",
            c,
            "does not match manifest",
            payload_dir=tdir,
            payload_files={"l1.bin", "l2.bin", "l3.bin"},
            manifest=manifest,
        )

    # 12. fwupd version below 1.9.8 (e.g. 1.3.4)
    c = clone_base()
    c["l3"].find("./requires/id").attrib["version"] = "1.3.4"
    assert_rejected("fwupd-version-too-low", c, "below minimum 1.9.8")

    # 13. Invalid priority order (L3 priority < L2 priority)
    c = clone_base()
    c["l3"].attrib["priority"] = "1"
    assert_rejected("wrong-priority-order", c, "invalid install priority order")

    print("PASS: P34WD-40 Hub-only CAB lint self-test (2 positive + 14 negative cases)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "target", nargs="?", type=Path, help="Directory with metainfo XMLs or .cab file"
    )
    parser.add_argument(
        "--check-payloads", action="store_true", help="Verify payload files exist in directory"
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        help="Optional JSON manifest binding L1/L2/L3 version, filename, and sha256",
    )
    parser.add_argument(
        "--self-test", action="store_true", help="Run built-in positive/negative lint tests"
    )
    args = parser.parse_args()

    if args.self_test:
        fixture_dir = args.target or Path(__file__).resolve().parent
        return run_self_test(fixture_dir)

    if args.target is None:
        parser.error("target path is required unless --self-test is specified")

    manifest = json.loads(args.manifest.read_text(encoding="utf-8")) if args.manifest else None

    if args.target.is_dir():
        errors = load_from_dir(
            args.target, check_payloads=args.check_payloads, manifest=manifest
        )
    elif args.target.is_file() and args.target.suffix == ".cab":
        errors = load_from_cab(args.target, manifest=manifest)
    else:
        print(f"unsupported target: {args.target}", file=sys.stderr)
        return 2

    if errors:
        for err in errors:
            print(f"ERROR: {err}", file=sys.stderr)
        return 1

    print(f"OK: {args.target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

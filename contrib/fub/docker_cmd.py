# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'docker' subcommand — generate and optionally build a Dockerfile for CI

import os
import shutil
import xml.etree.ElementTree as etree
from dataclasses import dataclass, field
from pathlib import Path

from .helpers import (
    ARCH_TO_DEPS_MAP,
    detect_os,
    detect_os_version,
    error,
    find_repo_root,
    logger,
    msg,
    RunCmd,
)

VALID_ENGINES = ("docker", "podman")


def register(subparsers):
    """Register the 'docker' subcommand."""
    parser = subparsers.add_parser(
        "docker",
        help="generate and optionally build a CI Dockerfile",
        description=(
            "Generate a Dockerfile from a CI template for the given "
            "distro/version/arch, and optionally build the container image."
        ),
    )
    parser.add_argument(
        "--distro",
        default=None,
        help="distribution name (default: auto-detect)",
    )
    parser.add_argument(
        "--version",
        default=None,
        help="distribution version/tag (default: auto-detect)",
    )
    parser.add_argument(
        "--arch",
        default="amd64",
        help="architecture (default: amd64)",
    )
    parser.add_argument(
        "--variant",
        default=None,
        help="build variant (e.g. i386, android, cross-s390x)",
    )
    parser.add_argument(
        "-F",
        dest="dockerfile",
        type=Path,
        default=Path("Dockerfile"),
        help="output Dockerfile path (default: Dockerfile)",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        default=False,
        help="build the container image after generating the Dockerfile",
    )
    parser.add_argument(
        "--engine",
        choices=VALID_ENGINES,
        default=None,
        help="container engine to use for --build (default: auto-detect)",
    )
    parser.add_argument(
        "--tag",
        default=None,
        help="image tag for --build (default: fwupd-DISTRO)",
    )
    parser.set_defaults(func=run)


def find_container_engine(engine):
    """Find the container engine binary.

    If engine is specified, use that. Otherwise auto-detect docker or podman.
    Returns the engine name, or None if not found.
    """
    if engine:
        if shutil.which(engine):
            return engine
        return None

    for candidate in VALID_ENGINES:
        if shutil.which(candidate):
            return candidate
    return None


@dataclass
class CIDependencies:
    """Dependencies parsed from contrib/ci/dependencies.xml."""

    deps: list[str] = field(default_factory=list)
    build_indep: list[str] = field(default_factory=list)

    @property
    def all(self) -> list[str]:
        """Return all dependencies combined."""
        return self.deps + self.build_indep


def parse_ci_dependencies(distro, variant, cross=False):
    """Parse contrib/ci/dependencies.xml for Dockerfile dependencies.

    This is the CI-specific parser that handles cross-compilation,
    multi-arch, native markers, and build-indep sections needed for
    Dockerfile generation.
    """
    deps_file = find_repo_root() / "contrib" / "ci" / "dependencies.xml"
    tree = etree.parse(deps_file)
    root = tree.getroot()

    result = CIDependencies()

    for child in root:
        if "id" not in child.attrib:
            continue
        for distro_elem in child:
            if "id" not in distro_elem.attrib:
                continue
            if distro_elem.attrib["id"] != distro:
                continue

            is_build_indep = bool(distro_elem.findall("build-indep"))

            if cross and distro_elem.findall("multi-arch"):
                deb_arch = {v: k for k, v in ARCH_TO_DEPS_MAP.items()}.get(
                    variant, variant
                )
                arch_suffix = f":{deb_arch}"
            elif distro_elem.findall("native"):
                arch_suffix = ":native"
            else:
                arch_suffix = ""

            if len(distro_elem.findall("package")) == 0:
                dep = child.attrib["id"]
                if dep:
                    entry = f"{dep}{arch_suffix}"
                    if is_build_indep:
                        result.build_indep.append(entry)
                    else:
                        result.deps.append(entry)

            for package in distro_elem.findall("package"):
                if variant and "variant" in package.attrib:
                    if package.attrib["variant"] != variant:
                        continue
                dep = package.text if package.text else child.attrib["id"]
                if dep:
                    entry = f"{dep}{arch_suffix}"
                    if is_build_indep:
                        result.build_indep.append(entry)
                    else:
                        result.deps.append(entry)

    return result


def generate_dockerfile(distro, version, arch, variant):
    """Generate a Dockerfile from the CI template.

    Returns the generated Dockerfile content as a string.
    """
    ci_dir = find_repo_root() / "contrib" / "ci"
    cross = (
        c if variant and (c := str(variant).removeprefix("cross-")) != variant else None
    )

    # Find the best matching template
    candidates = [
        ci_dir / f"Dockerfile-{distro}-{variant}.in",
        ci_dir / f"Dockerfile-{distro}.in",
    ]
    template_file = None
    for candidate in candidates:
        if candidate.is_file():
            template_file = candidate
            break

    if template_file is None:
        raise FileNotFoundError(f"Missing template Dockerfile for {distro}")

    content = template_file.read_text()
    content = content.replace("%%%VERSION%%%", version)

    # Special cases
    match (distro, variant):
        case ("debian", "i386"):
            content = content.replace(
                f"FROM debian:{version}", f"FROM i386/debian:{version}"
            )

    # Insert commands to prepare cross compile
    if cross:
        cross_setup = (
            f"    sed -i 's|Types: deb|Types: deb deb-src|' "
            f"/etc/apt/sources.list.d/debian.sources; \\\n"
            f"    dpkg --add-architecture {cross};"
        )
    else:
        cross_setup = "    "
    content = content.replace("%%%SETUP%%%", cross_setup)

    # Insert dependencies
    if cross:
        ci_deps = parse_ci_dependencies(distro, ARCH_TO_DEPS_MAP[cross], cross=True)
        all_deps = ci_deps.all + [f"crossbuild-essential-{cross}"]
    elif variant in ("i386", "android"):
        ci_deps = parse_ci_dependencies(distro, variant)
        all_deps = ci_deps.all
    else:
        ci_deps = parse_ci_dependencies(distro, ARCH_TO_DEPS_MAP[arch])
        all_deps = ci_deps.all

    all_deps = sorted(set(all_deps))
    all_deps = [f"    {d}" for d in all_deps]
    content = content.replace("%%%DEPENDENCIES%%%", " \\\n".join(all_deps))

    # Install android rust target
    rustup = []
    if variant == "android":
        rustup.append("COPY contrib/ci/android.sh .")
        rustup.append("RUN sh android.sh")
    content = content.replace("%%%RUSTUP%%%", "\n".join(rustup))

    return content


def run(args):
    """Generate and optionally build a CI Dockerfile."""
    distro = args.distro
    version = args.version

    if distro is None:
        distro = detect_os()
        if not distro:
            error(args, "Could not auto-detect distribution, use --distro")
            return 1
        logger.info(f"Auto-detected distro: {distro}")

    if version is None:
        version = detect_os_version()
        if not version:
            error(args, "Could not auto-detect version, use --version")
            return 1
        logger.info(f"Auto-detected version: {version}")

    try:
        content = generate_dockerfile(distro, version, args.arch, args.variant)
    except FileNotFoundError as e:
        error(args, str(e))
        return 1

    args.dockerfile.write_text(content)
    msg(args, f"Generated '{args.dockerfile}'")

    if not args.build:
        return 0

    engine = find_container_engine(args.engine)
    if engine is None:
        if args.engine:
            error(args, f"{args.engine} not found")
        else:
            error(args, "No container engine found (tried docker, podman)")
        return 1

    logger.info(f"Using container engine: {engine}")

    tag = args.tag if args.tag else f"fwupd-{distro}"
    build_cmd = [engine, "build", "-t", tag]

    http_proxy = os.environ.get("http_proxy")
    if http_proxy:
        build_cmd += [f"--build-arg=http_proxy={http_proxy}"]
    https_proxy = os.environ.get("https_proxy")
    if https_proxy:
        build_cmd += [f"--build-arg=https_proxy={https_proxy}"]

    build_cmd += ["-f", str(args.dockerfile), "."]

    msg(args, f"Building container image {tag}")
    result = RunCmd(build_cmd, capture=False, cwd=find_repo_root())
    if not result.success:
        error(args, "Container build failed")
        return 1

    return 0

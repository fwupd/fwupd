# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Dependency XML parsing for fub

import enum
import xml.etree.ElementTree as etree
from dataclasses import dataclass, field
from pathlib import Path
from typing import Self

from .logger import logger
from .osprofile import OsArch, OsName

PIP_PACKAGES: dict[str, tuple | None] = {
    "markdown": (3, 2, 0),
    "jinja2": None,
}


class BuildTarget(enum.StrEnum):
    ANY = enum.auto()
    NATIVE = "native"
    MULTI_ARCH = "multi-arch"
    BUILD_INDEPENDENT = "build-indep"


@dataclass
class Dependency:
    package_name: str
    inclusive: list[OsArch] = field(default_factory=list)
    exclusive: list[OsArch] = field(default_factory=list)
    version_requirement: str | None = None
    build_target: BuildTarget = BuildTarget.ANY


@dataclass
class _ParsedDistroPackage:
    """Represents a distro entry in the dependencies.xml file"""

    filters: dict[str | None, str] = field(default_factory=dict)
    """
    Filters in the form { "amd64": "foobar-devel" }. If the special
    None key is set it applies to any filter.
    """
    build_target: BuildTarget = BuildTarget.ANY
    inclusive: list[OsArch] = field(default_factory=list)
    exclusive: list[OsArch] = field(default_factory=list)
    version_requirement: str | None = None

    def package_name(self, filter: str | None) -> str | None:
        name = self.filters.get(filter)
        if not name:
            name = self.filters.get(None)
        return name


@dataclass
class _ParsedDependency:
    """
    Represents an dependency entry in the dependencies.xml file.

    For internal use only.
    """

    name: str
    """
    The typical dependency name. Each distribution may have a
    different name for this package though, use package_name() to
    access the installable package name at runtime.
    """
    distro_map: dict[OsName, _ParsedDistroPackage] = field(default_factory=dict)

    def get_dependency(
        self,
        distro: OsName,
        filter: str | None = None,
        cross_build_arch: OsName | None = None,
    ) -> Dependency | None:
        if (distro_pkg := self.distro_map.get(distro)) is not None:
            pkgname = distro_pkg.package_name(filter)
            if pkgname is None:
                return None
            logger.debug(f"{distro_pkg}")

            if distro in [OsName.DEBIAN, OsName.UBUNTU]:
                match distro_pkg.build_target:
                    case BuildTarget.MULTI_ARCH:
                        if cross_build_arch is not None:
                            suffix = f":{cross_build_arch}"
                        else:
                            suffix = ""
                    case BuildTarget.NATIVE:
                        suffix = ":native"
                    case _:
                        suffix = ""
                pkgname = f"{pkgname}{suffix}"

            version_requirement = None
            inclusive = distro_pkg.inclusive
            exclusive = distro_pkg.exclusive
            version_requirement = distro_pkg.version_requirement
            return Dependency(
                package_name=pkgname,
                inclusive=inclusive,
                exclusive=exclusive,
                version_requirement=version_requirement,
                build_target=distro_pkg.build_target,
            )
        elif not self.distro_map:
            # <dependency id="foo" />
            return Dependency(package_name=self.name)
        return None


@dataclass
class Dependencies:
    """
    Wraps our dependencies.xml file
    """

    _dependencies: list[_ParsedDependency]

    @classmethod
    def load_from(cls, file: Path) -> Self:
        """Parse contrib/ci/dependencies.xml for build dependencies."""
        tree = etree.parse(file)
        root = tree.getroot()

        deps: list[_ParsedDependency] = []
        for dependency_elem in root.findall("dependency"):
            if (dependency_name := dependency_elem.attrib.get("id")) is None:
                logger.warning(
                    f"Element {dependency_elem.tag} without required attribute 'id'"
                )
                continue

            distro_map = {}
            for distro_elem in dependency_elem.findall("distro"):
                if (distro_id := distro_elem.attrib.get("id")) is None:
                    logger.warning(
                        f"Element {distro_elem.tag} in dependency {dependency_name} without required attribute 'id'"
                    )
                    continue

                filters = {}
                try:
                    distro_name = OsName(distro_id)
                except ValueError:
                    logger.warning(
                        f"Unknown distro '{distro_id}' in dependency {dependency_name}, skipping"
                    )
                    continue

                for package in distro_elem.findall("package"):
                    package_name = package.attrib.get("name") or dependency_name
                    filter = package.attrib.get("if-filter")
                    # This intentionally maps None: package-name if no filter is set
                    # <package [name="foo"] />
                    filters[filter] = package_name

                # <distro id="foo" />
                if not filters:
                    filters[None] = dependency_name

                distro_dep = _ParsedDistroPackage(filters=filters)
                if (bt := distro_elem.find("build-target")) is not None:
                    if "mode" in bt.attrib:
                        distro_dep.build_target = BuildTarget(bt.attrib["mode"])

                if (control_elem := distro_elem.find("control")) is not None:
                    if v := control_elem.find("version"):
                        version = v.text
                    else:
                        version = None

                    incl = []
                    for inc in control_elem.findall("inclusive"):
                        if "arch" in inc.attrib:
                            try:
                                incl.append(OsArch.from_string(str(inc.attrib["arch"])))
                            except UnknownArchException:
                                logger.warning(
                                    f"Unknown architecture '{inc.attrib['arch']}' in dependency {dependency_name}, skipping"
                                )

                    excl = []
                    for exc in control_elem.findall("exclusive"):
                        if "arch" in exc.attrib:
                            try:
                                excl.append(OsArch.from_string(str(exc.attrib["arch"])))
                            except UnknownArchException:
                                logger.warning(
                                    f"Unknown architecture '{exc.attrib['arch']}' in dependency {dependency_name}, skipping"
                                )
                    distro_dep.inclusive = incl
                    distro_dep.exclusive = excl
                    distro_dep.version_requirement = version

                    # FIXME: not sure what the deal is with empty control/

                distro_map[distro_name] = distro_dep

            dep = _ParsedDependency(name=dependency_name, distro_map=distro_map)
            deps.append(dep)
        return cls(_dependencies=deps)

    def find_packages(
        self,
        distro: OsName,
        filter: str | None = None,
        cross_build_arch: OsName | None = None,
    ) -> list[Dependency]:
        """
        Returns a list of package names for the given OS profile.
        """
        logger.debug(f"Finding packages for {distro} with filter {filter}")
        deps = []

        if cross_build_arch is not None:
            if distro in [OsName.DEBIAN, OsName.UBUNTU]:
                deps.append(Dependency(f"crossbuild-essential-{cross_build_arch}"))

        for dep in self._dependencies:
            if dependency := dep.get_dependency(distro, filter, cross_build_arch):
                deps.append(dependency)

        return deps

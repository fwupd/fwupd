# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Shared utilities for fub


from dataclasses import dataclass
from pathlib import Path

import jinja2
import jinja2.environment

from .dependencies import Dependencies
from .osprofile import OsArch, OsName


def map_docker_distro(distro: OsName) -> str:
    return {OsName.ARCH: "archlinux"}.get(distro, distro.value)


@dataclass
class Dockerfile:
    distro: OsName
    arch: OsArch
    version: str
    variant: str | None
    cross_arch: OsName | None

    def generate_from_template(self, template_dir: Path) -> str:
        """
        Generate a Dockerfile from a template in the given directory.

        Template lookup is performed via
        """
        distro = map_docker_distro(self.distro)
        data = {
            "VERSION": self.version,
            "DISTRO": distro,
            "ARCH": self.arch.value,
        }
        if self.variant:
            data["VARIANT"] = self.variant

        if self.cross_arch:
            data["CROSSARCH"] = self.cross_arch

        dockerfiles = [
            Path(template_dir) / f"Dockerfile-{self.distro}-{self.variant}.in",
            Path(template_dir) / f"Dockerfile-{self.distro}.in",
        ]
        try:
            template_file = next(p for p in dockerfiles if p.exists())
        except StopIteration:
            raise FileNotFoundError(
                f"Missing template Dockerfile for {self.distro}"
            ) from None

        dependencies = Dependencies.load()
        filter = self.arch.value
        if self.variant in ["android"]:
            filter = self.variant
        if self.cross_arch:
            filter = self.cross_arch
        deps = dependencies.find_packages(
            self.distro, filter, cross_build_arch=self.cross_arch
        )
        pkgnames = [p.package_name for p in deps]

        data["DEPENDENCIES"] = sorted(set(pkgnames))

        loader = jinja2.FileSystemLoader(template_file.parent)
        jinja_env = jinja2.Environment(
            loader=loader,
            trim_blocks=True,
            lstrip_blocks=True,
        )
        jinja_tmpl = jinja_env.get_template(template_file.name)
        return jinja_tmpl.render(data)

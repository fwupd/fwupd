# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest
from fwupbuild.dependencies import Dependencies, PackageSource
from fwupbuild.osprofile import OsName


class TestDependencies:
    @staticmethod
    def _make_deps(xml_str, tmp_path):
        xml_file = tmp_path / "deps.xml"
        xml_file.write_text(
            f'<?xml version="1.0"?>\n<dependencies>\n{xml_str}\n</dependencies>\n'
        )
        return Dependencies.load_from(xml_file)

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_matching_distro(self, tagname, tmp_path):
        """find_packages returns packages for the matching distro."""
        deps = self._make_deps(
            f'<{tagname} id="libglib">\n'
            '  <distro id="fedora">\n'
            '    <package name="glib2-devel" only-if="x86_64" />\n'
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.FEDORA, filter="x86_64", source=source)
        assert len(result) == 1
        assert result[0].package_name == "glib2-devel"

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_no_matching_distro(self, tagname, tmp_path):
        """find_packages returns empty list when distro doesn't match."""
        deps = self._make_deps(
            f'<{tagname} id="libglib">\n'
            '  <distro id="nixos">\n'
            '    <package name="glib2-devel" only-if="x86_64" />\n'
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.FEDORA, filter="x86_64", source=source)
        assert result == []

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_filter_filtering(self, tagname, tmp_path):
        """find_packages only returns packages matching the filter."""
        deps = self._make_deps(
            f'<{tagname} id="hwdata">\n'
            '  <distro id="fedora">\n'
            '    <package name="hwdata-x86" only-if="x86_64" />\n'
            '    <package name="hwdata-arm" only-if="aarch64" />\n'
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.FEDORA, filter="x86_64", source=source)
        assert len(result) == 1
        assert result[0].package_name == "hwdata-x86"

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_fallback_to_dependency_id(self, tagname, tmp_path):
        """When <package> has no name attribute, the dependency id is used."""
        deps = self._make_deps(
            f'<{tagname} id="hwdata">\n'
            '  <distro id="fedora">\n'
            '    <package only-if="x86_64" />\n'
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.FEDORA, filter="x86_64", source=source)
        assert len(result) == 1
        assert result[0].package_name == "hwdata"

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_no_packages_uses_dependency_id(self, tagname, tmp_path):
        """When distro has no <package> children, the dependency id is used."""
        deps = self._make_deps(
            f'<{tagname} id="hwdata">\n'
            '  <distro id="fedora">\n'
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.FEDORA, source=source)
        assert len(result) == 1
        assert result[0].package_name == "hwdata"

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_skips_elements_without_id(self, tagname, tmp_path):
        """Elements without 'id' attribute are skipped."""
        deps = self._make_deps(
            f"<{tagname}>\n"  # no id
            '  <distro id="fedora">\n'
            '    <package name="something" />\n'
            "  </distro>\n"
            f"</{tagname}>\n"
            f'<{tagname} id="real">\n'
            '  <distro id="fedora">\n'
            '    <package name="real-pkg" />\n'
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.FEDORA, source=source)
        assert len(result) == 1
        assert result[0].package_name == "real-pkg"

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_multiple_dependencies(self, tagname, tmp_path):
        """find_packages collects from multiple dependency entries."""
        deps = self._make_deps(
            f'<{tagname} id="libA">\n'
            '  <distro id="fedora">\n'
            '    <package name="pkg-a" />\n'
            "  </distro>\n"
            f"</{tagname}>\n"
            f'<{tagname} id="libB">\n'
            '  <distro id="fedora">\n'
            '    <package name="pkg-b" />\n'
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.FEDORA, source=source)
        assert len(result) == 2
        assert result[0].package_name == "pkg-a"
        assert result[1].package_name == "pkg-b"

    @pytest.mark.parametrize("tagname", ["dependency", "pip"])
    def test_control_profiles(self, tagname, tmp_path):
        """Build profiles are returned with the parsed dependency."""
        deps = self._make_deps(
            f'<{tagname} id="python-flask">\n'
            '  <distro id="debian">\n'
            "    <control>\n"
            "      <profile> !nocheck </profile>\n"
            "      <profile>   </profile>\n"
            "    </control>\n"
            "  </distro>\n"
            f"</{tagname}>\n",
            tmp_path,
        )
        source = PackageSource.PIP if tagname == "pip" else PackageSource.PACKAGES
        result = deps.find_packages(OsName.DEBIAN, source=source)
        assert len(result) == 1
        assert result[0].profiles == ["!nocheck"]

    @pytest.mark.parametrize("want", [PackageSource.PACKAGES, PackageSource.PIP])
    def test_mixed_dependencies_and_pip(self, want, tmp_path):
        """find_packages collects from multiple dependency entries."""
        deps = self._make_deps(
            '<dependency id="libA">\n'
            '  <distro id="fedora">\n'
            '    <package name="pkg-a" />\n'
            "  </distro>\n"
            "</dependency>\n"
            '<pip id="libB">\n'
            '  <distro id="fedora">\n'
            '    <package name="pkg-b" />\n'
            "  </distro>\n"
            "</pip>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA, source=want)
        assert len(result) == 1
        match want:
            case PackageSource.PACKAGES:
                assert result[0].package_name == "pkg-a"
            case PackageSource.PIP:
                assert result[0].package_name == "pkg-b"

    def test_pip_version(self, tmp_path):
        """Parse the <pip version> element correctly."""

        deps = self._make_deps(
            '<pip id="python-flask" version="==1.2.3">\n'
            '  <distro id="debian">\n'
            "    <control>\n"
            "      <profile> !nocheck </profile>\n"
            "      <profile>   </profile>\n"
            "    </control>\n"
            "  </distro>\n"
            "</pip>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.DEBIAN, source=PackageSource.PIP)
        assert len(result) == 1
        assert result[0].package_name == "python-flask"
        assert result[0].version_requirement == "==1.2.3"

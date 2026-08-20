# SPDX-License-Identifier: LGPL-2.1-or-later


from fub.dependencies import Dependencies
from fub.osprofile import OsName


class TestDependencies:
    @staticmethod
    def _make_deps(xml_str, tmp_path):
        xml_file = tmp_path / "deps.xml"
        xml_file.write_text(
            f'<?xml version="1.0"?>\n<dependencies>\n{xml_str}\n</dependencies>\n'
        )
        return Dependencies.load_from(xml_file)

    def test_matching_distro(self, tmp_path):
        """find_packages returns packages for the matching distro."""
        deps = self._make_deps(
            '<dependency id="libglib">\n'
            '  <distro id="fedora">\n'
            '    <package name="glib2-devel" only-if="x86_64" />\n'
            "  </distro>\n"
            "</dependency>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA, filter="x86_64")
        assert len(result) == 1
        assert result[0].package_name == "glib2-devel"

    def test_no_matching_distro(self, tmp_path):
        """find_packages returns empty list when distro doesn't match."""
        deps = self._make_deps(
            '<dependency id="libglib">\n'
            '  <distro id="nixos">\n'
            '    <package name="glib2-devel" only-if="x86_64" />\n'
            "  </distro>\n"
            "</dependency>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA, filter="x86_64")
        assert result == []

    def test_filter_filtering(self, tmp_path):
        """find_packages only returns packages matching the filter."""
        deps = self._make_deps(
            '<dependency id="hwdata">\n'
            '  <distro id="fedora">\n'
            '    <package name="hwdata-x86" only-if="x86_64" />\n'
            '    <package name="hwdata-arm" only-if="aarch64" />\n'
            "  </distro>\n"
            "</dependency>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA, filter="x86_64")
        assert len(result) == 1
        assert result[0].package_name == "hwdata-x86"

    def test_fallback_to_dependency_id(self, tmp_path):
        """When <package> has no name attribute, the dependency id is used."""
        deps = self._make_deps(
            '<dependency id="hwdata">\n'
            '  <distro id="fedora">\n'
            '    <package only-if="x86_64" />\n'
            "  </distro>\n"
            "</dependency>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA, filter="x86_64")
        assert len(result) == 1
        assert result[0].package_name == "hwdata"

    def test_no_packages_uses_dependency_id(self, tmp_path):
        """When distro has no <package> children, the dependency id is used."""
        deps = self._make_deps(
            '<dependency id="hwdata">\n'
            '  <distro id="fedora">\n'
            "  </distro>\n"
            "</dependency>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA)
        assert len(result) == 1
        assert result[0].package_name == "hwdata"

    def test_skips_elements_without_id(self, tmp_path):
        """Elements without 'id' attribute are skipped."""
        deps = self._make_deps(
            "<dependency>\n"  # no id
            '  <distro id="fedora">\n'
            '    <package name="something" />\n'
            "  </distro>\n"
            "</dependency>\n"
            '<dependency id="real">\n'
            '  <distro id="fedora">\n'
            '    <package name="real-pkg" />\n'
            "  </distro>\n"
            "</dependency>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA)
        assert len(result) == 1
        assert result[0].package_name == "real-pkg"

    def test_multiple_dependencies(self, tmp_path):
        """find_packages collects from multiple dependency entries."""
        deps = self._make_deps(
            '<dependency id="libA">\n'
            '  <distro id="fedora">\n'
            '    <package name="pkg-a" />\n'
            "  </distro>\n"
            "</dependency>\n"
            '<dependency id="libB">\n'
            '  <distro id="fedora">\n'
            '    <package name="pkg-b" />\n'
            "  </distro>\n"
            "</dependency>\n",
            tmp_path,
        )
        result = deps.find_packages(OsName.FEDORA)
        assert len(result) == 2
        assert result[0].package_name == "pkg-a"
        assert result[1].package_name == "pkg-b"

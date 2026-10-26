#!/usr/bin/env python3
#
# Copyright 2026 Simon Glass <sjg@chromium.org>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Install updates on fake vbe-simple and vbe-abrec boards using fwupdtool

This fakes a VBE system, using a devicetree in place of /sys/firmware/fdt
and a file for the storage, then checks that each update reaches the
storage.
"""

import base64
import os
import subprocess
import sys
import tempfile
import unittest

GUID = "5ba31c9e-b9e6-5432-8f97-ca6c4c91a63b"  # VBE\COMPATIBLE_vendor-board

# the size of the storage, the firmware area within it, and the number of
# bytes at the start of each image which vbe-simple does not write
STORAGE_SIZE = 0x4000
AREA_START = 0x1000
AREA_SIZE = 0x3000
SKIP_OFFSET = 0x10

# where the firmware image goes in the area, as set by its store-offset
STORE_OFFSET = 0x200

# the layout for vbe-abrec: the state, then the A and B slots, each with its
# slot info at the end
ABREC_STORAGE_SIZE = 0x30000
ABREC_STATE_OFFSET = 0x1000
ABREC_STATE_SIZE = 0x40
ABREC_SLOT_A = 0x10000
ABREC_SLOT_B = 0x20000
ABREC_SLOT_SIZE = 0x10000
ABREC_SLOT_INFO_OFFSET = 0xF000
ABREC_SLOT_INFO_SIZE = 0x1000

# the flags in the vbe-abrec state, a 32-bit little-endian word whose low byte
# holds the tries left for B, whether to try B, the result of the last try and
# which firmware was picked
ABREC_FLAGS_OFFSET = 8
ABREC_TRY_COUNT = 3
ABREC_FLAG_TRY_B = 1 << 2
ABREC_RESULT_SHIFT = 4
ABREC_RESULT_TRYING = 1
ABREC_RESULT_OK = 2
ABREC_PICK_SHIFT = 6
ABREC_PICK_B = 1

# how long to let fwupdtool run, in seconds; each run takes well under one
TIMEOUT = 60

# the devicetree which the firmware passes to the OS, describing the
# vbe-simple method and the version which is running
FDT_XML = """\
<firmware gtype="FuFdtFirmware">
  <firmware gtype="FuFdtImage">
    <metadata key="compatible" format="str">vendor,board</metadata>
    <firmware gtype="FuFdtImage">
      <id>chosen</id>
      <firmware gtype="FuFdtImage">
        <id>fwupd</id>
        <metadata key="compatible" format="str">vendor,board</metadata>
        <firmware gtype="FuFdtImage">
          <id>firmware</id>
          <metadata key="compatible" format="str">fwupd,vbe-simple</metadata>
          <metadata key="cur-version" format="str">{version}</metadata>
          <metadata key="storage" format="str">{storage}</metadata>
          <metadata key="area-start" format="uint32">{area_start:#x}</metadata>
          <metadata key="area-size" format="uint32">{area_size:#x}</metadata>
          <metadata key="skip-offset" format="uint32">{skip_offset:#x}</metadata>
        </firmware>
      </firmware>
    </firmware>
  </firmware>
</firmware>
"""

# the devicetree which the firmware passes to the OS, describing the
# vbe-abrec method and the slot which booted
ABREC_FDT_XML = """\
<firmware gtype="FuFdtFirmware">
  <firmware gtype="FuFdtImage">
    <metadata key="compatible" format="str">vendor,board</metadata>
    <firmware gtype="FuFdtImage">
      <id>chosen</id>
      <firmware gtype="FuFdtImage">
        <id>fwupd</id>
        <metadata key="compatible" format="str">vendor,board</metadata>
        <firmware gtype="FuFdtImage">
          <id>firmware0</id>
          <metadata key="compatible" format="str">fwupd,vbe-abrec</metadata>
          <metadata key="storage" format="str">{storage}</metadata>
          <metadata key="area-start" format="uint32">0x0</metadata>
          <metadata key="state-offset" format="uint32">{state_offset:#x}</metadata>
          <metadata key="state-size" format="uint32">{state_size:#x}</metadata>
          <metadata key="slot-a-offset" format="uint32">{slot_a:#x}</metadata>
          <metadata key="slot-b-offset" format="uint32">{slot_b:#x}</metadata>
          <metadata key="slot-size" format="uint32">{slot_size:#x}</metadata>
          <metadata key="slot-info-offset" format="uint32">{slot_info_offset:#x}</metadata>
          <metadata key="slot-info-size" format="uint32">{slot_info_size:#x}</metadata>
          <metadata key="booted-slot" format="str">{booted_slot}</metadata>
        </firmware>
      </firmware>
    </firmware>
  </firmware>
</firmware>
"""

# a FIT with one firmware image, whose data is either inside the FIT or
# follows it; the configuration is selected by its compatible string, if any
FIT_XML = """\
<firmware gtype="{gtype}">
  <firmware gtype="FuFdtImage">
    <metadata key="timestamp" format="uint32">0x629d4abd</metadata>
    <firmware gtype="FuFdtImage">
      <id>images</id>
      <firmware gtype="FuFdtImage">
        <id>firmware-1</id>
        <metadata key="type" format="str">firmware</metadata>
        <metadata key="description" format="str">test</metadata>
        {data}
        <metadata key="store-offset" format="uint32">{store_offset:#x}</metadata>
      </firmware>
    </firmware>
    <firmware gtype="FuFdtImage">
      <id>configurations</id>
      <firmware gtype="FuFdtImage">
        <id>conf-1</id>
        {compatible}
        <metadata key="version" format="str">{version}</metadata>
        <metadata key="firmware" format="strlist">firmware-1</metadata>
      </firmware>
    </firmware>
  </firmware>
</firmware>
"""

# the metadata for a cab holding an update with the given version
METAINFO_XML = """\
<?xml version="1.0" encoding="UTF-8"?>
<component type="firmware">
  <id>com.Vbe.Test.firmware</id>
  <name>VBE Test</name>
  <summary>Firmware for testing VBE</summary>
  <provides>
    <firmware type="flashed">{guid}</firmware>
  </provides>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>LGPL-2.1-or-later</project_license>
  <developer_name>VBE</developer_name>
  <releases>
    <release version="{version}" date="2026-10-21">
      <checksum filename="firmware.fit" target="content"/>
    </release>
  </releases>
</component>
"""


def fake_env(tmpdir):
    """Get an environment which keeps fwupd's files in a directory

    This also makes fwupd use FWUPD_SYSFSFWDIR/fdt in place of
    /sys/firmware/fdt

    Args:
        tmpdir (str): the directory, in which this creates a subdirectory
            for each of fwupd's locations

    Returns:
        dict of str: the environment
    """
    env = dict(os.environ, NO_COLOR="1")

    # STATE_DIRECTORY and LOCALCONF_DIRECTORY take priority over
    # FWUPD_LOCALSTATEDIR, so set them too, in case the caller has
    for var, name in [
        ("FWUPD_SYSFSFWDIR", "sys"),
        ("FWUPD_LOCALSTATEDIR", "var"),
        ("STATE_DIRECTORY", "state"),
        ("LOCALCONF_DIRECTORY", "localconf"),
        ("FWUPD_LOCKDIR", "lock"),
        ("CACHE_DIRECTORY", "cache"),
        ("CONFIGURATION_DIRECTORY", "etc"),
    ]:
        path = os.path.join(tmpdir, name)
        os.mkdir(path)
        env[var] = path
    return env


def fwupdtool(*args, env):
    """Run fwupdtool with only the vbe plugin

    Args:
        args (str): the command and its arguments, e.g. "install", cab, GUID
        env (dict of str): the environment to run it in, from fake_env()

    Returns:
        subprocess.CompletedProcess: the result, with stdout holding both
        the standard output and the standard error, as text

    Raises:
        subprocess.TimeoutExpired: fwupdtool did not finish within TIMEOUT
    """
    cmd = ["fwupdtool", "-v", "--plugins", "vbe", *args]
    return subprocess.run(
        cmd,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
        timeout=TIMEOUT,
    )


def crc8(data):
    """Calculate the CRC8 of the vbe-abrec state, as the firmware does

    Args:
        data (bytes): the data to check

    Returns:
        int: the CRC8, using the polynomial 0x07
    """
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07 if crc & 0x80 else crc << 1) & 0xFF
    return crc


class VbeTest(unittest.TestCase):
    """Common code for the tests, which update a fake board

    Each test has its own temporary directory holding fwupd's state, the
    devicetree and the storage, which starts off as zeroes.

    Attributes:
        storage_size (int): the size of the storage, set by each subclass
    """

    storage_size = 0

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmpdir = tmp.name

        self.env = fake_env(self.tmpdir)

        self.storage = self.path("storage.img")
        with open(self.storage, "wb") as f:
            f.write(bytes(self.storage_size))

    def path(self, name):
        """Get the path of a file in the test's temporary directory

        Args:
            name (str): the filename

        Returns:
            str: the full path
        """
        return os.path.join(self.tmpdir, name)

    def write(self, name, contents):
        """Write a file in the test's temporary directory

        Args:
            name (str): the filename
            contents (str or bytes): the text or binary data to write

        Returns:
            str: the full path of the file
        """
        mode = "wb" if isinstance(contents, bytes) else "w"
        with open(self.path(name), mode) as f:
            f.write(contents)
        return self.path(name)

    def run_ok(self, *args):
        """Run fwupdtool, failing the test if it does not succeed

        Args:
            args (str): the command and its arguments

        Returns:
            str: the output from fwupdtool
        """
        result = fwupdtool(*args, env=self.env)
        self.assertEqual(
            result.returncode,
            0,
            f"fwupdtool {' '.join(args)} failed:\n{result.stdout}",
        )
        return result.stdout

    def build_fit(self, version, data, external=False, compatible=True):
        """Build a FIT holding the given data

        Args:
            version (str): the version in the FIT's configuration
            data (bytes): the data for the firmware image
            external (bool): True to put the data after the FIT, rather
                than inside it
            compatible (bool): True to give the configuration the board's
                compatible string, False to have it match any board

        Returns:
            bytes: the contents of the FIT
        """
        if external:
            # a FIT with external data is not valid until the data is added,
            # so build it as a plain devicetree
            gtype = "FuFdtFirmware"
            prop = (
                '<metadata key="data-offset" format="uint32">0x0</metadata>\n'
                f'<metadata key="data-size" format="uint32">{len(data):#x}'
                "</metadata>"
            )
        else:
            gtype = "FuFitFirmware"
            encoded = base64.b64encode(data).decode()
            prop = f'<metadata key="data" format="data">{encoded}</metadata>'
        compat = (
            '<metadata key="compatible" format="strlist">vendor,board</metadata>'
            if compatible
            else ""
        )
        xml = FIT_XML.format(
            gtype=gtype,
            data=prop,
            compatible=compat,
            version=version,
            store_offset=STORE_OFFSET,
        )
        fit = self.path("test.fit")
        self.run_ok("firmware-build", self.write("fit.builder.xml", xml), fit)
        with open(fit, "rb") as f:
            contents = f.read()

        # the data follows the FIT, which is padded to 4 bytes, as
        # 'mkimage -E' does
        if external:
            contents += bytes(-len(contents) % 4) + data
        return contents

    def install(self, fit, version):
        """Put the FIT in a cab and install it

        Args:
            fit (bytes): the contents of the FIT
            version (str): the version of the update, for the cab's metadata
        """
        fit_path = self.write("firmware.fit", fit)
        xml = METAINFO_XML.format(guid=GUID, version=version)
        metainfo = self.write("vbe.metainfo.xml", xml)
        cab = self.path("vbe.cab")
        self.run_ok("build-cabinet", cab, fit_path, metainfo, "--force")
        self.run_ok("install", cab, GUID)


class VbeSimpleTest(VbeTest):
    """Tests for updating a fake board which uses the vbe-simple method"""

    storage_size = STORAGE_SIZE

    def boot(self, version):
        """Create the devicetree as the firmware provides it at boot

        Args:
            version (str): the version of the firmware which is running
        """
        xml = FDT_XML.format(
            version=version,
            storage=self.storage,
            area_start=AREA_START,
            area_size=AREA_SIZE,
            skip_offset=SKIP_OFFSET,
        )
        fdt = os.path.join(self.env["FWUPD_SYSFSFWDIR"], "fdt")
        self.run_ok("firmware-build", self.write("fdt.builder.xml", xml), fdt)

    def check_storage(self, data):
        """Check that the image went into the area, and nothing else changed

        The first SKIP_OFFSET bytes of the image are not written.

        Args:
            data (bytes): the data for the firmware image
        """
        offset = AREA_START + STORE_OFFSET + SKIP_OFFSET
        expect = bytearray(STORAGE_SIZE)
        expect[offset : offset + len(data) - SKIP_OFFSET] = data[SKIP_OFFSET:]
        with open(self.storage, "rb") as f:
            got = f.read()
        self.assertEqual(len(got), STORAGE_SIZE, "storage changed size")
        diff = [i for i, (a, b) in enumerate(zip(got, expect)) if a != b]
        if diff:
            self.fail(f"storage differs in {len(diff)} bytes, from {diff[0]:#x}")

    def test_device(self):
        """The board has a vbe-simple device with the board's GUID"""
        self.boot("1.2.3")
        self.assertIn(GUID, self.run_ok("get-devices"))

    def test_install(self):
        """Install a FIT with a configuration for the board"""
        data = b"0123456789abcdef0123456789abcdef"
        self.boot("1.2.3")
        self.install(self.build_fit("1.2.4", data), "1.2.4")
        self.check_storage(data)

    def test_install_external(self):
        """Install a FIT with external data and a configuration for any board"""
        data = b"0123456789ABCDEF0123456789ABCDEF"
        self.boot("1.2.3")
        fit = self.build_fit("1.2.5", data, external=True, compatible=False)
        self.install(fit, "1.2.5")
        self.check_storage(data)


class VbeAbrecTest(VbeTest):
    """Tests for updating a fake board which uses the vbe-abrec method

    The board starts off running the firmware in A. An update goes to B,
    which the firmware tries; once the OS starts from B, fwupd copies B to A.
    """

    storage_size = ABREC_STORAGE_SIZE

    def boot(self, slot):
        """Create the devicetree as the firmware provides it at boot

        Args:
            slot (str): the slot which the firmware booted from, e.g. "a"
        """
        xml = ABREC_FDT_XML.format(
            storage=self.storage,
            state_offset=ABREC_STATE_OFFSET,
            state_size=ABREC_STATE_SIZE,
            slot_a=ABREC_SLOT_A,
            slot_b=ABREC_SLOT_B,
            slot_size=ABREC_SLOT_SIZE,
            slot_info_offset=ABREC_SLOT_INFO_OFFSET,
            slot_info_size=ABREC_SLOT_INFO_SIZE,
            booted_slot=slot,
        )
        fdt = os.path.join(self.env["FWUPD_SYSFSFWDIR"], "fdt")
        self.run_ok("firmware-build", self.write("fdt.builder.xml", xml), fdt)

    def get_flags(self):
        """Read the flags from the state in the storage

        Returns:
            int: the low byte of the flags, which holds all those used here
        """
        with open(self.storage, "rb") as f:
            f.seek(ABREC_STATE_OFFSET + ABREC_FLAGS_OFFSET)
            return f.read(1)[0]

    def try_b(self):
        """Update the state as the firmware does when it tries B

        This uses up a try, records that B is being tried and that B was
        picked.
        """
        with open(self.storage, "r+b") as f:
            f.seek(ABREC_STATE_OFFSET)
            state = bytearray(f.read(ABREC_STATE_SIZE))
            state[ABREC_FLAGS_OFFSET] = (
                ABREC_FLAG_TRY_B
                | (ABREC_TRY_COUNT - 1)
                | ABREC_RESULT_TRYING << ABREC_RESULT_SHIFT
                | ABREC_PICK_B << ABREC_PICK_SHIFT
            )
            state[0] = crc8(state[1:])
            f.seek(ABREC_STATE_OFFSET)
            f.write(state)

    def check_slot(self, offset, fit):
        """Check that a slot starts with the update

        Args:
            offset (int): the offset of the slot in the storage
            fit (bytes): the contents of the FIT
        """
        with open(self.storage, "rb") as f:
            f.seek(offset)
            got = f.read(len(fit))
        self.assertEqual(got, fit, f"slot at {offset:#x} differs")

    def test_update(self):
        """Install an update to B, try it, then commit it to A"""
        fit = self.build_fit("1.2.4", b"0123456789abcdef0123456789abcdef")
        self.boot("a")
        self.install(fit, "1.2.4")
        self.check_slot(ABREC_SLOT_B, fit)
        self.assertEqual(self.get_flags(), ABREC_FLAG_TRY_B | ABREC_TRY_COUNT)
        self.assertIn(
            "waiting for the update to be applied", self.run_ok("get-devices")
        )

        # the firmware tries B, which boots, so fwupd completes the update
        self.try_b()
        self.boot("b")
        self.assertRegex(self.run_ok("get-devices"), r"Current version: *1\.2\.4")
        self.check_slot(ABREC_SLOT_A, fit)
        self.assertEqual(
            self.get_flags(),
            ABREC_RESULT_OK << ABREC_RESULT_SHIFT | ABREC_PICK_B << ABREC_PICK_SHIFT,
        )


def main():
    """Run the tests, if fwupdtool has the vbe plugin

    Returns:
        int: the exit code, which is 77 if the tests are skipped
    """
    # skip if the plugin is not built, e.g. on a system other than Linux
    with tempfile.TemporaryDirectory() as tmpdir:
        result = fwupdtool("get-plugins", env=fake_env(tmpdir))
    if result.returncode:
        print(f"fwupdtool get-plugins failed:\n{result.stdout}")
        return 1
    if not any(line.startswith("vbe:") for line in result.stdout.splitlines()):
        print("vbe plugin not available")
        return 77
    prog = unittest.main(exit=False, verbosity=2)
    return 0 if prog.result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())

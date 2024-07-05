#!/usr/bin/env python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright 2021 Norbert Kaminski <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import os
import re
import subprocess
from qubes_fwupd_common import create_dirs

FWUPD_DOM0_DIR = "/var/cache/fwupd/qubes"
FWUPD_VM_DOWNLOAD = "/usr/libexec/qubes-fwupd/fwupd_download_updates.py"
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"

SPECIAL_CHAR_REGEX = re.compile(r"%20|&|\||#")
UPDATEVM_REGEX = re.compile(r"^sys-")

run_cmd = (
    "qvm-run",
    "--pass-io",
    "--no-gui",
    "--no-shell",
    "-q",
    "-a",
    "--filter-escape-chars",
    "--color-output=31",
    "--color-stderr=31",
    "--",
)


def run_in_tty(updatevm, args, **kwargs):
    return subprocess.check_call(
        (
            *run_cmd,
            updatevm,
            *args,
        ),
        stdin=subprocess.DEVNULL,
        **kwargs,
    )


class FwupdUpdate:
    def _specify_updatevm(self):
        cmd_updatevm = ["qubes-prefs", "--force-root", "updatevm"]
        p = subprocess.Popen(cmd_updatevm, stdout=subprocess.PIPE)
        self.updatevm = p.communicate()[0].decode().split("\n")[0]
        if p.returncode != 0 and not UPDATEVM_REGEX.match(self.updatevm):
            self.updatevm = None
            raise Exception("Specifying updatevm failed")

    def _check_updatevm(self):
        """Checks if updatevm is running"""
        cmd_xl_list = ["xl", "list", "--", self.updatevm]
        p = subprocess.Popen(
            cmd_xl_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        output = p.communicate()[0].decode()
        return p.returncode == 0

    def download_metadata(self, whonix=False, metadata_url=None):
        """Initialize downloading metadata files.

        Keywords arguments:
        whonix -- Flag enforces downloading the metadata updates via Tor
        metadata_url -- Download metadata from the custom url
        """
        if not whonix:
            self._specify_updatevm()
        else:
            self.updatevm = "sys-whonix"
        if not self._check_updatevm():
            raise Exception(f"{self.updatevm} is not running!!")
        if not os.path.exists(FWUPD_DOM0_DIR):
            create_dirs(FWUPD_DOM0_DIR)
        cmd_metadata = [FWUPD_VM_DOWNLOAD, "--metadata"]
        if metadata_url:
            cmd_metadata.append("--url=" + metadata_url)
        try:
            run_in_tty(self.updatevm, cmd_metadata)
        except subprocess.CalledProcessError:
            raise Exception("Metadata download failed.")

    def download_firmware_updates(self, url, sha, whonix=False):
        """Initializes downloading firmware update archive.

        Keywords arguments:
        url -- url path to the firmware update archive
        sha -- SHA256 checksum of the firmware update archive
        whonix -- Flag enforces downloading the updates via Tor
        """
        if not whonix:
            self._specify_updatevm()
        else:
            self.updatevm = "sys-whonix"
        if not self._check_updatevm():
            raise Exception(f"{self.updatevm} is not running!!")
        if not os.path.exists(FWUPD_DOM0_DIR):
            create_dirs(FWUPD_DOM0_DIR)
        self.arch_name = os.path.basename(url)
        self.arch_path = os.path.join(FWUPD_DOM0_UPDATES_DIR, self.arch_name)
        if not os.path.exists(self.arch_path):
            cmd_firmware_download = [
                "qvm-run",
                "--pass-io",
                "--quiet",
                "--autostart",
                "--no-shell",
                "--color-output=31",
                "--color-stderr=31",
                "--",
                self.updatevm,
                FWUPD_VM_DOWNLOAD,
                f"--url={url}",
                f"--sha={sha}",
            ]
            p = subprocess.Popen(cmd_firmware_download, stdin=subprocess.DEVNULL)
            p.wait()
            if p.returncode != 0:
                raise Exception("Firmware download failed.")
        else:
            self.cached = True
            print("Firmware already downloaded. Using cached files.")

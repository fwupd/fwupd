#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kaminski  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import grp
import os
import re
import subprocess

FWUPD_DOM0_DIR = "/var/cache/fwupd"
FWUPD_VM_DOWNLOAD = "/usr/libexec/qubes-fwupd/fwupd_download_updates.py"
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"

SPECIAL_CHAR_REGEX = re.compile(r"%20|&|\||#")
UPDATEVM_REGEX = re.compile(r"^sys-")

WARNING_COLOR = "\033[93m"

run_cmd = (
    "qvm-run",
    "--no-gui",
    "--no-shell",
    "-q",
    "-a",
    "--filter-escape-chars",
    "--color-output=31",
    "--color-stderr=31",
    "--",
)


def usbvm_run(args, **kwargs):
    return subprocess.check_call((*run_cmd, *args), **kwargs)


def run_in_tty(updatevm, args, **kwargs):
    return subprocess.check_call(
        (
            *run_cmd,
            updatevm,
            "script",
            "--quiet",
            "--return",
            "--command",
            shlex.join(args),
            "/dev/null",
        ),
        stdin=subprocess.DEVNULL,
        **kwargs,
    )


class FwupdUpdate:
    def _create_dirs(self, *args):
        """Method creates directories.

        Keyword arguments:
        *args -- paths to be created
        """
        qubes_gid = grp.getgrnam("qubes").gr_gid
        self.old_umask = os.umask(0o002)
        if args is None:
            raise Exception("Creating directories failed, no paths given.")
        for file_path in args:
            if not os.path.exists(file_path):
                os.mkdir(file_path)
                os.chown(file_path, -1, qubes_gid)
            elif os.stat(file_path).st_gid != qubes_gid:
                print(
                    f"{WARNING_COLOR}Warning: You should move a personal files"
                    f" from {file_path}. Cleaning cache will cause lose of "
                    f"the personal data!!{WARNING_COLOR}"
                )

    def _specify_updatevm(self):
        cmd_updatevm = ["qubes-prefs", "--force-root", "updatevm"]
        p = subprocess.Popen(cmd_updatevm, stdout=subprocess.PIPE)
        self.updatevm = p.communicate()[0].decode().split("\n")[0]
        if p.returncode != 0 and not UPDATEVM_REGEX.match(self.updatevm):
            self.updatevm = None
            raise Exception("Specifying updatevm failed")

    def _check_updatevm(self):
        """Checks if usbvm is running"""
        cmd_xl_list = ["xl", "list"]
        p = subprocess.Popen(cmd_xl_list, stdout=subprocess.PIPE)
        output = p.communicate()[0].decode()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Firmware downgrade failed")
        return self.updatevm in output

    def _encrypt_update_url(self, url):
        self.enc_url = url
        self.arch_path = os.path.join(FWUPD_DOM0_UPDATES_DIR, self.arch_name)
        self.arch_name = "untrusted.cab"

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
            self._create_dirs(FWUPD_DOM0_DIR)
        cmd_metadata = [FWUPD_VM_DOWNLOAD, "--metadata"]
        if metadata_url:
            cmd_metadata.append("--url=" + metadata_url)
        try:
            run_in_tty(updatevm, cmd_metadata)
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
            self._create_dirs(FWUPD_DOM0_DIR)
        self._encrypt_update_url(url)
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
                "script",
                "--quiet",
                "--return",
                "--command",
                shlex.join(
                    (FWUPD_VM_DOWNLOAD, f"--url={self.enc_url}", f"--sha={sha}")
                ),
            ]
            p = subprocess.Popen(cmd_firmware_download, stdin=subprocess.DEVNULL)
            p.wait()
            if p.returncode != 0:
                raise Exception("Firmware download failed.")
        else:
            self.cached = True
            print("Firmware already downloaded. Using cached files.")

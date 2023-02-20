#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
import sys
import subprocess
import os

from fwupd_common_vm import FwupdVmCommon

FWUPD_VM_DIR = "/home/user/.cache/fwupd"
FWUPD_VM_UPDATES_DIR = os.path.join(FWUPD_VM_DIR, "updates")
FWUPD_VM_METADATA_DIR = os.path.join(FWUPD_VM_DIR, "metadata")
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"
METADATA_URL = "https://fwupd.org/downloads/firmware.xml.gz"
METADATA_URL_JCAT = "https://fwupd.org/downloads/firmware.xml.gz.jcat"


class DownloadData(FwupdVmCommon):
    def _download_metadata_file(self):
        """Download metadata file"""
        if self.custom_url is None:
            metadata_url = METADATA_URL
        else:
            metadata_url = self.custom_url
        cmd_metadata = ["wget", "-P", FWUPD_VM_METADATA_DIR, metadata_url]
        p = subprocess.Popen(cmd_metadata)
        p.wait()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Downloading metadata file failed")
        if not os.path.exists(self.metadata_file):
            raise FileNotFoundError(
                "fwupd-qubes: Downloaded metadata file does not exist"
            )

    def _download_metadata_jcat(self):
        """Download metadata jcat signature"""
        if self.custom_url is None:
            metadata_url = METADATA_URL
        else:
            metadata_url = self.custom_url
        cmd_metadata = ["wget", "-P", FWUPD_VM_METADATA_DIR, f"{metadata_url}.jcat"]
        p = subprocess.Popen(cmd_metadata)
        p.wait()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Downloading metadata file failed")
        if not os.path.exists(f"{self.metadata_file}.jcat"):
            raise FileNotFoundError(
                "fwupd-qubes: Downloaded metadata file does not exist"
            )
        cmd_export = [
            "jcat-tool",
            f"--prefix={FWUPD_VM_METADATA_DIR}/",
            "--",
            "export",
            f"{self.metadata_file}.jcat",
        ]
        environ = os.environ.copy()
        environ["LC_ALL"] = "C"
        p = subprocess.Popen(cmd_export, stdout=subprocess.PIPE, env=environ)
        stdout, _ = p.communicate()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Extracting jcat file failed")
        # rename extracted files to match jcat base name, instead of "ID"
        # inside jcat
        for line in stdout.decode("ascii").splitlines():
            if not line.startswith("Wrote "):
                continue
            path = line.split(" ", 1)[1]
            base_path, ext = os.path.splitext(path)
            if base_path == self.metadata_file:
                continue
            new_path = f"{self.metadata_file}{ext}"
            os.rename(path, new_path)

    def download_metadata(self, url=None):
        """Downloads default metadata and its signatures"""
        if url is not None:
            self.custom_url = url
            custom_metadata_name = os.path.basename(url)
            self.metadata_file = os.path.join(
                FWUPD_VM_METADATA_DIR, custom_metadata_name
            )
        else:
            self.custom_url = None
            self.metadata_file = os.path.join(FWUPD_VM_METADATA_DIR, "firmware.xml.gz")
        self.validate_vm_dirs()
        self._download_metadata_file()
        self._download_metadata_jcat()

    def download_updates(self, url, sha):
        """
        Downloads update form given url

        Keyword argument:
        url - url address of the update
        """
        self.validate_vm_dirs()
        self.arch_name = os.path.basename(url)
        update_path = os.path.join(FWUPD_VM_UPDATES_DIR, self.arch_name)
        cmd_update = ["wget", "-O", update_path, url]
        p = subprocess.Popen(cmd_update)
        p.wait()
        if p.returncode != 0:
            raise Exception("fwupd-qubes: Downloading update file failed")
        if not os.path.exists(update_path):
            raise FileNotFoundError(
                "fwupd-qubes: Downloaded update file does not exist"
            )
        self.check_shasum(update_path, sha)
        print("Update file downloaded successfully")


def main():
    url = None
    sha = None
    dn = DownloadData()
    for arg in sys.argv:
        if "--url=" in arg:
            url = arg.replace("--url=", "")
        if "--sha=" in arg:
            sha = arg.replace("--sha=", "")
    if "--metadata" in sys.argv:
        dn.download_metadata(url=url)
    elif url and sha:
        dn.download_updates(url, sha)
    else:
        raise Exception("Invalid command!!!")


if __name__ == "__main__":
    main()

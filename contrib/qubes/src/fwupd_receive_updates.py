#!/usr/bin/env python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright 2010 Rafal Wojtczuk <rafal@invisiblethingslab.com>
# Copyright 2020 Norbert Kamiński <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import hashlib
import os
import shutil
import subprocess
import tempfile
from qubes_fwupd_common import create_dirs

FWUPD_DOM0_DIR = "/var/cache/fwupd/qubes"
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOM0_UNTRUSTED_DIR = os.path.join(FWUPD_DOM0_UPDATES_DIR, "untrusted")
FWUPD_DOM0_METADATA_DIR = os.path.join(FWUPD_DOM0_DIR, "metadata")

FWUPD_VM_DIR = "/home/user/.cache/fwupd"
FWUPD_VM_UPDATES_DIR = os.path.join(FWUPD_VM_DIR, "updates")
FWUPD_VM_METADATA_DIR = os.path.join(FWUPD_VM_DIR, "metadata")
FWUPD_PKI = "/etc/pki/fwupd"
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"
HEADS_UPDATES_DIR = "/boot/updates"


class FwupdReceiveUpdates:
    def _check_shasum(self, file_path, sha):
        """Compares computed SHA256 checksum with `sha` parameter.

        Keyword arguments:
        file_path -- absolute path to the file
        sha -- SHA256 checksum of the file
        """
        with open(file_path, "rb") as f:
            c_sha = hashlib.sha256(f.read()).hexdigest()
        if c_sha != sha:
            self.clean_cache()
            raise ValueError(f"Computed checksum {c_sha} did NOT match {sha}.")

    def _jcat_verification(self, file_path, file_directory):
        """Verifies SHA1+SHA256 checksum and PKCS#7 signature.

        Keyword argument:
        file_path -- absolute path to jcat file
        file_directory -- absolute path to the directory to jcat file location
        """
        assert file_path.startswith("/"), f"bad file path {file_path!r}"
        cmd_jcat = ["jcat-tool", "verify", file_path, "--public-keys", FWUPD_PKI]
        p = subprocess.Popen(
            cmd_jcat, cwd=file_directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        stdout, __ = p.communicate()
        verification = stdout.decode("utf-8")
        print(verification)
        if p.returncode != 0:
            self.clean_cache()
            raise Exception("jcat-tool: Verification failed")

    def handle_fw_update(self, updatevm, sha, filename):
        """Copies firmware update archives from the updateVM.

        Keyword arguments:
        updatevm -- update VM name
        sha -- SHA256 checksum of the firmware update archive
        filename -- name of the firmware update archive
        """
        create_dirs(FWUPD_DOM0_UPDATES_DIR, FWUPD_DOM0_UNTRUSTED_DIR)

        with tempfile.TemporaryDirectory(dir=FWUPD_DOM0_UNTRUSTED_DIR) as tmpdir:
            dom0_firmware_untrusted_path = os.path.join(tmpdir, filename)
            updatevm_firmware_file_path = os.path.join(FWUPD_VM_UPDATES_DIR, filename)

            cmd_copy = [
                "qvm-run",
                "--pass-io",
                "--no-gui",
                "-q",
                "-a",
                "--no-shell",
                "--",
                updatevm,
                "cat",
                "--",
                updatevm_firmware_file_path,
            ]
            with open(dom0_firmware_untrusted_path, "bx") as untrusted_file:
                p = subprocess.Popen(cmd_copy, stdout=untrusted_file, shell=False)
            p.wait()
            if p.returncode != 0:
                raise Exception("qvm-run: Copying firmware file failed!!")

            self._check_shasum(dom0_firmware_untrusted_path, sha)
            # jcat verification will be done by fwupd itself
            self.arch_name = filename
            self.arch_path = os.path.join(FWUPD_DOM0_UPDATES_DIR, filename)
            shutil.move(dom0_firmware_untrusted_path, self.arch_path)

    def handle_metadata_update(self, updatevm, metadata_url):
        """Copies metadata files from the updateVM.

        Keyword argument:
        updatevm -- update VM name
        """
        metadata_name = os.path.basename(metadata_url)
        self.metadata_file = os.path.join(FWUPD_DOM0_METADATA_DIR, metadata_name)
        self.metadata_file_jcat = self.metadata_file + ".jcat"
        self.metadata_file_updatevm = os.path.join(FWUPD_VM_METADATA_DIR, metadata_name)
        create_dirs(FWUPD_DOM0_METADATA_DIR, FWUPD_DOM0_UNTRUSTED_DIR)
        with tempfile.TemporaryDirectory(dir=FWUPD_DOM0_UNTRUSTED_DIR) as tmpdir:
            cmd_copy_metadata_file = [
                "qvm-run",
                "--pass-io",
                "--no-gui",
                "--no-shell",
                "--",
                updatevm,
                "cat",
                "--",
                self.metadata_file_updatevm,
            ]
            # TODO: switch to ed25519 once firmware.xml.gz.jcat will have it
            cmd_copy_metadata_file_signature = [
                "qvm-run",
                "--pass-io",
                "--no-gui",
                "--no-shell",
                "--",
                updatevm,
                "cat",
                "--",
                self.metadata_file_updatevm + ".jcat",
            ]
            untrusted_metadata_file = os.path.join(tmpdir, metadata_name)

            with open(untrusted_metadata_file, "bx") as untrusted_file_1, open(
                untrusted_metadata_file + ".jcat", "bx"
            ) as untrusted_file_2, subprocess.Popen(
                cmd_copy_metadata_file, stdout=untrusted_file_1
            ) as p, subprocess.Popen(
                cmd_copy_metadata_file_signature, stdout=untrusted_file_2
            ) as q:
                p.wait()
                q.wait()
            if p.returncode != 0:
                raise Exception("qvm-run: Copying metadata file failed!!")
            if q.returncode != 0:
                raise Exception("qvm-run: Copying metadata jcat failed!!")

            self._jcat_verification(
                untrusted_metadata_file + ".jcat",
                os.path.dirname(untrusted_metadata_file),
            )
            # verified, move into trusted dir
            shutil.move(untrusted_metadata_file, self.metadata_file)
            shutil.move(untrusted_metadata_file + ".jcat", self.metadata_file_jcat)

    def clean_cache(self):
        """Removes updates data"""
        print("Cleaning dom0 cache directories")
        if os.path.exists(FWUPD_DOM0_METADATA_DIR):
            shutil.rmtree(FWUPD_DOM0_METADATA_DIR)
        if os.path.exists(FWUPD_DOM0_UPDATES_DIR):
            shutil.rmtree(FWUPD_DOM0_UPDATES_DIR)
        if os.path.exists(HEADS_UPDATES_DIR):
            shutil.rmtree(HEADS_UPDATES_DIR)

#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import grp
import hashlib
import os
import shutil
import subprocess

FWUPD_VM_DIR = "/home/user/.cache/fwupd"
FWUPD_VM_UPDATES_DIR = os.path.join(FWUPD_VM_DIR, "updates")
FWUPD_VM_METADATA_DIR = os.path.join(FWUPD_VM_DIR, "metadata")
WARNING_COLOR = '\033[93m'
FWUPD_PKI = "/etc/pki/fwupd"


class FwupdVmCommon:
    def _create_dirs(self, *args):
        """Method creates directories.

        Keyword arguments:
        *args -- paths to be created
        """
        qubes_gid = grp.getgrnam('qubes').gr_gid
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

    def check_shasum(self, file_path, sha):
        """Compares computed SHA256 checksum with `sha` parameter.

        Keyword arguments:
        file_path -- absolute path to the file
        sha -- SHA256 checksum of the file
        """
        with open(file_path, 'rb') as f:
            c_sha = hashlib.sha256(f.read()).hexdigest()
        if c_sha != sha:
            self.clean_vm_cache()
            raise ValueError(
                "Computed checksum %s did NOT match %s. " %
                (c_sha, sha)
            )

    def validate_vm_dirs(self):
        """Validates and creates directories"""
        print("Validating directories")
        if not os.path.exists(FWUPD_VM_DIR):
            self._create_dirs(FWUPD_VM_DIR)
        if os.path.exists(FWUPD_VM_METADATA_DIR):
            shutil.rmtree(FWUPD_VM_METADATA_DIR)
            self._create_dirs(FWUPD_VM_METADATA_DIR)
        else:
            self._create_dirs(FWUPD_VM_METADATA_DIR)
        if not os.path.exists(FWUPD_VM_UPDATES_DIR):
            self._create_dirs(FWUPD_VM_UPDATES_DIR)
        os.umask(self.old_umask)

    def _jcat_verification(self, file_path, file_directory):
        """Verifies sha1 and sha256 checksum, GPG signature,
        and PKCS#7 signature.

        Keyword argument:
        file_path -- absolute path to jcat file
        file_directory -- absolute path to the directory to jcat file location
        """
        cmd_jcat = [
            "jcat-tool",
            "verify",
            f"{file_path}",
            "--public-keys",
            FWUPD_PKI
        ]
        p = subprocess.Popen(
            cmd_jcat,
            cwd=file_directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        stdout, __ = p.communicate()
        verification = stdout.decode('utf-8')
        print(verification)
        if p.returncode != 0:
            self.clean_vm_cache()
            raise Exception('jcat-tool: Verification failed')

    def clean_vm_cache(self):
        """Removes updates data"""
        print("Cleaning cache directories")
        if os.path.exists(FWUPD_VM_METADATA_DIR):
            shutil.rmtree(FWUPD_VM_METADATA_DIR)
        if os.path.exists(FWUPD_VM_UPDATES_DIR):
            shutil.rmtree(FWUPD_VM_UPDATES_DIR)

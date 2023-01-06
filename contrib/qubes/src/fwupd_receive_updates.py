#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
#               2020  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import glob
import grp
import hashlib
import os
import shutil
import subprocess

FWUPD_DOM0_DIR = "/var/cache/fwupd/qubes"
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOM0_UNTRUSTED_DIR = os.path.join(FWUPD_DOM0_UPDATES_DIR, "untrusted")
FWUPD_DOM0_METADATA_DIR = os.path.join(FWUPD_DOM0_DIR, "metadata")
FWUPD_DOM0_METADATA_FILE = os.path.join(FWUPD_DOM0_METADATA_DIR, "firmware.xml.gz")
FWUPD_DOM0_METADATA_JCAT = os.path.join(FWUPD_DOM0_METADATA_DIR, "firmware.xml.gz.jcat")

FWUPD_VM_DIR = "/home/user/.cache/fwupd"
FWUPD_VM_UPDATES_DIR = os.path.join(FWUPD_VM_DIR, "updates")
FWUPD_VM_METADATA_DIR = os.path.join(FWUPD_VM_DIR, "metadata")
FWUPD_VM_METADATA_FILE = os.path.join(FWUPD_VM_METADATA_DIR, "firmware.xml.gz")
FWUPD_VM_METADATA_JCAT = os.path.join(FWUPD_VM_METADATA_DIR, "firmware.xml.gz.jcat")
FWUPD_PKI = "/etc/pki/fwupd"
FWUPD_PKI_PGP = "/etc/pki/fwupd/GPG-KEY-Linux-Vendor-Firmware-Service"
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"
HEADS_UPDATES_DIR = "/boot/updates"
WARNING_COLOR = "\033[93m"


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
                os.chmod(file_path, 0o0775)
            elif os.stat(file_path).st_gid != qubes_gid:
                print(
                    f"{WARNING_COLOR}Warning: You should move a personal files"
                    f" from {file_path}. Cleaning cache will cause lose of "
                    f"the personal data!!{WARNING_COLOR}"
                )

    def _jcat_verification(self, file_path, file_directory):
        """Verifies sha1 and sha256 checksum, GPG signature,
        and PKCS#7 signature.

        Keyword argument:
        file_path -- absolute path to jcat file
        file_directory -- absolute path to the directory to jcat file location
        """
        assert file_path.startswith("/"), "bad file path {file_path!r}"
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

    def _pgp_verification(self, signature_path, file_path):
        """Verifies GPG signature.

        Keyword argument:
        signature_path -- absolute path to signature file
        file_path -- absolute path to the signed file location
        """
        assert file_path.startswith("/"), "bad file path {file_path!r}"
        cmd_verify = [
            "sqv",
            "--keyring",
            FWUPD_PKI_PGP,
            "--",
            signature_path,
            file_path,
        ]
        p = subprocess.Popen(cmd_verify, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, __ = p.communicate()
        signature_key = stdout.decode("utf-8")
        if p.returncode != 0 or not signature_key:
            self.clean_cache()
            raise Exception("pgp: Verification failed")

    def _reconstruct_jcat(self, jcat_path, file_path):
        """Reconstruct jcat file from verified parts

        Currently included parts: .asc, .sha256
        Hashes are generated locally if missing

        Arguments:
        jcat_path - absolute path to the output jcat file
        file_path - absolute path to the signed file
        """
        # generate missing hashes
        hashes = ("sha256",)
        for hash_ext in hashes:
            hash_fname = f"{file_path}.{hash_ext}"
            if not os.path.exists(hash_fname):
                # TODO: switch to hashlib.file_digest (py3.11)
                with open(file_path, "rb") as f_data:
                    hash_val = hashlib.new(hash_ext, f_data.read()).hexdigest()
                with open(hash_fname, "w") as f_hash:
                    f_hash.write(hash_val)

        signatures = ("asc",)

        file_id = os.path.basename(file_path)
        for sign_type in signatures + hashes:
            sign_path = f"{file_path}.{sign_type}"
            if not os.path.exists(sign_path):
                raise Exception(f"Missing signature: {sign_path}")
            jcat_cmd = ["jcat-tool", "import", jcat_path, file_id, sign_path]
            subprocess.check_call(jcat_cmd)

    def handle_fw_update(self, updatevm, sha, filename):
        """Copies firmware update archives from the updateVM.

        Keyword arguments:
        updatevm -- update VM name
        sha -- SHA256 checksum of the firmware update archive
        filename -- name of the firmware update archive
        """
        dom0_firmware_untrusted_path = os.path.join(FWUPD_DOM0_UNTRUSTED_DIR, filename)
        updatevm_firmware_file_path = os.path.join(FWUPD_VM_UPDATES_DIR, filename)

        if os.path.exists(FWUPD_DOM0_UNTRUSTED_DIR):
            shutil.rmtree(FWUPD_DOM0_UNTRUSTED_DIR)
        self._create_dirs(FWUPD_DOM0_UPDATES_DIR, FWUPD_DOM0_UNTRUSTED_DIR)

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
        with open(dom0_firmware_untrusted_path, "wbx") as untrusted_file:
            p = subprocess.Popen(cmd_copy, stdout=untrusted_file, shell=False)
        p.wait()
        if p.returncode != 0:
            raise Exception("qvm-run: Copying firmware file failed!!")

        self._check_shasum(dom0_firmware_untrusted_path, sha)
        # jcat verification will be done by fwupd itself
        os.umask(self.old_umask)
        self.arch_name = filename
        self.arch_path = os.path.join(FWUPD_DOM0_UPDATES_DIR, filename)
        shutil.move(dom0_firmware_untrusted_path, self.arch_path)

    def handle_metadata_update(self, updatevm, metadata_url=None):
        """Copies metadata files from the updateVM.

        Keyword argument:
        updatevm -- update VM name
        """
        if metadata_url:
            metadata_name = metadata_url.replace(FWUPD_DOWNLOAD_PREFIX, "")
            self.metadata_file = os.path.join(FWUPD_DOM0_METADATA_DIR, metadata_name)
        else:
            self.metadata_file = FWUPD_DOM0_METADATA_FILE
        self.metadata_file_jcat = self.metadata_file + ".jcat"
        self.metadata_file_updatevm = self.metadata_file.replace(
            FWUPD_DOM0_METADATA_DIR, FWUPD_VM_METADATA_DIR
        )
        self._create_dirs(FWUPD_DOM0_METADATA_DIR)
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
            self.metadata_file_updatevm + '.asc',
        ]
        with open(self.metadata_file, "wbx") as untrusted_file_1, open(
            self.metadata_file + '.asc', "wbx"
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
            raise Exception("qvm-run: Copying metadata signature failed!!")

        self._pgp_verification(self.metadata_file + '.asc', self.metadata_file)
        self._reconstruct_jcat(self.metadata_file_jcat, self.metadata_file)

    def clean_cache(self):
        """Removes updates data"""
        print("Cleaning dom0 cache directories")
        if os.path.exists(FWUPD_DOM0_METADATA_DIR):
            shutil.rmtree(FWUPD_DOM0_METADATA_DIR)
        if os.path.exists(FWUPD_DOM0_UPDATES_DIR):
            shutil.rmtree(FWUPD_DOM0_UPDATES_DIR)
        if os.path.exists(HEADS_UPDATES_DIR):
            shutil.rmtree(HEADS_UPDATES_DIR)

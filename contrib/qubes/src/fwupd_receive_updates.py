#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
#               2020  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import base64
import glob
import hashlib
import itertools
import os
import shutil
import struct
import subprocess
import tempfile
from qubes_fwupd_common import EXIT_CODES, create_dirs

FWUPD_DOM0_DIR = "/var/cache/fwupd/qubes"
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOM0_UNTRUSTED_DIR = os.path.join(FWUPD_DOM0_UPDATES_DIR, "untrusted")
FWUPD_DOM0_METADATA_DIR = os.path.join(FWUPD_DOM0_DIR, "metadata")

FWUPD_VM_DIR = "/home/user/.cache/fwupd"
FWUPD_VM_UPDATES_DIR = os.path.join(FWUPD_VM_DIR, "updates")
FWUPD_VM_METADATA_DIR = os.path.join(FWUPD_VM_DIR, "metadata")
FWUPD_PKI = "/etc/pki/fwupd"
FWUPD_PKI_PGP = "/etc/pki/fwupd/GPG-KEY-Linux-Vendor-Firmware-Service"
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

    def _crc24(self, data):
        """Calculate CRC-24

        Checksum calculation for PGP armored signature. This algorithm isn't
        available in Python standard library, but it's simple enough to
        implement here. It doesn't need to be fast, nor side-channel resistant.
        """
        crc = 0xB704CE  # crc24_init
        for b in data:
            crc ^= b << 16
            for i in range(8):
                crc <<= 1
                if crc & 0x1000000:
                    crc ^= 0x1864CFB  # crc24_poly
        return crc

    def _pgp_parse(self, signature_path):
        """Verifies if GPG signature is correctly formatted

        Verify if signature is well formed - sqv will verify if the signature
        itself correct, but may accept extra data in the signature packets
        that could later confuse GnuPG used by fwupd.
        Verify also armor checksum, as sqv doesn't do that.
        """

        # sigparse expects binary format, so decode base64 first
        with open(signature_path, "rb") as sig:
            data = sig.read(4096)  # arbitrary size limit
            if sig.read(1) != b"":
                raise Exception("pgp: signature too big")
            lines = data.splitlines()
            # format described in RFC-4880 ch 6
            if lines[0:2] != [b"-----BEGIN PGP SIGNATURE-----", b""]:
                raise Exception("pgp: invalid header")
            if lines[-1] != b"-----END PGP SIGNATURE-----":
                raise Exception("pgp: invalid footer")
            checksum = lines[-2]
            if checksum[0] != ord("=") or len(checksum) != 5:
                raise Exception("pgp: invalid checksum format")
            base64_data = b"".join(lines[2:-2])
            data = base64.b64decode(base64_data, validate=True)
            crc = base64.b64decode(checksum[1:], validate=True)
            crc = struct.unpack(">I", b"\0" + crc)[0]
            if crc != self._crc24(data):
                raise Exception("pgp: invalid checksum")
            with tempfile.NamedTemporaryFile() as tmp:
                tmp.write(data)
                tmp.flush()
                p = subprocess.Popen(["sigparse", tmp.name], stderr=subprocess.PIPE)
                _, stderr = p.communicate()
                if p.returncode != 0:
                    raise Exception("pgp: invalid signature format: " + stderr.decode())
        # reconstruct armored data to ensure its canonical form
        with open(signature_path, "wb") as sig:
            sig.write(b"-----BEGIN PGP SIGNATURE-----\n")
            sig.write(b"\n")
            encoded = iter(base64.b64encode(data))
            while line := bytes(itertools.islice(encoded, 76)):
                sig.write(line + b"\n")
            sig.write(checksum + b"\n")
            sig.write(b"-----END PGP SIGNATURE-----\n")

    def _pgp_verification(self, signature_path, file_path):
        """Verifies GPG signature.

        Keyword argument:
        signature_path -- absolute path to signature file
        file_path -- absolute path to the signed file location
        """
        assert file_path.startswith("/"), "bad file path {file_path!r}"
        try:
            self._pgp_parse(signature_path)
        except Exception:
            self.clean_cache()
            raise
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
                self.metadata_file_updatevm + ".asc",
            ]
            untrusted_metadata_file = os.path.join(tmpdir, metadata_name)

            with open(untrusted_metadata_file, "bx") as untrusted_file_1, open(
                untrusted_metadata_file + ".asc", "bx"
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

            self._pgp_verification(
                untrusted_metadata_file + ".asc", untrusted_metadata_file
            )
            self._reconstruct_jcat(
                untrusted_metadata_file + ".jcat", untrusted_metadata_file
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

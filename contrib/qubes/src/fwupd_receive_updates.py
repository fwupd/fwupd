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
import re
import shutil
import subprocess

FWUPD_DOM0_DIR = "/root/.cache/fwupd"
FWUPD_DOM0_UPDATES_DIR = os.path.join(FWUPD_DOM0_DIR, "updates")
FWUPD_DOM0_UNTRUSTED_DIR = os.path.join(FWUPD_DOM0_UPDATES_DIR, "untrusted")
FWUPD_DOM0_METADATA_DIR = os.path.join(FWUPD_DOM0_DIR, "metadata")
FWUPD_DOM0_METADATA_FILE = os.path.join(
    FWUPD_DOM0_METADATA_DIR,
    "firmware.xml.gz"
)
FWUPD_DOM0_METADATA_JCAT = os.path.join(
    FWUPD_DOM0_METADATA_DIR,
    "firmware.xml.gz.jcat"
)

FWUPD_VM_DIR = "/home/user/.cache/fwupd"
FWUPD_VM_UPDATES_DIR = os.path.join(FWUPD_VM_DIR, "updates")
FWUPD_VM_METADATA_DIR = os.path.join(FWUPD_VM_DIR, "metadata")
FWUPD_VM_METADATA_FILE = os.path.join(
    FWUPD_VM_METADATA_DIR,
    "firmware.xml.gz"
)
FWUPD_VM_METADATA_JCAT = os.path.join(
    FWUPD_VM_METADATA_DIR,
    "firmware.xml.gz.jcat"
)
FWUPD_PKI = "/etc/pki/fwupd"
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"
FWUPD_METADATA_FLAG_REGEX = re.compile(r"^metaflag")
FWUPD_METADATA_FILES_REGEX = re.compile(
    r"^firmware[a-z0-9\[\]\@\<\>\.\"\-]{0,128}.xml.gz.?[aj]?[sc]?[ca]?t?$"
)
HEADS_UPDATES_DIR = "/boot/updates"
WARNING_COLOR = '\033[93m'


class FwupdReceiveUpdates:
    def _check_shasum(self, file_path, sha):
        """Compares computed SHA256 checksum with `sha` parameter.

        Keyword arguments:
        file_path -- absolute path to the file
        sha -- SHA256 checksum of the file
        """
        with open(file_path, 'rb') as f:
            c_sha = hashlib.sha256(f.read()).hexdigest()
        if c_sha != sha:
            self.clean_cache()
            raise ValueError(f"Computed checksum {c_sha} did NOT match {sha}.")

    def _check_domain(self, updatevm):
        """Checks if domain given as `updatevm` is allowed to send update
        files.

        Keyword argument:
        updatevm - domain to be checked
        """
        cmd = ['qubes-prefs', '--force-root', 'updatevm']
        p = subprocess.check_output(cmd)
        source = p.decode('ascii').rstrip()
        if source != updatevm and "sys-whonix" != updatevm:
            raise Exception(
                f'Domain {updatevm} not allowed to send dom0 updates'
            )

    def _verify_received(self, files_path, regex_pattern, updatevm):
        """Checks if sent files match  regex filename pattern.

        Keyword arguments:

        files_path -- absolute path to inspected directory
        regex_pattern -- pattern of the expected files
        updatevm - domain to be checked
        """
        for untrusted_f in os.listdir(files_path):
            if not regex_pattern.match(untrusted_f):
                raise Exception(f'Domain {updatevm} sent unexpected file')
            f = untrusted_f
            assert '/' not in f
            assert '\0' not in f
            assert '\x1b' not in f
            path_f = os.path.join(files_path, f)
            if os.path.islink(path_f) or not os.path.isfile(path_f):
                raise Exception(f'Domain {updatevm} sent not regular file')

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
                os.chmod(file_path, 0o0775)
            elif os.stat(file_path).st_gid != qubes_gid:
                print(
                    f"{WARNING_COLOR}Warning: You should move a personal files"
                    f" from {file_path}. Cleaning cache will cause lose of "
                    f"the personal data!!{WARNING_COLOR}"
                )

    def _extract_archive(self, archive_path, output_path):
        """Extracts archive file to the specified directory.

        Keyword arguments:
        archive_path -- absolute path to archive file
        output_path -- absolute path to the output directory
        """
        cmd_extract = [
            "gcab",
            "-x",
            f"--directory={output_path}",
            f"{archive_path}"
        ]
        shutil.copy(archive_path, FWUPD_DOM0_UPDATES_DIR)
        p = subprocess.Popen(cmd_extract, stdout=subprocess.PIPE)
        p.communicate()[0].decode('ascii')
        if p.returncode != 0:
            raise Exception(
                f'gcab: Error while extracting {archive_path}.'
            )

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
            self.clean_cache()
            raise Exception('jcat-tool: Verification failed')

    def handle_fw_update(self, updatevm, sha, filename):
        """Copies firmware update archives from the updateVM.

        Keyword arguments:
        updatevm -- update VM name
        sha -- SHA256 checksum of the firmware update archive
        filename -- name of the firmware update archive
        """
        fwupd_firmware_file_regex = re.compile(filename)
        dom0_firmware_untrusted_path = os.path.join(
            FWUPD_DOM0_UNTRUSTED_DIR,
            filename
        )
        updatevm_firmware_file_path = os.path.join(
            FWUPD_VM_UPDATES_DIR,
            filename
        )

        self._check_domain(updatevm)
        if os.path.exists(FWUPD_DOM0_UNTRUSTED_DIR):
            shutil.rmtree(FWUPD_DOM0_UNTRUSTED_DIR)
        self._create_dirs(FWUPD_DOM0_UPDATES_DIR, FWUPD_DOM0_UNTRUSTED_DIR)

        cmd_copy = 'qvm-run --pass-io %s %s > %s' % (
            updatevm,
            "'cat %s'" % updatevm_firmware_file_path,
            dom0_firmware_untrusted_path
        )
        p = subprocess.Popen(cmd_copy, shell=True)
        p.wait()
        if p.returncode != 0:
            raise Exception('qvm-run: Copying firmware file failed!!')

        self._verify_received(
            FWUPD_DOM0_UNTRUSTED_DIR,
            fwupd_firmware_file_regex,
            updatevm
        )
        self._check_shasum(dom0_firmware_untrusted_path, sha)
        untrusted_dir_name = filename.replace(".cab", "")
        self._extract_archive(
            dom0_firmware_untrusted_path,
            FWUPD_DOM0_UNTRUSTED_DIR
        )
        signature_name = os.path.join(
            FWUPD_DOM0_UNTRUSTED_DIR,
            "firmware*.jcat"
        )
        file_path = glob.glob(signature_name)
        if not file_path:
            raise FileNotFoundError("jcat file not found!")
        self._jcat_verification(file_path[0], FWUPD_DOM0_UNTRUSTED_DIR)
        os.umask(self.old_umask)
        if untrusted_dir_name == "untrusted":
            untrusted_dir_name = "trusted"
            verified_file = os.path.join(FWUPD_DOM0_UPDATES_DIR, filename)
            self.arch_name = "trusted.cab"
            self.arch_path = os.path.join(
                FWUPD_DOM0_UPDATES_DIR,
                self.arch_name
            )
            shutil.move(verified_file, self.arch_path)
        else:
            self.arch_path = os.path.join(FWUPD_DOM0_UPDATES_DIR, filename)
        dir_name = os.path.join(FWUPD_DOM0_UPDATES_DIR, untrusted_dir_name)
        os.remove(dom0_firmware_untrusted_path)
        shutil.move(FWUPD_DOM0_UNTRUSTED_DIR, dir_name)

    def handle_metadata_update(self, updatevm, metadata_url=None):
        """Copies metadata files from the updateVM.

        Keyword argument:
        updatevm -- update VM name
        """
        if metadata_url:
            metadata_name = metadata_url.replace(
                FWUPD_DOWNLOAD_PREFIX,
                ""
            )
            self.metadata_file = os.path.join(
                FWUPD_DOM0_METADATA_DIR,
                metadata_name
            )
            self.metadata_file_jcat = self.metadata_file + '.jcat'
        else:
            self.metadata_file = FWUPD_DOM0_METADATA_FILE
            self.metadata_file_jcat = FWUPD_DOM0_METADATA_JCAT
        self.metadata_file_updatevm = self.metadata_file.replace(
            FWUPD_DOM0_METADATA_DIR,
            FWUPD_VM_METADATA_DIR
        )
        self.metadata_file_jcat_updatevm = self.metadata_file_jcat.replace(
            FWUPD_DOM0_METADATA_DIR,
            FWUPD_VM_METADATA_DIR
        )
        self._check_domain(updatevm)
        self._create_dirs(FWUPD_DOM0_METADATA_DIR)
        cmd_file = "'cat %s'" % self.metadata_file_updatevm
        cmd_jcat = "'cat %s'" % self.metadata_file_jcat_updatevm
        cmd_copy_metadata_file = 'qvm-run --pass-io %s %s > %s' % (
            updatevm,
            cmd_file,
            self.metadata_file
        )
        cmd_copy_metadata_jcat = 'qvm-run --pass-io %s %s > %s' % (
            updatevm,
            cmd_jcat,
            self.metadata_file_jcat
        )

        p = subprocess.Popen(cmd_copy_metadata_file, shell=True)
        p.wait()
        if p.returncode != 0:
            raise Exception('qvm-run: Copying metadata file failed!!')
        p = subprocess.Popen(cmd_copy_metadata_jcat, shell=True)
        p.wait()
        if p.returncode != 0:
            raise Exception('qvm-run": Copying metadata jcat failed!!')

        self._verify_received(
            FWUPD_DOM0_METADATA_DIR,
            FWUPD_METADATA_FILES_REGEX,
            updatevm
        )
        self._jcat_verification(
            self.metadata_file_jcat,
            FWUPD_DOM0_METADATA_DIR
        )
        os.umask(self.old_umask)

    def clean_cache(self, usbvm=False):
        """Removes updates data

        Keyword arguments:
        usbvm -- usbvm support flag
        """
        print("Cleaning dom0 cache directories")
        if os.path.exists(FWUPD_DOM0_METADATA_DIR):
            shutil.rmtree(FWUPD_DOM0_METADATA_DIR)
        if os.path.exists(FWUPD_DOM0_UPDATES_DIR):
            shutil.rmtree(FWUPD_DOM0_UPDATES_DIR)
        if os.path.exists(HEADS_UPDATES_DIR):
            shutil.rmtree(HEADS_UPDATES_DIR)
        if usbvm:
            print("Cleaning usbvm cache directories")
            self._clean_usbvm()

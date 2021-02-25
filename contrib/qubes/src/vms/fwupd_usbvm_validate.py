#!/usr/bin/python3
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2021  Norbert Kamiński  <norbert.kaminski@3mdeb.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

import glob
import os
import shutil
import subprocess
import sys

from fwupd_common_vm import FwupdVmCommon

FWUPD_VM_DIR = "/home/user/.cache/fwupd"
FWUPD_VM_UPDATES_DIR = os.path.join(FWUPD_VM_DIR, "updates")
FWUPD_VM_METADATA_DIR = os.path.join(FWUPD_VM_DIR, "metadata")
FWUPD_VM_METADATA_JCAT = os.path.join(
    FWUPD_VM_METADATA_DIR,
    "firmware.xml.gz.jcat"
)
FWUPD_VM_METADATA_FILE = os.path.join(
    FWUPD_VM_METADATA_DIR,
    "firmware.xml.gz"
)
FWUPDMGR = "/bin/fwupdmgr"
FWUPD_DOWNLOAD_PREFIX = "https://fwupd.org/downloads/"


class FwupdUsbvmUpdates(FwupdVmCommon):
    def _verify_received(self, files_path, regex_pattern):
        """Checks if sent files match  regex filename pattern.

        Keyword arguments:

        files_path -- absolute path to inspected directory
        regex_pattern -- pattern of the expected files
        """
        for untrusted_f in os.listdir(files_path):
            if not regex_pattern.match(untrusted_f):
                raise Exception(
                    'Dom0 sent unexpected file'
                )
            f = untrusted_f
            assert '/' not in f
            assert '\0' not in f
            assert '\x1b' not in f
            path_f = os.path.join(files_path, f)
            if os.path.islink(path_f) or not os.path.isfile(path_f):
                raise Exception(
                    'Dom0 sent not regular file'
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
        p = subprocess.Popen(cmd_extract, stdout=subprocess.PIPE)
        p.communicate()[0].decode('ascii')
        if p.returncode != 0:
            raise Exception(
                'gcab: Error while extracting %s.' %
                archive_path
            )

    def validate_metadata(self, metadata_url=None):
        """Validates received the metadata files."""
        print("Running validation of the metadata files")
        if metadata_url:
            metadata_name = metadata_url.replace(
                FWUPD_DOWNLOAD_PREFIX,
                ""
            )
            metadata_file = os.path.join(
                FWUPD_VM_METADATA_DIR,
                metadata_name
            )
        else:
            metadata_file = FWUPD_VM_METADATA_FILE
        try:
            self._jcat_verification(
                f"{metadata_file}.jcat",
                FWUPD_VM_METADATA_DIR
            )
        except Exception as e:
            print(str(e), file=sys.stderr)
            self.clean_vm_cache()
            exit(1)

    def validate_updates(self, archive_path, sha):
        """Validates recived an update file.

        Keyword arguments:
        archive_path - path to the firmware update archive
        sha -- SHA256 checksum of the firmware update archive
        """
        print("Running validation of the update archive")
        self.check_shasum(archive_path, sha)
        archive_name = archive_path.replace(f"{FWUPD_VM_UPDATES_DIR}/", "")
        output_path = archive_path.replace(".cab", "")
        arch_temp = os.path.join(output_path, archive_name)
        os.mkdir(output_path)
        shutil.copyfile(archive_path, arch_temp)
        self._extract_archive(
            arch_temp,
            output_path
        )
        signature_name = os.path.join(output_path, "firmware*.jcat")
        file_path = glob.glob(signature_name)
        try:
            self._jcat_verification(file_path[0], output_path)
            shutil.rmtree(output_path)
        except Exception as e:
            print(str(e), file=sys.stderr)
            self.clean_vm_cache()
            exit(1)


def main():
    f = FwupdUsbvmUpdates()
    f_val = FwupdVmCommon()
    metadata_url = None
    if len(sys.argv) < 2:
        raise Exception("Invalid number of arguments.")
    for arg in sys.argv:
        if "--url=" in arg:
            metadata_url = arg.replace("--url=", "")
    if sys.argv[1] == "metadata":
        f.validate_metadata(metadata_url=metadata_url)
    elif sys.argv[1] == "dirs":
        f_val.validate_vm_dirs()
    elif sys.argv[1] == "clean":
        f.clean_vm_cache()
    elif sys.argv[1] == "updates" and len(sys.argv) < 4:
        raise Exception(
            "Invalid number of arguments.\n"
            "Expected archive path and checksum."
        )
    elif sys.argv[1] == "updates" and not len(sys.argv) < 4:
        f.validate_updates(sys.argv[2], sys.argv[3])
    else:
        raise Exception("Invaild command")


if __name__ == '__main__':
    main()

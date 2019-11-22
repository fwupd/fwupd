#!/usr/bin/python3
#
# Copyright (C) 2017 Max Ehrlich max.ehr@gmail.com
#
# SPDX-License-Identifier: LGPL-2.1+
#

import argparse
import subprocess
import contextlib
import os
import shutil
import tempfile
import time


@contextlib.contextmanager
def cd(path):
    prev_cwd = os.getcwd()
    os.chdir(path)
    yield
    os.chdir(prev_cwd)

firmware_metainfo_template = """
<?xml version="1.0" encoding="UTF-8"?>
<component type="firmware">
  <id>org.{developer_name}.guid{firmware_id}</id>
  <name>{firmware_name}</name>
  <summary>{firmware_summary}</summary>
  <description>
    {firmware_description}
  </description>
  <provides>
    <firmware type="flashed">{device_guid}</firmware>
  </provides>
  <url type="homepage">{firmware_homepage}</url>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>proprietary</project_license>
  <updatecontact>{contact_info}</updatecontact>
  <developer_name>{developer_name}</developer_name>
  <releases>
    <release version="{release_version}" timestamp="{timestamp}">
      <description>
        {release_description}
      </description>
    </release>
  </releases>
</component>
"""


def make_firmware_metainfo(firmware_info, dst):
    local_info = vars(firmware_info)
    local_info["firmware_id"] = local_info["device_guid"][0:8]
    firmware_metainfo = firmware_metainfo_template.format(**local_info, timestamp=time.time())

    with open(os.path.join(dst, 'firmware.metainfo.xml'), 'w') as f:
        f.write(firmware_metainfo)


def extract_exe(exe, dst):
    command = ['7z', 'x', '-o{}'.format(dst), exe]
    subprocess.check_call(command, stdout=subprocess.DEVNULL)


def get_firmware_bin(root, bin_path, dst):
    with cd(root):
        shutil.copy(bin_path, os.path.join(dst, 'firmware.bin'))


def create_firmware_cab(exe, folder):
    with cd(folder):
        if os.name == "nt":
          directive = os.path.join (folder, "directive")
          with open (directive, 'w') as wfd:
            wfd.write('.OPTION EXPLICIT\r\n')
            wfd.write('.Set CabinetNameTemplate=firmware.cab\r\n')
            wfd.write('.Set DiskDirectory1=.\r\n')
            wfd.write('firmware.bin\r\n')
            wfd.write('firmware.metainfo.xml\r\n')
          command = ['makecab.exe', '/f', directive]
        else:
          command = ['gcab', '--create', 'firmware.cab', 'firmware.bin', 'firmware.metainfo.xml']
        subprocess.check_call(command)


def main(args):
    with tempfile.TemporaryDirectory() as dir:
        print('Using temp directory {}'.format(dir))

        if args.exe:
            print('Extracting firmware exe')
            extract_exe(args.exe, dir)

        print('Locating firmware bin')
        get_firmware_bin(dir, args.bin, dir)

        print('Creating metainfo')
        make_firmware_metainfo(args, dir)

        print('Cabbing firmware files')
        create_firmware_cab(args, dir)

        print('Done')
        shutil.copy(os.path.join(dir, 'firmware.cab'), args.out)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create fwupd packaged from windows executables')
    parser.add_argument('--firmware-name', help='Name of the firmware package can be customized (e.g. DellTBT)', required=True)
    parser.add_argument('--firmware-summary', help='One line description of the firmware package')
    parser.add_argument('--firmware-description', help='Longer description of the firmware package')
    parser.add_argument('--device-guid', help='GUID of the device this firmware will run on, this *must* match the output of one of the GUIDs in `fwupdmgr get-devices`', required=True)
    parser.add_argument('--firmware-homepage', help='Website for the firmware provider')
    parser.add_argument('--contact-info', help='Email address of the firmware developer')
    parser.add_argument('--developer-name', help='Name of the firmware developer', required=True)
    parser.add_argument('--release-version', help='Version number of the firmware package', required=True)
    parser.add_argument('--release-description', help='Description of the firmware release')
    parser.add_argument('--exe', help='(optional) Executable file to extract firmware from')
    parser.add_argument('--bin', help='Path to the .bin file (Relative if inside the executable; Absolute if outside) to use as the firmware image', required=True)
    parser.add_argument('--out', help='Output cab file path', required=True)
    args = parser.parse_args()

    main(args)

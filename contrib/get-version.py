#!/usr/bin/python3
#
# Copyright (C) 2019 Dell, Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#

import xml.etree.ElementTree as etree
import os
import subprocess

def sanitize_for_ci(version):
    if not 'CI' in os.environ:
        return version
    OS=os.getenv('OS')
    if not OS:
        return version
    if "fedora" in OS:
        return version.replace('-','.')
    return version

def get_version_git():
    try:
        version = subprocess.check_output(['git', 'describe'], stderr=subprocess.DEVNULL)
        return version.strip().decode('utf-8')
    except (subprocess.CalledProcessError, PermissionError, FileNotFoundError):
        return ''

def get_version():
    tree = etree.parse(os.path.join("data", "org.freedesktop.fwupd.metainfo.xml"))
    version = ''
    for child in tree.findall('releases'):
        for release in child:
            if not "version" in release.attrib:
                continue
            if release.attrib['version'] > version:
                version = release.attrib['version']
    return version

if __name__ == '__main__':

    version = get_version_git()
    if version:
        version = sanitize_for_ci(version)
    else:
        version = get_version()
    print(version)

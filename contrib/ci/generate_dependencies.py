#!/usr/bin/python3
#
# Copyright (C) 2017-2018 Dell, Inc.
# Copyright (C) 2020 Intel, Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#
import os
import sys
import argparse
import xml.etree.ElementTree as etree

def parse_dependencies(OS, SUBOS, requested_type):
    deps = []
    dep = ''
    directory = os.path.dirname(sys.argv[0])
    tree = etree.parse(os.path.join(directory, "dependencies.xml"))
    root = tree.getroot()
    for child in root:
        if "type" not in child.attrib or "id" not in child.attrib:
            continue
        for distro in child:
            if "id" not in distro.attrib:
                continue
            if distro.attrib["id"] != OS:
                continue
            packages = distro.findall("package")
            for package in packages:
                if SUBOS:
                    if 'variant' not in package.attrib:
                        continue
                    if package.attrib['variant'] != SUBOS:
                        continue
                if package.text:
                    dep = package.text
                else:
                    dep = child.attrib["id"]
                if child.attrib["type"] == requested_type and dep:
                    deps.append(dep)
    return deps

if __name__ == '__main__':

    try:
        import distro
        target = distro.linux_distribution()[0]
    except ModuleNotFoundError:
        target = None

    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--os",
                        default=target,
                        choices=["fedora",
                                 "centos",
                                 "flatpak",
                                 "debian",
                                 "ubuntu",
                                 "arch"],
                        help="dependencies for OS")
    args = parser.parse_args()

    target = os.getenv('OS', args.os)
    if target is None:
        print("Missing OS environment variable")
        sys.exit(1)

    _os = target.lower()
    _sub_os = ''
    split = target.split('-')
    if len(split) >= 2:
        _os, _sub_os = split[:2]
    dependencies = parse_dependencies(_os, _sub_os, "build")
    print(*dependencies, sep='\n')

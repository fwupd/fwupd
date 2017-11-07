#!/usr/bin/python3
#
# Copyright (C) 2017 Dell Inc.
#
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
import os
import sys
import xml.etree.ElementTree as etree
from collections import defaultdict

def find_build_version(root, OS, id):
    for child in root:
        if child.attrib["type"] != "build" or child.attrib['id'] != id:
            continue
        for distro in child:
            if not "id" in distro.attrib or distro.attrib["id"] != OS:
                continue
            control = distro.find("control")
            if control is None:
                return None
            version = control.find("version")
            if version is not None:
                return version
    return None

def parse_control_dependencies():
    build_deps = []
    runtime_deps = []
    inc_deps = defaultdict(list)
    dep = ''

    TARGET=os.getenv('OS')
    if TARGET == '':
        print("Missing OS environment variable")
        sys.exit(1)
    OS = TARGET
    SUBOS = ''
    split = TARGET.split('-')
    if len(split) >= 2:
        OS = split[0]
        SUBOS = split[1]

    tree = etree.parse(os.path.join(directory, "dependencies.xml"))
    root = tree.getroot()
    for child in root:
        if not "type" in child.attrib or not "id" in child.attrib:
            continue
        for distro in child:
            if not "id" in distro.attrib or distro.attrib["id"] != OS:
                continue
            control = distro.find("control")
            if control is None:
                continue
            packages = distro.findall("package")
            for package in packages:
                if SUBOS:
                    if not 'variant' in package.attrib:
                        continue
                    if package.attrib['variant'] != SUBOS:
                        continue
                if package.text:
                    dep = package.text
                else:
                    dep = child.attrib["id"]
                if child.attrib["type"] == "runtime" and dep:
                    #check if we need to lookup the associated build element
                    version = control.find("version")
                    if version is None:
                        version = find_build_version(root, OS, child.attrib["id"])
                    if version is not None:
                        dep = "Requires: %s{?_isa} %s" % (dep, version.text)
                    else:
                        dep = "Requires: %s" % dep
                    runtime_deps.append(dep)
                if child.attrib["type"] == "build" and dep:
                    dep = "BuildRequires: %s" % dep
                    version = control.find('version')
                    if version is not None:
                        dep = "%s %s" % (dep, version.text)
                    inclusion = control.find('inclusive')
                    if inclusion is None:
                        build_deps.append(dep)
                    else:
                        inc_deps[inclusion.text].append(dep)
    return (build_deps, runtime_deps, inc_deps)

directory = os.path.dirname(sys.argv[0])
if (len(sys.argv) < 3):
    print("Missing input and output file")
    sys.exit(1)

build_deps, runtime_deps, inclusive_deps = parse_control_dependencies()

input = sys.argv[1]
if not os.path.exists(input):
    print("Missing input file %s" % input)
    sys.exit(1)

with open(input, 'r') as rfd:
    lines = rfd.readlines()

build_deps.sort()
output = sys.argv[2]
fwupd_version = sys.argv[3]
with open(output, 'w') as wfd:
    for line in lines:
        if "#VERSION#" in line:
            wfd.write(line.replace("#VERSION#", fwupd_version))
        elif "#BUILD#" in line:
            wfd.write(line.replace("#BUILD#", "1"))
        elif "#ALPHATAG" in line:
            wfd.write(line.replace("#ALPHATAG#", "alpha"))
        elif "enable_dummy 0" in line:
            wfd.write(line.replace("enable_dummy 0, enable_dummy 1"))
        elif "Source0" in line:
            wfd.write("Source0:\tfwupd-%s.tar.xz\n" % fwupd_version)
        elif "#LONGDATE#" in line:
            wfd.write(line.replace("#LONGDATE#", ))
        elif line.startswith("BuildRequires: %%%DYNAMIC%%%"):
            for i in range(0, len(build_deps)):
                wfd.write("%s\n" % build_deps[i])
            wfd.write("\n")
            for item in inclusive_deps.keys():
                wfd.write("%%if 0%%{?%s}\n" % item)
                for key in inclusive_deps[item]:
                        wfd.write("%s\n" % key)
                wfd.write("%endif\n\n")
        elif line.startswith("Requires: %%%DYNAMIC%%%"):
            for i in range(0, len(runtime_deps)):
                wfd.write("%s\n" % runtime_deps[i])
        else:
            wfd.write(line)

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

def parse_control_dependencies(requested_type):
    TARGET=os.getenv('OS')
    deps = []
    dep = ''

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
            if not "id" in distro.attrib:
                continue
            if distro.attrib["id"] != OS:
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
                if child.attrib["type"] == requested_type and dep:
                    version = control.find('version')
                    if version is not None:
                        dep = "%s %s" % (dep, version.text)
                    inclusions = control.findall('inclusive')
                    if inclusions:
                        for i in range(0, len(inclusions)):
                            prefix = ''
                            suffix = ' '
                            if i == 0:
                                prefix = " ["
                            if i == len(inclusions) - 1:
                                suffix = "]"
                            dep = "%s%s%s%s" % (dep, prefix, inclusions[i].text, suffix)
                    exclusions = control.findall('exclusive')
                    if exclusions:
                        for i in range(0, len(exclusions)):
                            prefix = '!'
                            suffix = ' '
                            if i == 0:
                                prefix = " [!"
                            if i == len(exclusions) - 1:
                                suffix = "]"
                            dep = "%s%s%s%s" % (dep, prefix, exclusions[i].text, suffix)
                    deps.append(dep)
    return deps

directory = os.path.dirname(sys.argv[0])
if (len(sys.argv) < 3):
    print("Missing input and output file")
    sys.exit(1)

deps = parse_control_dependencies("build")

input = sys.argv[1]
if not os.path.exists(input):
    print("Missing input file %s" % input)
    sys.exit(1)

with open(input, 'r') as rfd:
    lines = rfd.readlines()

deps.sort()
output = sys.argv[2]
with open(output, 'w') as wfd:
    for line in lines:
        if line.startswith("Build-Depends: %%%DYNAMIC%%%"):
            wfd.write("Build-Depends:\n")
            for i in range(0, len(deps)):
                wfd.write("\t%s,\n" % deps[i])
        else:
            wfd.write(line)

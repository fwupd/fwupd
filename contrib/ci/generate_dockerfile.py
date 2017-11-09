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

def parse_dependencies(OS, SUBOS, requested_type):
    deps = []
    dep = ''
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
                    deps.append(dep)
    return deps

directory = os.path.dirname(sys.argv[0])
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

deps = parse_dependencies(OS, SUBOS, "build")

input = os.path.join(directory, "Dockerfile-%s.in" % OS)
if not os.path.exists(input):
    print("Missing input file %s for %s" % (input, OS))
    sys.exit(1)

with open(input, 'r') as rfd:
    lines = rfd.readlines()

out = os.path.join(directory, "Dockerfile")

with open(out, 'w') as wfd:
    for line in lines:
        if line.startswith("FROM %%%ARCH_PREFIX%%%"):
            if OS == "debian" and SUBOS == "i386":
                replace = SUBOS + "/"
            else:
                replace = ''
            wfd.write(line.replace("%%%ARCH_PREFIX%%%", replace))
        elif line == "%%%INSTALL_DEPENDENCIES_COMMAND%%%\n":
            if OS == "fedora":
                wfd.write("RUN dnf --enablerepo=updates-testing -y install \\\n")
            elif OS == "debian" or OS == "ubuntu":
                wfd.write("RUN apt update -qq && \\\n")
                wfd.write("\tapt install -yq --no-install-recommends\\\n")
            elif OS == "arch":
                wfd.write("RUN pacman -Syu --noconfirm \\\n")
            for i in range(0, len(deps)):
                if i < len(deps)-1:
                    wfd.write("\t%s \\\n" % deps[i])
                else:
                    wfd.write("\t%s \n" % deps[i])
        elif line == "%%%ARCH_SPECIFIC_COMMAND%%%\n":
            if OS == "debian" and SUBOS == "s390x":
                #add sources
                wfd.write('RUN cat /etc/apt/sources.list | sed "s/deb/deb-src/" >> /etc/apt/sources.list\n')
                #add new architecture
                wfd.write('RUN dpkg --add-architecture %s\n' % SUBOS)
        else:
            wfd.write(line)

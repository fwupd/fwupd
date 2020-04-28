#!/usr/bin/python3
#
# Copyright (C) 2020 Dell Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#
import os
import argparse
import xml.etree.ElementTree as etree

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("version", help="Generate news for release")
    args = parser.parse_args()

    tree = etree.parse(os.path.join("data", "org.freedesktop.fwupd.metainfo.xml"))
    root = tree.getroot()
    for release in root.iter("release"):
        if "version" not in release.attrib:
            continue
        if release.attrib["version"] != args.version:
            continue
        description = release.find("description")
        str = etree.tostring(description, encoding="unicode", method="text")
        print(str.strip())

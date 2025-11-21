#!/usr/bin/env python3
#
# Copyright 2020 Dell Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
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
        result = etree.tostring(description, encoding="unicode", method="text")
        print(result.strip())

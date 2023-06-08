#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import argparse
import xml.etree.ElementTree as ET

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-r",
        "--releases",
        type=int,
        default=5,
    )
    parser.add_argument(
        "filename_src", action="store", type=str, help="metainfo source"
    )
    parser.add_argument(
        "filename_dst", action="store", type=str, help="metainfo destination"
    )
    args = parser.parse_args()

    tree = ET.parse(args.filename_src)
    root = tree.getroot().findall("releases")[0]
    for release in root.findall("release")[args.releases :]:
        root.remove(release)

    with open(args.filename_dst, "wb") as f:
        tree.write(f, encoding="UTF-8", xml_declaration=True)

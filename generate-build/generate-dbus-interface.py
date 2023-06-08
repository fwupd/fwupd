#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import argparse
import xml.etree.ElementTree as ET


def _remove_docs(parent):

    namespaces = {"doc": "http://www.freedesktop.org/dbus/1.0/doc.dtd"}
    for node in parent.findall("doc:doc", namespaces):
        parent.remove(node)
    parent.text = ""


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("src", action="store", type=str, help="metainfo source")
    parser.add_argument("dst", action="store", type=str, help="metainfo destination")
    args = parser.parse_args()

    tree = ET.parse(args.src)
    tree.text = ""
    for node in tree.findall("interface"):
        for node_prop in node.findall("property"):
            _remove_docs(node_prop)
        for node_signal in node.findall("signal"):
            for node_arg in node_signal.findall("arg"):
                _remove_docs(node_arg)
            _remove_docs(node_signal)
        for node_method in node.findall("method"):
            for node_arg in node_method.findall("arg"):
                _remove_docs(node_arg)
            _remove_docs(node_method)
        _remove_docs(node)

    ET.indent(tree, space=" ", level=0)
    with open(args.dst, "wb") as f:
        tree.write(f, encoding="UTF-8", xml_declaration=True)

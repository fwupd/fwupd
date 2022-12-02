#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import argparse
import datetime
import sys
import json
import os

from typing import Dict, List, Any

import xml.etree.ElementTree as ET

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--version", type=str, help="fwupd version")
    parser.add_argument("-s", "--schema-version", type=str, help="schema version")
    parser.add_argument(
        "filename_dst", action="store", type=str, help="XML destination"
    )
    parser.add_argument("json_attrs", nargs="+", help="JSON attributes")
    args = parser.parse_args()

    # parse JSON
    items: List[str] = []
    for fn in sorted(args.json_attrs):
        try:
            with open(fn, "rb") as f:
                item = json.loads(f.read())
        except json.decoder.JSONDecodeError as e:
            print("failed to parse {}: {}".format(fn, str(e)))
            sys.exit(1)
        for tag in ["id", "name", "failure-results"]:
            if tag not in item:
                print("skipping {} as no {}".format(fn), tag)
                continue
        items.append(item)

    oval_definitions = ET.Element("oval_definitions")
    oval_definitions.set("xmlns", "http://oval.mitre.org/XMLSchema/oval-definitions-5")
    oval_definitions.set("xmlns:oval", "http://oval.mitre.org/XMLSchema/oval-common-5")
    oval_definitions.set(
        "xmlns:unix-def", "http://oval.mitre.org/XMLSchema/oval-definitions-5#unix"
    )
    oval_definitions.set(
        "xmlns:red-def", "http://oval.mitre.org/XMLSchema/oval-definitions-5#linux"
    )
    oval_definitions.set(
        "xmlns:ind-def",
        "http://oval.mitre.org/XMLSchema/oval-definitions-5#independent",
    )
    oval_definitions.set("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance")
    oval_definitions.set(
        "xsi:schemaLocation",
        "http://oval.mitre.org/XMLSchema/oval-common-5 oval-common-schema.xsd "
        "http://oval.mitre.org/XMLSchema/oval-definitions-5 oval-definitions-schema.xsd "
        "http://oval.mitre.org/XMLSchema/oval-definitions-5#unix unix-definitions-schema.xsd "
        "http://oval.mitre.org/XMLSchema/oval-definitions-5#linux linux-definitions-schema.xsd",
    )

    generator = ET.SubElement(oval_definitions, "generator")
    for key, value in {
        "oval:product_name": "fwupd",
        "oval:product_version": args.version,
        "oval:schema_version": args.schema_version,
        "oval:timestamp": datetime.datetime.now().isoformat(),
    }.items():
        oval = ET.SubElement(generator, key)
        oval.text = value

    definitions = ET.SubElement(oval_definitions, "definitions")
    for item in items:

        definition = ET.SubElement(definitions, "definition")
        definition.set("class", "patch")
        definition.set("id", "oval:{}:def:1".format(item["id"]))
        definition.set("version", "1")

        metadata = ET.SubElement(definition, "metadata")

        ET.SubElement(metadata, "title").text = item["name"]

        affected = ET.SubElement(metadata, "affected")
        affected.set("family", "unix")
        ET.SubElement(affected, "platform").text = "All"  # is this valid?

        if "issues" in item:
            for issue in item["issues"]:
                reference = ET.SubElement(metadata, "reference")
                reference.set("ref_id", issue)
                if issue.startswith("CVE-"):
                    reference.set(
                        "ref_url", "https://nvd.nist.gov/vuln/detail/{}".format(issue)
                    )
                    reference.set("source", "CVE")

        if "description" in item:
            ET.SubElement(metadata, "description").text = "\n".join(item["description"])

        criteria = ET.SubElement(definition, "criteria")
        criteria.set("operator", "OR")
        criterion = ET.SubElement(criteria, "criterion")
        criterion.set("comment", item["name"])
        criterion.set("test_ref", "oval:{}:tst:1".format(item["id"]))

    tests = ET.SubElement(oval_definitions, "tests")
    for item in items:

        red_def = ET.SubElement(tests, "red-def:fwupdsecattr_test")
        red_def.set("check", "at least one")
        red_def.set("comment", item["name"])
        red_def.set("id", "oval:{}:tst:1".format(item["id"]))
        red_def.set("version", "1")

        red_def_object = ET.SubElement(red_def, "red-def:object")
        red_def_object.set("object_ref", "oval:{}:obj:1".format(item["id"]))
        red_def_state = ET.SubElement(red_def, "red-def:state")
        red_def_state.set("state_ref", "oval:{}:ste:1".format(item["id"]))

    objects = ET.SubElement(oval_definitions, "objects")
    for item in items:

        red_def = ET.SubElement(objects, "red-def:fwupdsecattr_object")
        red_def.set("id", "oval:{}:obj:1".format(item["id"]))
        red_def.set("version", "1")

        red_def_stream_id = ET.SubElement(red_def, "red-def:stream-id")
        red_def_stream_id.set("datatype", "string")
        red_def_stream_id.text = format(item["id"])

    states = ET.SubElement(oval_definitions, "states")
    for item in items:

        red_def = ET.SubElement(states, "red-def:fwupdsecattr_state")
        red_def.set("id", "oval:{}:ste:1".format(item["id"]))
        red_def.set("version", "1")

        red_def_security_attr = ET.SubElement(red_def, "red-def:security-attr")
        red_def_security_attr.set("datatype", "string")
        red_def_security_attr.set("operation", "pattern match")
        red_def_security_attr.text = "|".join(
            [value for value, _ in item["failure-results"].items()]
        )

    ET.indent(oval_definitions, space=" ", level=0)
    with open(args.filename_dst, "wb") as f:
        f.write(ET.tostring(oval_definitions, encoding="utf-8", xml_declaration=True))

#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import argparse
import sys
import json
import os
from typing import Dict, List, Any

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "filename_src", action="store", type=str, help="markdown source"
    )
    parser.add_argument(
        "filename_dst", action="store", type=str, help="markdown destination"
    )
    parser.add_argument("json_attrs", nargs="+", help="JSON attributes")
    args = parser.parse_args()

    with open(args.filename_src, "rb") as f:
        template = f.read()

    txt: List[str] = []
    for fn in sorted(args.json_attrs):
        try:
            with open(fn, "rb") as f:
                item = json.loads(f.read())
        except json.decoder.JSONDecodeError as e:
            print("failed to parse {}: {}".format(fn, str(e)))
            sys.exit(1)
        if "id" not in item:
            print("skipping {} as no id".format(fn))
            continue
        txt += ['<a id="{}"></a>'.format(item["id"])]
        if "deprecated-ids" in item:
            for deprecated_id in item["deprecated-ids"]:
                txt += ['<a id="{}"></a>'.format(deprecated_id)]
        if "name" in item:
            txt += ["### [{}](#{})".format(item["name"], item["id"])]
        if "description" in item:
            for para in item["description"]:
                txt += [para]
        if "failure-impact" in item:
            txt += ["**Impact:**"]
            for para in item["failure-impact"]:
                txt += [para]
        if "failure-results" in item and "success-results" in item:
            txt += ["**Possible results:**"]
            tmp: List[str] = []
            for value, desc in item["failure-results"].items():
                tmp += ["- `{}`: {} (failure)".format(value, desc)]
            for value, desc in item["success-results"].items():
                tmp += ["- `{}`: {} (success)".format(value, desc)]
            txt += ["\n".join(tmp)]
        if "hsi-level" in item and "fwupd-version" in item:
            txt += [
                "A test success result is needed to meet HSI-{} on "
                "systems that run this test. *[v{}]*".format(
                    item["hsi-level"], item["fwupd-version"]
                )
            ]
        if "resolution" in item:
            txt += ["**Resolution:** {}".format(item["resolution"])]
        if "issues" in item:
            txt += ["**Issues:**"]
            tmp: List[str] = []
            for issue in item["issues"]:
                if issue.startswith("CVE-"):
                    tmp += [
                        "- [{}](https://nvd.nist.gov/vuln/detail/{})".format(
                            issue, issue
                        )
                    ]
                else:
                    tmp += ["- {}".format(issue)]
            txt += ["\n".join(tmp)]
        if "references" in item:
            txt += ["**References:**"]
            tmp: List[str] = []
            for url, title in item["references"].items():
                tmp += ["- [{}]({})".format(title, url)]
            txt += ["\n".join(tmp)]
        if "requires" in item:
            txt += ["**Hardware requirements:**"]
            if "CPUID\\VID_GenuineIntel" in item["requires"]:
                txt += ["This attribute will only be available when using Intel CPUs."]
            elif "CPUID\\VID_AuthenticAMD" in item["requires"]:
                txt += ["This attribute will only be available when using AMD CPUs."]
        if "more-information" in item:
            txt += ["**More information:**"]
            for para in item["more-information"]:
                txt += [para]

    with open(args.filename_dst, "wb") as f:
        f.write(template.decode().replace("{{tests}}", "\n\n".join(txt)).encode())

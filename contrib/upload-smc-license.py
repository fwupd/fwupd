#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import sys
import argparse
from urllib import request, parse
import base64
import ssl

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-u",
        "--user",
        type=str,
        help="Redfish user name",
        default="ADMIN",
    )
    parser.add_argument(
        "-p",
        "--password",
        type=str,
        help="Redfish user password",
        default="ADMIN",
    )
    parser.add_argument(
        "-f",
        "--file",
        type=str,
        help="License file",
    )
    parser.add_argument("hostname", type=str, help="BMC IP of hostname")

    args = parser.parse_args()
    license = ""
    if len(sys.argv) == 1:
        print("hostname required")
        sys.exit(1)
    if not args.file:
        print("License:")
        license = sys.stdin.read()
    else:
        with open(args.file, "r") as fd:
            license = fd.read()

    sslctx = ssl.create_default_context()
    sslctx.check_hostname = False
    sslctx.verify_mode = ssl.CERT_NONE
    url = (
        f"https://{args.hostname}/redfish/v1/Managers/1/LicenseManager/ActivateLicense"
    )
    auth = str(
        base64.b64encode(bytes(f"{args.user}:{args.password}", "latin1")),
        encoding="latin1",
    )
    headers = {
        "Content-Type": "application/json",
        "Authorization": f"Basic {auth}",
    }
    req = request.Request(url, headers=headers, data=bytes(license, "latin1"))
    resp = request.urlopen(req, context=sslctx)

    if resp.status < 300:
        print("Success")
    else:
        print("Failed")
        sys.exit(-1)

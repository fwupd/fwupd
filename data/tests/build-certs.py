#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import os
import sys
import subprocess
import tempfile
from datetime import datetime, timedelta, UTC


def _build_certs(path: str):
    # expire in 7 days to avoid people using these in production
    dt_activation = (datetime.now(UTC) - timedelta(days=1)).isoformat()
    dt_expiration = (datetime.now(UTC) + timedelta(days=7)).isoformat()

    # certificate authority
    ca = "fwupd"
    ca_privkey = os.path.join(path, f"{ca}-CA.key")
    ca_certificate = os.path.join(path, f"{ca}-CA.pem")
    if not os.path.exists(ca_privkey):
        print("generating private key...")
        argv = ["certtool", "--generate-privkey", "--outfile", ca_privkey]
        rc = subprocess.run(argv, check=True)
        if rc.returncode != 0:
            return 1
    if not os.path.exists(ca_certificate):
        print("generating self-signed certificate...")

        # build config
        lines = []
        lines.append('organization = "fwupd"')
        lines.append('cn = "fwupd CA"')
        lines.append('uri = "http://fwupd.org/"')
        lines.append('email = "admin@fwupd.org"')
        lines.append('crl_dist_points = "http://fwupd.org/pki/"')
        lines.append("serial = 1")
        lines.append("crl_number = 1")
        lines.append("path_len = 1")
        lines.append('activation_date = "{}"'.format(dt_activation))
        lines.append('expiration_date = "{}"'.format(dt_expiration))
        lines.append("ca")
        lines.append("cert_signing_key")
        lines.append("crl_signing_key")
        lines.append("code_signing_key")
        cfg = tempfile.NamedTemporaryFile(
            mode="w", prefix="cert_", suffix=".cfg", dir=None, delete=True
        )
        cfg.write("\n".join(lines))
        cfg.flush()
        argv = [
            "certtool",
            "--generate-self-signed",
            "--load-privkey",
            ca_privkey,
            "--template",
            cfg.name,
            "--outfile",
            ca_certificate,
        ]
        rc = subprocess.run(argv, check=True)
        if rc.returncode != 0:
            return 1

    # per-user key
    user = "testuser"
    user_privkey = os.path.join(path, f"{user}.key")
    user_certificate = os.path.join(path, f"{user}.pem")
    user_certificate_signed = os.path.join(path, f"{user}_signed.pem")
    user_request = os.path.join(path, f"{user}.csr")

    # build config
    lines = []
    lines.append('cn = "Test Key"')
    lines.append('uri = "https://fwupd.org/testuser"')
    lines.append('email = "testuser@fwupd.org"')
    lines.append('activation_date = "{}"'.format(dt_activation))
    lines.append('expiration_date = "{}"'.format(dt_expiration))
    lines.append("signing_key")
    lines.append("code_signing_key")
    cfg = tempfile.NamedTemporaryFile(
        mode="w", prefix="cert_", suffix=".cfg", dir=None, delete=True
    )
    cfg.write("\n".join(lines))
    cfg.flush()

    if not os.path.exists(user_privkey):
        print("generating user private key...")
        argv = [
            "certtool",
            "--generate-privkey",
            "--rsa",
            "--bits",
            "2048",
            "--outfile",
            user_privkey,
        ]
        rc = subprocess.run(argv, check=True)
        if rc.returncode != 0:
            return 1
    if not os.path.exists(user_certificate):
        print("generating self-signed certificate...")
        argv = [
            "certtool",
            "--generate-self-signed",
            "--rsa",
            "--bits",
            "2048",
            "--load-privkey",
            user_privkey,
            "--template",
            cfg.name,
            "--outfile",
            user_certificate,
        ]
        rc = subprocess.run(argv, check=True)
        if rc.returncode != 0:
            return 1

    if not os.path.exists(user_request):
        print("generating certificate...")
        argv = [
            "certtool",
            "--generate-request",
            "--load-privkey",
            user_privkey,
            "--template",
            cfg.name,
            "--outfile",
            user_request,
        ]
        rc = subprocess.run(argv, check=True)
        if rc.returncode != 0:
            return 1

    # sign the user
    if not os.path.exists(user_certificate_signed):
        print("generating CA-signed certificate...")
        argv = [
            "certtool",
            "--generate-certificate",
            "--rsa",
            "--bits",
            "2048",
            "--load-request",
            user_request,
            "--load-ca-certificate",
            ca_certificate,
            "--load-ca-privkey",
            ca_privkey,
            "--template",
            cfg.name,
            "--outfile",
            user_certificate_signed,
        ]
        rc = subprocess.run(argv, check=True)
        if rc.returncode != 0:
            return 1

    # success
    return 0


if __name__ == "__main__":
    sys.exit(_build_certs(sys.argv[1]))

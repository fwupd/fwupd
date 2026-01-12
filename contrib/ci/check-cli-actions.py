#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright 2025 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import subprocess
import sys

if __name__ == "__main__":
    rc: int = 0

    # load the manpage, bash-completion, etc
    data: dict[str, str] = {}
    for fn in [
        "src/fwupdmgr.md",
        "data/bash-completion/fwupdmgr",
        "data/fish-completion/fwupdmgr.fish",
    ]:
        with open(fn, "rb") as f:
            data[fn] = f.read().decode()

    # if we can't run the binary for some reason, assume everything is okay
    try:
        pr = subprocess.run(
            ["venv/build/src/fwupdmgr", "get-actions", "--force"],
            cwd=".",
            # stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            encoding="utf-8",
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        sys.exit(0)

    # check each action is at least mentioned in each file
    for fn, txt in data.items():
        for action in pr.stdout.split("\n"):
            if txt.find(action) == -1:
                print(f"* CLI action {action} not documented in {fn}")
                rc = 1
    sys.exit(rc)

#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import sys
import markdown

from packaging.version import Version

# https://github.com/fwupd/fwupd/pull/3337#issuecomment-858947695
if Version(markdown.__version__) < Version("3.3.3"):
    print("python3-markdown version 3.3.3 required for gi-docgen")
    sys.exit(1)

# success
sys.exit(0)

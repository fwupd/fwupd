#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import sys
import markdown

from distutils.version import LooseVersion

# https://github.com/fwupd/fwupd/pull/3337#issuecomment-858947695
if LooseVersion(markdown.version) < LooseVersion("3.3.3"):
    print("python3-markdown version 3.3.3 required for gi-docgen")
    sys.exit(1)

# success
sys.exit(0)

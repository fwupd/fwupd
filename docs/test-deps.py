#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import sys

# https://github.com/fwupd/fwupd/pull/3337#issuecomment-858947695
MINIMUM_VERSION = "3.3.3"


def error():
    print("python3-markdown version %s required for gi-docgen" % MINIMUM_VERSION)
    sys.exit(1)


try:
    import markdown
except ImportError:
    error()

try:
    from packaging.version import Version

    if Version(markdown.__version__) < Version(MINIMUM_VERSION):
        error()
except ImportError:
    from distutils.version import LooseVersion

    if LooseVersion(markdown.version) < LooseVersion(MINIMUM_VERSION):
        error()

# success
sys.exit(0)

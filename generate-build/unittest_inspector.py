#! /usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2020 Canonical Ltd
#
# Authors:
#       Marco Trevisan <marco.trevisan@canonical.com>

import argparse
import importlib.util
import inspect
import os
import unittest


def list_tests(module):
    tests = []
    for name, obj in inspect.getmembers(module):
        if inspect.isclass(obj) and issubclass(obj, unittest.TestCase):
            cases = unittest.defaultTestLoader.getTestCaseNames(obj)
            tests += [(obj, f"{name}.{t}") for t in cases]
    return tests


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("unittest_source", type=argparse.FileType("r"))

    args = parser.parse_args()
    source_path = args.unittest_source.name
    spec = importlib.util.spec_from_file_location(
        os.path.basename(source_path), source_path
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    for machine, human in list_tests(module):
        print(human)

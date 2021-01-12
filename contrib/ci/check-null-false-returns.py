#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring,too-many-branches,too-many-statements
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import sys


def parse_rvif(line):

    # remove whitespace
    line = line.replace("\t", "")
    line = line.replace(" ", "")

    # find value
    line = line.replace(",", "|")
    line = line.replace(")", "|")
    return line.rsplit("|", maxsplit=2)[1]


def test_rvif(fn, line_num, valid_values, line):

    # parse "g_return_val_if_fail (SOMETHING (foo), NULL)"
    value = parse_rvif(line)

    # enumerated enum, so ignore
    if value.find("_") != -1:
        return True

    # convert alternate forms back to normal
    if value in ["0x0", "0x00", "0x0000"]:
        value = "0"
    if value in ["0xffffffff"]:
        value = "G_MAXUINT32"
    if value in ["0xffff"]:
        value = "G_MAXUINT16"
    if value in ["0xff"]:
        value = "G_MAXUINT8"
    if value in ["G_SOURCE_REMOVE"]:
        value = "FALSE"
    if value in ["G_SOURCE_CONTINUE"]:
        value = "TRUE"
    if value not in valid_values:
        print(
            "{} line {} got {}, expected {}".format(
                fn, line_num, value, ", ".join(valid_values)
            )
        )
        return False

    # success
    return True


def test_file(fn):
    allokay = True
    with open(fn) as f:
        valid_values = None
        line_num = 0
        for line in f.readlines():
            line_num += 1
            line = line.rstrip()
            if not line:
                continue
            idx = line.find("g_return_val_if_fail")
            if idx != -1 and valid_values:
                if not test_rvif(fn, line_num, valid_values, line):
                    allokay = False
                continue

            # not a function header
            if line[0] in ["#", " ", "\t", "{", "}", "/"]:
                continue

            # label
            if line.endswith(":"):
                continue

            # remove static prefix
            if line.startswith("static"):
                line = line[7:]

            # a pointer
            if line.endswith("*"):
                valid_values = ["NULL"]
                continue

            # not a leading line
            if line.find(" ") != -1:
                continue

            # a type we know
            if line in ["void"]:
                valid_values = []
                continue
            if line in ["gpointer"]:
                valid_values = ["NULL"]
                continue
            if line in ["gboolean"]:
                valid_values = ["TRUE", "FALSE"]
                continue
            if line in ["guint32"]:
                valid_values = ["0", "G_MAXUINT32"]
                continue
            if line in ["GQuark", "GType"]:
                valid_values = ["0"]
                continue
            if line in ["guint64"]:
                valid_values = ["0", "G_MAXUINT64"]
                continue
            if line in ["guint16"]:
                valid_values = ["0", "G_MAXUINT16"]
                continue
            if line in ["guint8"]:
                valid_values = ["0", "G_MAXUINT8"]
                continue
            if line in ["gint64"]:
                valid_values = ["0", "-1", "G_MAXINT64"]
                continue
            if line in ["gint32"]:
                valid_values = ["0", "-1", "G_MAXINT32"]
                continue
            if line in ["gint16"]:
                valid_values = ["0", "-1", "G_MAXINT16"]
                continue
            if line in ["gint8"]:
                valid_values = ["0", "-1", "G_MAXINT8"]
                continue
            if line in ["gint", "int"]:
                valid_values = ["0", "-1", "G_MAXINT"]
                continue
            if line in ["guint"]:
                valid_values = ["0", "G_MAXUINT"]
                continue
            if line in ["gulong"]:
                valid_values = ["0", "G_MAXLONG"]
                continue
            if line in ["gsize", "size_t"]:
                valid_values = ["0", "G_MAXSIZE"]
                continue
            if line in ["gssize", "ssize_t"]:
                valid_values = ["0", "-1", "G_MAXSSIZE"]
                continue
            # print('unknown return type {}'.format(line))
            valid_values = None

    # global success
    return allokay


def test_files():

    # test all C source files
    rc = 0
    for fn in glob.glob("**/*.c", recursive=True):
        if not test_file(fn):
            rc = 1
    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())

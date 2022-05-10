#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring,too-many-branches
# pylint: disable=too-many-statements,too-many-return-statements,too-few-public-methods
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import sys
from typing import List


def _tokenize(line: str) -> List[str]:

    # remove whitespace
    line = line.strip()
    line = line.replace("\t", "")
    line = line.replace(";", "")

    # find value
    line = line.replace(" ", "|")
    line = line.replace(",", "|")
    line = line.replace(")", "|")
    line = line.replace("(", "|")

    # return empty tokens
    tokens = []
    for token in line.rsplit("|"):
        if token:
            tokens.append(token)
    return tokens


class ReturnValidator:
    def __init__(self):
        self.warnings: List[str] = []

        # internal state
        self._fn = None
        self._line_num = None
        self._value = None
        self._nret = None
        self._rvif = None
        self._line = None

    @property
    def _tokens(self) -> List[str]:
        return _tokenize(self._line)

    @property
    def _value_relaxed(self) -> str:
        if self._value in ["0x0", "0x00", "0x0000"]:
            return "0"
        if self._value in ["0xffffffff"]:
            return "G_MAXUINT32"
        if self._value in ["0xffff"]:
            return "G_MAXUINT16"
        if self._value in ["0xff"]:
            return "G_MAXUINT8"
        if self._value in ["G_SOURCE_REMOVE"]:
            return "FALSE"
        if self._value in ["G_SOURCE_CONTINUE"]:
            return "TRUE"
        return self._value

    def _test_rvif(self) -> None:

        # parse "g_return_val_if_fail (SOMETHING (foo), NULL);"
        self._value = self._tokens[-1]

        # enumerated enum, so ignore
        if self._value.find("_") != -1:
            return

        # is invalid
        if self._rvif and self._value_relaxed not in self._rvif:
            self.warnings.append(
                "{} line {} got {}, expected {}".format(
                    self._fn, self._line_num, self._value, ", ".join(self._rvif)
                )
            )

    def _test_return(self) -> None:

        # parse "return 0x0;"
        self._value = self._tokens[-1]

        # is invalid
        if self._nret and self._value_relaxed in self._nret:
            self.warnings.append(
                "{} line {} got {}, which is not valid".format(
                    self._fn, self._line_num, self._value
                )
            )

    def parse(self, fn: str) -> None:

        self._fn = fn
        with open(fn) as f:
            self._rvif = None
            self._nret = None
            self._line_num = 0
            for line in f.readlines():
                self._line_num += 1
                line = line.rstrip()
                if not line:
                    continue
                if line.endswith("\\"):
                    continue
                if line.endswith("&&"):
                    continue
                self._line = line
                idx = line.find("g_return_val_if_fail")
                if idx != -1:
                    self._test_rvif()
                    continue
                idx = line.find("return")
                if idx != -1:
                    # continue
                    if len(self._tokens) == 2:
                        self._test_return()
                    continue

                # not a function header
                if line[0] in ["#", " ", "\t", "{", "}", "/"]:
                    continue

                # label
                if line.endswith(":"):
                    continue

                # remove prefixes
                if line.startswith("static"):
                    line = line[7:]
                if line.startswith("inline"):
                    line = line[7:]

                # a pointer
                if line.endswith("*"):
                    self._rvif = ["NULL"]
                    self._nret = ["FALSE"]
                    continue

                # not a leading line
                if line.find(" ") != -1:
                    continue

                # a type we know
                if line in ["void"]:
                    self._rvif = []
                    self._nret = []
                    continue
                if line in ["gpointer"]:
                    self._rvif = ["NULL"]
                    self._nret = ["FALSE"]
                    continue
                if line in ["gboolean"]:
                    self._rvif = ["TRUE", "FALSE"]
                    self._nret = ["NULL", "0"]
                    continue
                if line in ["guint32"]:
                    self._rvif = ["0", "G_MAXUINT32"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["GQuark", "GType"]:
                    self._rvif = ["0"]
                    self._nret = ["NULL", "0", "TRUE", "FALSE"]
                    continue
                if line in ["guint64"]:
                    self._rvif = ["0", "G_MAXUINT64"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["guint16"]:
                    self._rvif = ["0", "G_MAXUINT16"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["guint8"]:
                    self._rvif = ["0", "G_MAXUINT8"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gint64"]:
                    self._rvif = ["0", "-1", "G_MAXINT64"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gint32"]:
                    self._rvif = ["0", "-1", "G_MAXINT32"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gint16"]:
                    self._rvif = ["0", "-1", "G_MAXINT16"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gint8"]:
                    self._rvif = ["0", "-1", "G_MAXINT8"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gint", "int"]:
                    self._rvif = ["0", "-1", "G_MAXINT"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["guint"]:
                    self._rvif = ["0", "G_MAXUINT"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gulong"]:
                    self._rvif = ["0", "G_MAXLONG"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gsize", "size_t"]:
                    self._rvif = ["0", "G_MAXSIZE"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                if line in ["gssize", "ssize_t"]:
                    self._rvif = ["0", "-1", "G_MAXSSIZE"]
                    self._nret = ["NULL", "TRUE", "FALSE"]
                    continue
                # print('unknown return type {}'.format(line))
                self._rvif = None
                self._nret = None


def test_files():

    # test all C source files
    validator = ReturnValidator()
    for fn in glob.glob("**/*.c", recursive=True):
        if fn.startswith("dist/") or fn.startswith("subprojects/"):
            continue
        validator.parse(fn)
    for warning in validator.warnings:
        print(warning)

    return 1 if validator.warnings else 0


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())

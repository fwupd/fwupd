#!/usr/bin/python3
#
# Copyright (C) 2017 Dell Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#

import argparse
import os
import re
import sys
import subprocess

CLANG_FORMATTERS = ['clang-format-6.0', 'clang-format-5.0', 'clang-format-4.0']
FIXUPS = {'g_autoptr (' : 'g_autoptr(',
          'sizeof ('    : 'sizeof(',
          'g_auto ('    : 'g_auto('
         }

def parse_args():
    parser = argparse.ArgumentParser(description="Reformat code to match style")
    parser.add_argument("files", nargs='*', help="files to reformat")
    args = parser.parse_args()
    if (len(args.files) == 0):
        parser.print_help()
        sys.exit (0)
    return args

def select_clang_version ():
        for formatter in CLANG_FORMATTERS:
                try:
                        ret = subprocess.check_call ([formatter, '--version'])
                        if (ret == 0):
                                return formatter
                except FileNotFoundError:
                        continue
        return None

# workaround until https://reviews.llvm.org/D27651 is in a version of clang
def fix_pointer (string):
        result = ''
        move = None
        hints = 0
        if 'const' in string:
                hints += 1
        if '=' in string:
                hints += 1
        if 'uint' in string:
                hints += 1
        if 'struct' in string:
                hints += 1
        parts = re.split(r'(\S+)', string)
        #simple case of whitespace type whitespace * whitespace definition
        if len(parts) < 8:
                hints += 1
        for part in parts:
                if hints > 0 and part is '*':
                        move = part
                else:
                        if move and part.strip():
                                result += move
                                move = None
                        result += part
        return result

def reformat_file (formatter, f):
        print ("Reformatting %s using %s" % (f, formatter))
        ret = subprocess.check_call ([formatter, '-i', '-style=file', f])
        lines = None
        with open (f, 'r') as rfd:
                lines = rfd.readlines()
        with open (f, 'w') as wfd:
                comment = False
                for line in lines:
                        for fixup in FIXUPS:
                                if fixup in line:
                                        line = line.replace(fixup, FIXUPS[fixup])
                        if '/*' in line:
                                comment = True
                        if '*/' in line:
                                comment = False
                        if not comment and (' * ' in line or (' *	') in line):
                                line = fix_pointer (line)
                        wfd.write (line)

## Entry Point ##
if __name__ == '__main__':
        args = parse_args()
        formatter = select_clang_version ()
        if not formatter:
                print ("No clang formatter installed")
                sys.exit (1)
        for f in args.files:
                if not os.path.exists(f):
                        print ("%s does not exist" % f)
                        sys.exit(1)
                if not (f.endswith (".c") or f.endswith ('.h')):
                        print ("Skipping %s" % f)
                        continue
                reformat_file (formatter, f)
        sys.exit (0)

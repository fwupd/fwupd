#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import sys
import os
import subprocess

def _do_msgattrib(fn):
    argv = ['msgattrib',
            '--no-location',
            '--translated',
            '--no-wrap',
            '--sort-output',
            fn,
            '--output-file=' + fn]
    ret = subprocess.run(argv)
    if ret.returncode != 0:
        return

def _do_nukeheader(fn):
    clean_lines = []
    with open(fn) as f:
        lines = f.readlines()
    for line in lines:
        if line.startswith('"POT-Creation-Date:'):
            continue
        if line.startswith('"PO-Revision-Date:'):
            continue
        if line.startswith('"Last-Translator:'):
            continue
        clean_lines.append(line)
    with open(fn, 'w') as f:
        f.writelines(clean_lines)

def _process_file(fn):
    _do_msgattrib(fn)
    _do_nukeheader(fn)

if __name__ == '__main__':
    if len(sys.argv) == 1:
        print('path required')
        sys.exit(1)
    try:
        dirname = sys.argv[1]
        for fn in os.listdir(dirname):
            if fn.endswith('.po'):
                _process_file(os.path.join(dirname, fn))
    except NotADirectoryError as _:
        print('path required')
        sys.exit(2)

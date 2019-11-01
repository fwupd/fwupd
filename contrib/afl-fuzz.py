#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import argparse
import sys
import subprocess
import os

def main():
    parser = argparse.ArgumentParser(description='Run afl-fuzz on all cores')
    parser.add_argument('--input', '-i', help='fuzzing input directory')
    parser.add_argument('--output', '-o', help='findings output directory')
    parser.add_argument('--command', type=str, help='fuzzer tool command')
    parser.add_argument('path', type=str, help='the fuzzer tool')
    args = parser.parse_args()
    if not args.input and not args.output:
        print('-i and -o required')
        return 1
    if not args.path:
        print('tool name required')
        return 1

    # create if not already exists
    if not os.path.exists(args.output):
        os.makedirs(args.output)

    # run the main instance
    envp = None
    argv = ['afl-fuzz', '-m300', '-i', args.input, '-o', args.output,
            '-M', 'fuzzer00', args.path]
    if args.command:
        argv.append(args.command)
    argv.append('@@')
    print(argv)
    p = subprocess.Popen(argv, env=envp)

    # run the secondary instances
    cs = []
    for i in range(1, os.cpu_count()):
        argv = ['afl-fuzz', '-m300', '-i', args.input, '-o', args.output,
                '-S', 'fuzzer%02i' % i, args.path]
        if args.command:
            argv.append(args.command)
        argv.append('@@')
        print(argv)
        cs.append(subprocess.Popen(argv, env=envp, stdout=subprocess.DEVNULL))

    # wait for the main instance
    try:
        p.wait()
    except KeyboardInterrupt as _:
        pass
    for c in cs:
        c.terminate()
    return 0

if __name__ == '__main__':
    sys.exit(main())

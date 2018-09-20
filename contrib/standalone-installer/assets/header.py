#!/usr/bin/python3
#
# Copyright (C) 2017 Dell, Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#
from base64 import b64decode
import io
import os
import subprocess
import sys
import shutil
import tempfile
import zipfile
TAG = b'#\x00'

def parse_args():
    import argparse
    parser = argparse.ArgumentParser(description="Self extracting firmware updater")
    parser.add_argument("--directory", help="Directory to extract to")
    parser.add_argument("--uninstall", action='store_true', help="Uninstall tools when done with installation")
    parser.add_argument("--verbose", action='store_true', help="Run the tool in verbose mode")
    parser.add_argument("command", choices=["install", "extract"], help="Command to run")
    args = parser.parse_args()
    return args

def error (msg):
    print(msg)
    sys.exit(1)

def bytes_slicer(length, source):
    start = 0
    stop = length
    while start < len(source):
        yield source[start:stop]
        start = stop
        stop += length

def get_zip():
    script = os.path.realpath (__file__)
    bytes_out = io.BytesIO()
    with open(script, 'rb') as source:
        for line in source:
            if not line.startswith(TAG):
                continue
            bytes_out.write(b64decode(line[len(TAG):-1]))
    return bytes_out

def unzip (destination):
    zipf = get_zip ()
    source = zipfile.ZipFile (zipf, 'r')
    for item in source.namelist():
        # extract handles the sanitization
        source.extract (item, destination)

def copy_cabs (source, target):
    if not os.path.exists (target):
        os.makedirs (target)
    cabs = []
    for root, dirs, files in os.walk (source):
        for f in files:
            if (f.endswith ('.cab')):
                origf = os.path.join(root, f)
                shutil.copy (origf, target)
                cabs.append (os.path.join (target, f))
    return cabs


def install_snap (directory, verbose, uninstall):
    app = 'fwupd'
    common = '/root/snap/%s/common' % app

    #check if snap is installed
    with open(os.devnull, 'w') as devnull:
        subprocess.run (['snap'], check=True, stdout=devnull, stderr=devnull)

    #check existing installed
    cmd = ['snap', 'list', app]
    with open(os.devnull, 'w') as devnull:
        if verbose:
            print(cmd)
        ret = subprocess.run (cmd, stdout=devnull, stderr=devnull)
        if ret.returncode == 0:
            cmd = ['snap', 'remove', app]
            if verbose:
                print(cmd)
            subprocess.run (cmd, check=True)

    # install the snap
    cmd = ['snap', 'ack', os.path.join (directory, 'fwupd.assert')]
    if verbose:
        print(cmd)
    subprocess.run (cmd, check=True)
    cmd = ['snap', 'install', '--classic', os.path.join (directory, 'fwupd.snap')]
    if verbose:
        print(cmd)
    subprocess.run (cmd, check=True)

    # copy the CAB files
    cabs = copy_cabs (directory, common)

    # run the snap
    for cab in cabs:
        cmd = ["%s.fwupdmgr" % app, 'install', cab]
        if verbose:
            cmd += ["--verbose"]
            print(cmd)
        subprocess.run (cmd)

    #remove copied cabs
    for f in cabs:
        os.remove(f)

    #cleanup
    if uninstall:
        cmd = ['snap', 'remove', app]
        if verbose:
            print(cmd)
        subprocess.run (cmd)

def install_flatpak (directory, verbose, uninstall):
    app = 'org.freedesktop.fwupd'
    common = '%s/.var/app/%s' % (os.getenv ('HOME'), app)

    with open(os.devnull, 'w') as devnull:
        if not verbose:
            output = devnull
        else:
            output = None
        #look for dependencies
        dep = 'org.gnome.Platform/x86_64/3.28'
        repo = 'flathub'
        repo_url = 'https://flathub.org/repo/flathub.flatpakrepo'
        cmd = ['flatpak', 'info', dep]
        if verbose:
            print(cmd)
        ret = subprocess.run (cmd, stdout=output, stderr=output)
        #not installed
        if ret.returncode != 0:
            #look for remotes
            cmd = ['flatpak', 'remote-info', repo, dep]
            if verbose:
                print(cmd)
            ret = subprocess.run (cmd, stdout=output, stderr=output)
            #not enabled, enable it
            if ret.returncode != 0:
                cmd = ['flatpak', 'remote-add', repo, repo_url]
                if verbose:
                    print(cmd)
                ret = subprocess.run (cmd, stderr=output)
            # install dep
            cmd = ['flatpak', 'install', repo, dep]
            if verbose:
                print(cmd)
            ret = subprocess.run (cmd)

        #check existing installed
        cmd = ['flatpak', 'info', app]
        if verbose:
            print(cmd)
        ret = subprocess.run (cmd, stdout=output, stderr=output)
        if ret.returncode == 0:
            cmd = ['flatpak', 'remove', app]
            if verbose:
                print(cmd)
            subprocess.run (cmd, check=True)

    #install the flatpak
    cmd = ['flatpak', 'install', os.path.join (directory, 'fwupd.flatpak')]
    if verbose:
        print(cmd)
    subprocess.run (cmd, check=True)

    # copy the CAB files
    cabs = copy_cabs (directory, common)

    #run command
    for cab in cabs:
        cmd = ['flatpak', 'run', app, 'install', cab]
        if verbose:
            cmd += ["--verbose"]
            print(cmd)
        subprocess.run (cmd)

    #remove copied cabs
    for f in cabs:
        os.remove(f)

    #cleanup
    if uninstall:
        cmd = ['flatpak', 'remove', app]
        if verbose:
            print(cmd)
        subprocess.run (cmd)


def run_installation (directory, verbose, uninstall):
    try_snap = False
    try_flatpak = False

    # determine what self extracting binary has
    if os.path.exists (os.path.join (directory, 'fwupd.snap')) and \
        os.path.exists (os.path.join (directory, 'fwupd.assert')):
        try_snap = True
    if os.path.exists (os.path.join (directory, 'fwupd.flatpak')):
        try_flatpak = True

    if try_snap:
        try:
            install_snap (directory, verbose, uninstall)
            return True
        except:
            if verbose:
                print ("Snap installation failed")
            if not try_flatpak:
                error ("Snap installation failed")
    if try_flatpak:
        install_flatpak (directory, verbose, uninstall)

if __name__ == '__main__':
    args = parse_args()
    if 'extract' in args.command:
        if args.uninstall:
            error ("Uninstall argument doesn't make sense with command %s" % args.command)
        if args.directory is None:
            error ("No directory specified")
        if not os.path.exists (args.directory):
            print ("Creating %s" % args.directory)
            os.makedirs (args.directory)
        unzip (args.directory)
    else:
        if args.directory:
            error ("Directory argument %s doesn't make sense with command %s" % (args.directory, args.command))
        if os.getuid() != 0:
            error ("This tool must be run as root")
        with tempfile.TemporaryDirectory (prefix='fwupd') as target:
            unzip (target)
            run_installation (target, args.verbose, args.uninstall)

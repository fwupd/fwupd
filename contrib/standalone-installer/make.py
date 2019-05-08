#!/usr/bin/python3
#
# Copyright (C) 2017 Dell, Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#
from base64 import b64encode
import io
import os
import subprocess
import shutil
import sys
import tempfile
import zipfile
from assets.header import TAG

def error (msg):
    print(msg)
    sys.exit(1)

def parse_args():
    import argparse
    parser = argparse.ArgumentParser(description="Generate a standalone firmware updater")
    parser.add_argument("--disable-snap-download", action='store_true', help="Don't download support for snap")
    parser.add_argument("--disable-flatpak-download", action='store_true', help="Don't download support for flatpak")
    parser.add_argument("--snap-channel", help="Channel to download snap from (optional)")
    parser.add_argument("--minimum", help="Use already installed fwupd version if at least this version")
    parser.add_argument("cab", help="CAB file or directory containing CAB files to automatically install")
    parser.add_argument('target', help='target file to create')
    args = parser.parse_args()
    return args

def bytes_slicer(length, source):
    start = 0
    stop = length
    while start < len(source):
        yield source[start:stop]
        start = stop
        stop += length

def generate_installer (directory, target):
    asset_base = os.path.join (os.path.dirname(os.path.realpath(__file__)),
                               "assets")

    #header
    shutil.copy (os.path.join (asset_base, "header.py"), target)

    #zip file
    buffer = io.BytesIO()
    archive = zipfile.ZipFile(buffer, "a")
    for root, dirs, files in os.walk (directory):
        for f in files:
            source = os.path.join(root, f)
            archive_fname = source.split (directory) [1]
            archive.write(source, archive_fname)
    if 'DEBUG' in os.environ:
        print (archive.namelist())
    archive.close()

    with open (target, 'ab') as bytes_out:
        encoded = b64encode(buffer.getvalue())
        for section in bytes_slicer(64, encoded):
            bytes_out.write(TAG)
            bytes_out.write(section)
            bytes_out.write(b'\n')

def download_snap (directory, channel):
    cmd = ['snap', 'download', 'fwupd']
    if channel is not None:
        cmd += ['--channel', channel]
    if 'DEBUG' in os.environ:
        print(cmd)
    subprocess.run (cmd, cwd=directory, check=True)
    for f in os.listdir (directory):
        # the signatures associated with the snap
        if f.endswith(".assert"):
            shutil.move (os.path.join(directory, f), os.path.join(directory, 'fwupd.assert'))
        # the snap binary itself
        elif f.endswith(".snap"):
            shutil.move (os.path.join(directory, f), os.path.join(directory, 'fwupd.snap'))

def download_cab_file (directory, uri):
    cmd = ['wget', uri]
    if 'DEBUG' in os.environ:
        print(cmd)
    subprocess.run (cmd, cwd=directory, check=True)

def download_flatpak (directory):
    dep = 'org.freedesktop.fwupd'
    flatpak_dir = os.path.join(os.getenv('HOME'),'.local', 'share', 'flatpak')
    verbose = 'DEBUG' in os.environ

    #check if we have installed locally already or not
    if not os.path.exists (os.path.join (flatpak_dir, 'app', dep)):
        # install into local user's repo
        cmd = ['flatpak', 'install', '--user',
            'https://www.flathub.org/repo/appstream/org.freedesktop.fwupd.flatpakref', '--no-deps', '-y']
        if verbose:
            print(cmd)
        subprocess.run (cmd, cwd=directory, check=True)

    # generate a bundle
    repo = os.path.join(flatpak_dir, 'repo')
    cmd = ['flatpak', 'build-bundle', repo, 'fwupd.flatpak', dep, 'stable']
    if verbose:
        print(cmd)
    subprocess.run (cmd, cwd=directory, check=True)

if __name__ == '__main__':
    args = parse_args()

    if not args.cab.startswith("http"):
        local = args.cab

    with tempfile.TemporaryDirectory (prefix='fwupd') as directory:
        if local:
            if not os.path.exists (local):
                error ("%s doesn't exist" % local)
            if not os.path.isdir(local):
                shutil.copy (local, directory)
            else:
                for root, dirs, files in os.walk(local):
                    for f in files:
                        shutil.copy (os.path.join(root, f), directory)
        else:
            download_cab_file (directory, args.cab)

        if not args.disable_snap_download:
            download_snap (directory, args.snap_channel)

        if not args.disable_flatpak_download:
            download_flatpak (directory)

        if args.minimum:
            with open(os.path.join(directory, "minimum"), "w") as wfd:
                wfd.write(args.minimum)

        generate_installer (directory, args.target)

#!/usr/bin/python3
import subprocess
import os
import json
import shutil

def prepare (target):
    #clone the flatpak json
    cmd = ['git', 'submodule', 'update', '--remote', 'contrib/flatpak']
    subprocess.run (cmd, check=True)

    #clone the submodules for that
    cmd = ['git', 'submodule', 'update', '--init', '--remote', 'shared-modules/']
    subprocess.run (cmd, cwd='contrib/flatpak', check=True)

    #parse json
    if os.path.isdir ('build'):
        shutil.rmtree ('build')
    data = {}
    with open ('contrib/flatpak/org.freedesktop.fwupd.json', 'r') as rfd:
        data = json.load (rfd, strict=False)
    platform = 'runtime/%s/x86_64/%s' % (data['runtime'], data['runtime-version'])
    sdk = 'runtime/%s/x86_64/%s' % (data['sdk'], data['runtime-version'])
    num_modules = len (data['modules'])

    #update to build from master
    data["branch"] = "master"
    for index in range(0, num_modules):
        module = data['modules'][index]
        if type (module) != dict or not 'name' in module:
            continue
        name = module['name']
        if not 'fwupd' in name:
            continue
        data['modules'][index]['sources'][0].pop ('url')
        data['modules'][index]['sources'][0].pop ('sha256')
        data['modules'][index]['sources'][0]['type'] = 'dir'
        data['modules'][index]['sources'][0]['skip'] = [".git"]
        data['modules'][index]['sources'][0]['path'] = ".."

    #write json
    os.mkdir('build')
    with open (target, 'w') as wfd:
        json.dump(data, wfd, indent=4)
    os.symlink ('../contrib/flatpak/shared-modules','build/shared-modules')

    # install the runtimes (parsed from json!)
    repo = 'flathub'
    repo_url = 'https://dl.flathub.org/repo/flathub.flatpakrepo'
    print ("Installing dependencies")
    cmd = ['flatpak', 'remote-add', '--if-not-exists', repo, repo_url]
    subprocess.run (cmd, check=True)
    cmd = ['flatpak', 'install', '--assumeyes', repo, sdk]
    subprocess.run (cmd, check=True)
    cmd = ['flatpak', 'install', '--assumeyes', repo, platform]
    subprocess.run (cmd, check=True)


def build (target):
    cmd = ['flatpak-builder', '--repo=repo', '--force-clean', '--disable-rofiles-fuse', 'build-dir', target]
    subprocess.run (cmd, check=True)
    cmd = ['flatpak', 'build-bundle', 'repo', 'fwupd.flatpak', 'org.freedesktop.fwupd']
    subprocess.run (cmd, check=True)

if __name__ == '__main__':
    t = os.path.join ('build', 'org.freedesktop.fwupd.json')
    prepare (t)
    build (t)

# to run from the builddir:
# sudo flatpak-builder --run build-dir org.freedesktop.fwupd.json /app/libexec/fwupd/fwupdtool get-devices

# install the single file bundle
# flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
# flatpak install fwupd.flatpak

# to run a shell in the same environment that flatpak sees:
# flatpak run --command=sh --devel org.freedesktop.fwupd

# to run fwupdtool as root:
# sudo flatpak run org.freedesktop.fwupd --verbose get-devices

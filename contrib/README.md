# Distribution packages

The relevant packaging necessary to generate *RPM*, *DEB* and *PKG* distribution packages is contained here.
It is used regularly for continuous integration using [Travis CI](http://travis-ci.org). The generated packages can be used on a distribution such as Fedora, Debian, Ubuntu or Arch Linux.

The build can be performed using Linux containers with [Docker](https://www.docker.com).

## RPM packages

A Dockerfile for Fedora can be generated in `contrib`.

To prepare the Docker container run this command:

```shell
./generate_docker.py --distro fedora --version 44 --arch amd64 build
```

To build the RPMs run this command (from the root of your git checkout):

```shell
docker run --privileged -t -v `pwd`:/github/workspace fwupd-fedora
```

RPMs will be made available in your working directory when complete.

To build additional RPM packages for Qubes OS (fwupd-qubes-dom0 and
fwupd-qubes-vm) add `QUBES=true` environment variable:

```shell
docker run --privileged -e QUBES=true  -t -v `pwd`:/github/workspace fwupd-fedora
```

## DEB packages

A Dockerfile for Debian or Ubuntu can be generated in `contrib`.

To prepare the Docker container run one of these commands:

```shell
./generate_docker.py --distro debian --version testing --arch amd64 build
./generate_docker.py --distro debian --version testing --arch amd64 --variant i386 build
./generate_docker.py --distro ubuntu --version rolling --arch amd64 build
```

To build the DEBs run one of these commands (from the root of your git checkout):

```shell
docker run --privileged -t -v `pwd`:/github/workspace fwupd-debian-x86_64
docker run --privileged -t -v `pwd`:/github/workspace fwupd-debian-i386
docker run --privileged -t -v `pwd`:/github/workspace fwupd-ubuntu-x86_64
```

DEBs will be made available in your working directory when complete.

To build additional DEB package for Qubes OS (fwupd-qubes-vm-whonix)
add `QUBES=true` environment variable:

```shell
docker run --privileged -t -v `pwd`:/github/workspace fwupd-debian-x86_64-qubes
```

## PKG packages

A Dockerfile for Arch can be generated in `contrib`.

To prepare the Docker container run this command:

```shell
./generate_docker.py --distro arch --version latest --arch amd64 build
```

To build the PKGs run this command (from the root of your git checkout):

```shell
docker run -t -v `pwd`:/build fwupd-arch
```

PKGs will be made available in your working directory when complete.

## Additional packages

Submissions for generating additional packages for other distribution mechanisms are also welcome.
All builds should occur in Docker containers.

Please feel free to submit the following:

* Dockerfile for the container for your distro
* Relevant technical packaging scripts (such as ebuilds, spec file etc)
* A shell script that can be launched in the container to generate distribution packages

Distribution packages
=====================
The relevant packaging necessary to generate *RPM*, *DEB* and *PKG* distribution packages is contained here.
It is used regularly for continuous integration using [Travis CI](http://travis-ci.org). The generated packages can be used on a distribution such as Fedora, Debian, Ubuntu or Arch Linux.

The build can be performed using Linux containers with [Docker](www.docker.com).

## RPM packages
A Dockerfile for Fedora can be generated in `contrib`.

To prepare the Docker container run this command:
```OS=fedora ./generate_dockerfile.py; docker build -t fwupd-fedora -f contrib/ci/Dockerfile .```

`docker build -t fwupd-fedora -f contrib/ci/Dockerfile-fedora .`

To build the RPMs run this command (from the root of your git checkout):

```docker run -t -v `pwd`:/build fwupd-fedora ./contrib/ci/build_and_install_rpms.sh```

RPMs will be made available in your working directory when complete.

## DEB packages
A Dockerfile for Debian or Ubuntu can be generated in `contrib`.

To prepare the Docker container run this command:

```OS=debian ./generate_dockerfile.py; docker build -t fwupd-debian -f contrib/ci/Dockerfile .```

To build the DEBs run this command (from the root of your git checkout):

```docker run -t -v `pwd`:/build fwupd-debian ./contrib/ci/build_and_install_debs.sh```

DEBs will be made available in your working directory when complete.

## PKG packages
A Dockerfile for Arch can be generated in `contrib`.

To prepare the Docker container run this command:

```OS=arch ./generate_dockerfile.py; docker build -t fwupd-arch -f contrib/ci/Dockerfile .```

To build the PKGs run this command (from the root of your git checkout):

```docker run -t -v `pwd`:/build fwupd-arch ./contrib/ci/build_and_install_pkgs.sh```

PKGs will be made available in your working directory when complete.

## Additional packages
Submissions for generating additional packages for other distribution mechanisms are also welcome.  
All builds should occur in Docker containers.

Please feel free to submit the following:
* Dockerfile for the container for your distro
* Relevant technical packaging scripts (such as ebuilds, spec file etc)
* A shell script that can be launched in the container to generate distribution packages

Distribution packages
=====================
The relevant packaging necessary to generate *RPM* and *DEB* distribution packages is contained here.
It is used regularly for continuous integration using [Travis CI](http://travis-ci.org). The generated packages can be used on a distribution such as Fedora, Debian or Ubuntu.

The build can be performed using Linux containers with [Docker](www.docker.com).

## RPM packages
A Dockerfile for Fedora 25 is available here in `contrib`.

To prepare the Docker container run this command:

`docker build -t fwupd-fedora-25 -f contrib/ci/Dockerfile-fedora-25 .`

To build the RPMs run this command (from the root of your git checkout):

```docker run -t -v `pwd`:/build fwupd-fedora-25 ./contrib/ci/build_and_install_rpms.sh```

RPMs will be made available in your working directory when complete.

## DEB packages
A Dockerfile is available for Debian unstable and Debian experimental.
*(Currently)* builds can only be performed in Debian experimental due to dependencies not yet available in Debian unstable.

To prepare the Docker container run this command:

`docker build -t fwupd-debian-experimental -f contrib/ci/Dockerfile-debian-experimental .`

To build the DEBs run this command (from the root of your git checkout):

```docker run -t -v `pwd`:/build fwupd-debian-experimental ./contrib/ci/build_and_install_debs.sh```

DEBs will be made available in your working directory when complete.

To use the packages, you may need to enable the experimental repository for dependency resolution.
Additional information is available here: https://wiki.debian.org/DebianExperimental

## Additional packages
Submissions for generating additional packages for other distribution mechanisms are also welcome.  
All builds should occur in Docker containers.

Please feel free to submit the following:
* Dockerfile for the container for your distro
* Relevant technical packaging scripts (such as ebuilds, spec file etc)
* A shell script that can be launched in the container to generate distribution packages

Continuous Integration
======================
Continuous integration for fwupd is provided by [Travis CI](https://travis-ci.org/hughsie/fwupd).

By using Travis CI, builds are exercised across a variety of environments attempting to maximize code coverage.
For every commit or pull request 5 builds are performed:

Fedora
------

* A fully packaged RPM build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.

Debian unstable (gcc)
------

* A fully packaged DEB build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.
* All packages are removed

Debian unstable (clang)
------

* A fully packaged DEB build with all plugins enabled
* Compiled under clang
* Tests without -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.
* All packages are removed

Debian unstable (cross compile s390x)
------

* Not packaged
* Compiled under gcc
* Tests with -Werror enabled
* Runs local test suite using qemu-user

Arch Linux
----------

* A fully packaged pkg build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Compile with the deprecated USB plugin enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed


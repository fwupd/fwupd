Continuous Integration
======================
Continuous integration for fwupd is provided by [Travis CI](https://travis-ci.org/hughsie/fwupd).

By using Travis CI, builds are exercised across a variety of environments attempting to maximize code coverage.
For every commit or pull request 5 builds are performed:

Fedora (x86_64)
------

* A fully packaged RPM build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.

Debian testing (x86_64)
------

* A fully packaged DEB build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.
* All packages are removed

Debian testing (i386)
------

* A fully packaged DEB build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.
* All packages are removed

Ubuntu devel release (x86_64)
------

* A fully packaged DEB build with all plugins enabled
* Compiled under clang
* Tests without -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.
* All packages are removed

Debian testing (cross compile s390x)
------

* Not packaged
* No redfish support
* Compiled under gcc
* Tests with -Werror enabled
* Runs local test suite using qemu-user

Arch Linux (x86_64)
----------

* A fully packaged pkg build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Compile with the deprecated USB plugin enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed


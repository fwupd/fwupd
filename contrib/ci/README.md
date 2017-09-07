Continuous Integration
======================
Continuous integration for fwupd is provided by (Travis CI)[www.travis-ci.org].

By using Travis CI, builds are exercised across a variety of environments attempting to maximize code coverage.
For every commit or pull request 3 builds are performed:

Fedora
------

* A fully packaged RPM build with all plugins enabled
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.

Debian unstable
------

* A fully packaged DEB build with all plugins enabled
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.
* All packages are removed

Arch Linux
------

* A fully packaged pkg build with all plugins enabled
* Tests with -Werror enabled
* Compile with the deprecated USB plugin enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed

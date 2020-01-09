Continuous Integration
======================
Continuous integration for fwupd is provided by [Travis CI](https://travis-ci.org/fwupd/fwupd).

By using Travis CI, builds are exercised across a variety of environments attempting to maximize code coverage.
For every commit or pull request 6 builds are performed:

Fedora (x86_64)
------

* A fully packaged RPM build with all plugins enabled
* Compiled under gcc with AddressSanitizer
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS.
* With modem manager disabled

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
* Tests for missing translation files
* No redfish support
* Compiled under gcc
* Tests with -Werror enabled
* Runs local test suite using qemu-user
* Modem manager disabled

Arch Linux (x86_64)
----------

* A fully packaged pkg build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Compile with the deprecated USB plugin enabled
* Tests with the built in local test suite for all plugins.
* All packages are installed

Flatpak
----------
* A flatpak bundle with all plugins enabled
* Compiled under gcc with the org.gnome.Sdk/x86_64/3.28 runtime
* Builds without the daemon, so only fwupdtool is available
* No GPG, PKCS-7, GObjectIntrospection, systemd or ConsoleKit support
* No tests

Adding a new target
===================
Dockerfiles are generated dynamically by the python script ```generate_dockerfile.py```.
The python script will recognize the environment variable ***OS*** to determine what target to generate a Dockerfile for.

dependencies.xml
----------------
Initially the python script will read in ___dependencies.xml___ to generate a dependency list for that target.
The XML is organized by a top level element representing the dependencies needed for building fwupd.

The child elements represent individual dependencies for all distributions.
* This element has an attribute ***id*** that represents the most common package name used by distributions
* This element has an attribute ***type*** that represents if the package is needed at build time or runtime.

Each dependency then has a mapping to individual distributions (___distro___).
* This element has an attribute ***id*** that represents the distribution.

Each distribution will have ***package*** elements and ***control*** elements.
***Package*** elements represent the name of the package needed for the distribution.
* An optional attribute ***variant*** represents one deviation of that distribution.  For example building a specific architecture or with a different compiler.
* If the ***package*** element is empty the ***id*** of the ___<dependency>___ element will be used.
***Control*** elements represent specific requirements associated to a dependency. They will contain elements with individual details.
* ___version___ elements represent a minimum version to be installed
* ___inclusive___ elements represent an inclusive list of architectures to be installed on
* ___exclusive___ elements represent an exclusive list of architectures to not be installed on

For convenience there is also a standalone script __generate_dependencies.py__ that parses ___dependencies.xml___.

Dockerfile.in
-------------
The ***Dockerfile.in*** file will be used as a template to build the container.  No hardcoded dependencies should be put in this file.  They should be stored in ***dependencies.xml***.

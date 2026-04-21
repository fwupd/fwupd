# Continuous Integration

By using CI, builds are exercised across a variety of environments attempting to maximize code coverage.
The following builds are performed for every commit or pull request:

## Library build (x86_64)

* A minimal library-only build used to generate a source tarball for RPM-based distributions
* Compiled under gcc on Ubuntu
* Produces a versioned `.tar.xz` source archive passed to downstream container builds

## Fedora (x86_64)

* A fully packaged RPM build with all plugins enabled
* Compiled under gcc with AddressSanitizer and UndefinedBehaviorSanitizer
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS
* With modem manager enabled

## CentOS Stream 9 (x86_64)

* A fully packaged RPM build
* Compiled under gcc
* All packages are installed
* An installed testing run to check the daemon can start

## Debian testing (x86_64)

* A fully packaged DEB build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS
* All packages are removed

## Debian testing (ARM64)

* A fully packaged DEB build with all plugins enabled
* Compiled under gcc on a native ARM64 runner
* Tests with the built in local test suite for all plugins
* All packages are installed
* An installed testing run with the "test" plugin and pulling from LVFS
* All packages are removed

## Debian testing (cross compile i386)

* A cross-compiled DEB build targeting i386
* Compiled under gcc on an x86_64 runner with cross-compilation toolchain
* No unit tests (disabled for cross builds)
* All packages are built and installed

## Debian testing (cross compile s390x)

* A cross-compiled DEB build targeting s390x
* Compiled under gcc on an ARM64 runner with cross-compilation toolchain
* No unit tests (disabled for cross builds)
* All packages are built and installed

## Ubuntu rolling (x86_64)

* Not packaged — built directly with meson
* Compiled under clang
* Tests with the built in local test suite for all plugins
* Coverage report generated
* Documentation built and published
* RSS memory and CPU usage checks

## Arch Linux (x86_64)

* A fully packaged pkg build with all plugins enabled
* Compiled under gcc
* Tests with -Werror enabled
* Tests with the built in local test suite for all plugins
* All packages are installed
* Qt5 threading test compiled and run
* TPM simulator tests using swtpm

## macOS (x86_64)

* Built with meson using Homebrew dependencies
* No tests
* Only fwupdtool is available (no systemd)

## Windows (x86_64)

* Cross-compiled using MinGW64 on a Fedora container
* Produces a `.msi` installer package
* Installer is smoke-tested with Wine

## FreeBSD

* Built using the venv-based build wrapper
* Compiled under gcc
* No installed tests

## Snap

* A Snap package build
* Tested with installed-tests suite
* Deployed to the Snap Store edge or candidate channel on release

## OpenBMC

* A minimal meson build targeting the OpenBMC environment
* Compiled on Ubuntu

## Adding a new target

Dockerfiles are generated dynamically by the python script `generate_docker.py`.
The python script will recognize the environment variable `TARGET_DISTRO` to determine what target to generate a Dockerfile for.

### dependencies.xml

Initially the python script will read in `dependencies.xml` to generate a dependency list for that target.
The XML is organized by a top level element representing the dependencies needed for building fwupd.

The child elements represent individual dependencies for all distributions.

* This element has an attribute `id` that represents the most common package name used by distributions
* This element has an attribute `type` that represents if the package is needed at build time or runtime.

Each dependency then has a mapping to individual distributions (`distro`).

* This element has an attribute `id` that represents the distribution.

Each distribution will have `package` elements and `control` elements.
`Package` elements represent the name of the package needed for the distribution.

* An optional attribute `variant` represents one deviation of that distribution.  For example building a specific architecture or with a different compiler.
* If the `package` element is empty the `id` of the `dependency` element will be used.
* `Control` elements represent specific requirements associated to a dependency. They will contain elements with individual details.
* `version` elements represent a minimum version to be installed
* `inclusive` elements represent an inclusive list of architectures to be installed on
* `exclusive` elements represent an exclusive list of architectures to not be installed on

For convenience there is also a helper script `./contrib/ci/fwupd_setup_helpers.py install-dependencies` that parses `dependencies.xml`.

### Dockerfile.in

The `Dockerfile.in` file will be used as a template to build the container.  No hardcoded dependencies should be put in this file.  They should be stored in `dependencies.xml`.

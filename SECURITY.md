# Security Policy

Due to the nature of what we are doing, fwupd takes security very seriously.
If you have any concerns please let us know.

## Supported Versions

The `1.2.x` and `1.1.x` branches are fully supported by the upstream authors.
Additionally, the `1.0.x` branch is supported for security and bug fixes.

Older releases than this are unsupported by upstream but may be supported by
your distributor or distribution. If you open an issue with one of these older
releases the very first question from us is going to be asking if it's fixed on
a supported branch. You can use the flatpak or snap packages if your distributor
is unwilling to update to a supported version.

| Version | Supported          |
| ------- | ------------------ |
| 1.2.x   | :heavy_check_mark: |
| 1.1.x   | :heavy_check_mark: |
| 1.0.x   | :white_check_mark: |
| 0.9.x   | :x:                |
| 0.8.x   | :x:                |

## Reporting a Vulnerability

If you find a vulnerability in fwupd your first thing you should do is email
all the maintainers, which are currently listed in the `MAINTAINERS` file in
this repository.

Failing that, please report the issue against the `fwupd` component in Red Hat
bugzilla, with the security checkbox set. You should get a response within 3
days. We have no bug bountry program, but we're happy to credit you in updates
if this is what you would like us to do.

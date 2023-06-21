# Security Policy

Due to the nature of what we are doing, fwupd takes security very seriously.
If you have any concerns please let us know.

## Supported Versions

The `main`, and `1.8.x`, branches are fully supported by the upstream authors
with all unstable code belonging in `wip` branches.
Additionally, the `1.6.x` and `1.7.x` branches are supported for security fixes.

| Version | Supported          |
| ------- | ------------------ |
| 1.9.x   | :heavy_check_mark: |
| 1.8.x   | :heavy_check_mark: |
| 1.7.x   | :white_check_mark: |
| 1.6.x   | :white_check_mark: |
| 1.5.x   | :x: EOL 2022-01-01 |
| 1.4.x   | :x: EOL 2021-05-01 |
| 1.3.x   | :x: EOL 2020-07-01 |
| 1.2.x   | :x: EOL 2019-12-01 |
| 1.1.x   | :x: EOL 2018-11-01 |
| 1.0.x   | :x: EOL 2018-10-01 |
| 0.9.x   | :x: EOL 2018-02-01 |

Older releases than this are unsupported by upstream but may be supported by
your distributor or distribution. If you open an issue with one of these older
releases the very first question from us is going to be asking if it's fixed on
a supported branch. You can use the flatpak or snap packages if your distributor
is unwilling to update to a supported version.

## Reporting a Vulnerability

If you find a vulnerability in fwupd your first thing you should do is email
all the maintainers, which are currently listed in the `MAINTAINERS` file in
this repository.

Failing that, please report the issue against the `fwupd` component in Red Hat
bugzilla, with the security checkbox set. You should get a response within 3
days. We have no bug bounty program, but we're happy to credit you in updates
if this is what you would like us to do.

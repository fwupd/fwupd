# Snap support

Snaps are containerised software packages that are simple to create and install. They auto-update and are safe to run. And because they bundle their dependencies, they work on all major Linux systems without modification.

## stable vs unstable
Two yaml files are distributed:

* snapcraft.yaml
This uses tarball releases for all dependencies and what is currently in tree for fwupd.

* snapcraft-master.yaml
This uses git for most dependencies and may be considered unstable.

# Building

Builds can be performed using snapcraft:

```
# snapcraft cleanbuild
```

# Installing

A "classic" snap is produced, and locally built snaps can be installed like this:

```
# snap install fwupd_daily_amd64.snap --dangerous --classic
```

The `--dangerous` flag is because snaps built locally are not signed.
Snaps distributed by a store will not need this flag.

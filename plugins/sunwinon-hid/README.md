# How to use

- Clone from [fwupd official repository](https://github.com/fwupd/fwupd.git).
- Choose a branch other than dev branch. If you use a Linux desktop, you can check fwupd version of your own machine by running `fwupdmgr --version` command, then choose the version tag same as your local fwupd.
- Make sure this directory and fwupd directory are in the same parent directory, so it looks like below:

```
parent_directory/
  ├── fwupd/
  └── sunwinon-hid/
```

- Call `./install-into-fwupd.sh` script to copy files into fwupd directory.
- Build fwupd by following instructions in fwupd official repository.
- `fwupdtool get-devices` to detect devices, `fwupdtool install-blob <path-to-fw>` to install raw firmware without check.
- `--plugins sunwinon-hid` to enable this plugin only.
- `--verbose` to enable displaying details, `--verbose --verbose` to enable debug details.

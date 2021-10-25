# Whonix support

The qubes-fwupd uses the sys-whonix VM as the update VM to handle downloading
updates and metadata via Tor. The tests detect if sys-whonix is running, but
do not check if you are connected with Tor. So before running the test make sure
that sys-whonix has access to the network.

## Refresh

```shell
sudo qubes-fwupdmgr refresh --whonix
```

## Update

```shell
sudo qubes-fwupdmgr update --whonix
```

## Downgrade

```shell
sudo qubes-fwupdmgr downgrade --whonix

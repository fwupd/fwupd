# Message of the day integration

Message on the day integration is used to display the availability of updates when connecting to a remote console.

It has two elements:
* Automatic firmware metadata refresh
* Message of the day display

## Automatic firmware metadata refresh
This uses a systemd timer to run on a regular cadence.
To enable this, run
```
# systemctl enable fwupd-refresh.timer
```

## Motd display
Motd display is dependent upon the availability of the update-motd snippet consumption service such as pam_motd.

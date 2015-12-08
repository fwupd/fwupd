#!/usr/bin/python

# Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
# Licensed under the GNU General Public License Version 2

import gi
gi.require_version('Dfu', '1.0')

from gi.repository import Dfu
from gi.repository import Gio

cancellable = Gio.Cancellable.new()
ctx = Dfu.Context.new()
ctx.enumerate()

for dev in ctx.get_devices():

    # print details about the device
    dev.open(Dfu.DeviceOpenFlags.NONE, cancellable)
    print "getting firmware from %s:%s" % (dev.get_state(), dev.get_status())

    # transfer firmware from device to host and show summary
    fw = dev.upload(Dfu.TargetTransferFlags.DETACH, cancellable)
    print fw.to_string()

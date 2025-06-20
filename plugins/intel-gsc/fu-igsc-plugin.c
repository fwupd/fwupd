/*
 * Copyright 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR Apache-2.0
 */

#include "config.h"

#include "fu-igsc-aux-device.h"
#include "fu-igsc-aux-firmware.h"
#include "fu-igsc-code-firmware.h"
#include "fu-igsc-device.h"
#include "fu-igsc-oprom-device.h"
#include "fu-igsc-oprom-firmware.h"
#include "fu-igsc-plugin.h"

// TODO: Identify the correct location we should store this.
#define RECOVERY_CAB = "/var/lib/fwupd/metadata/recovery/"

struct _FuIgscPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIgscPlugin, fu_igsc_plugin, FU_TYPE_PLUGIN)

static void
fu_igsc_plugin_init(FuIgscPlugin *self)
{
}

static void
fu_igsc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_IGSC_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_IGSC_OPROM_DEVICE); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_IGSC_AUX_DEVICE);   /* coverage */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_CODE_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_AUX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_OPROM_FIRMWARE);
}

static void
fu_igsc_plugin_device_changed(FuPlugin *plugin, FuDevice *device)
{
	const gchar *wedged = NULL;
	if (FU_IS_UDEV_DEVICE(device))
    		wedged = fu_udev_device_read_property(FU_UDEV_DEVICE(device), "WEDGED");
	if (wedged && g_strcmp0(wedged, "firmware-flash") == 0) {
	    	fu_device_set_version(device, "0.0");
	    	fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_RECOVERY_REQUIRED);
	    	// Optionally, copy .cab to /var/lib/fwupd/metadata/recovery/ here
	    	g_warning("Detected WEDGED=firmware-flash uevent. Device is now in recovery mode
	    	      	  "(version 0.0). You can recover by running:\n fwupdmgr update\n"
		      	  "If you have a recovery .cab, it is available in %s", RECOVERY_CAB);
	}
}
//	// Check for the WEDGED property in device metadata
//	const gchar *wedged = fu_device_get_metadata(device, "WEDGED");
//	if (wedged && g_strcmp0(wedged, "firmware-flash") == 0) {
//		// Prompt user to recover manually
//		g_warning("Detected WEDGED=firmware-flash uevent. Install recovery firmware that "
//			  "is included in the cab under /recovery with:\n/usr/bin/fwuptool install "
//			  "<recovery-firmware.bin>\nAfter installing recovery firmware, you must "
//			  "shutdown and reboot (cold boot) to apply changes.");
//		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_RECOVERY_REQUIRED);
//	  	// TODO: Explore automated recovery options.
//	}
//}

static void
fu_igsc_plugin_class_init(FuIgscPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_igsc_plugin_constructed;
	plugin_class->device_changed = fu_igsc_plugin_device_changed;
}

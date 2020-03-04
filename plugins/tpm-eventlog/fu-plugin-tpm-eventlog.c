/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hash.h"
#include "fu-plugin-vfuncs.h"

#include "fu-tpm-eventlog-device.h"

struct FuPluginData {
	GPtrArray		*pcr0s;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_BEFORE, "uefi");
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	if (data->pcr0s != NULL)
		g_ptr_array_unref (data->pcr0s);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	gsize bufsz = 0;
	const gchar *fn = "/sys/kernel/security/tpm0/binary_bios_measurements";
	g_autofree gchar *str = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuTpmEventlogDevice) dev = NULL;

	if (!g_file_get_contents (fn, (gchar **) &buf, &bufsz, error))
		return FALSE;
	if (bufsz == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to read data from %s", fn);
		return FALSE;
	}
	dev = fu_tpm_eventlog_device_new (buf, bufsz, error);
	if (dev == NULL)
		return FALSE;
	if (!fu_device_setup (FU_DEVICE (dev), error))
		return FALSE;

	/* save this so we can compare against system-firmware */
	data->pcr0s = fu_tpm_eventlog_device_get_checksums (dev, 0, error);
	if (data->pcr0s == NULL)
		return FALSE;
	for (guint i = 0; i < data->pcr0s->len; i++) {
		const gchar *csum = g_ptr_array_index (data->pcr0s, i);
		fu_device_add_checksum (FU_DEVICE (dev), csum);
	}

	/* add optional report metadata */
	str = fu_tpm_eventlog_device_report_metadata (dev);
	g_debug ("using TPM event log report data of:\n%s", str);
	fu_plugin_add_report_metadata (plugin, "TpmEventLog", str);
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GPtrArray *checksums;

	/* only care about UEFI devices from ESRT */
	if (g_strcmp0 (fu_device_get_plugin (device), "uefi") != 0)
		return;

	/* only the system-firmware device gets checksums */
	checksums = fu_device_get_checksums (device);
	if (checksums->len == 0)
		return;
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index (checksums, i);
		for (guint j = 0; j < data->pcr0s->len; j++) {
			const gchar *checksum_tmp = g_ptr_array_index (data->pcr0s, j);
			if (g_strcmp0 (checksum, checksum_tmp) == 0) {
				g_debug ("TPM reconstructed event log matched PCR0 reading");
				return;
			}
		}
	}

	/* urgh, this is unexpected */
	fu_device_set_update_error (device,
				    "TPM PCR0 differs from reconstruction, "
				    "please see https://github.com/fwupd/fwupd/wiki/TPM-PCR0-differs-from-reconstruction");
}

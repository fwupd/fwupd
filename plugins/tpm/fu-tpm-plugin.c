/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-tpm-plugin.h"
#include "fu-tpm-v1-device.h"
#include "fu-tpm-v2-device.h"

struct _FuTpmPlugin {
	FuPlugin parent_instance;
	FuTpmDevice *tpm_device;
	FuDevice *bios_device;
	FuTpmEventlog *eventlog;
};

G_DEFINE_TYPE(FuTpmPlugin, fu_tpm_plugin, FU_TYPE_PLUGIN)

static void
fu_tpm_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	if (self->tpm_device != NULL) {
		fwupd_codec_string_append(str,
					  idt,
					  "TpmDevice",
					  fu_device_get_id(self->tpm_device));
	}
	if (self->bios_device != NULL) {
		fwupd_codec_string_append(str,
					  idt,
					  "BiosDevice",
					  fu_device_get_id(self->bios_device));
	}
}

static void
fu_tpm_plugin_set_bios_pcr0s(FuPlugin *plugin)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(GPtrArray) pcr0s = NULL;

	if (self->tpm_device == NULL)
		return;
	if (self->bios_device == NULL)
		return;

	/* add all the PCR0s */
	pcr0s = fu_tpm_device_get_checksums(self->tpm_device, 0);
	if (pcr0s->len == 0)
		return;
	for (guint i = 0; i < pcr0s->len; i++) {
		const gchar *checksum = g_ptr_array_index(pcr0s, i);
		fu_device_add_checksum(self->bios_device, checksum);
	}
	fu_device_add_flag(self->bios_device, FWUPD_DEVICE_FLAG_CAN_VERIFY);
}

/* set the PCR0 as the device checksum */
static void
fu_tpm_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE)) {
		g_set_object(&self->bios_device, device);
		fu_tpm_plugin_set_bios_pcr0s(plugin);
	}
}

static void
fu_tpm_plugin_device_added(FuPlugin *plugin, FuDevice *dev)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(GPtrArray) pcr0s = NULL;
	const gchar *family = fu_tpm_device_get_family(FU_TPM_DEVICE(dev));

	g_set_object(&self->tpm_device, FU_TPM_DEVICE(dev));
	if (family != NULL)
		fu_plugin_add_report_metadata(plugin, "TpmFamily", family);

	/* ensure */
	fu_tpm_plugin_set_bios_pcr0s(plugin);

	/* add extra plugin metadata */
	pcr0s = fu_tpm_device_get_checksums(self->tpm_device, 0);
	for (guint i = 0; i < pcr0s->len; i++) {
		const gchar *csum = g_ptr_array_index(pcr0s, i);
		GChecksumType csum_type = fwupd_checksum_guess_kind(csum);
		if (csum_type == G_CHECKSUM_SHA1) {
			fu_plugin_add_report_metadata(plugin, "Pcr0_SHA1", csum);
			continue;
		}
		if (csum_type == G_CHECKSUM_SHA256) {
			fu_plugin_add_report_metadata(plugin, "Pcr0_SHA256", csum);
			continue;
		}
		if (csum_type == G_CHECKSUM_SHA384) {
			fu_plugin_add_report_metadata(plugin, "Pcr0_SHA384", csum);
			continue;
		}
	}
}

static void
fu_tpm_plugin_add_security_attr_version(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_FOUND);
	fu_security_attrs_append(attrs, attr);

	/* check exists, and in v2.0 mode */
	if (self->tpm_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}
	if (g_strcmp0(fu_tpm_device_get_family(self->tpm_device), "2.0") != 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		return;
	}

	/* success */
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(FU_DEVICE(self->tpm_device)));
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_tpm_plugin_add_security_attr_eventlog(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	gboolean reconstructed = TRUE;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s_calc = NULL;
	g_autoptr(GPtrArray) pcr0s_real = NULL;

	/* no TPM device */
	if (self->tpm_device == NULL)
		return;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0);
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(self->tpm_device));
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* check reconstructed to PCR0 */
	if (self->eventlog == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* calculate from the eventlog */
	pcr0s_calc = fu_tpm_eventlog_calc_checksums(self->eventlog, 0, &error);
	if (pcr0s_calc == NULL) {
		g_warning("failed to get eventlog reconstruction: %s", error->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* compare against the real PCR0s */
	pcr0s_real = fu_tpm_device_get_checksums(self->tpm_device, 0);
	for (guint i = 0; i < pcr0s_calc->len; i++) {
		const gchar *checksum = g_ptr_array_index(pcr0s_calc, i);
		reconstructed = FALSE;
		for (guint j = 0; j < pcr0s_real->len; j++) {
			const gchar *checksum_tmp = g_ptr_array_index(pcr0s_real, j);
			/* skip unless same algorithm */
			if (strlen(checksum) != strlen(checksum_tmp))
				continue;
			g_debug("comparing TPM %s and EVT %s", checksum, checksum_tmp);
			if (g_strcmp0(checksum, checksum_tmp) == 0) {
				reconstructed = TRUE;
				break;
			}
		}
		/* all algorithms must match */
		if (!reconstructed)
			break;
	}
	if (!reconstructed) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_tpm_plugin_add_security_attr_empty(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no TPM device */
	if (self->tpm_device == NULL)
		return;

	/* add attributes */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR);
	fwupd_security_attr_add_guids(attr, fu_device_get_guids(self->tpm_device));
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* check PCRs 0 through 7 for empty checksums */
	for (guint pcr = 0; pcr <= 7; pcr++) {
		g_autoptr(GPtrArray) checksums = fu_tpm_device_get_checksums(self->tpm_device, pcr);
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(checksums, i);
			gboolean empty = TRUE;

			/* empty checksum is zero, so made entirely of zeroes */
			for (guint j = 0; checksum[j] != '\0'; j++) {
				if (checksum[j] != '0') {
					empty = FALSE;
					break;
				}
			}
			if (empty) {
				fwupd_security_attr_set_result(
				    attr,
				    FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
				return;
			}
		}
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_tpm_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	if (fu_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
		return;
	fu_tpm_plugin_add_security_attr_version(plugin, attrs);
	fu_tpm_plugin_add_security_attr_eventlog(plugin, attrs);
	fu_tpm_plugin_add_security_attr_empty(plugin, attrs);
}

static gchar *
fu_tpm_plugin_eventlog_report_metadata(FuPlugin *plugin)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	GString *str = g_string_new("");
	g_autoptr(GPtrArray) pcrs = NULL;
	g_autoptr(GPtrArray) items = fu_firmware_get_images(FU_FIRMWARE(self->eventlog));

	for (guint i = 0; i < items->len; i++) {
		FuTpmEventlogItem *item = g_ptr_array_index(items, i);
		FuTpmEventlogItemKind kind = fu_tpm_eventlog_item_get_kind(item);
		g_autofree gchar *blobstr = NULL;
		g_autofree gchar *checksum = NULL;
		g_autoptr(GBytes) blob = fu_firmware_get_bytes(FU_FIRMWARE(item), NULL);

		if (blob == NULL)
			continue;
		checksum = fu_firmware_get_checksum(FU_FIRMWARE(item), G_CHECKSUM_SHA1, NULL);
		if (checksum == NULL) {
			checksum =
			    fu_firmware_get_checksum(FU_FIRMWARE(item), G_CHECKSUM_SHA256, NULL);
		}
		if (checksum == NULL) {
			checksum =
			    fu_firmware_get_checksum(FU_FIRMWARE(item), G_CHECKSUM_SHA384, NULL);
		}
		if (checksum == NULL)
			continue;
		g_string_append_printf(str, "0x%08x %s", kind, checksum);
		blobstr = g_base64_encode((const guchar *)g_bytes_get_data(blob, NULL),
					  g_bytes_get_size(blob));
		if (blobstr != NULL)
			g_string_append_printf(str, " [%s]", blobstr);
		g_string_append(str, "\n");
	}
	pcrs = fu_tpm_eventlog_calc_checksums(self->eventlog, 0, NULL);
	if (pcrs != NULL) {
		for (guint j = 0; j < pcrs->len; j++) {
			const gchar *csum = g_ptr_array_index(pcrs, j);
			g_string_append_printf(str, "PCR0: %s\n", csum);
		}
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_string_free(str, FALSE);
}

static gboolean
fu_tpm_plugin_coldplug_eventlog(FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(FuFirmware) eventlog = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *str = NULL;

	/* do not show a warning if no TPM exists, or the kernel is too old */
	fn = fu_context_build_filename(ctx,
				       NULL,
				       FU_PATH_KIND_SYSFSDIR,
				       "kernel",
				       "security",
				       "tpm0",
				       "binary_bios_measurements",
				       NULL);
	if (fn == NULL)
		return TRUE;
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_debug("no %s, so skipping", fn);
		return TRUE;
	}
	blob = fu_bytes_get_contents(fn, error);
	if (blob == NULL)
		return FALSE;
	stream = g_memory_input_stream_new_from_bytes(blob);
	eventlog = fu_firmware_new_from_gtypes(stream,
					       0x0,
					       FU_FIRMWARE_PARSE_FLAG_NONE,
					       error,
					       FU_TYPE_TPM_EVENTLOG_V2,
					       FU_TYPE_TPM_EVENTLOG_V1,
					       G_TYPE_INVALID);
	if (eventlog == NULL)
		return FALSE;
	self->eventlog = FU_TPM_EVENTLOG(g_steal_pointer(&eventlog));

	/* add optional report metadata */
	str = fu_tpm_plugin_eventlog_report_metadata(plugin);
	fu_plugin_add_report_metadata(plugin, "TpmEventLog", str);
	return TRUE;
}

static gboolean
fu_tpm_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuTpmPlugin *self = FU_TPM_PLUGIN(plugin);
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *fn_pcrs = NULL;

	/* look for TPM v2.0 via software TCTI */
	if (g_getenv("TPM2TOOLS_TCTI") != NULL) {
		g_autoptr(FuDeviceLocker) locker = NULL;

		self->tpm_device = fu_tpm_v2_device_new(ctx);
		fu_device_set_physical_id(FU_DEVICE(self->tpm_device), "TCTI");
		locker = fu_device_locker_new(FU_DEVICE(self->tpm_device), error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_add_device(plugin, FU_DEVICE(self->tpm_device));
		return TRUE;
	}

	/* best effort */
	if (!fu_tpm_plugin_coldplug_eventlog(plugin, &error_local))
		g_warning("failed to load eventlog: %s", error_local->message);

	/* look for TPM v1.2 */
	fn_pcrs =
	    fu_context_build_filename(ctx, error, FU_PATH_KIND_SYSFSDIR_TPM, "tpm0", "pcrs", NULL);
	if (fn_pcrs == NULL)
		return FALSE;
	if (g_file_test(fn_pcrs, G_FILE_TEST_EXISTS)) {
		self->tpm_device = fu_tpm_v1_device_new(ctx);
		g_object_set(self->tpm_device, "device-file", fn_pcrs, NULL);
		fu_device_set_physical_id(FU_DEVICE(self->tpm_device), "tpm");
		if (!fu_device_probe(FU_DEVICE(self->tpm_device), error))
			return FALSE;
		fu_plugin_add_device(plugin, FU_DEVICE(self->tpm_device));
	}

	/* success */
	return TRUE;
}

static void
fu_tpm_plugin_init(FuTpmPlugin *self)
{
}

static void
fu_tpm_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* old name */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "tpm_eventlog");
	if (g_getenv("TPM2TOOLS_TCTI") == NULL)
		fu_plugin_add_device_udev_subsystem(plugin, "tpm");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_TPM_V2_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TPM_V1_DEVICE); /* coverage */
}

static void
fu_tpm_plugin_finalize(GObject *obj)
{
	FuTpmPlugin *self = FU_TPM_PLUGIN(obj);
	if (self->tpm_device != NULL)
		g_object_unref(self->tpm_device);
	if (self->bios_device != NULL)
		g_object_unref(self->bios_device);
	if (self->eventlog != NULL)
		g_object_unref(self->eventlog);
	G_OBJECT_CLASS(fu_tpm_plugin_parent_class)->finalize(obj);
}

static void
fu_tpm_plugin_class_init(FuTpmPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_tpm_plugin_finalize;
	plugin_class->constructed = fu_tpm_plugin_constructed;
	plugin_class->to_string = fu_tpm_plugin_to_string;
	plugin_class->coldplug = fu_tpm_plugin_coldplug;
	plugin_class->device_added = fu_tpm_plugin_device_added;
	plugin_class->device_registered = fu_tpm_plugin_device_registered;
	plugin_class->add_security_attrs = fu_tpm_plugin_add_security_attrs;
}

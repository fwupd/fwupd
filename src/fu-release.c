/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuRelease"

#include "config.h"

#include "fu-device-private.h"
#include "fu-release-common.h"
#include "fu-release.h"

/**
 * FuRelease:
 *
 * An installable entity that has been loaded and verified for a specific device.
 *
 * See also: [class@FwupdRelease]
 */

struct _FuRelease {
	FwupdRelease parent_instance;
	FuEngineRequest *request;
	FuDevice *device;
	FwupdRemote *remote;
	FuEngineConfig *config;
	GInputStream *stream;
	gchar *update_request_id;
	gchar *device_version_old;
	GPtrArray *soft_reqs; /* nullable, element-type XbNode */
	GPtrArray *hard_reqs; /* nullable, element-type XbNode */
	guint64 priority;
};

G_DEFINE_TYPE(FuRelease, fu_release, FWUPD_TYPE_RELEASE)

static gboolean
fu_release_ensure_trust_flags(FuRelease *self, XbNode *rel, GError **error);

gchar *
fu_release_to_string(FuRelease *self)
{
	const guint idt = 1;
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);

	/* parent */
	fwupd_codec_add_string(FWUPD_CODEC(self), 0, str);

	/* instance */
	if (self->request != NULL)
		fwupd_codec_add_string(FWUPD_CODEC(self->request), idt, str);
	if (self->device != NULL)
		fwupd_codec_string_append(str, idt, "Device", fu_device_get_id(self->device));
	if (self->device_version_old != NULL)
		fwupd_codec_string_append(str, idt, "DeviceVersionOld", self->device_version_old);
	if (self->remote != NULL)
		fwupd_codec_string_append(str, idt, "Remote", fwupd_remote_get_id(self->remote));
	fwupd_codec_string_append_bool(str, idt, "HasConfig", self->config != NULL);
	fwupd_codec_string_append_bool(str, idt, "HasStream", self->stream != NULL);
	if (self->update_request_id != NULL)
		fwupd_codec_string_append(str, idt, "UpdateRequestId", self->update_request_id);
	if (self->soft_reqs != NULL)
		fwupd_codec_string_append_hex(str, idt, "SoftReqs", self->soft_reqs->len);
	if (self->hard_reqs != NULL)
		fwupd_codec_string_append_hex(str, idt, "HardReqs", self->hard_reqs->len);
	if (self->priority != 0)
		fwupd_codec_string_append_hex(str, idt, "Priority", self->priority);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_release_set_request:
 * @self: a #FuRelease
 * @request: (nullable): a #FuEngineRequest
 *
 * Sets the user request which created this operation.
 **/
void
fu_release_set_request(FuRelease *self, FuEngineRequest *request)
{
	g_return_if_fail(FU_IS_RELEASE(self));
	g_set_object(&self->request, request);
}

/**
 * fu_release_get_request:
 * @self: a #FuRelease
 *
 * Gets the user request which created this operation.
 *
 * Returns: (transfer none) (nullable): request
 **/
FuEngineRequest *
fu_release_get_request(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);
	return self->request;
}

/**
 * fu_release_get_device_version_old:
 * @self: a #FuRelease
 *
 * Gets the original [before update] device version.
 *
 * Returns: a string value, or %NULL if never set.
 **/
const gchar *
fu_release_get_device_version_old(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);
	return self->device_version_old;
}

static void
fu_release_set_device_version_old(FuRelease *self, const gchar *device_version_old)
{
	g_return_if_fail(FU_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(self->device_version_old, device_version_old) == 0)
		return;

	g_free(self->device_version_old);
	self->device_version_old = g_strdup(device_version_old);
}

/**
 * fu_release_set_device:
 * @self: a #FuRelease
 * @device: (nullable): a #FuDevice
 *
 * Sets the device this release should use when checking requirements.
 **/
void
fu_release_set_device(FuRelease *self, FuDevice *device)
{
	g_return_if_fail(FU_IS_RELEASE(self));
	g_set_object(&self->device, device);
	fu_release_set_device_version_old(self, fu_device_get_version(device));
}

/**
 * fu_release_get_device:
 * @self: a #FuRelease
 *
 * Gets the device this release was loaded for.
 *
 * Returns: (transfer none) (nullable): device
 **/
FuDevice *
fu_release_get_device(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);
	return self->device;
}

/**
 * fu_release_get_stream:
 * @self: a #FuRelease
 *
 * Gets the firmware stream to use when installing this release.
 *
 * Returns: (transfer none) (nullable): data
 **/
GInputStream *
fu_release_get_stream(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);
	return self->stream;
}

/**
 * fu_release_get_soft_reqs:
 * @self: a #FuRelease
 *
 * Gets the additional soft requirements that need to be checked in the engine.
 *
 * Returns: (transfer none) (nullable) (element-type XbNode): nodes
 **/
GPtrArray *
fu_release_get_soft_reqs(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);
	return self->soft_reqs;
}

/**
 * fu_release_get_soft_reqs:
 * @self: a #FuRelease
 *
 * Gets the additional hard requirements that need to be checked in the engine.
 *
 * Returns: (transfer none) (nullable) (element-type XbNode): nodes
 **/
GPtrArray *
fu_release_get_hard_reqs(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);
	return self->hard_reqs;
}

/**
 * fu_release_get_update_request_id:
 * @self: a #FuRelease
 *
 * Gets the update request ID as specified from `LVFS::UpdateRequestId`.
 *
 * Returns: a string value, or %NULL if never set.
 **/
const gchar *
fu_release_get_update_request_id(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), NULL);
	return self->update_request_id;
}

static void
fu_release_set_update_request_id(FuRelease *self, const gchar *update_request_id)
{
	g_return_if_fail(FU_IS_RELEASE(self));

	/* not changed */
	if (g_strcmp0(self->update_request_id, update_request_id) == 0)
		return;

	g_free(self->update_request_id);
	self->update_request_id = g_strdup(update_request_id);
}

/**
 * fu_release_set_remote:
 * @self: a #FuRelease
 * @remote: (nullable): a #FwupdRemote
 *
 * Sets the remote this release should use when loading. This is typically set by the engine by
 *watching the `remote-id` property to be set and then querying the internal cached list of
 *`FuRemote`s.
 **/
void
fu_release_set_remote(FuRelease *self, FwupdRemote *remote)
{
	g_return_if_fail(FU_IS_RELEASE(self));
	g_set_object(&self->remote, remote);
}

/**
 * fu_release_set_config:
 * @self: a #FuRelease
 * @config: (nullable): a #FuEngineConfig
 *
 * Sets the config to use when loading. The config may be used for things like ordering attributes
 *like protocol priority.
 **/
void
fu_release_set_config(FuRelease *self, FuEngineConfig *config)
{
	g_return_if_fail(FU_IS_RELEASE(self));
	g_set_object(&self->config, config);
}

static gchar *
fu_release_get_localized_xpath(FuRelease *self, const gchar *element)
{
	GString *xpath = g_string_new(element);
	const gchar *locale = NULL;

	/* optional; not set in tests */
	if (self->request != NULL)
		locale = fu_engine_request_get_locale(self->request);

	/* prefer the users locale if set */
	if (locale != NULL) {
		g_autofree gchar *xpath_locale = NULL;
		xpath_locale = g_strdup_printf("%s[@xml:lang='%s']|", element, locale);
		g_string_prepend(xpath, xpath_locale);
	}
	return g_string_free(xpath, FALSE);
}

/* convert hex and decimal versions to dotted style */
static gchar *
fu_release_get_release_version(FuRelease *self, const gchar *version, GError **error)
{
	FwupdVersionFormat fmt = fu_device_get_version_format(self->device);
	guint64 ver_uint32;
	g_autoptr(GError) error_local = NULL;

	/* already dotted notation */
	if (g_strstr_len(version, -1, ".") != NULL)
		return g_strdup(version);

	/* don't touch my version! */
	if (fmt == FWUPD_VERSION_FORMAT_PLAIN || fmt == FWUPD_VERSION_FORMAT_UNKNOWN)
		return g_strdup(version);

	/* parse as integer */
	if (!fu_strtoull(version, &ver_uint32, 1, G_MAXUINT32, &error_local)) {
		g_warning("invalid release version %s: %s", version, error_local->message);
		return g_strdup(version);
	}

	/* convert to dotted decimal */
	return fu_version_from_uint32((guint32)ver_uint32, fmt);
}

static gboolean
fu_release_load_test_result(FuRelease *self, XbNode *n, GError **error)
{
	const gchar *tmp;
	g_autoptr(FwupdReport) report = fwupd_report_new();
	g_autoptr(GPtrArray) custom = NULL;
	g_autoptr(XbNode) os = NULL;
	g_autoptr(XbNode) vendor_name = NULL;

	tmp = xb_node_get_attr(n, "date");
	if (tmp != NULL) {
		g_autoptr(GDateTime) dt = NULL;
		g_autofree gchar *iso8601 = g_strdup_printf("%sT00:00:00Z", tmp);
		dt = g_date_time_new_from_iso8601(iso8601, NULL);
		if (dt != NULL)
			fwupd_report_set_created(report, g_date_time_to_unix(dt));
	}
	tmp = xb_node_query_text(n, "device", NULL);
	if (tmp != NULL)
		fwupd_report_set_device_name(report, tmp);
	tmp = xb_node_query_text(n, "previous_version", NULL);
	if (tmp != NULL) {
		fwupd_report_set_version_old(report, tmp);
		if (fu_version_compare(fu_release_get_version(self),
				       tmp,
				       FWUPD_VERSION_FORMAT_UNKNOWN) > 0) {
			fwupd_report_add_flag(report, FWUPD_REPORT_FLAG_IS_UPGRADE);
		}
	}
	vendor_name = xb_node_query_first(n, "vendor_name", NULL);
	if (vendor_name != NULL) {
		guint64 vendor_id = xb_node_get_attr_as_uint(vendor_name, "id");
		fwupd_report_set_vendor(report, xb_node_get_text(vendor_name));
		if (vendor_id != G_MAXUINT64)
			fwupd_report_set_vendor_id(report, vendor_id);
	}
	os = xb_node_query_first(n, "os", NULL);
	if (os != NULL) {
		tmp = xb_node_get_attr(os, "version");
		if (tmp != NULL)
			fwupd_report_set_distro_version(report, tmp);
		tmp = xb_node_get_attr(os, "variant");
		if (tmp != NULL)
			fwupd_report_set_distro_variant(report, tmp);
		fwupd_report_set_distro_id(report, xb_node_get_text(os));
	}
	if (fu_release_get_remote_id(self) != NULL)
		fwupd_report_set_remote_id(report, fu_release_get_remote_id(self));
	custom = xb_node_query(n, "custom/value", 0, NULL);
	if (custom != NULL) {
		for (guint i = 0; i < custom->len; i++) {
			XbNode *c = g_ptr_array_index(custom, i);
			if (g_strcmp0(xb_node_get_attr(c, "key"), "FromOEM") == 0) {
				fwupd_report_add_flag(report, FWUPD_REPORT_FLAG_FROM_OEM);
				continue;
			}
			if (xb_node_get_attr(c, "key") == NULL || xb_node_get_text(c) == NULL) {
				g_debug("ignoring metadata: %s=%s",
					xb_node_get_attr(c, "key"),
					xb_node_get_text(c));
				continue;
			}
			fwupd_report_add_metadata_item(report,
						       xb_node_get_attr(c, "key"),
						       xb_node_get_text(c));
		}
	}

	/* success */
	fwupd_release_add_report(FWUPD_RELEASE(self), report);
	return TRUE;
}

static gboolean
fu_release_load_artifact(FuRelease *self, XbNode *artifact, GError **error)
{
	const gchar *filename;
	guint64 size;
	g_autoptr(GPtrArray) locations = NULL;
	g_autoptr(GPtrArray) checksums = NULL;
	g_autoptr(GPtrArray) test_result = NULL;

	/* filename */
	filename = xb_node_query_text(artifact, "filename", NULL);
	if (filename != NULL && !g_str_has_suffix(filename, ".cab")) {
		/* some firmware archives was signed with <artifact type="binary"> where the
		 * checksums were the *content* checksums, not the *container* checksum */
		g_debug("ignoring non-binary artifact entry: %s", filename);
		return TRUE;
	}
	if (filename != NULL)
		fwupd_release_set_filename(FWUPD_RELEASE(self), filename);

	/* location */
	locations = xb_node_query(artifact, "location", 0, NULL);
	if (locations != NULL) {
		for (guint i = 0; i < locations->len; i++) {
			XbNode *n = g_ptr_array_index(locations, i);

			/* check the scheme is allowed */
			if (self->config != NULL) {
				g_autofree gchar *scheme =
				    fu_release_uri_get_scheme(xb_node_get_text(n));
				if (scheme != NULL) {
					guint prio =
					    fu_engine_config_get_uri_scheme_prio(self->config,
										 scheme);
					if (prio == G_MAXUINT)
						continue;
				}
			}

			/* build the complete URI */
			if (self->remote != NULL) {
				g_autofree gchar *uri = NULL;
				uri = fwupd_remote_build_firmware_uri(self->remote,
								      xb_node_get_text(n),
								      NULL);
				if (uri != NULL) {
					fwupd_release_add_location(FWUPD_RELEASE(self), uri);
					continue;
				}
			}
			fwupd_release_add_location(FWUPD_RELEASE(self), xb_node_get_text(n));
		}
	}

	/* checksum */
	checksums = xb_node_query(artifact, "checksum", 0, NULL);
	if (checksums != NULL) {
		for (guint i = 0; i < checksums->len; i++) {
			XbNode *n = g_ptr_array_index(checksums, i);
			fwupd_release_add_checksum(FWUPD_RELEASE(self), xb_node_get_text(n));
		}
	}

	/* test results */
	test_result = xb_node_query(artifact, "testing/test_result", 0, NULL);
	if (test_result != NULL) {
		for (guint i = 0; i < test_result->len; i++) {
			XbNode *n = g_ptr_array_index(test_result, i);
			if (!fu_release_load_test_result(self, n, error))
				return FALSE;
		}
	}

	/* size */
	size = xb_node_query_text_as_uint(artifact, "size[@type='installed']", NULL);
	if (size != G_MAXUINT64)
		fwupd_release_set_size(FWUPD_RELEASE(self), size);

	/* success */
	return TRUE;
}

static gint
fu_release_scheme_compare_cb(gconstpointer a, gconstpointer b, gpointer user_data)
{
	FuRelease *self = FU_RELEASE(user_data);
	const gchar *location1 = *((const gchar **)a);
	const gchar *location2 = *((const gchar **)b);
	g_autofree gchar *scheme1 = fu_release_uri_get_scheme(location1);
	g_autofree gchar *scheme2 = fu_release_uri_get_scheme(location2);
	guint prio1 = fu_engine_config_get_uri_scheme_prio(self->config, scheme1);
	guint prio2 = fu_engine_config_get_uri_scheme_prio(self->config, scheme2);
	if (prio1 < prio2)
		return -1;
	if (prio1 > prio2)
		return 1;
	return 0;
}

static gboolean
fu_release_check_requirements_version_check(FuRelease *self, GError **error)
{
	if (self->hard_reqs != NULL) {
		for (guint i = 0; i < self->hard_reqs->len; i++) {
			XbNode *req = g_ptr_array_index(self->hard_reqs, i);
			if (g_strcmp0(xb_node_get_element(req), "firmware") == 0 &&
			    xb_node_get_text(req) == NULL) {
				return TRUE;
			}
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no firmware requirement");
	return FALSE;
}

static gchar *
fu_release_verfmts_to_string(GPtrArray *verfmts)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; i < verfmts->len; i++) {
		XbNode *verfmt = g_ptr_array_index(verfmts, i);
		const gchar *tmp = xb_node_get_text(verfmt);
		g_string_append_printf(str, "%s;", tmp);
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_string_free(str, FALSE);
}

static gboolean
fu_release_check_verfmt(FuRelease *self,
			GPtrArray *verfmts,
			FwupdInstallFlags flags,
			GError **error)
{
	FwupdVersionFormat fmt_dev = fu_device_get_version_format(self->device);
	g_autofree gchar *verfmts_str = NULL;

	/* no device format */
	if (fmt_dev == FWUPD_VERSION_FORMAT_UNKNOWN && (flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		verfmts_str = fu_release_verfmts_to_string(verfmts);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "release version format '%s' but no device version format",
			    verfmts_str);
		return FALSE;
	}

	/* compare all version formats */
	for (guint i = 0; i < verfmts->len; i++) {
		XbNode *verfmt = g_ptr_array_index(verfmts, i);
		const gchar *tmp = xb_node_get_text(verfmt);
		FwupdVersionFormat fmt_rel = fwupd_version_format_from_string(tmp);
		if (fmt_dev == fmt_rel)
			return TRUE;
	}
	verfmts_str = fu_release_verfmts_to_string(verfmts);
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Firmware version formats were different, "
			    "device was '%s' and release is '%s'",
			    fwupd_version_format_to_string(fmt_dev),
			    verfmts_str);
		return FALSE;
	}
	g_warning("ignoring version format difference %s:%s",
		  fwupd_version_format_to_string(fmt_dev),
		  verfmts_str);
	return TRUE;
}

/* these can all be done without the daemon */
static gboolean
fu_release_check_requirements(FuRelease *self,
			      XbNode *component,
			      XbNode *rel,
			      FwupdInstallFlags install_flags,
			      GError **error)
{
	const gchar *branch_new;
	const gchar *branch_old;
	const gchar *protocol;
	const gchar *version;
	const gchar *version_lowest;
	gboolean matches_guid = FALSE;
	gint vercmp;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) provides = NULL;
	g_autoptr(GPtrArray) verfmts = NULL;

	/* does this component provide a GUID the device has */
	provides = xb_node_query(component, "provides/firmware[@type='flashed']", 0, &error_local);
	if (provides == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "No supported devices found: %s",
			    error_local->message);
		return FALSE;
	}
	for (guint i = 0; i < provides->len; i++) {
		XbNode *provide = g_ptr_array_index(provides, i);
		if (fu_device_has_guid(self->device, xb_node_get_text(provide))) {
			matches_guid = TRUE;
			break;
		}
	}
	if (!matches_guid) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "No supported devices found");
		return FALSE;
	}

	/* device requires a version check */
	if (fu_device_has_flag(self->device, FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED)) {
		if (!fu_release_check_requirements_version_check(self, error)) {
			g_prefix_error(error, "device requires firmware with a version check: ");
			return FALSE;
		}
	}

	/* does the protocol match */
	protocol = xb_node_query_text(component, "custom/value[@key='LVFS::UpdateProtocol']", NULL);
	if (fu_device_get_protocols(self->device)->len != 0 && protocol != NULL &&
	    !fu_device_has_protocol(self->device, protocol) &&
	    (install_flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_autofree gchar *str = NULL;
		str = fu_strjoin("|", fu_device_get_protocols(self->device));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s does not support %s, only %s",
			    fu_device_get_name(self->device),
			    protocol,
			    str);
		return FALSE;
	}

	/* check the device is not locked */
	if (fu_device_has_flag(self->device, FWUPD_DEVICE_FLAG_LOCKED)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s [%s] is locked",
			    fu_device_get_name(self->device),
			    fu_device_get_id(self->device));
		return FALSE;
	}

	/* check the branch is not switching */
	branch_new = xb_node_query_text(component, "branch", NULL);
	branch_old = fu_device_get_branch(self->device);
	if ((install_flags & FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH) == 0 &&
	    g_strcmp0(branch_old, branch_new) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s [%s] would switch firmware branch from %s to %s",
			    fu_device_get_name(self->device),
			    fu_device_get_id(self->device),
			    branch_old != NULL ? branch_old : "default",
			    branch_new != NULL ? branch_new : "default");
		return FALSE;
	}

	/* no update abilities */
	if (!fu_engine_request_has_feature_flag(self->request, FWUPD_FEATURE_FLAG_SHOW_PROBLEMS) &&
	    !fu_device_has_flag(self->device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_autoptr(GString) str = g_string_new(NULL);
		g_string_append_printf(str,
				       "Device %s [%s] does not currently allow updates",
				       fu_device_get_name(self->device),
				       fu_device_get_id(self->device));
		if (fu_device_get_update_error(self->device) != NULL) {
			g_string_append_printf(str,
					       ": %s",
					       fu_device_get_update_error(self->device));
		}
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, str->str);
		return FALSE;
	}

	/* called with online update, test if device is supposed to allow this */
	if ((install_flags & FWUPD_INSTALL_FLAG_OFFLINE) == 0 &&
	    (install_flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
	    fu_device_has_flag(self->device, FWUPD_DEVICE_FLAG_ONLY_OFFLINE)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s [%s] only allows offline updates",
			    fu_device_get_name(self->device),
			    fu_device_get_id(self->device));
		return FALSE;
	}

	/* get device */
	version = fu_device_get_version(self->device);
	if (version == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Device %s [%s] has no firmware version",
			    fu_device_get_name(self->device),
			    fu_device_get_id(self->device));
		return FALSE;
	}

	/* check the version formats match if set in the release */
	if ((install_flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
	    (install_flags & FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH) == 0) {
		verfmts =
		    xb_node_query(component, "custom/value[@key='LVFS::VersionFormat']", 0, NULL);
		if (verfmts != NULL) {
			if (!fu_release_check_verfmt(self, verfmts, install_flags, error))
				return FALSE;
		}
	}

	/* compare to the lowest supported version, if it exists */
	version_lowest = fu_device_get_version_lowest(self->device);
	if (version_lowest != NULL &&
	    fu_version_compare(version_lowest,
			       fu_release_get_version(self),
			       fu_device_get_version_format(self->device)) > 0 &&
	    (install_flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Specified firmware is older than the minimum "
			    "required version '%s < %s'",
			    fu_release_get_version(self),
			    version_lowest);
		return FALSE;
	}

	/* is this a downgrade or re-install */
	vercmp = fu_version_compare(version,
				    fu_release_get_version(self),
				    fu_device_get_version_format(self->device));
	if (fu_device_has_flag(self->device, FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE) &&
	    vercmp > 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device only supports version upgrades");
		return FALSE;
	}
	if (vercmp == 0 && (install_flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_VERSION_SAME,
			    "Specified firmware is already installed '%s'",
			    fu_release_get_version(self));
		return FALSE;
	}
	if (vercmp > 0)
		fu_release_add_flag(self, FWUPD_RELEASE_FLAG_IS_DOWNGRADE);
	if (fu_release_has_flag(self, FWUPD_RELEASE_FLAG_IS_DOWNGRADE) &&
	    (install_flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) == 0 &&
	    (install_flags & FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_VERSION_NEWER,
			    "Specified firmware is older than installed '%s < %s'",
			    fu_release_get_version(self),
			    version);
		return FALSE;
	}

	/* success */
	return TRUE;
}

void
fu_release_set_priority(FuRelease *self, guint64 priority)
{
	g_return_if_fail(FU_IS_RELEASE(self));
	self->priority = priority;
}

guint64
fu_release_get_priority(FuRelease *self)
{
	g_return_val_if_fail(FU_IS_RELEASE(self), 0);
	return self->priority;
}

static void
fu_release_ensure_device_by_checksum(FuRelease *self, XbNode *component, XbNode *rel)
{
	g_autoptr(GPtrArray) device_checksums = NULL;

	/* sanity check */
	if (fu_device_get_checksums(self->device)->len == 0)
		return;
	device_checksums = xb_node_query(rel, "checksum[@target='device']", 0, NULL);
	if (device_checksums == NULL)
		return;
	for (guint i = 0; i < device_checksums->len; i++) {
		XbNode *device_checksum = g_ptr_array_index(device_checksums, i);
		if (!fu_device_has_checksum(self->device, xb_node_get_text(device_checksum)))
			continue;
		fu_device_ensure_from_component(self->device, component);
		if (fu_device_has_internal_flag(self->device,
						FU_DEVICE_INTERNAL_FLAG_MD_SET_VERSION)) {
			const gchar *rel_version = xb_node_get_attr(rel, "version");
			if (rel_version == NULL)
				continue;
			fu_device_set_version(self->device, rel_version);
			fu_device_remove_internal_flag(self->device,
						       FU_DEVICE_INTERNAL_FLAG_MD_SET_VERSION);
		}
		break;
	}
}

/**
 * fu_release_load:
 * @self: a #FuRelease
 * @cabinet: a #FuCabinet
 * @component: (not nullable): a #XbNode
 * @rel_optional: (nullable): a #XbNode
 * @install_flags: a #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: (nullable): optional return location for an error
 *
 * Loads then checks any requirements of this release. This will typically involve checking
 * that the device can accept the component (the GUIDs match) and that the device can be
 * upgraded with this firmware version.
 *
 * Returns: %TRUE if the release was loaded and the requirements passed
 **/
gboolean
fu_release_load(FuRelease *self,
		FuCabinet *cabinet,
		XbNode *component,
		XbNode *rel_optional,
		FwupdInstallFlags install_flags,
		GError **error)
{
	const gchar *tmp;
	guint64 tmp64;
	GBytes *blob_basename;
	g_autofree gchar *name_xpath = NULL;
	g_autofree gchar *namevs_xpath = NULL;
	g_autofree gchar *summary_xpath = NULL;
	g_autofree gchar *description_xpath = NULL;
	g_autoptr(GPtrArray) cats = NULL;
	g_autoptr(GPtrArray) tags = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autoptr(XbNode) artifact = NULL;
	g_autoptr(XbNode) description = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(GError) error_soft = NULL;
	g_autoptr(GError) error_hard = NULL;

	g_return_val_if_fail(FU_IS_RELEASE(self), FALSE);
	g_return_val_if_fail(cabinet == NULL || FU_IS_CABINET(cabinet), FALSE);
	g_return_val_if_fail(XB_IS_NODE(component), FALSE);
	g_return_val_if_fail(rel_optional == NULL || XB_IS_NODE(rel_optional), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* set from the component */
	tmp = xb_node_query_text(component, "id", NULL);
	if (tmp != NULL)
		fwupd_release_set_appstream_id(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "url[@type='homepage']", NULL);
	if (tmp != NULL)
		fwupd_release_set_homepage(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "project_license", NULL);
	if (tmp != NULL)
		fwupd_release_set_license(FWUPD_RELEASE(self), tmp);
	name_xpath = fu_release_get_localized_xpath(self, "name");
	tmp = xb_node_query_text(component, name_xpath, NULL);
	if (tmp != NULL)
		fwupd_release_set_name(FWUPD_RELEASE(self), tmp);
	summary_xpath = fu_release_get_localized_xpath(self, "summary");
	tmp = xb_node_query_text(component, summary_xpath, NULL);
	if (tmp != NULL)
		fwupd_release_set_summary(FWUPD_RELEASE(self), tmp);
	namevs_xpath = fu_release_get_localized_xpath(self, "name_variant_suffix");
	tmp = xb_node_query_text(component, namevs_xpath, NULL);
	if (tmp != NULL)
		fwupd_release_set_name_variant_suffix(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "branch", NULL);
	if (tmp != NULL)
		fwupd_release_set_branch(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "developer_name", NULL);
	if (tmp != NULL)
		fwupd_release_set_vendor(FWUPD_RELEASE(self), tmp);

	/* use default release */
	if (rel_optional == NULL) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(XbQuery) query = NULL;
		query = xb_query_new_full(xb_node_get_silo(component),
					  "releases/release",
					  XB_QUERY_FLAG_FORCE_NODE_CACHE,
					  error);
		if (query == NULL)
			return FALSE;
		rel = xb_node_query_first_full(component, query, &error_local);
		if (rel == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get default release: %s",
				    error_local->message);
			return FALSE;
		}
	} else {
		rel = g_object_ref(rel_optional);
	}

	/* find the remote */
	tmp = xb_node_query_text(component, "../custom/value[@key='fwupd::RemoteId']", NULL);
	if (tmp != NULL)
		fwupd_release_set_remote_id(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "../custom/value[@key='LVFS::Distributor']", NULL);
	if (tmp != NULL && g_str_has_prefix(tmp, "community"))
		fwupd_release_add_flag(FWUPD_RELEASE(self), FWUPD_RELEASE_FLAG_IS_COMMUNITY);

	/* use the metadata to set the device attributes */
	if (!fu_release_ensure_trust_flags(self, rel, error))
		return FALSE;
	if (self->device != NULL &&
	    fu_release_has_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_METADATA)) {
		if (fu_device_has_internal_flag(self->device,
						FU_DEVICE_INTERNAL_FLAG_MD_ONLY_CHECKSUM)) {
			fu_release_ensure_device_by_checksum(self, component, rel);
		} else {
			fu_device_ensure_from_component(self->device, component);
		}
	}

	/* per-release priority wins, but fallback to per-component priority */
	tmp64 = xb_node_get_attr_as_uint(rel, "priority");
	if (tmp64 == G_MAXUINT64)
		tmp64 = xb_node_get_attr_as_uint(component, "priority");
	if (tmp64 != G_MAXUINT64)
		fu_release_set_priority(self, tmp64);

	/* the version is fixed up with the device format */
	tmp = xb_node_get_attr(rel, "version");
	if (tmp == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "version unset");
		return FALSE;
	}
	if (self->device != NULL) {
		g_autofree gchar *version_rel = NULL;
		version_rel = fu_release_get_release_version(self, tmp, error);
		if (version_rel == NULL)
			return FALSE;
		fwupd_release_set_version(FWUPD_RELEASE(self), version_rel);
	} else {
		fwupd_release_set_version(FWUPD_RELEASE(self), tmp);
	}

	/* optional release ID -- currently a integer but maybe namespaced in the future */
	fwupd_release_set_id(FWUPD_RELEASE(self), xb_node_get_attr(rel, "id"));

	/* this is the more modern way to do this */
	artifact = xb_node_query_first(rel, "artifacts/artifact[@type='binary']", NULL);
	if (artifact != NULL) {
		if (!fu_release_load_artifact(self, artifact, error))
			return FALSE;
	}
	description_xpath = fu_release_get_localized_xpath(self, "description");
	description = xb_node_query_first(rel, description_xpath, NULL);
	if (description != NULL) {
		g_autofree gchar *xml = NULL;
		g_autoptr(GString) str = NULL;
		xml = xb_node_export(description, XB_NODE_EXPORT_FLAG_ONLY_CHILDREN, NULL);
		str = g_string_new(xml);
		if (self->device != NULL && self->request != NULL &&
		    fu_device_has_flag(self->device, FWUPD_DEVICE_FLAG_AFFECTS_FDE) &&
		    !fu_engine_request_has_feature_flag(self->request,
							FWUPD_FEATURE_FLAG_FDE_WARNING)) {
			g_string_prepend(
			    str,
			    "<p>Some of the platform secrets may be invalidated when "
			    "updating this firmware. Please ensure you have the volume "
			    "recovery key before continuing.</p>");
		}
		if (fwupd_release_has_flag(FWUPD_RELEASE(self), FWUPD_RELEASE_FLAG_IS_COMMUNITY) &&
		    self->request != NULL &&
		    !fu_engine_request_has_feature_flag(self->request,
							FWUPD_FEATURE_FLAG_COMMUNITY_TEXT)) {
			g_string_prepend(
			    str,
			    "<p>This firmware is provided by LVFS community "
			    "members and is not provided (or supported) by the original "
			    "hardware vendor. "
			    "Installing this update may also void any device warranty.</p>");
		}
		if (str->len > 0)
			fwupd_release_set_description(FWUPD_RELEASE(self), str->str);
	}
	if (fwupd_release_get_locations(FWUPD_RELEASE(self))->len == 0) {
		tmp = xb_node_query_text(rel, "location", NULL);
		if (tmp != NULL) {
			g_autofree gchar *uri = NULL;
			if (self->remote != NULL)
				uri = fwupd_remote_build_firmware_uri(self->remote, tmp, NULL);
			if (uri == NULL)
				uri = g_strdup(tmp);
			fwupd_release_add_location(FWUPD_RELEASE(self), uri);
		}
	}
	if (fwupd_release_get_locations(FWUPD_RELEASE(self))->len == 0 && self->remote != NULL &&
	    fwupd_remote_get_kind(self->remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
		tmp = xb_node_query_text(component,
					 "../custom/value[@key='fwupd::FilenameCache']",
					 NULL);
		if (tmp != NULL) {
			g_autofree gchar *uri = g_strdup_printf("file://%s", tmp);
			fwupd_release_add_location(FWUPD_RELEASE(self), uri);
		}
	}
	if (fwupd_release_get_filename(FWUPD_RELEASE(self)) == NULL) {
		tmp = xb_node_query_text(rel, "checksum[@target='content']", NULL);
		if (tmp != NULL)
			fwupd_release_set_filename(FWUPD_RELEASE(self), tmp);
	}
	tmp = xb_node_query_text(rel, "url[@type='details']", NULL);
	if (tmp != NULL)
		fwupd_release_set_details_url(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(rel, "url[@type='source']", NULL);
	if (tmp != NULL)
		fwupd_release_set_source_url(FWUPD_RELEASE(self), tmp);
	if (fwupd_release_get_checksums(FWUPD_RELEASE(self))->len == 0) {
		g_autoptr(GPtrArray) checksums = NULL;
		checksums = xb_node_query(rel, "checksum[@target='container']", 0, NULL);
		if (checksums != NULL) {
			for (guint i = 0; i < checksums->len; i++) {
				XbNode *n = g_ptr_array_index(checksums, i);
				if (xb_node_get_text(n) == NULL)
					continue;
				fwupd_release_add_checksum(FWUPD_RELEASE(self),
							   xb_node_get_text(n));
			}
		}
	}
	if (fwupd_release_get_size(FWUPD_RELEASE(self)) == 0) {
		tmp64 = xb_node_query_text_as_uint(rel, "size[@type='installed']", NULL);
		if (tmp64 != G_MAXUINT64)
			fwupd_release_set_size(FWUPD_RELEASE(self), tmp64);
	}
	if (fwupd_release_get_size(FWUPD_RELEASE(self)) == 0) {
		GBytes *sz = xb_node_get_data(rel, "fwupd::ReleaseSize");
		if (sz != NULL) {
			const guint64 *sizeptr = g_bytes_get_data(sz, NULL);
			fwupd_release_set_size(FWUPD_RELEASE(self), *sizeptr);
		}
	}
	tmp = xb_node_get_attr(rel, "urgency");
	if (tmp != NULL)
		fwupd_release_set_urgency(FWUPD_RELEASE(self),
					  fwupd_release_urgency_from_string(tmp));
	tmp64 = xb_node_get_attr_as_uint(rel, "install_duration");
	if (tmp64 != G_MAXUINT64)
		fwupd_release_set_install_duration(FWUPD_RELEASE(self), tmp64);
	tmp64 = xb_node_get_attr_as_uint(rel, "timestamp");
	if (tmp64 != G_MAXUINT64)
		fwupd_release_set_created(FWUPD_RELEASE(self), tmp64);
	cats = xb_node_query(component, "categories/category", 0, NULL);
	if (cats != NULL) {
		for (guint i = 0; i < cats->len; i++) {
			XbNode *n = g_ptr_array_index(cats, i);
			fwupd_release_add_category(FWUPD_RELEASE(self), xb_node_get_text(n));
		}
	}
	tags = xb_node_query(component, "tags/tag[@namespace=$'lvfs']", 0, NULL);
	if (tags != NULL) {
		for (guint i = 0; i < tags->len; i++) {
			XbNode *tag = g_ptr_array_index(tags, i);
			fwupd_release_add_tag(FWUPD_RELEASE(self), xb_node_get_text(tag));
		}
	}
	issues = xb_node_query(rel, "issues/issue", 0, NULL);
	if (issues != NULL) {
		for (guint i = 0; i < issues->len; i++) {
			XbNode *n = g_ptr_array_index(issues, i);
			fwupd_release_add_issue(FWUPD_RELEASE(self), xb_node_get_text(n));
		}
	}
	tmp = xb_node_query_text(component, "screenshots/screenshot/caption", NULL);
	if (tmp != NULL)
		fwupd_release_set_detach_caption(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "screenshots/screenshot/image", NULL);
	if (tmp != NULL) {
		if (self->remote != NULL) {
			g_autofree gchar *img = NULL;
			img = fwupd_remote_build_firmware_uri(self->remote, tmp, error);
			if (img == NULL)
				return FALSE;
			fwupd_release_set_detach_image(FWUPD_RELEASE(self), img);
		} else {
			fwupd_release_set_detach_image(FWUPD_RELEASE(self), tmp);
		}
	}
	tmp = xb_node_query_text(component, "custom/value[@key='LVFS::UpdateProtocol']", NULL);
	if (tmp != NULL)
		fwupd_release_set_protocol(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "custom/value[@key='LVFS::UpdateMessage']", NULL);
	if (tmp != NULL)
		fwupd_release_set_update_message(FWUPD_RELEASE(self), tmp);
	tmp = xb_node_query_text(component, "custom/value[@key='LVFS::UpdateImage']", NULL);
	if (tmp != NULL) {
		if (self->remote != NULL) {
			g_autofree gchar *img = NULL;
			img = fwupd_remote_build_firmware_uri(self->remote, tmp, error);
			if (img == NULL)
				return FALSE;
			fwupd_release_set_update_image(FWUPD_RELEASE(self), img);
		} else {
			fwupd_release_set_update_image(FWUPD_RELEASE(self), tmp);
		}
	}
	tmp = xb_node_query_text(component, "custom/value[@key='LVFS::UpdateRequestId']", NULL);
	if (tmp != NULL)
		fu_release_set_update_request_id(self, tmp);

	/* hard and soft requirements */
	self->hard_reqs = xb_node_query(component, "requires/*", 0, &error_hard);
	if (self->hard_reqs == NULL) {
		if (!g_error_matches(error_hard, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
		    !g_error_matches(error_hard, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
			g_propagate_error(error, g_steal_pointer(&error_hard));
			return FALSE;
		}
	}
	self->soft_reqs = xb_node_query(component, "suggests/*|recommends/*", 0, &error_soft);
	if (self->soft_reqs == NULL) {
		if (!g_error_matches(error_soft, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
		    !g_error_matches(error_soft, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
			g_propagate_error(error, g_steal_pointer(&error_soft));
			return FALSE;
		}
	}

	/* get per-release firmware stream */
	blob_basename = xb_node_get_data(rel, "fwupd::FirmwareBasename");
	if (cabinet != NULL && blob_basename != NULL) {
		const gchar *basename = (const gchar *)g_bytes_get_data(blob_basename, NULL);
		g_autoptr(FuFirmware) img = NULL;
		img = fu_firmware_get_image_by_id(FU_FIRMWARE(cabinet), basename, error);
		if (img == NULL) {
			g_prefix_error(error, "failed to find %s: ", basename);
			return FALSE;
		}
		self->stream = fu_firmware_get_stream(img, error);
		if (self->stream == NULL)
			return FALSE;
	}

	/* to build the firmware */
	tmp = g_object_get_data(G_OBJECT(component), "fwupd::BuilderScript");
	if (tmp != NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "fwupd::BuilderScript is no longer supported");
		return FALSE;
	}

	/* sort the locations by scheme */
	if (self->config != NULL) {
		g_ptr_array_sort_with_data(fwupd_release_get_locations(FWUPD_RELEASE(self)),
					   fu_release_scheme_compare_cb,
					   self);
	}

	/* check requirements for device */
	if (self->device != NULL && self->request != NULL &&
	    !fu_engine_request_has_flag(self->request, FU_ENGINE_REQUEST_FLAG_NO_REQUIREMENTS)) {
		if (!fu_release_check_requirements(self, component, rel, install_flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_release_ensure_trust_flags(FuRelease *self, XbNode *rel, GError **error)
{
	GBytes *blob;

	g_return_val_if_fail(FU_IS_RELEASE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* in the self tests */
	if (g_getenv("FWUPD_SELF_TEST") != NULL) {
		fu_release_add_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD);
		fu_release_add_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_METADATA);
		return TRUE;
	}

	/* populated from an actual cab archive */
	blob = g_object_get_data(G_OBJECT(rel), "fwupd::ReleaseFlags");
	if (blob != NULL) {
		FwupdReleaseFlags flags = FWUPD_RELEASE_FLAG_NONE;
		if (!fu_memcpy_safe((guint8 *)&flags,
				    sizeof(flags),
				    0x0, /* dst */
				    g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    0x0, /* src */
				    sizeof(flags),
				    error))
			return FALSE;
		if (flags & FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD)
			fu_release_add_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD);
		if (flags & FWUPD_RELEASE_FLAG_TRUSTED_METADATA)
			fu_release_add_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_METADATA);
	}

	/* do not require signatures for anything installed to the immutable datadir */
	if (fu_release_get_flags(self) == FWUPD_RELEASE_FLAG_NONE && self->remote != NULL) {
		if (fwupd_remote_get_keyring_kind(self->remote) == FWUPD_KEYRING_KIND_NONE &&
		    (fwupd_remote_get_kind(self->remote) == FWUPD_REMOTE_KIND_LOCAL ||
		     fwupd_remote_get_kind(self->remote) == FWUPD_REMOTE_KIND_DIRECTORY)) {
			g_debug("remote %s has kind=%s and Keyring=none and so marking as trusted",
				fwupd_remote_get_id(self->remote),
				fwupd_remote_kind_to_string(fwupd_remote_get_kind(self->remote)));
			fu_release_add_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD);
			fu_release_add_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_METADATA);
		} else if (fwupd_remote_get_keyring_kind(self->remote) != FWUPD_KEYRING_KIND_NONE) {
			g_debug("remote %s has kind=%s and so marking as trusted",
				fwupd_remote_get_id(self->remote),
				fwupd_remote_kind_to_string(fwupd_remote_get_kind(self->remote)));
			fu_release_add_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_METADATA);
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_release_get_action_id:
 * @self: a #FuEngine
 *
 * Gets the PolicyKit action ID to use for the install operation.
 *
 * Returns: string, e.g. `org.freedesktop.fwupd.update-internal-trusted`
 **/
const gchar *
fu_release_get_action_id(FuRelease *self)
{
	/* relax authentication checks for removable devices */
	if (!fu_device_has_flag(self->device, FWUPD_DEVICE_FLAG_INTERNAL)) {
		if (fu_release_has_flag(self, FWUPD_RELEASE_FLAG_IS_DOWNGRADE)) {
			if (fu_release_has_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD))
				return "org.freedesktop.fwupd.downgrade-hotplug-trusted";
			return "org.freedesktop.fwupd.downgrade-hotplug";
		}
		if (fu_release_has_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD))
			return "org.freedesktop.fwupd.update-hotplug-trusted";
		return "org.freedesktop.fwupd.update-hotplug";
	}

	/* internal device */
	if (fu_release_has_flag(self, FWUPD_RELEASE_FLAG_IS_DOWNGRADE)) {
		if (fu_release_has_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD))
			return "org.freedesktop.fwupd.downgrade-internal-trusted";
		return "org.freedesktop.fwupd.downgrade-internal";
	}
	if (fu_release_has_flag(self, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD))
		return "org.freedesktop.fwupd.update-internal-trusted";
	return "org.freedesktop.fwupd.update-internal";
}

/**
 * fu_release_compare:
 * @release1: first task to compare.
 * @release2: second task to compare.
 *
 * Compares two releases.
 *
 * Returns: 1, 0 or -1 if @release1 is greater, equal, or less than @release2, respectively.
 **/
gint
fu_release_compare(FuRelease *release1, FuRelease *release2)
{
	FuDevice *device1 = fu_release_get_device(release1);
	FuDevice *device2 = fu_release_get_device(release2);

	/* device order, lower is better */
	if (device1 != NULL && device2 != NULL && device1 != device2) {
		if (fu_device_get_order(device1) < fu_device_get_order(device2))
			return -1;
		if (fu_device_get_order(device1) > fu_device_get_order(device2))
			return 1;
	}

	/* release priority, higher is better */
	if (release1->priority > release2->priority)
		return -1;
	if (release1->priority < release2->priority)
		return 1;

	/* remote priority, higher is better */
	if (release1->remote != NULL && release2->remote != NULL) {
		if (fwupd_remote_get_priority(release1->remote) >
		    fwupd_remote_get_priority(release2->remote))
			return -1;
		if (fwupd_remote_get_priority(release1->remote) <
		    fwupd_remote_get_priority(release2->remote))
			return 1;
	}

	/* FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES has to be from oldest to newest */
	return fu_version_compare(fu_release_get_version(release1),
				  fu_release_get_version(release2),
				  fu_device_get_version_format(device1));
}

static void
fu_release_init(FuRelease *self)
{
	fu_release_set_flags(self, FWUPD_RELEASE_FLAG_NONE);
}

static void
fu_release_finalize(GObject *obj)
{
	FuRelease *self = FU_RELEASE(obj);

	g_free(self->update_request_id);
	g_free(self->device_version_old);
	if (self->request != NULL)
		g_object_unref(self->request);
	if (self->device != NULL)
		g_object_unref(self->device);
	if (self->remote != NULL)
		g_object_unref(self->remote);
	if (self->config != NULL)
		g_object_unref(self->config);
	if (self->stream != NULL)
		g_object_unref(self->stream);
	if (self->soft_reqs != NULL)
		g_ptr_array_unref(self->soft_reqs);
	if (self->hard_reqs != NULL)
		g_ptr_array_unref(self->hard_reqs);

	G_OBJECT_CLASS(fu_release_parent_class)->finalize(obj);
}

static void
fu_release_class_init(FuReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_release_finalize;
}

FuRelease *
fu_release_new(void)
{
	FuRelease *self;
	self = g_object_new(FU_TYPE_RELEASE, NULL);
	return FU_RELEASE(self);
}

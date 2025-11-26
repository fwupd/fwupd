/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include "fu-engine-requirements.h"

static gboolean
fu_engine_requirements_require_vercmp_part(const gchar *compare,
					   const gchar *version_req,
					   const gchar *version,
					   FwupdVersionFormat fmt,
					   GError **error)
{
	gboolean ret = FALSE;
	gint rc = 0;

	if (g_strcmp0(compare, "eq") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc == 0;
	} else if (g_strcmp0(compare, "ne") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc != 0;
	} else if (g_strcmp0(compare, "lt") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc < 0;
	} else if (g_strcmp0(compare, "gt") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc > 0;
	} else if (g_strcmp0(compare, "le") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc <= 0;
	} else if (g_strcmp0(compare, "ge") == 0) {
		rc = fu_version_compare(version, version_req, fmt);
		ret = rc >= 0;
	} else if (g_strcmp0(compare, "glob") == 0) {
		ret = g_pattern_match_simple(version_req, version);
	} else if (g_strcmp0(compare, "regex") == 0) {
		ret = g_regex_match_simple(version_req, version, 0, 0);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to compare [%s] and [%s]",
			    version_req,
			    version);
		return FALSE;
	}

	/* set error */
	if (!ret) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed predicate [%s %s %s]",
			    version_req,
			    compare,
			    version);
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef struct {
	FuRelease *release;
	FwupdInstallFlags install_flags;
	gchar *fwupd_version;
	gboolean has_hardware_req;
	gboolean has_not_hardware_req;
	gboolean has_id_requirement_glob;
	gboolean has_client_id_requirement_glob;
} FuEngineRequirementsHelper;

static void
fu_engine_requirements_helper_free(FuEngineRequirementsHelper *helper)
{
	g_object_unref(helper->release);
	g_free(helper->fwupd_version);
	g_free(helper);
}

static gboolean
fu_engine_requirements_check_fwupd_version(FuEngineRequirementsHelper *helper,
					   const gchar *fwupd_version_req,
					   GError **error)
{
	if (fu_version_compare(helper->fwupd_version,
			       fwupd_version_req,
			       FWUPD_VERSION_FORMAT_UNKNOWN) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "needs %s >= %s",
			    FWUPD_DBUS_SERVICE,
			    fwupd_version_req);
		return FALSE;
	}
	return TRUE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuEngineRequirementsHelper, fu_engine_requirements_helper_free)

static gboolean
fu_engine_requirements_require_vercmp(XbNode *req,
				      const gchar *version,
				      FwupdVersionFormat fmt,
				      FuEngineRequirementsHelper *helper,
				      GError **error)
{
	const gchar *compare = xb_node_get_attr(req, "compare");
	const gchar *version_req = xb_node_get_attr(req, "version");
	g_auto(GStrv) split = NULL;

	/* parse globbed version, e.g. `1.9.*=1.9.7|1.8.*=1.8.23|2.0.15`, or just `2.0.5` */
	split = g_strsplit(version_req, "|", 0);
	for (guint i = 0; split[i] != NULL; i++) {
		g_auto(GStrv) kv = g_strsplit(split[i], "=", 2);
		if (g_strv_length(kv) > 1) {
			helper->has_id_requirement_glob = TRUE;
			if (!g_pattern_match_simple(kv[0], version)) {
				g_debug("skipping vercmp %s as version %s", kv[0], version);
				continue;
			}
			g_debug("checking vercmp %s as version %s", kv[1], version);
			return fu_engine_requirements_require_vercmp_part(compare,
									  kv[1],
									  version,
									  fmt,
									  error);
		}
		return fu_engine_requirements_require_vercmp_part(compare,
								  kv[0],
								  version,
								  fmt,
								  error);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_requirements_check_not_child(FuEngine *self,
				       XbNode *req,
				       FuDevice *device,
				       FuEngineRequirementsHelper *helper,
				       GError **error)
{
	GPtrArray *children = fu_device_get_children(device);

	/* only <firmware> supported */
	if (g_strcmp0(xb_node_get_element(req), "firmware") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot handle not-child %s requirement",
			    xb_node_get_element(req));
		return FALSE;
	}

	/* check each child */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		const gchar *version = fu_device_get_version(child);
		if (version == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no version provided by %s, child of %s",
				    fu_device_get_name(child),
				    fu_device_get_name(device));
			return FALSE;
		}
		if (fu_engine_requirements_require_vercmp(req,
							  version,
							  fu_device_get_version_format(child),
							  helper,
							  NULL)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not compatible with child device version %s",
				    version);
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_engine_requirements_check_vendor_id(FuEngine *self,
				       XbNode *req,
				       FuDevice *device,
				       GError **error)
{
	GPtrArray *vendor_ids;
	const gchar *vendor_ids_metadata;
	g_autofree gchar *vendor_ids_device = NULL;

	/* devices without vendor IDs should not exist! */
	vendor_ids = fu_device_get_vendor_ids(device);
	if (vendor_ids->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device [%s] has no vendor ID",
			    fu_device_get_id(device));
		return FALSE;
	}

	/* metadata with empty vendor IDs should not exist! */
	vendor_ids_metadata = xb_node_get_attr(req, "version");
	if (vendor_ids_metadata == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "metadata has no vendor ID");
		return FALSE;
	}

	/* it is always safe to use a regex, even for simple strings */
	vendor_ids_device = fu_strjoin("|", vendor_ids);
	if (!g_regex_match_simple(vendor_ids_metadata, vendor_ids_device, 0, 0)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Not compatible with vendor %s: got %s",
			    vendor_ids_device,
			    vendor_ids_metadata);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
_fu_device_has_guids_any(FuDevice *self, gchar **guids)
{
	g_return_val_if_fail(FU_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(guids != NULL, FALSE);
	for (guint i = 0; guids[i] != NULL; i++) {
		if (fu_device_has_guid(self, guids[i]))
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_engine_requirements_check_firmware(FuEngine *self,
				      XbNode *req,
				      FuEngineRequirementsHelper *helper,
				      GError **error)
{
	FuDevice *device = fu_release_get_device(helper->release);
	const gchar *version;
	const gchar *depth_str;
	gint64 depth = G_MAXINT64;
	g_autoptr(FuDevice) device_actual = g_object_ref(device);
	g_autoptr(GError) error_local = NULL;
	g_auto(GStrv) guids = NULL;

	/* self tests */
	if (device == NULL)
		return TRUE;

	/* look at the parent device */
	depth_str = xb_node_get_attr(req, "depth");
	if (depth_str != NULL) {
		if (!fu_strtoll(depth_str, &depth, -1, 10, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		for (gint64 i = 0; i < depth; i++) {
			FuDevice *device_tmp = fu_device_get_parent(device_actual);
			if (device_tmp == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No parent device for %s "
					    "(%" G_GINT64_FORMAT "/%" G_GINT64_FORMAT ")",
					    fu_device_get_name(device_actual),
					    i,
					    depth);
				return FALSE;
			}
			g_set_object(&device_actual, device_tmp);
		}
	}

	/* check fwupd version requirement */
	if (depth < 0) {
		if (!fu_engine_requirements_check_fwupd_version(helper, "1.9.7", error)) {
			g_prefix_error_literal(error, "requirement child firmware: ");
			return FALSE;
		}
	}

	/* old firmware version */
	if (xb_node_get_text(req) == NULL) {
		version = fu_device_get_version(device_actual);
		if (!fu_engine_requirements_require_vercmp(
			req,
			version,
			fu_device_get_version_format(device_actual),
			helper,
			&error_local)) {
			if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Not compatible with firmware version %s, requires >= %s",
				    version,
				    xb_node_get_attr(req, "version"));
				return FALSE;
			}
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Not compatible with firmware version: %s",
				    error_local->message);
			return FALSE;
		}
		return TRUE;
	}

	/* bootloader version */
	if (g_strcmp0(xb_node_get_text(req), "bootloader") == 0) {
		version = fu_device_get_version_bootloader(device_actual);
		if (!fu_engine_requirements_require_vercmp(
			req,
			version,
			fu_device_get_version_format(device_actual),
			helper,
			&error_local)) {
			if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not compatible with bootloader version %s, requires >= %s",
				    version,
				    xb_node_get_attr(req, "version"));
				return FALSE;
			}
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Bootloader is not compatible");
			return FALSE;
		}
		return TRUE;
	}

	/* vendor ID */
	if (g_strcmp0(xb_node_get_text(req), "vendor-id") == 0) {
		if (helper->install_flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID)
			return TRUE;
		return fu_engine_requirements_check_vendor_id(self, req, device_actual, error);
	}

	/* child version */
	if (g_strcmp0(xb_node_get_text(req), "not-child") == 0)
		return fu_engine_requirements_check_not_child(self,
							      req,
							      device_actual,
							      helper,
							      error);

	/* another device, specified by GUID|GUID|GUID */
	guids = g_strsplit(xb_node_get_text(req), "|", -1);
	for (guint i = 0; guids[i] != NULL; i++) {
		if (!fwupd_guid_is_valid(guids[i])) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "%s is not a valid GUID",
				    guids[i]);
			return FALSE;
		}
	}

	/* find if any of the other devices exists */
	if (depth == G_MAXINT64) {
		g_autoptr(FuDevice) device_tmp = NULL;
		for (guint i = 0; guids[i] != NULL; i++) {
			g_autoptr(GPtrArray) devices =
			    fu_engine_get_devices_by_guid(self, guids[i], NULL);
			if (devices != NULL && devices->len > 0) {
				device_tmp = g_object_ref(g_ptr_array_index(devices, 0));
				break;
			}
		}
		if (device_tmp == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "No other device %s found",
				    xb_node_get_text(req));
			return FALSE;
		}
		g_set_object(&device_actual, device_tmp);

	} else if (depth == -1) {
		GPtrArray *children;
		FuDevice *child = NULL;

		/* look for a child */
		children = fu_device_get_children(device);
		for (guint i = 0; i < children->len; i++) {
			child = g_ptr_array_index(children, i);
			if (_fu_device_has_guids_any(child, guids))
				break;
			child = NULL;
		}
		if (child == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "No child found with GUID of %s",
				    xb_node_get_text(req));
			return FALSE;
		}
		g_set_object(&device_actual, child);

		/* look for a sibling */
	} else if (depth == 0) {
		FuDevice *child = NULL;
		FuDevice *parent = fu_device_get_parent(device_actual);
		GPtrArray *children;

		/* no parent, so look for GUIDs on this device */
		if (parent == NULL) {
			if (!_fu_device_has_guids_any(device_actual, guids)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No GUID of %s on device %s",
					    xb_node_get_text(req),
					    fu_device_get_name(device_actual));
				return FALSE;
			}
			return TRUE;
		}
		children = fu_device_get_children(parent);
		for (guint i = 0; i < children->len; i++) {
			child = g_ptr_array_index(children, i);
			if (_fu_device_has_guids_any(child, guids))
				break;
			child = NULL;
		}
		if (child == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "No sibling found with GUID of %s",
				    xb_node_get_text(req));
			return FALSE;
		}
		g_set_object(&device_actual, child);

		/* verify the parent device has the GUID */
	} else {
		if (!_fu_device_has_guids_any(device_actual, guids)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "No GUID of %s on parent device %s",
				    xb_node_get_text(req),
				    fu_device_get_name(device_actual));
			return FALSE;
		}
	}

	/* get the version of the other device */
	version = fu_device_get_version(device_actual);
	if (version != NULL && xb_node_get_attr(req, "compare") != NULL &&
	    !fu_engine_requirements_require_vercmp(req,
						   version,
						   fu_device_get_version_format(device_actual),
						   helper,
						   &error_local)) {
		if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Not compatible with %s version %s, requires >= %s",
				    fu_device_get_name(device_actual),
				    version,
				    xb_node_get_attr(req, "version"));
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Not compatible with %s: %s",
			    fu_device_get_name(device_actual),
			    error_local->message);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_requirements_check_id(FuEngine *self,
				XbNode *req,
				FuEngineRequirementsHelper *helper,
				GError **error)
{
	FuContext *ctx = fu_engine_get_context(self);
	g_autoptr(GError) error_local = NULL;
	const gchar *version;

	/* sanity check */
	if (xb_node_get_text(req) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no requirement value supplied");
		return FALSE;
	}
	version = fu_context_get_runtime_version(ctx, xb_node_get_text(req));
	if (version == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no version available for %s",
			    xb_node_get_text(req));
		return FALSE;
	}
	if (!fu_engine_requirements_require_vercmp(req,
						   version,
						   FWUPD_VERSION_FORMAT_UNKNOWN,
						   helper,
						   &error_local)) {
		if (g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Not compatible with %s version %s, requires >= %s",
				    xb_node_get_text(req),
				    version,
				    xb_node_get_attr(req, "version"));
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Not compatible with %s version: %s",
			    xb_node_get_text(req),
			    error_local->message);
		return FALSE;
	}

	g_debug("requirement %s %s %s -> %s passed",
		xb_node_get_attr(req, "version"),
		xb_node_get_attr(req, "compare"),
		version,
		xb_node_get_text(req));
	return TRUE;
}

static gboolean
fu_engine_requirements_check_hardware(FuEngine *self,
				      XbNode *req,
				      FuEngineRequirementsHelper *helper,
				      GError **error)
{
	FuContext *ctx = fu_engine_get_context(self);
	FuDevice *device = fu_release_get_device(helper->release);
	g_auto(GStrv) hwid_split = NULL;

	/* skip for tests */
	if (device == NULL || fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* sanity check */
	if (xb_node_get_text(req) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no requirement value supplied");
		return FALSE;
	}

	/* split and treat as OR */
	hwid_split = g_strsplit(xb_node_get_text(req), "|", -1);
	for (guint i = 0; hwid_split[i] != NULL; i++) {
		if (fu_context_has_hwid_guid(ctx, hwid_split[i])) {
			g_debug("HWID provided %s", hwid_split[i]);
			return TRUE;
		}
	}

	/* nothing matched */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "no HWIDs matched %s",
		    xb_node_get_text(req));
	return FALSE;
}

static gboolean
fu_engine_requirements_check_not_hardware(FuEngine *self,
					  XbNode *req,
					  FuEngineRequirementsHelper *helper,
					  GError **error)
{
	FuContext *ctx = fu_engine_get_context(self);
	g_auto(GStrv) hwid_split = NULL;

	/* check fwupd version requirement */
	if (!fu_engine_requirements_check_fwupd_version(helper, "1.9.10", error)) {
		g_prefix_error_literal(error, "requirement not_hardware: ");
		return FALSE;
	}

	/* sanity check */
	if (xb_node_get_text(req) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no requirement value supplied");
		return FALSE;
	}

	/* split and treat as OR */
	hwid_split = g_strsplit(xb_node_get_text(req), "|", -1);
	for (guint i = 0; hwid_split[i] != NULL; i++) {
		if (fu_context_has_hwid_guid(ctx, hwid_split[i])) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "%s HWIDs matched",
				    hwid_split[i]);
			return FALSE;
		}
	}

	/* nothing matched */
	return TRUE;
}

static gboolean
fu_engine_requirements_check_client(FuEngine *self,
				    XbNode *req,
				    FuEngineRequirementsHelper *helper,
				    GError **error)
{
	FuEngineRequest *request = fu_release_get_request(helper->release);
	FwupdFeatureFlags feature_flags;
	g_auto(GStrv) feature_split = NULL;

	/* sanity check */
	if (xb_node_get_text(req) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no requirement value supplied");
		return FALSE;
	}

	/* split and treat as AND */
	feature_split = g_strsplit(xb_node_get_text(req), "|", -1);
	feature_flags = fu_engine_request_get_feature_flags(request);
	for (guint i = 0; feature_split[i] != NULL; i++) {
		FuEngineCapabilityFlags capability_flag;
		FwupdFeatureFlags feature_flag;

		/* client feature */
		feature_flag = fwupd_feature_flag_from_string(feature_split[i]);
		if (feature_flag != FWUPD_FEATURE_FLAG_UNKNOWN) {
			if ((feature_flags & feature_flag) == 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "client feature requirement %s not supported",
					    feature_split[i]);
				return FALSE;
			}
			continue;
		}

		/* assumed by the daemon version, see https://github.com/fwupd/fwupd/pull/8949 */
		capability_flag = fu_engine_capability_flags_from_string(feature_split[i]);
		if (capability_flag != FU_ENGINE_CAPABILITY_FLAG_UNKNOWN) {
			if (capability_flag == FU_ENGINE_CAPABILITY_FLAG_ID_REQUIREMENT_GLOB) {
				helper->has_client_id_requirement_glob = TRUE;
				continue;
			}
		}

		/* not recognized */
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "client requirement %s unknown",
			    feature_split[i]);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_engine_requirements_check_hard(FuEngine *self,
				  XbNode *req,
				  FuEngineRequirementsHelper *helper,
				  GError **error)
{
	FuContext *ctx = fu_engine_get_context(self);

	/* ensure component requirement */
	if (g_strcmp0(xb_node_get_element(req), "id") == 0)
		return fu_engine_requirements_check_id(self, req, helper, error);

	/* ensure firmware requirement */
	if (g_strcmp0(xb_node_get_element(req), "firmware") == 0)
		return fu_engine_requirements_check_firmware(self, req, helper, error);

	/* ensure hardware requirement */
	if (g_strcmp0(xb_node_get_element(req), "hardware") == 0) {
		helper->has_hardware_req = TRUE;
		if (!fu_context_has_flag(ctx, FU_CONTEXT_FLAG_LOADED_HWINFO))
			return TRUE;
		return fu_engine_requirements_check_hardware(self, req, helper, error);
	}
	if (g_strcmp0(xb_node_get_element(req), "not_hardware") == 0) {
		helper->has_not_hardware_req = TRUE;
		if (!fu_context_has_flag(ctx, FU_CONTEXT_FLAG_LOADED_HWINFO))
			return TRUE;
		return fu_engine_requirements_check_not_hardware(self, req, helper, error);
	}

	/* ensure client requirement */
	if (g_strcmp0(xb_node_get_element(req), "client") == 0)
		return fu_engine_requirements_check_client(self, req, helper, error);

	/* not supported */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot handle requirement type %s",
		    xb_node_get_element(req));
	return FALSE;
}

static gboolean
fu_engine_requirements_check_soft(FuEngine *self,
				  XbNode *req,
				  FuEngineRequirementsHelper *helper,
				  GError **error)
{
	g_autoptr(GError) error_local = NULL;
	if (!fu_engine_requirements_check_hard(self, req, helper, &error_local)) {
		if (helper->install_flags & FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS) {
			g_info("ignoring soft-requirement: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_engine_requirements_is_specific_req(XbNode *req)
{
	if (g_strcmp0(xb_node_get_element(req), "firmware") == 0 &&
	    xb_node_get_attr(req, "depth") != NULL)
		return TRUE;
	if (g_strcmp0(xb_node_get_element(req), "hardware") == 0)
		return TRUE;
	return FALSE;
}

static gchar *
fu_engine_requirements_get_newest_fwupd_version(FuEngine *self, FuRelease *release, GError **error)
{
	GPtrArray *reqs = fu_release_get_hard_reqs(release);
	g_autoptr(GString) newest_version = g_string_new("1.0.0");

	/* trivial case */
	if (reqs == NULL)
		return g_string_free(g_steal_pointer(&newest_version), FALSE);

	/* find the newest fwupd requirement */
	for (guint i = 0; i < reqs->len; i++) {
		XbNode *req = g_ptr_array_index(reqs, i);
		if (g_strcmp0(xb_node_get_text(req), FWUPD_DBUS_SERVICE) == 0 &&
		    g_strcmp0(xb_node_get_attr(req, "compare"), "ge") == 0) {
			const gchar *version = xb_node_get_attr(req, "version");
			g_auto(GStrv) split = NULL;

			if (version == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "no version provided for requirement %s",
					    xb_node_get_text(req));
				return NULL;
			}

			/* only care about the fallback version if using globs */
			split = g_strsplit(version, "|", -1);
			for (guint j = 0; split[j] != NULL; j++) {
				if (g_strstr_len(split[j], -1, "=") != NULL)
					continue;
				/* is this newer than what we have */
				if (fu_version_compare(split[j],
						       newest_version->str,
						       FWUPD_VERSION_FORMAT_UNKNOWN) > 0) {
					g_string_assign(newest_version, split[j]);
				}
			}
		}
	}

	/* success */
	return g_string_free(g_steal_pointer(&newest_version), FALSE);
}

gboolean
fu_engine_requirements_check(FuEngine *self,
			     FuRelease *release,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDevice *device = fu_release_get_device(release);
	GPtrArray *reqs;
	gboolean has_specific_requirement = FALSE;
	g_autoptr(FuEngineRequirementsHelper) helper = g_new0(FuEngineRequirementsHelper, 1);

	/* create a small helper with common data */
	helper->release = g_object_ref(release);
	helper->install_flags = flags;

	/* get the newest fwupd version requirement */
	helper->fwupd_version =
	    fu_engine_requirements_get_newest_fwupd_version(self, release, error);
	if (helper->fwupd_version == NULL)
		return FALSE;

	/* sanity check */
	if (device != NULL && !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s [%s] is not updatable",
			    fu_device_get_name(device),
			    fu_device_get_id(device));
		return FALSE;
	}

	/* verify protocol */
	if (device != NULL && fu_release_get_protocol(release) != NULL &&
	    !fu_device_has_protocol(device, fu_release_get_protocol(release))) {
		g_autofree gchar *protocols = fu_strjoin(",", fu_device_get_protocols(device));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "release needs protocol %s but device has %s",
			    fu_release_get_protocol(release),
			    protocols);
		return FALSE;
	}

	/* hard requirements */
	reqs = fu_release_get_hard_reqs(release);
	if (reqs != NULL) {
		for (guint i = 0; i < reqs->len; i++) {
			XbNode *req = g_ptr_array_index(reqs, i);
			if (!fu_engine_requirements_check_hard(self, req, helper, error))
				return FALSE;
			if (fu_engine_requirements_is_specific_req(req))
				has_specific_requirement = TRUE;
		}
	}

	/* it does not make sense to allowlist and denylist at the same time */
	if (helper->has_hardware_req && helper->has_not_hardware_req) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "using hardware and not_hardware at the same time is not supported");
		return FALSE;
	}

	/* if we're using ID requirements with globs we have to have a client requirement */
	if (helper->has_id_requirement_glob && !helper->has_client_id_requirement_glob) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "using <id> version requirements with globs also needs "
				    "<client>id-requirement-glob</client>");
		return FALSE;
	}

	/* if a device uses a generic ID (i.e. not matching the OEM) then check to make sure the
	 * firmware is specific enough, e.g. by using a CHID or depth requirement */
	if (device != NULL && !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED) &&
	    fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES) &&
	    !has_specific_requirement) {
#ifdef SUPPORTED_BUILD
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "generic GUID requires a CHID, child, parent or sibling requirement");
		return FALSE;
#else
		if ((flags & FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "generic GUID requires --force, a CHID, child, parent "
					    "or sibling requirement");
			return FALSE;
		}
		g_info("ignoring enforce-requires requirement due to --force");
#endif
	}

	/* soft requirements */
	reqs = fu_release_get_soft_reqs(release);
	if (reqs != NULL) {
		for (guint i = 0; i < reqs->len; i++) {
			XbNode *req = g_ptr_array_index(reqs, i);
			if (!fu_engine_requirements_check_soft(self, req, helper, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

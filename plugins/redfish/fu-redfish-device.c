/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-backend.h"
#include "fu-redfish-common.h"
#include "fu-redfish-device.h"
#include "fu-redfish-firmware.h"

typedef struct {
	FuRedfishBackend *backend;
	FwupdJsonObject *json_obj_member;
	guint64 milestone;
	gchar *build;
	gchar *software_id;
	guint reset_pre_delay;	/* default of 0ms */
	guint reset_post_delay; /* default of 0ms */
} FuRedfishDevicePrivate;

enum { PROP_0, PROP_BACKEND, PROP_MEMBER, PROP_LAST };

G_DEFINE_TYPE_WITH_PRIVATE(FuRedfishDevice, fu_redfish_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_redfish_device_get_instance_private(o))

static void
fu_redfish_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE(device);
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "Milestone", priv->milestone);
	fwupd_codec_string_append(str, idt, "Build", priv->build);
	fwupd_codec_string_append(str, idt, "SoftwareId", priv->software_id);
	fwupd_codec_string_append_int(str, idt, "ResetPretDelay", priv->reset_pre_delay);
	fwupd_codec_string_append_int(str, idt, "ResetPostDelay", priv->reset_post_delay);
}

static void
fu_redfish_device_set_device_class(FuRedfishDevice *self, const gchar *tmp)
{
	if (g_strcmp0(tmp, "NetworkController") == 0) {
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_NETWORK_WIRED);
		return;
	}
	if (g_strcmp0(tmp, "MassStorageController") == 0) {
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_DRIVE_MULTIDISK);
		return;
	}
	if (g_strcmp0(tmp, "DisplayController") == 0) {
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_VIDEO_DISPLAY);
		return;
	}
	if (g_strcmp0(tmp, "DockingStation") == 0) {
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_DOCK);
		return;
	}
	if (g_strcmp0(tmp, "WirelessController") == 0) {
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_NETWORK_WIRELESS);
		return;
	}
	g_debug("no icon mapping for %s", tmp);
}

static gchar *
fu_redfish_device_build_namespace(const gchar *namespace, const gchar *id)
{
	if (namespace == NULL)
		return g_strdup(id);
	return g_strdup_printf("%s>%s", namespace, id);
}

static gboolean
fu_redfish_device_probe_related_pcie_item(FuRedfishDevice *self,
					  const gchar *uri,
					  const gchar *namespace,
					  GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	const gchar *tmp;
	guint64 vendor_id = 0x0;
	guint64 model_id = 0x0;
	guint64 subsystem_vendor_id = 0x0;
	guint64 subsystem_model_id = 0x0;
	g_autofree gchar *key_ven = NULL;
	g_autofree gchar *key_dev = NULL;
	g_autofree gchar *key_subsys = NULL;
	g_autofree gchar *namespace_new = NULL;
	g_autofree gchar *namespace_id = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;

	request = fu_redfish_backend_request_new(priv->backend, error);
	if (request == NULL)
		return FALSE;

	/* get URI */
	if (!fu_redfish_request_perform(request,
					uri,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
					    FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE,
					error))
		return FALSE;

	/* optional properties */
	json_obj = fu_redfish_request_get_json_object(request, error);
	if (json_obj == NULL)
		return FALSE;
	tmp = fwupd_json_object_get_string(json_obj, "DeviceClass", NULL);
	if (tmp != NULL && tmp[0] != '\0')
		fu_redfish_device_set_device_class(self, tmp);
	tmp = fwupd_json_object_get_string(json_obj, "VendorId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp, &vendor_id, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
	}
	tmp = fwupd_json_object_get_string(json_obj, "DeviceId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp, &model_id, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
	}
	tmp = fwupd_json_object_get_string(json_obj, "SubsystemVendorId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp,
				 &subsystem_vendor_id,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}
	tmp = fwupd_json_object_get_string(json_obj, "SubsystemId", NULL);
	if (tmp != NULL && tmp[0] != '\0') {
		if (!fu_strtoull(tmp,
				 &subsystem_model_id,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}

	/* add vendor ID */
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "PCI", vendor_id);

	/* add more instance IDs if possible */
	namespace_id = g_strdup_printf("VEN_%04X&DEV_%04X&SUBSYS_%04X%04X",
				       (guint)vendor_id,
				       (guint)model_id,
				       (guint)subsystem_vendor_id,
				       (guint)subsystem_model_id);
	namespace_new = fu_redfish_device_build_namespace(namespace, namespace_id);
	key_ven = g_strdup_printf("%s\\VEN", namespace_new);
	key_dev = g_strdup_printf("%s\\DEV", namespace_new);
	key_subsys = g_strdup_printf("%s\\SUBSYS", namespace_new);
	if (vendor_id != 0x0)
		fu_device_add_instance_u16(FU_DEVICE(self), key_ven, vendor_id);
	if (model_id != 0x0)
		fu_device_add_instance_u16(FU_DEVICE(self), key_dev, model_id);
	if (subsystem_vendor_id != 0x0 && subsystem_model_id != 0x0) {
		g_autofree gchar *subsys = NULL;
		subsys = g_strdup_printf("%04X%04X",
					 (guint)subsystem_vendor_id,
					 (guint)subsystem_model_id);
		fu_device_add_instance_str(FU_DEVICE(self), key_subsys, subsys);
	}
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "PCI", key_ven, key_dev, NULL);
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    "PCI",
				    key_ven,
				    key_dev,
				    key_subsys,
				    NULL);

	/* a component that comes in board variants takes a different image on each */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_REDFISH_DEVICE_FLAG_RELATED_CHASSIS_PCIE)) {
		fu_device_build_instance_id(FU_DEVICE(self),
					    NULL,
					    "REDFISH",
					    "VENDOR",
					    "ID",
					    key_ven,
					    key_dev,
					    key_subsys,
					    NULL);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_device_probe_related_pcie_functions(FuRedfishDevice *self,
					       const gchar *uri,
					       const gchar *namespace,
					       GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonArray) members_array = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	request = fu_redfish_backend_request_new(priv->backend, error);
	if (request == NULL)
		return FALSE;

	/* get URI */
	if (!fu_redfish_request_perform(request,
					uri,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
					    FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE,
					error))
		return FALSE;
	json_obj = fu_redfish_request_get_json_object(request, error);
	if (json_obj == NULL)
		return FALSE;
	members_array = fwupd_json_object_get_array(json_obj, "Members", NULL);
	if (members_array != NULL) {
		for (guint i = 0; i < fwupd_json_array_get_size(members_array); i++) {
			const gchar *id;
			g_autoptr(FwupdJsonObject) json_obj_member = NULL;

			json_obj_member = fwupd_json_array_get_object(members_array, i, error);
			if (json_obj_member == NULL)
				return FALSE;
			id = fwupd_json_object_get_string(json_obj_member, "@odata.id", NULL);
			if (id != NULL) {
				g_autofree gchar *namespace_new =
				    fu_redfish_device_build_namespace(namespace, "Func");
				if (!fu_redfish_device_probe_related_pcie_item(self,
									       id,
									       namespace_new,
									       error))
					return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_device_probe_related_item(FuRedfishDevice *self,
				     const gchar *uri,
				     const gchar *namespace,
				     GError **error);

static gboolean
fu_redfish_device_probe_related_pcie_devices(FuRedfishDevice *self,
					     const gchar *uri,
					     const gchar *namespace,
					     GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonArray) members_array = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	request = fu_redfish_backend_request_new(priv->backend, error);
	if (request == NULL)
		return FALSE;

	/* get URI */
	if (!fu_redfish_request_perform(request,
					uri,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
					    FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE,
					error))
		return FALSE;
	json_obj = fu_redfish_request_get_json_object(request, error);
	if (json_obj == NULL)
		return FALSE;
	members_array = fwupd_json_object_get_array(json_obj, "Members", NULL);
	if (members_array == NULL)
		return TRUE;
	for (guint i = 0; i < fwupd_json_array_get_size(members_array); i++) {
		const gchar *id;
		g_autoptr(FwupdJsonObject) json_obj_member = NULL;
		g_autofree gchar *namespace_new = NULL;

		json_obj_member = fwupd_json_array_get_object(members_array, i, error);
		if (json_obj_member == NULL)
			return FALSE;
		id = fwupd_json_object_get_string(json_obj_member, "@odata.id", NULL);
		if (id == NULL)
			continue;
		namespace_new = fu_redfish_device_build_namespace(namespace, "Devs");
		if (!fu_redfish_device_probe_related_item(self, id, namespace_new, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_device_probe_related_item(FuRedfishDevice *self,
				     const gchar *uri,
				     const gchar *namespace,
				     GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_pcie = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	request = fu_redfish_backend_request_new(priv->backend, error);
	if (request == NULL)
		return FALSE;

	/* get URI */
	if (!fu_redfish_request_perform(request,
					uri,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
					    FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE,
					error))
		return FALSE;

	/* optional properties */
	json_obj = fu_redfish_request_get_json_object(request, error);
	if (json_obj == NULL)
		return FALSE;
	tmp = fwupd_json_object_get_string(json_obj, "SerialNumber", NULL);
	if (tmp != NULL && tmp[0] != '\0' && g_strcmp0(tmp, "N/A") != 0)
		fu_device_set_serial(FU_DEVICE(self), tmp);

	/* this is better than the heuristic we get from the device name */
	if (fwupd_json_object_has_node(json_obj, "HotPluggable")) {
		gboolean hotpluggable = FALSE;
		if (!fwupd_json_object_get_boolean(json_obj, "HotPluggable", &hotpluggable, error))
			return FALSE;
		if (hotpluggable)
			fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
		else
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	}

	/* sometimes an array, sometimes an object! */
	json_pcie = fwupd_json_object_get_object(json_obj, "PCIeFunctions", NULL);
	if (json_pcie != NULL) {
		const gchar *id = fwupd_json_object_get_string(json_pcie, "@odata.id", NULL);
		if (id != NULL) {
			if (!fu_redfish_device_probe_related_pcie_functions(self,
									    id,
									    namespace,
									    error))
				return FALSE;
		}
	}

	/* a chassis lists its PCIe devices, but can also be the whole system */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_REDFISH_DEVICE_FLAG_RELATED_CHASSIS_PCIE)) {
		g_autoptr(FwupdJsonObject) json_devices =
		    fwupd_json_object_get_object(json_obj, "PCIeDevices", NULL);
		if (json_devices != NULL) {
			const gchar *id =
			    fwupd_json_object_get_string(json_devices, "@odata.id", NULL);
			if (id != NULL) {
				if (!fu_redfish_device_probe_related_pcie_devices(self,
										  id,
										  namespace,
										  error))
					return FALSE;
			}
		}
	}
	return TRUE;
}

/* parses a Lenovo XCC-format version like "11A-1.02" */
static gboolean
fu_redfish_device_set_version_lenovo(FuRedfishDevice *self, const gchar *version, GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *out_build = NULL;
	g_autofree gchar *out_version = NULL;

	/* split up Lenovo format */
	if (!fu_redfish_common_parse_version_lenovo(version, &out_build, &out_version, error))
		return FALSE;

	/* split out milestone */
	priv->milestone = g_ascii_strtoull(out_build, NULL, 10); /* nocheck:blocked */

	/* odd numbered builds are unsigned */
	if (priv->milestone % 2 != 0) {
		fu_device_add_private_flag(FU_DEVICE(self), FU_REDFISH_DEVICE_FLAG_UNSIGNED_BUILD);
	}

	/* build is only one letter from A -> Z */
	if (!g_ascii_isalpha(out_build[2])) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "build letter invalid");
		return FALSE;
	}
	priv->build = g_strndup(out_build + 2, 1);
	fu_device_set_version(FU_DEVICE(self), out_version);
	fu_device_set_version_format(FU_DEVICE(self), fu_version_guess_format(out_version));
	return TRUE;
}

static void
fu_redfish_device_set_version(FuRedfishDevice *self, const gchar *tmp)
{
	/* OEM specific */
	if (g_strcmp0(fu_device_get_vendor(FU_DEVICE(self)), "Lenovo") == 0) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_redfish_device_set_version_lenovo(self, tmp, &error_local)) {
			g_debug("failed to parse Lenovo version %s: %s", tmp, error_local->message);
		}
	}

	/* fallback */
	if (fu_device_get_version(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *ver = fu_redfish_common_fix_version(tmp);
		if (ver != NULL) {
			fu_device_set_version(FU_DEVICE(self), ver);
			fu_device_set_version_format(FU_DEVICE(self), fu_version_guess_format(ver));
		}
	}
}

static void
fu_redfish_device_set_version_lowest(FuRedfishDevice *self, const gchar *tmp)
{
	/* OEM specific */
	if (g_strcmp0(fu_device_get_vendor(FU_DEVICE(self)), "Lenovo") == 0) {
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *out_version = NULL;
		if (!fu_redfish_common_parse_version_lenovo(tmp,
							    NULL,
							    &out_version,
							    &error_local)) {
			g_debug("failed to parse Lenovo version %s: %s", tmp, error_local->message);
		}
		fu_device_set_version_lowest(FU_DEVICE(self), out_version);
	}

	/* fallback */
	if (fu_device_get_version_lowest(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *ver = fu_redfish_common_fix_version(tmp);
		fu_device_set_version_lowest(FU_DEVICE(self), ver);
	}
}

static void
fu_redfish_device_set_name(FuRedfishDevice *self, const gchar *name)
{
	/* useless */
	if (g_str_has_prefix(name, "Firmware:"))
		name += 9;

	/* device type */
	if (g_str_has_prefix(name, "DEVICE-")) {
		name += 7;
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	} else if (g_str_has_prefix(name, "DISK-")) {
		name += 5;
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_DRIVE_HARDDISK);
	} else if (g_str_has_prefix(name, "POWER-")) {
		name += 6;
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_AC_ADAPTER);
		fu_device_set_summary(FU_DEVICE(self), "Redfish power supply unit");
	} else {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	}

	/* heuristics */
	if (g_strcmp0(name, "BMC") == 0)
		fu_device_set_summary(FU_DEVICE(self), "Redfish baseboard management controller");
	if (g_str_has_suffix(name, "HBA"))
		fu_device_set_summary(FU_DEVICE(self), "Redfish host bus adapter");

	/* success */
	fu_device_set_name(FU_DEVICE(self), name);
}

static void
fu_redfish_device_set_vendor(FuRedfishDevice *self, const gchar *vendor)
{
	g_autofree gchar *vendor_upper = NULL;

	/* fixup a common mistake */
	if (g_strcmp0(vendor, "LEN") == 0 || g_strcmp0(vendor, "LNVO") == 0)
		vendor = "Lenovo";
	fu_device_set_vendor(FU_DEVICE(self), vendor);

	/* add vendor-id */
	vendor_upper = g_ascii_strup(vendor, -1);
	g_strdelimit(vendor_upper, " ", '_');
	fu_device_build_vendor_id(FU_DEVICE(self), "REDFISH", vendor_upper);
}

static gboolean
fu_redfish_device_smc_license_check(FuRedfishDevice *self, GError **error)
{
	FuRedfishBackend *backend;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(GError) error_local = NULL;

	/* see if we don't get an license error */
	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return FALSE;
	request = fu_redfish_backend_request_new(backend, error);
	if (request == NULL)
		return FALSE;
	if (!fu_redfish_request_perform(request,
					fu_redfish_backend_get_push_uri_path(backend),
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					&error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			fu_device_add_problem(FU_DEVICE(self),
					      FWUPD_DEVICE_PROBLEM_MISSING_LICENSE);
		} else {
			g_debug("supermicro license check returned %s", error_local->message);
		}
	}
	return TRUE;
}

static gboolean
fu_redfish_device_probe_oem_hpe_targets(FuRedfishDevice *self,
					FwupdJsonArray *json_arr,
					const gchar *device_class,
					GError **error)
{
	for (guint i = 0; i < fwupd_json_array_get_size(json_arr); i++) {
		const gchar *tmp = fwupd_json_array_get_string(json_arr, i, NULL);
		if (tmp == NULL)
			continue;
		fu_device_add_instance_id(FU_DEVICE(self), tmp);

		/* neither is unique on its own: the target is shared with other devices
		 * of the board and the class with other platforms, so also add the pair */
		if (device_class != NULL) {
			g_autofree gchar *target = g_ascii_strdown(tmp, -1);
			g_autofree gchar *key_vendor = g_strdup_printf("HPE-%u\\VENDOR", i);
			g_autofree gchar *key_class = g_strdup_printf("HPE-%u\\CLASS", i);
			g_autofree gchar *key_target = g_strdup_printf("HPE-%u\\TARGET", i);

			fu_device_add_instance_str(FU_DEVICE(self), key_vendor, "HPE");
			fu_device_add_instance_str(FU_DEVICE(self), key_class, device_class);
			fu_device_add_instance_str(FU_DEVICE(self), key_target, target);
			if (!fu_device_build_instance_id(FU_DEVICE(self),
							 error,
							 "REDFISH",
							 key_vendor,
							 key_class,
							 key_target,
							 NULL))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_redfish_device_probe_oem_hpe(FuRedfishDevice *self, FwupdJsonObject *json_obj, GError **error)
{
	const gchar *tmp;
	g_autofree gchar *device_class = NULL;
	g_autoptr(FwupdJsonArray) json_arr = NULL;

	tmp = fwupd_json_object_get_string(json_obj, "DeviceClass", NULL);
	if (tmp != NULL) {
		fu_device_add_instance_id(FU_DEVICE(self), tmp);
		device_class = g_ascii_strdown(tmp, -1);
	}
	json_arr = fwupd_json_object_get_array(json_obj, "Targets", NULL);
	if (json_arr != NULL) {
		if (!fu_redfish_device_probe_oem_hpe_targets(self, json_arr, device_class, error))
			return FALSE;
	}
	return TRUE;
}

/* Dell reports the PCI identity of a device as hex strings without a prefix, e.g. "1344" */
static gboolean
fu_redfish_device_dell_get_u16(FwupdJsonObject *json_obj, const gchar *key, guint16 *value)
{
	const gchar *tmp = fwupd_json_object_get_string(json_obj, key, NULL);
	guint64 tmp64 = 0;

	if (tmp == NULL || tmp[0] == '\0')
		return FALSE;
	if (!fu_strtoull(tmp, &tmp64, 0x1, G_MAXUINT16, FU_INTEGER_BASE_16, NULL))
		return FALSE;
	*value = (guint16)tmp64;
	return TRUE;
}

/* Some Dell devices, e.g. disks, report a SoftwareId of "0" and a ComponentID of "0", which
 * does not tell one device from another. The PCI identity they report in DellSoftwareInventory
 * does, and is the same as the PCIInfo of the device in the Dell catalog. */
static void
fu_redfish_device_probe_oem_dell_pci_id(FuRedfishDevice *self, FwupdJsonObject *software_info)
{
	guint16 vendor_id = 0;
	guint16 device_id = 0;
	guint16 subvendor_id = 0;
	guint16 subdevice_id = 0;
	g_autofree gchar *subsys = NULL;

	if (!fu_redfish_device_dell_get_u16(software_info, "VendorID", &vendor_id) ||
	    !fu_redfish_device_dell_get_u16(software_info, "DeviceID", &device_id) ||
	    !fu_redfish_device_dell_get_u16(software_info, "SubVendorID", &subvendor_id) ||
	    !fu_redfish_device_dell_get_u16(software_info, "SubDeviceID", &subdevice_id))
		return;

	subsys = g_strdup_printf("%04X%04X", subvendor_id, subdevice_id);
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", vendor_id);
	fu_device_add_instance_u16(FU_DEVICE(self), "DEV", device_id);
	fu_device_add_instance_str(FU_DEVICE(self), "SUBSYS", subsys);

	/* the same device needs different firmware in different systems */
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    "REDFISH",
				    "VENDOR",
				    "SYSTEMID",
				    "VEN",
				    "DEV",
				    "SUBSYS",
				    NULL);
}

static gboolean
fu_redfish_device_probe_oem_dell(FuRedfishDevice *self,
				 FwupdJsonObject *json_obj,
				 gboolean has_software_id,
				 GError **error)
{
	const gchar *tmp;
	g_autoptr(FwupdJsonObject) software_info = NULL;

	/* ignore */
	software_info = fwupd_json_object_get_object(json_obj, "DellSoftwareInventory", NULL);
	if (software_info == NULL)
		return TRUE;

	tmp = fwupd_json_object_get_string(software_info, "Status", NULL);
	if (g_strcmp0(tmp, "AvailableForInstallation") == 0)
		fu_device_add_private_flag(FU_DEVICE(self), FU_REDFISH_DEVICE_FLAG_IS_BACKUP);
	tmp = fwupd_json_object_get_string(software_info, "Id", NULL);
	if (tmp != NULL && g_ascii_strncasecmp(tmp, "DCIM:INSTALLED", 14) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware is in repository");
		return FALSE;
	}

	/* it does not seem that Dell allows targeting a device when updating */
	fu_device_add_private_flag(FU_DEVICE(self), FU_REDFISH_DEVICE_FLAG_WILDCARD_TARGETS);

	/* on Dell systems the @odata.id URI contains the firmware version, which changes after an
	 * update and causes the device hash to change -- use the DellSoftwareInventory Id instead
	 * as it is version-independent */
	if (tmp != NULL)
		fu_device_set_logical_id(FU_DEVICE(self), tmp);

	/* SYSTEMID is set by the backend */
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "REDFISH",
					      "VENDOR",
					      "SYSTEMID",
					      NULL))
		return FALSE;

	/* identify the device by what it is, as the SoftwareId may not be usable */
	fu_redfish_device_probe_oem_dell_pci_id(self, software_info);

	/* there is no SOFTWAREID to build from when the device does not report a useful one */
	if (!has_software_id)
		return TRUE;
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "REDFISH",
					      "VENDOR",
					      "SOFTWAREID",
					      NULL))
		return FALSE;
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "REDFISH",
					   "VENDOR",
					   "SYSTEMID",
					   "SOFTWAREID",
					   NULL);
}

static void
fu_redfish_device_set_software_id(FuRedfishDevice *self, const gchar *software_id)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *software_id_lower = g_ascii_strdown(software_id, -1);

	/* save this as-provided */
	g_set_str(&priv->software_id, software_id);

	/* already a GUID */
	if (fwupd_guid_is_valid(software_id_lower)) {
		fu_device_add_instance_id(FU_DEVICE(self), software_id_lower);
		return;
	}

	/* for Lenovo */
	fu_device_add_instance_str(FU_DEVICE(self), "SOFTWAREID", software_id);
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_REDFISH_DEVICE_FLAG_UNSIGNED_BUILD))
		fu_device_add_instance_str(FU_DEVICE(self), "TYPE", "UNSIGNED");
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    "REDFISH",
				    "VENDOR",
				    "SOFTWAREID",
				    "TYPE",
				    NULL);
}

static gboolean
fu_redfish_device_probe(FuDevice *dev, GError **error)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE(dev);
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	const gchar *software_id;
	gboolean updateable = FALSE;
	g_autoptr(FwupdJsonObject) json_oem = NULL;

	/* sanity check */
	if (priv->json_obj_member == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no member");
		return FALSE;
	}

	/* required to POST later */
	tmp = fwupd_json_object_get_string(priv->json_obj_member, "@odata.id", error);
	if (tmp == NULL)
		return FALSE;
	fu_device_set_physical_id(dev, "Redfish-Inventory");
	fu_device_set_logical_id(dev, tmp);

	tmp = fwupd_json_object_get_string(priv->json_obj_member, "Id", NULL);
	if (tmp != NULL)
		fu_device_set_backend_id(dev, tmp);
	fu_device_add_instance_str(dev, "ID", fu_device_get_backend_id(dev));

	/* device properties */
	tmp = fwupd_json_object_get_string(priv->json_obj_member, "Manufacturer", NULL);
	if (tmp != NULL && tmp[0] != '\0')
		fu_redfish_device_set_vendor(self, tmp);
	else
		fu_redfish_device_set_vendor(self, fu_redfish_backend_get_vendor(priv->backend));
	fu_device_add_instance_strsafe(dev, "VENDOR", fu_device_get_vendor(dev));

	/* the version can encode the instance ID suffix */
	tmp = fwupd_json_object_get_string(priv->json_obj_member, "Version", NULL);
	if (tmp != NULL && tmp[0] != '\0')
		fu_redfish_device_set_version(self, tmp);

	/* ReleaseDate may or may not have a timezone */
	tmp = fwupd_json_object_get_string(priv->json_obj_member, "ReleaseDate", NULL);
	if (tmp != NULL && tmp[0] != '\0' && g_strcmp0(tmp, "00:00:00Z") != 0) {
		g_autoptr(GDateTime) dt = NULL;
		g_autoptr(GTimeZone) tz = g_time_zone_new_utc();
		dt = g_date_time_new_from_iso8601(tmp, tz);
		if (dt != NULL) {
			guint64 unixtime = (guint64)g_date_time_to_unix(dt);
			fu_device_set_version_build_date(dev, unixtime);
		} else {
			g_warning("failed to parse ISO8601 %s", tmp);
		}
	}

	/* some vendors use a GUID, others use an ID like BMC-AFBT-10 */
	software_id = fwupd_json_object_get_string(priv->json_obj_member, "SoftwareId", NULL);
	if (g_strcmp0(software_id, "0") == 0) {
		/* Dell reports "0" for devices that have none, like disks: every one of them would
		 * share an instance ID, so a firmware for one would match them all */
		software_id = NULL;
	}
	if (software_id != NULL)
		fu_redfish_device_set_software_id(self, software_id);

	/* get vendor-specific properties too */
	json_oem = fwupd_json_object_get_object(priv->json_obj_member, "Oem", NULL);
	if (json_oem != NULL) {
		g_autoptr(FwupdJsonObject) json_dell = NULL;
		g_autoptr(FwupdJsonObject) json_hpe = NULL;

		json_hpe = fwupd_json_object_get_object(json_oem, "Hpe", NULL);
		if (json_hpe != NULL) {
			if (!fu_redfish_device_probe_oem_hpe(self, json_hpe, error))
				return FALSE;
		}
		json_dell = fwupd_json_object_get_object(json_oem, "Dell", NULL);
		if (json_dell != NULL) {
			if (!fu_redfish_device_probe_oem_dell(self,
							      json_dell,
							      software_id != NULL,
							      error))
				return FALSE;
		}
	}

	/* used for matching, quirking and parenting */
	fu_device_build_instance_id_full(dev,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "REDFISH",
					 "VENDOR",
					 "ID",
					 NULL);

	tmp = fwupd_json_object_get_string(priv->json_obj_member, "Name", NULL);
	if (tmp != NULL && tmp[0] != '\0')
		fu_redfish_device_set_name(self, tmp);
	tmp = fwupd_json_object_get_string(priv->json_obj_member, "LowestSupportedVersion", NULL);
	if (tmp != NULL && tmp[0] != '\0')
		fu_redfish_device_set_version_lowest(self, tmp);
	tmp = fwupd_json_object_get_string(priv->json_obj_member, "Description", NULL);
	if (tmp != NULL && tmp[0] != '\0')
		fu_device_set_summary(dev, tmp);

	/* reasons why the device might not be updatable */
	if (!fwupd_json_object_get_boolean_with_default(priv->json_obj_member,
							"Updateable",
							&updateable,
							TRUE,
							error))
		return FALSE;
	if (updateable)
		fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* not useful to export */
	if (fu_device_has_private_flag(dev, FU_REDFISH_DEVICE_FLAG_IS_BACKUP)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s is a backup partition",
			    fu_device_get_backend_id(dev));
		return FALSE;
	}

	/* use related items to set extra instance IDs */
	if (updateable && fwupd_json_object_has_node(priv->json_obj_member, "RelatedItem")) {
		g_autoptr(FwupdJsonArray) json_array_related = NULL;
		json_array_related =
		    fwupd_json_object_get_array(priv->json_obj_member, "RelatedItem", NULL);
		if (json_array_related != NULL) {
			for (guint i = 0; i < fwupd_json_array_get_size(json_array_related); i++) {
				g_autoptr(FwupdJsonObject) json_object_related = NULL;

				json_object_related =
				    fwupd_json_array_get_object(json_array_related, i, error);
				if (json_object_related == NULL)
					return FALSE;
				tmp = fwupd_json_object_get_string(json_object_related,
								   "@odata.id",
								   NULL);
				if (tmp == NULL)
					continue;
				if (!fu_redfish_device_probe_related_item(self, tmp, NULL, error))
					return FALSE;
			}
		}
	}

	/* for Supermicro check whether we have a proper Redfish license installed */
	if (g_strcmp0("SMCI", fu_device_get_vendor(dev)) == 0) {
		if (!fu_redfish_device_smc_license_check(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

FuRedfishBackend *
fu_redfish_device_get_backend(FuRedfishDevice *self, GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->backend == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no backend");
		return NULL;
	}
	return priv->backend;
}

gboolean
fu_redfish_device_verify_target_identity(FuRedfishDevice *self, GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *logical_id = fu_device_get_logical_id(FU_DEVICE(self));
	const gchar *software_id_now;
	gboolean reset_pending;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(GError) error_local = NULL;

	/* a wildcard update has no single member to re-confirm */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_REDFISH_DEVICE_FLAG_WILDCARD_TARGETS))
		return TRUE;

	/* nothing was captured at probe to compare against */
	if (logical_id == NULL || priv->software_id == NULL)
		return TRUE;

	/* while a reset or activation is pending the member is expected to be
	 * unreachable or stale, so an unavailable readback is tolerated; otherwise
	 * the task claimed completion and a missing readback is a real problem */
	reset_pending = fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT) ||
			fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);

	request = fu_redfish_backend_request_new(priv->backend, error);
	if (request == NULL)
		return FALSE;
	if (!fu_redfish_request_perform(request,
					logical_id,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					&error_local)) {
		if (reset_pending) {
			g_debug("failed to re-read %s to confirm target identity: %s",
				logical_id,
				error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to re-read %s to confirm target identity: ",
					   logical_id);
		return FALSE;
	}

	/* a success with an empty body parses no object, leaving nothing to compare */
	json_obj = fu_redfish_request_get_json_object(request, &error_local);
	if (json_obj == NULL) {
		if (reset_pending) {
			g_debug("no readback body for %s to confirm target identity", logical_id);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to re-read %s to confirm target identity: ",
					   logical_id);
		return FALSE;
	}

	/* the SoftwareId was saved as-provided at probe, so compare it as-provided */
	software_id_now = fwupd_json_object_get_string(json_obj, "SoftwareId", NULL);
	if (g_strcmp0(software_id_now, priv->software_id) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Redfish target identity changed: member %s reported SoftwareId %s at "
			    "discovery "
			    "but %s on re-read",
			    logical_id,
			    priv->software_id,
			    software_id_now != NULL ? software_id_now : "(none)");
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef struct {
	gchar *location;
	gboolean completed;
	GHashTable *messages_seen;
	FuProgress *progress;
} FuRedfishDevicePollCtx;

gboolean
fu_redfish_device_parse_message_id(FuRedfishDevice *self,
				   const gchar *message_id,
				   const gchar *message,
				   FuProgress *progress,
				   GError **error)
{
	/* fallback */
	if (message == NULL)
		message = "Unknown failure";

	/* ignore */
	if (g_pattern_match_simple("TaskEvent.*.TaskProgressChanged", message_id) ||
	    g_pattern_match_simple("TaskEvent.*.TaskCompletedWarning", message_id) ||
	    g_pattern_match_simple("TaskEvent.*.TaskCompletedOK", message_id) ||
	    g_pattern_match_simple("Base.*.Success", message_id))
		return TRUE;

	/* set flags */
	if (g_pattern_match_simple("Base.*.ResetRequired", message_id) ||
	    g_pattern_match_simple("iLO.*.SystemResetRequired", message_id) ||
	    g_pattern_match_simple("IDRAC.*.JCP001", message_id) ||
	    g_pattern_match_simple("IDRAC.*.RED014", message_id)) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		return TRUE;
	}

	if (g_pattern_match_simple("IDRAC.*.PR19", message_id)) {
		/* Some firmware can be immediately installed on Dell servers, but the inventory
		   is only refreshed on reboot
		*/
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		return TRUE;
	}

	/* set error code */
	if (g_pattern_match_simple("Update.*.AwaitToActivate", message_id)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NEEDS_USER_ACTION, message);
		return FALSE;
	}
	if (g_pattern_match_simple("Update.*.TransferFailed", message_id)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, message);
		return FALSE;
	}
	if (g_pattern_match_simple("Update.*.ActivateFailed", message_id)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, message);
		return FALSE;
	}
	if (g_pattern_match_simple("Update.*.VerificationFailed", message_id) ||
	    g_pattern_match_simple("LenovoFirmwareUpdateRegistry.*.UpdateVerifyFailed",
				   message_id)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, message);
		return FALSE;
	}
	if (g_pattern_match_simple("Update.*.ApplyFailed", message_id) ||
	    g_pattern_match_simple("iLO.*.UpdateFailed", message_id)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, message);
		return FALSE;
	}

	/* set status */
	if (g_pattern_match_simple("Update.*.TargetDetermined", message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_LOADING);
		return TRUE;
	}
	if (g_pattern_match_simple("LenovoFirmwareUpdateRegistry.*.UpdateAssignment", message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_LOADING);
		return TRUE;
	}
	if (g_pattern_match_simple("LenovoFirmwareUpdateRegistry.*.PayloadApplyInProgress",
				   message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
		return TRUE;
	}
	if (g_pattern_match_simple("LenovoFirmwareUpdateRegistry.*.PayloadApplyCompleted",
				   message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_IDLE);
		return TRUE;
	}
	if (g_pattern_match_simple("LenovoFirmwareUpdateRegistry.*.UpdateVerifyInProgress",
				   message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
		return TRUE;
	}
	if (g_pattern_match_simple("Update.*.TransferringToComponent", message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_LOADING);
		return TRUE;
	}
	if (g_pattern_match_simple("Update.*.VerifyingAtComponent", message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
		return TRUE;
	}
	if (g_pattern_match_simple("Update.*.UpdateInProgress", message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
		return TRUE;
	}
	if (g_pattern_match_simple("Update.*.UpdateSuccessful", message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_IDLE);
		return TRUE;
	}
	if (g_pattern_match_simple("Update.*.InstallingOnComponent", message_id)) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
		return TRUE;
	}

	/* nothing to do */
	return TRUE;
}

/**
 * fu_redfish_device_parse_message:
 * @self: a #FuRedfishDevice
 * @json_message: a #FwupdJsonObject holding `MessageId` and `Message` members
 * @progress: a #FuProgress
 * @messages_seen: (nullable): a #GHashTable used to deduplicate already-handled
 *     messages, or %NULL to handle every message
 * @message: (nullable): a #GString updated in-place with the `Message` string
 *     when one is present, or %NULL to ignore it
 * @error: (nullable): optional return location for an error
 *
 * Reads the `MessageId` and `Message` members from a Redfish message object and
 * dispatches them to fu_redfish_device_parse_message_id().
 *
 * When @messages_seen is set, a message that has already been handled is skipped
 * so its side effects are not applied twice. When @message is set and the object
 * carries a `Message` string, @message is assigned that value; otherwise it is
 * left untouched so any default it was seeded with is preserved.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_redfish_device_parse_message(FuRedfishDevice *self,
				FwupdJsonObject *json_message,
				FuProgress *progress,
				GHashTable *messages_seen,
				GString *message,
				GError **error)
{
	const gchar *message_id;
	const gchar *message_tmp;

	message_id = fwupd_json_object_get_string(json_message, "MessageId", NULL);
	message_tmp = fwupd_json_object_get_string(json_message, "Message", NULL);
	if (message != NULL && message_tmp != NULL)
		g_string_assign(message, message_tmp);

	/* ignore messages we've seen before */
	if (messages_seen != NULL) {
		g_autofree gchar *message_key =
		    message_tmp == NULL ? g_strdup(message_id)
					: g_strdup_printf("%s;%s", message_id, message_tmp);
		if (g_hash_table_contains(messages_seen, message_key)) {
			g_debug("ignoring %s", message_key);
			return TRUE;
		}
		g_hash_table_add(messages_seen, g_steal_pointer(&message_key));
	}

	/* use the message */
	g_debug("message [%s]: %s", message_id, message_tmp);
	return fu_redfish_device_parse_message_id(self, message_id, message_tmp, progress, error);
}

static gboolean
fu_redfish_device_poll_task_once(FuRedfishDevice *self, FuRedfishDevicePollCtx *ctx, GError **error)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *state_tmp;
	gint64 pc = 0;
	g_autoptr(GString) message = g_string_new("Unknown failure");
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(FwupdJsonArray) json_msgs = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	request = fu_redfish_backend_request_new(priv->backend, error);
	if (request == NULL)
		return FALSE;

	/* create URI and poll */
	if (!fu_redfish_request_perform(request,
					ctx->location,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;

	/* percentage is optional */
	json_obj = fu_redfish_request_get_json_object(request, error);
	if (json_obj == NULL)
		return FALSE;
	if (!fwupd_json_object_get_integer_with_default(json_obj,
							"PercentComplete",
							&pc,
							-1,
							error))
		return FALSE;
	if (pc >= 0 && pc <= 100)
		fu_progress_set_percentage(ctx->progress, (guint)pc);

	/* print all messages we've not seen yet */
	json_msgs = fwupd_json_object_get_array(json_obj, "Messages", NULL);
	if (json_msgs != NULL) {
		for (guint i = 0; i < fwupd_json_array_get_size(json_msgs); i++) {
			g_autoptr(FwupdJsonObject) json_message = NULL;

			/* set additional device properties */
			json_message = fwupd_json_array_get_object(json_msgs, i, error);
			if (json_message == NULL)
				return FALSE;
			if (!fu_redfish_device_parse_message(self,
							     json_message,
							     ctx->progress,
							     ctx->messages_seen,
							     message,
							     error))
				return FALSE;
		}
	}

	/* use taskstate to set context */
	if (!fwupd_json_object_has_node(json_obj, "TaskState")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no TaskState for task manager");
		return FALSE;
	}
	state_tmp = fwupd_json_object_get_string(json_obj, "TaskState", NULL);
	g_debug("TaskState now %s", state_tmp);
	if (g_strcmp0(state_tmp, "Completed") == 0 ||
	    fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT)) {
		ctx->completed = TRUE;
		return TRUE;
	}
	if (g_strcmp0(state_tmp, "Cancelled") == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "Task was cancelled");
		return FALSE;
	}
	if (g_strcmp0(state_tmp, "Exception") == 0 ||
	    g_strcmp0(state_tmp, "UserIntervention") == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, message->str);
		return FALSE;
	}

	/* try again */
	return TRUE;
}

static FuRedfishDevicePollCtx *
fu_redfish_device_poll_ctx_new(FuProgress *progress, const gchar *location)
{
	FuRedfishDevicePollCtx *ctx = g_new0(FuRedfishDevicePollCtx, 1);
	ctx->messages_seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	ctx->location = g_strdup(location);
	ctx->progress = g_object_ref(progress);
	return ctx;
}

static void
fu_redfish_device_poll_ctx_free(FuRedfishDevicePollCtx *ctx)
{
	g_hash_table_unref(ctx->messages_seen);
	g_object_unref(ctx->progress);
	g_free(ctx->location);
	g_free(ctx);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuRedfishDevicePollCtx, fu_redfish_device_poll_ctx_free)
#pragma clang diagnostic pop

gboolean
fu_redfish_device_poll_task(FuRedfishDevice *self,
			    const gchar *location,
			    FuProgress *progress,
			    GError **error)
{
	const guint timeout = 2400;
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(FuRedfishDevicePollCtx) ctx = fu_redfish_device_poll_ctx_new(progress, location);

	/* sleep and then reprobe hardware */
	do {
		fu_device_sleep(FU_DEVICE(self), 1000); /* ms */
		if (!fu_redfish_device_poll_task_once(self, ctx, error))
			return FALSE;
		if (ctx->completed) {
			fu_progress_finished(progress);
			return TRUE;
		}
	} while (g_timer_elapsed(timer, NULL) < timeout);

	/* success */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "failed to poll %s for success after %u seconds",
		    location,
		    timeout);
	return FALSE;
}

guint
fu_redfish_device_get_reset_pre_delay(FuRedfishDevice *self)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	return priv->reset_pre_delay;
}

guint
fu_redfish_device_get_reset_post_delay(FuRedfishDevice *self)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	return priv->reset_post_delay;
}

static gboolean
fu_redfish_device_set_quirk_kv(FuDevice *device,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE(device);
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp = 0;

	if (g_strcmp0(key, "RedfishResetPreDelay") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->reset_pre_delay = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "RedfishResetPostDelay") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->reset_post_delay = tmp;
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_redfish_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE(object);
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_BACKEND:
		g_value_set_object(value, priv->backend);
		break;
	case PROP_MEMBER:
		g_value_set_pointer(value, priv->json_obj_member);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_redfish_device_set_member(FuRedfishDevice *self, FwupdJsonObject *json_obj_member)
{
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	g_clear_pointer(&priv->json_obj_member, fwupd_json_object_unref);
	if (json_obj_member != NULL)
		priv->json_obj_member = fwupd_json_object_ref(json_obj_member);
}

static void
fu_redfish_device_set_property(GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE(object);
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_BACKEND:
		g_set_object(&priv->backend, g_value_get_object(value));
		break;
	case PROP_MEMBER:
		fu_redfish_device_set_member(self, g_value_get_pointer(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_redfish_device_init(FuRedfishDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Redfish device");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_REDFISH_FIRMWARE);
	fu_device_add_protocol(FU_DEVICE(self), "org.dmtf.redfish");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_ICON);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VENDOR);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
}

static void
fu_redfish_device_finalize(GObject *object)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE(object);
	FuRedfishDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->backend != NULL)
		g_object_unref(priv->backend);
	if (priv->json_obj_member != NULL)
		fwupd_json_object_unref(priv->json_obj_member);
	g_free(priv->build);
	g_free(priv->software_id);
	G_OBJECT_CLASS(fu_redfish_device_parent_class)->finalize(object);
}

static void
fu_redfish_device_class_init(FuRedfishDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->get_property = fu_redfish_device_get_property;
	object_class->set_property = fu_redfish_device_set_property;
	object_class->finalize = fu_redfish_device_finalize;

	device_class->to_string = fu_redfish_device_to_string;
	device_class->probe = fu_redfish_device_probe;
	device_class->set_quirk_kv = fu_redfish_device_set_quirk_kv;
	fu_device_register_private_flag(device_class, FU_REDFISH_DEVICE_FLAG_IS_BACKUP);
	fu_device_register_private_flag(device_class, FU_REDFISH_DEVICE_FLAG_UNSIGNED_BUILD);
	fu_device_register_private_flag(device_class, FU_REDFISH_DEVICE_FLAG_WILDCARD_TARGETS);
	fu_device_register_private_flag(device_class, FU_REDFISH_DEVICE_FLAG_MANAGER_RESET);
	fu_device_register_private_flag(device_class, FU_REDFISH_DEVICE_FLAG_RELATED_CHASSIS_PCIE);
	fu_device_register_private_flag(device_class,
					FU_REDFISH_DEVICE_FLAG_NO_MANAGER_RESET_REQUEST);

	/**
	 * FuRedfishDevice:backend:
	 *
	 * The backend that added the device.
	 */
	pspec =
	    g_param_spec_object("backend",
				NULL,
				NULL,
				FU_TYPE_BACKEND,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_BACKEND, pspec);

	/**
	 * FuRedfishDevice:member:
	 *
	 * The JSON root member for the device.
	 */
	pspec =
	    g_param_spec_pointer("member",
				 NULL,
				 NULL,
				 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_MEMBER, pspec);
}

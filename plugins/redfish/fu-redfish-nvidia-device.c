/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-backend.h"
#include "fu-redfish-nvidia-device.h"
#include "fu-redfish-request.h"

struct _FuRedfishNvidiaDevice {
	FuRedfishDevice parent_instance;
};

G_DEFINE_TYPE(FuRedfishNvidiaDevice, fu_redfish_nvidia_device, FU_TYPE_REDFISH_DEVICE)

static FwupdJsonObject *
fu_redfish_nvidia_device_get_json_object(FuRedfishNvidiaDevice *self,
					 const gchar *uri,
					 GError **error)
{
	FuRedfishBackend *backend;
	g_autoptr(FuRedfishRequest) request = NULL;

	backend = fu_redfish_device_get_backend(FU_REDFISH_DEVICE(self), error);
	if (backend == NULL)
		return NULL;
	request = fu_redfish_backend_request_new(backend, error);
	if (request == NULL)
		return NULL;
	if (!fu_redfish_request_perform(request,
					uri,
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
					    FU_REDFISH_REQUEST_PERFORM_FLAG_USE_CACHE,
					error))
		return NULL;
	return fu_redfish_request_get_json_object(request, error);
}

static gboolean
fu_redfish_nvidia_device_ensure_parent(FuRedfishNvidiaDevice *self, GError **error)
{
	FuDevice *device = FU_DEVICE(self);
	g_autoptr(FwupdJsonArray) json_arr = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;

	json_obj = fu_redfish_nvidia_device_get_json_object(self, "/redfish/v1/Managers", error);
	if (json_obj == NULL)
		return FALSE;
	json_arr = fwupd_json_object_get_array(json_obj, "Members", error);
	if (json_arr == NULL)
		return FALSE;
	for (guint i = 0; i < fwupd_json_array_get_size(json_arr); i++) {
		const gchar *bmc_id;
		const gchar *image_uri;
		const gchar *manager_uri;
		g_autoptr(FwupdJsonObject) json_image = NULL;
		g_autoptr(FwupdJsonObject) json_inventory = NULL;
		g_autoptr(FwupdJsonObject) json_links = NULL;
		g_autoptr(FwupdJsonObject) json_manager = NULL;
		g_autoptr(FwupdJsonObject) json_member = NULL;

		json_member = fwupd_json_array_get_object(json_arr, i, error);
		if (json_member == NULL)
			return FALSE;
		manager_uri = fwupd_json_object_get_string(json_member, "@odata.id", error);
		if (manager_uri == NULL)
			return FALSE;
		json_manager = fu_redfish_nvidia_device_get_json_object(self, manager_uri, error);
		if (json_manager == NULL)
			return FALSE;
		if (g_strcmp0(fwupd_json_object_get_string(json_manager, "ManagerType", NULL),
			      "BMC") != 0)
			continue;

		/* the inventory member for the firmware the BMC is running */
		json_links = fwupd_json_object_get_object(json_manager, "Links", NULL);
		if (json_links == NULL)
			continue;
		json_image = fwupd_json_object_get_object(json_links, "ActiveSoftwareImage", NULL);
		if (json_image == NULL)
			continue;
		image_uri = fwupd_json_object_get_string(json_image, "@odata.id", NULL);
		if (image_uri == NULL)
			continue;
		if (g_strcmp0(image_uri, fu_device_get_logical_id(device)) == 0) {
			fu_device_add_private_flag(device,
						   FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN);
			return TRUE;
		}
		json_inventory = fu_redfish_nvidia_device_get_json_object(self, image_uri, error);
		if (json_inventory == NULL)
			return FALSE;
		bmc_id = fwupd_json_object_get_string(json_inventory, "Id", error);
		if (bmc_id == NULL)
			return FALSE;
		fu_device_add_parent_backend_id(device, bmc_id);
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_nvidia_device_probe(FuDevice *device, GError **error)
{
	/* FuRedfishDevice->probe */
	if (!FU_DEVICE_CLASS(fu_redfish_nvidia_device_parent_class)->probe(device, error))
		return FALSE;

	/* every member reports the same Name */
	if (fu_device_get_backend_id(device) != NULL)
		fu_device_set_name(device, fu_device_get_backend_id(device));

	/* installing firmware is not supported yet */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* every other component is a child of the BMC */
	return fu_redfish_nvidia_device_ensure_parent(FU_REDFISH_NVIDIA_DEVICE(device), error);
}

static void
fu_redfish_nvidia_device_init(FuRedfishNvidiaDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Redfish NVIDIA device");
}

static void
fu_redfish_nvidia_device_class_init(FuRedfishNvidiaDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_redfish_nvidia_device_probe;
}

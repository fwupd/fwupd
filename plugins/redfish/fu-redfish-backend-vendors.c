/*
 * Copyright 2025 Arno Dubois <arno.du@orange.fr>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-backend-vendors.h"
#include "fu-redfish-backend.h"

G_DEFINE_TYPE(FuRedfishBackendVendorSpecific, fu_redfish_backend_vendors_specific, G_TYPE_OBJECT)

struct _FuRedfishBackendDellSpecific {
	FuRedfishBackendVendorSpecific parent_instance;
	guint16 system_id;
};
G_DEFINE_TYPE(FuRedfishBackendDellSpecific,
	      fu_redfish_backend_vendors_dell_specific,
	      FU_TYPE_REDFISH_BACKEND_VENDOR_SPECIFIC)

static void
fu_redfish_backend_vendors_specific_class_init(FuRedfishBackendVendorSpecificClass *klass)
{
}

static void
fu_redfish_backend_vendors_specific_init(FuRedfishBackendVendorSpecific *self)
{
}

static void
fu_redfish_backend_vendors_dell_specific_class_init(FuRedfishBackendDellSpecificClass *klass)
{
}

static void
fu_redfish_backend_vendors_dell_specific_init(FuRedfishBackendDellSpecific *self)
{
	self->system_id = 0;
}

gboolean
fu_redfish_backend_vendors_dell_specific_init_systemid(FuBackend *backend,
						       FuRedfishBackendDellSpecific *dell_specific,
						       FuProgress *progress,
						       GError **error)
{
	JsonObject *json_obj = NULL;
	g_autoptr(FuRedfishRequest) request =
	    fu_redfish_backend_request_new(FU_REDFISH_BACKEND(backend));
	const gchar *member_uri = NULL;
	JsonArray *members = NULL;

	if (!fu_redfish_request_perform(request,
					"/redfish/v1/Systems",
					FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					error))
		return FALSE;
	json_obj = fu_redfish_request_get_json_object(request);
	if (!json_object_has_member(json_obj, "Members")) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no Members object");
		return FALSE;
	}

	members = json_object_get_array_member(json_obj, "Members");
	if (members != NULL && json_array_get_length(members) > 0) {
		JsonObject *member = json_array_get_object_element(members, 0);
		member_uri = json_object_get_string_member(member, "@odata.id");
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no Members array or array empty");
		return FALSE;
	}

	g_free(request);
	request = fu_redfish_backend_request_new(FU_REDFISH_BACKEND(backend));

	if (fu_redfish_request_perform(request,
				       member_uri,
				       FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
				       error)) {
		JsonObject *oem_obj = NULL;
		json_obj = fu_redfish_request_get_json_object(request);
		oem_obj = json_object_get_object_member(json_obj, "Oem");
		if (oem_obj != NULL) {
			JsonObject *dell_obj = json_object_get_object_member(oem_obj, "Dell");
			if (dell_obj != NULL) {
				JsonObject *dell_system_obj =
				    json_object_get_object_member(dell_obj, "DellSystem");
				if (dell_system_obj != NULL &&
				    json_object_has_member(dell_system_obj, "SystemID")) {
					dell_specific->system_id = (unsigned short)
					    json_object_get_int_member(dell_system_obj, "SystemID");
					return TRUE;
				}
			}
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no SystemID in system properties");
	}
	return FALSE;
}

guint16
fu_redfish_backend_vendors_dell_specific_get_systemid(FuRedfishBackendDellSpecific *self)
{
	return self->system_id;
}

/*
 * Copyright 2025 Arno Dubois <arno.du@orange.fr>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REDFISH_BACKEND_VENDOR_SPECIFIC (fu_redfish_backend_vendors_specific_get_type())
G_DECLARE_DERIVABLE_TYPE(FuRedfishBackendVendorSpecific,
			 fu_redfish_backend_vendors_specific,
			 FU,
			 REDFISH_BACKEND_VENDOR_SPECIFIC,
			 GObject)

struct _FuRedfishBackendVendorSpecificClass {
	GObjectClass parent_class;
};

#define FU_TYPE_REDFISH_BACKEND_DELL_SPECIFIC (fu_redfish_backend_vendors_dell_specific_get_type())
G_DECLARE_FINAL_TYPE(FuRedfishBackendDellSpecific,
		     fu_redfish_backend_vendors_dell_specific,
		     FU,
		     REDFISH_BACKEND_DELL_SPECIFIC,
		     FuRedfishBackendVendorSpecific)

gboolean
fu_redfish_backend_vendors_dell_specific_init_systemid(FuBackend *self,
						       FuRedfishBackendDellSpecific *dell_backend,
						       FuProgress *progress,
						       GError **error);

guint16
fu_redfish_backend_vendors_dell_specific_get_systemid(FuRedfishBackendDellSpecific *self);

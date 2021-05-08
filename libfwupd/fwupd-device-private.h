/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <json-glib/json-glib.h>

#include "fwupd-device.h"

G_BEGIN_DECLS

GVariant	*fwupd_device_to_variant		(FwupdDevice	*self);
GVariant	*fwupd_device_to_variant_full		(FwupdDevice	*self,
							 FwupdDeviceFlags flags);
void		 fwupd_device_incorporate		(FwupdDevice	*self,
							 FwupdDevice	*donor);
void		 fwupd_device_to_json			(FwupdDevice	*self,
							 JsonBuilder	*builder);

G_END_DECLS

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_PROVIDER_DELL_H
#define __FU_PROVIDER_DELL_H

#include <gusb.h>
#include "fu-device.h"
#include "fu-provider.h"

G_BEGIN_DECLS

#define FU_TYPE_PROVIDER_DELL (fu_provider_dell_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuProviderDell, fu_provider_dell, FU, PROVIDER_DELL, FuProvider)

struct _FuProviderDellClass
{
	FuProviderClass			 parent_class;
};

FuProvider	*fu_provider_dell_new		(void);
void
fu_provider_dell_inject_fake_data (FuProviderDell *provider_dell,
				   guint32 *output, guint16 vid, guint16 pid,
				   guint8 *buf);
gboolean
fu_provider_dell_detect_tpm (FuProvider *provider, GError **error);

void
fu_provider_dell_device_added_cb (GUsbContext *ctx,
				  GUsbDevice *device,
				  FuProviderDell *provider_dell);

void
fu_provider_dell_device_removed_cb (GUsbContext *ctx,
				    GUsbDevice *device,
				    FuProviderDell *provider_dell);

G_END_DECLS

#endif /* __FU_PROVIDER_DELL_H */

/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-steelseries-fizz-struct.h"

#define FU_TYPE_STEELSERIES_FIZZ_IMPL (fu_steelseries_fizz_impl_get_type())
G_DECLARE_INTERFACE(FuSteelseriesFizzImpl,
		    fu_steelseries_fizz_impl,
		    FU,
		    STEELSERIES_FIZZ_IMPL,
		    GObject)

struct _FuSteelseriesFizzImplInterface {
	GTypeInterface g_iface;
	gboolean (*request)(FuSteelseriesFizzImpl *self, const GByteArray *buf, GError **error);
	GByteArray *(*response)(FuSteelseriesFizzImpl *self, GError **error);
	gchar *(*get_version)(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error);
	gboolean (*get_battery_level)(FuSteelseriesFizzImpl *self,
				      gboolean tunnel,
				      guint8 *level,
				      GError **error);
	guint8 (*get_fs_id)(FuSteelseriesFizzImpl *self, gboolean is_receiver);
	guint8 (*get_file_id)(FuSteelseriesFizzImpl *self, gboolean is_receiver);
	gboolean (*get_paired_status)(FuSteelseriesFizzImpl *self, guint8 *status, GError **error);
	gboolean (*get_connection_status)(FuSteelseriesFizzImpl *self,
					  FuSteelseriesFizzConnectionStatus *status,
					  GError **error);
	gboolean (*is_updatable)(FuSteelseriesFizzImpl *self, FuDevice *device, GError **error);
	gchar *(*get_serial)(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error);
};

gboolean
fu_steelseries_fizz_impl_request(FuSteelseriesFizzImpl *self, GByteArray *buf, GError **error);

GByteArray *
fu_steelseries_fizz_impl_response(FuSteelseriesFizzImpl *self, GError **error);

gchar *
fu_steelseries_fizz_impl_get_version(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error);

gboolean
fu_steelseries_fizz_impl_get_battery_level(FuSteelseriesFizzImpl *self,
					   gboolean tunnel,
					   guint8 *level,
					   GError **error);

gboolean
fu_steelseries_fizz_impl_get_fs_id(FuSteelseriesFizzImpl *self,
				   gboolean is_receiver,
				   guint8 *id,
				   GError **error);

gboolean
fu_steelseries_fizz_impl_get_file_id(FuSteelseriesFizzImpl *self,
				     gboolean is_receiver,
				     guint8 *id,
				     GError **error);

gboolean
fu_steelseries_fizz_impl_get_paired_status(FuSteelseriesFizzImpl *self,
					   guint8 *status,
					   GError **error);

gboolean
fu_steelseries_fizz_impl_get_connection_status(FuSteelseriesFizzImpl *self,
					       FuSteelseriesFizzConnectionStatus *status,
					       GError **error);

gboolean
fu_steelseries_fizz_impl_is_updatable(FuSteelseriesFizzImpl *self,
				      FuDevice *device,
				      GError **error);

gchar *
fu_steelseries_fizz_impl_get_serial(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error);

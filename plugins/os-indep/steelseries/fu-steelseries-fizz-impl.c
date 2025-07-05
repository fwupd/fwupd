/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-fizz-impl.h"

G_DEFINE_INTERFACE(FuSteelseriesFizzImpl, fu_steelseries_fizz_impl, G_TYPE_OBJECT)

static void
fu_steelseries_fizz_impl_default_init(FuSteelseriesFizzImplInterface *iface)
{
}

gboolean
fu_steelseries_fizz_impl_request(FuSteelseriesFizzImpl *self, GByteArray *buf, GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), FALSE);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->request == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->request not implemented");
		return FALSE;
	}
	return (*iface->request)(self, buf, error);
}

GByteArray *
fu_steelseries_fizz_impl_response(FuSteelseriesFizzImpl *self, GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), NULL);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->response == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->response not implemented");
		return NULL;
	}
	return (*iface->response)(self, error);
}

gchar *
fu_steelseries_fizz_impl_get_version(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), NULL);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->get_version == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->get_version not implemented");
		return NULL;
	}
	return (*iface->get_version)(self, tunnel, error);
}

gboolean
fu_steelseries_fizz_impl_get_battery_level(FuSteelseriesFizzImpl *self,
					   gboolean tunnel,
					   guint8 *level,
					   GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), FALSE);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->get_battery_level == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->get_battery_level not implemented");
		return FALSE;
	}
	return (*iface->get_battery_level)(self, tunnel, level, error);
}

gboolean
fu_steelseries_fizz_impl_get_fs_id(FuSteelseriesFizzImpl *self,
				   gboolean is_receiver,
				   guint8 *id,
				   GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), FALSE);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->get_fs_id == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->get_fs_id not implemented");
		return FALSE;
	}
	*id = (*iface->get_fs_id)(self, is_receiver);
	return TRUE;
}

gboolean
fu_steelseries_fizz_impl_get_file_id(FuSteelseriesFizzImpl *self,
				     gboolean is_receiver,
				     guint8 *id,
				     GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), FALSE);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->get_file_id == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->get_file_id not implemented");
		return FALSE;
	}
	*id = (*iface->get_file_id)(self, is_receiver);
	return TRUE;
}

gboolean
fu_steelseries_fizz_impl_get_paired_status(FuSteelseriesFizzImpl *self,
					   guint8 *status,
					   GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), FALSE);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->get_paired_status == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->get_paired_status not implemented");
		return FALSE;
	}
	return (*iface->get_paired_status)(self, status, error);
}

gboolean
fu_steelseries_fizz_impl_get_connection_status(FuSteelseriesFizzImpl *self,
					       FuSteelseriesFizzConnectionStatus *status,
					       GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), FALSE);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->get_connection_status == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->get_connection_status not implemented");
		return FALSE;
	}
	return (*iface->get_connection_status)(self, status, error);
}

gboolean
fu_steelseries_fizz_impl_is_updatable(FuSteelseriesFizzImpl *self, FuDevice *device, GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), FALSE);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->is_updatable != NULL)
		return (*iface->is_updatable)(self, device, error);

	/* assume device is supported by default */
	return TRUE;
}

gchar *
fu_steelseries_fizz_impl_get_serial(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error)
{
	FuSteelseriesFizzImplInterface *iface;

	g_return_val_if_fail(FU_IS_STEELSERIES_FIZZ_IMPL(self), NULL);

	iface = FU_STEELSERIES_FIZZ_IMPL_GET_IFACE(self);
	if (iface->get_serial == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->get_serial not implemented");
		return NULL;
	}
	return (*iface->get_serial)(self, tunnel, error);
}

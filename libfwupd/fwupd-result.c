/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <string.h>

#include "fwupd-common-private.h"
#include "fwupd-deprecated.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"
#include "fwupd-release-private.h"
#include "fwupd-result-private.h"

static void fwupd_result_finalize	 (GObject *object);

typedef struct {
	FwupdDevice			*device;
	FwupdRelease			*release;
} FwupdResultPrivate;

enum {
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_DEVICE_ID,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (FwupdResult, fwupd_result, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fwupd_result_get_instance_private (o))

/**
 * fwupd_result_get_release:
 * @result: A #FwupdResult
 *
 * Gets the default release for this result.
 *
 * Returns: (transfer none): the #FwupdRelease
 *
 * Since: 0.9.3
 **/
FwupdRelease *
fwupd_result_get_release (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return priv->release;
}

/**
 * fwupd_result_get_device:
 * @result: A #FwupdResult
 *
 * Gets the default device for this result.
 *
 * Returns: (transfer none): the #FwupdDevice
 *
 * Since: 0.9.3
 **/
FwupdDevice *
fwupd_result_get_device (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);
	return priv->device;
}

void
fwupd_result_set_release (FwupdResult *result, FwupdRelease *release)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	g_set_object (&priv->release, release);
}

void
fwupd_result_set_device (FwupdResult *result, FwupdDevice *device)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	g_return_if_fail (FWUPD_IS_RESULT (result));
	g_set_object (&priv->device, device);
}

/**
 * fwupd_result_to_string:
 * @result: A #FwupdResult
 *
 * Builds a text representation of the object.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 0.7.0
 **/
gchar *
fwupd_result_to_string (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	GString *str;
	g_autofree gchar *device_str = NULL;
	g_autofree gchar *release_str = NULL;

	g_return_val_if_fail (FWUPD_IS_RESULT (result), NULL);

	str = g_string_new ("");

	/* device and release */
	device_str = fwupd_device_to_string (priv->device);
	g_string_append (str, device_str);
	release_str = fwupd_release_to_string (priv->release);
	g_string_append (str, release_str);

	return g_string_free (str, FALSE);
}

static void
fwupd_result_get_property (GObject *object, guint prop_id,
			   GValue *value, GParamSpec *pspec)
{
	FwupdResult *result = FWUPD_RESULT (object);
	FwupdResultPrivate *priv = GET_PRIVATE (result);

	switch (prop_id) {
	case PROP_DEVICE_ID:
		g_value_set_string (value, fwupd_device_get_id (priv->device));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_result_set_property (GObject *object, guint prop_id,
			   const GValue *value, GParamSpec *pspec)
{
	FwupdResult *result = FWUPD_RESULT (object);
	FwupdResultPrivate *priv = GET_PRIVATE (result);

	switch (prop_id) {
	case PROP_DEVICE_ID:
		fwupd_device_set_id (priv->device, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fwupd_result_class_init (FwupdResultClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fwupd_result_finalize;
	object_class->get_property = fwupd_result_get_property;
	object_class->set_property = fwupd_result_set_property;

	/**
	 * FwupdResult:device-id:
	 *
	 * The device ID for this result.
	 *
	 * Since: 0.7.0
	 */
	pspec = g_param_spec_string ("device-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DEVICE_ID, pspec);
}

static void
fwupd_result_init (FwupdResult *result)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	priv->release = fwupd_release_new ();
	priv->device = fwupd_device_new ();
}

static void
fwupd_result_finalize (GObject *object)
{
	FwupdResult *result = FWUPD_RESULT (object);
	FwupdResultPrivate *priv = GET_PRIVATE (result);

	g_object_unref (priv->device);
	g_object_unref (priv->release);

	G_OBJECT_CLASS (fwupd_result_parent_class)->finalize (object);
}

static void
fwupd_result_from_variant_iter (FwupdResult *result, GVariantIter *iter)
{
	FwupdResultPrivate *priv = GET_PRIVATE (result);
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next (iter, "{&sv}", &key, &value)) {
		fwupd_release_from_key_value (priv->release, key, value);
		fwupd_device_from_key_value (priv->device, key, value);
		g_variant_unref (value);
	}
}

/**
 * fwupd_result_new_from_data:
 * @data: a #GVariant
 *
 * Creates a new result using packed data.
 *
 * Returns: a new #FwupdResult, or %NULL if @data was invalid
 *
 * Since: 0.7.0
 **/
FwupdResult *
fwupd_result_new_from_data (GVariant *data)
{
	FwupdResult *res = NULL;
	const gchar *id;
	const gchar *type_string;
	g_autoptr(GVariantIter) iter = NULL;

	/* format from GetDetails */
	type_string = g_variant_get_type_string (data);
	if (g_strcmp0 (type_string, "(a{sv})") == 0) {
		res = fwupd_result_new ();
		g_variant_get (data, "(a{sv})", &iter);
		fwupd_result_from_variant_iter (res, iter);
	} else if (g_strcmp0 (type_string, "{sa{sv}}") == 0) {
		res = fwupd_result_new ();
		g_variant_get (data, "{&sa{sv}}", &id, &iter);
		fwupd_device_set_id (fwupd_result_get_device (res), id);
		fwupd_result_from_variant_iter (res, iter);
	} else {
		g_warning ("type %s not known", type_string);
	}
	return res;
}

/**
 * fwupd_result_new:
 *
 * Creates a new result.
 *
 * Returns: a new #FwupdResult
 *
 * Since: 0.7.0
 **/
FwupdResult *
fwupd_result_new (void)
{
	FwupdResult *result;
	result = g_object_new (FWUPD_TYPE_RESULT, NULL);
	return FWUPD_RESULT (result);
}

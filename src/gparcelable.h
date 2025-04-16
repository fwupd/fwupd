/*
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later???
 */

#pragma once

#include <android/binder_status.h>
#include <glib.h>

typedef struct AParcel AParcel;
typedef struct APersistableBundle APersistableBundle;
typedef struct AStatus AStatus;

#define GP_ERROR g_quark_from_static_string("GParcelable")

// TODO: how to avoid redefinition of binder_status_t?
//       maybe these functions should expose GError?
typedef int32_t binder_status_t;

APersistableBundle *
gp_vardict_to_persistable_bundle(GVariant *vardict, GError **error);

binder_status_t
gp_parcel_write_variant(AParcel *parcel, GVariant *value, GError **error);

void
gp_persistable_bundle_to_vardict(GVariantBuilder *builder,
				 APersistableBundle *bundle,
				 GError **error);

void
gp_parcel_to_variant(GVariantBuilder *builder,
		     const AParcel *parcel,
		     const GVariantType *type,
		     GError **error);

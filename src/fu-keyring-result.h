/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_KEYRING_RESULT (fu_keyring_result_get_type ())

G_DECLARE_FINAL_TYPE (FuKeyringResult, fu_keyring_result, FU, KEYRING_RESULT, GObject)

gint64		 fu_keyring_result_get_timestamp	(FuKeyringResult	*self);
const gchar	*fu_keyring_result_get_authority	(FuKeyringResult	*self);

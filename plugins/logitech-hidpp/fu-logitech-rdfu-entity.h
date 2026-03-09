/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_RDFU_ENTITY (fu_logitech_rdfu_entity_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechRdfuEntity,
		     fu_logitech_rdfu_entity,
		     FU,
		     LOGITECH_RDFU_ENTITY,
		     FuFirmware)

gboolean
fu_logitech_rdfu_entity_add_block(FuLogitechRdfuEntity *self,
				  FwupdJsonObject *json_obj,
				  GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_logitech_rdfu_entity_add_entry(FuLogitechRdfuEntity *self,
				  FwupdJsonObject *json_obj,
				  GError **error) G_GNUC_NON_NULL(1, 2);

const gchar *
fu_logitech_rdfu_entity_get_model_id(FuLogitechRdfuEntity *self) G_GNUC_NON_NULL(1);
GByteArray *
fu_logitech_rdfu_entity_get_magic(FuLogitechRdfuEntity *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_logitech_rdfu_entity_get_blocks(FuLogitechRdfuEntity *self) G_GNUC_NON_NULL(1);

FuLogitechRdfuEntity *
fu_logitech_rdfu_entity_new(void) G_GNUC_WARN_UNUSED_RESULT;

/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-build.h"

G_BEGIN_DECLS

typedef struct FwupdJsonNode FwupdJsonNode;
typedef struct FwupdJsonArray FwupdJsonArray;
typedef struct FwupdJsonObject FwupdJsonObject;

typedef enum {
	FWUPD_JSON_NODE_KIND_RAW,
	FWUPD_JSON_NODE_KIND_STRING,
	FWUPD_JSON_NODE_KIND_ARRAY,
	FWUPD_JSON_NODE_KIND_OBJECT,
} FwupdJsonNodeKind;

typedef enum {
	FWUPD_JSON_EXPORT_FLAG_NONE = 0,
	FWUPD_JSON_EXPORT_FLAG_INDENT = 1 << 0,
} G_GNUC_FLAG_ENUM FwupdJsonExportFlags;

typedef enum {
	FWUPD_JSON_LOAD_FLAG_NONE = 0,
	FWUPD_JSON_LOAD_FLAG_TRUSTED = 1 << 0,
	FWUPD_JSON_LOAD_FLAG_STATIC_KEYS = 1 << 1,
} G_GNUC_FLAG_ENUM FwupdJsonLoadFlags;

const gchar *
fwupd_json_node_kind_to_string(FwupdJsonNodeKind node_kind);

G_END_DECLS

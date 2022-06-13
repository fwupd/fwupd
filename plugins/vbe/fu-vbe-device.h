/*
 * VBE plugin for fwupd,mmc-simple
 *
 * Copyright (C) 2022 Google LLC
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

typedef FuDevice *(*vbe_device_new_func)(FuContext *ctx,
					 const gchar *vbe_method,
					 const gchar *fdt,
					 gint node,
					 const gchar *vbe_dir);

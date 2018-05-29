/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FWUPD_RELEASE_PRIVATE_H
#define __FWUPD_RELEASE_PRIVATE_H

#include <glib-object.h>

#include "fwupd-release.h"

G_BEGIN_DECLS

FwupdRelease	*fwupd_release_from_variant		(GVariant	*data);
GVariant	*fwupd_release_to_variant		(FwupdRelease	*release);

G_END_DECLS

#endif /* __FWUPD_RELEASE_PRIVATE_H */


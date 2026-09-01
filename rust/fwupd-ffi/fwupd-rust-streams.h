/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * This is the "parent class" for all rust stream implementations that we
 * can use to pass the underlying streams to each other.
 */
typedef struct FuRsStreamImpl FuRsStreamImpl;

void
fu_stream_impl_free(FuRsStreamImpl *stream);

G_END_DECLS

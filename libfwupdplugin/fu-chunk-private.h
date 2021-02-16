/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <xmlb.h>

#include "fu-chunk.h"

void		 fu_chunk_add_string			(FuChunk	*self,
							 guint		 idt,
							 GString	*str);
gboolean	 fu_chunk_build				(FuChunk	*self,
							 XbNode		*n,
							 GError		**error);

/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define _DEFINE_CLEANUP_FUNCTION0(Type, name, func) \
  static inline VOID name(VOID *v) \
  { \
    if (*(Type*)v) \
      func (*(Type*)v); \
  }
_DEFINE_CLEANUP_FUNCTION0(VOID *, _FreePool_p, FreePool)
#define _cleanup_free __attribute__ ((cleanup(_FreePool_p)))

static inline VOID *
_steal_pointer(VOID *pp)
{
	VOID **ptr = (VOID **) pp;
	VOID *ref = *ptr;
	*ptr = NULL;
	return ref;
}

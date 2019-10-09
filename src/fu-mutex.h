/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#if !GLIB_CHECK_VERSION(2, 61, 1)

/* Backported GRWLock autoptr support for older glib versions */

typedef void GRWLockWriterLocker;

static inline GRWLockWriterLocker *
g_rw_lock_writer_locker_new (GRWLock *rw_lock)
{
	g_rw_lock_writer_lock (rw_lock);
	return (GRWLockWriterLocker *) rw_lock;
}

static inline void
g_rw_lock_writer_locker_free (GRWLockWriterLocker *locker)
{
	g_rw_lock_writer_unlock ((GRWLock *) locker);
}

typedef void GRWLockReaderLocker;

static inline GRWLockReaderLocker *
g_rw_lock_reader_locker_new (GRWLock *rw_lock)
{
	g_rw_lock_reader_lock (rw_lock);
	return (GRWLockReaderLocker *) rw_lock;
}

static inline void
g_rw_lock_reader_locker_free (GRWLockReaderLocker *locker)
{
	g_rw_lock_reader_unlock ((GRWLock *) locker);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GRWLockWriterLocker, g_rw_lock_writer_locker_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GRWLockReaderLocker, g_rw_lock_reader_locker_free)

#endif

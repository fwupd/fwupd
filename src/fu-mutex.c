/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mutex.h"

struct _FuMutex
{
	GObject			 parent_instance;
	GRWLock			 rw_lock;
#ifdef FU_MUTEX_DEBUG
	gchar			*id;
	GString			*reader;
	GString			*writer;
#endif
};

G_DEFINE_TYPE (FuMutex, fu_mutex, G_TYPE_OBJECT)

void
fu_mutex_locker_free (FuMutexLocker *locker)
{
	if (locker == NULL)
		return;
	fu_mutex_unlock (locker->mutex, locker->kind);
	g_free (locker);
}

#ifdef FU_MUTEX_DEBUG
void
fu_mutex_lock_dbg (FuMutex *self, FuMutexAccess kind, const gchar *strloc, const gchar *strfunc)
{
	g_debug ("LOCK  \t%s\t%s\t%s\t%s",
		 self->id,
		 kind == FU_MUTEX_ACCESS_READ ? "READ" : "WRITE",
		 strloc,
		 strfunc);
	if (kind == FU_MUTEX_ACCESS_READ) {
		if (!g_rw_lock_reader_trylock (&self->rw_lock)) {
			g_debug ("failed to read lock, write lock held by %s",
				 self->writer->str);
		}
		g_string_printf (self->reader, "%s:%s", strloc, strfunc);
	} else {
		if (!g_rw_lock_writer_trylock (&self->rw_lock)) {
			g_debug ("failed to write lock, read lock held by %s, "
				 "write lock held by %s",
				 self->reader->str, self->writer->str);
		}
		g_string_printf (self->writer, "%s:%s", strloc, strfunc);
	}
}

void
fu_mutex_unlock_dbg (FuMutex *self, FuMutexAccess kind, const gchar *strloc, const gchar *strfunc)
{
	g_debug ("UNLOCK\t%s\t%s\t%s\t%s",
		 self->id,
		 kind == FU_MUTEX_ACCESS_READ ? "READ" : "WRITE",
		 strloc,
		 strfunc);
	if (kind == FU_MUTEX_ACCESS_READ) {
		g_rw_lock_reader_unlock (&self->rw_lock);
		g_string_assign (self->reader, "");
	} else {
		g_rw_lock_writer_unlock (&self->rw_lock);
		g_string_assign (self->writer, "");
	}
}

FuMutexLocker *
fu_mutex_locker_new_dbg (FuMutex *self, FuMutexAccess kind, const gchar *strloc, const gchar *strfunc)
{
	FuMutexLocker *locker = g_new0 (FuMutexLocker, 1);
	locker->mutex = self;
	locker->kind = kind;
	fu_mutex_lock_dbg (locker->mutex, locker->kind, strloc, strfunc);
	return locker;
}

#else

FuMutexLocker *
fu_mutex_locker_new (FuMutex *mutex, FuMutexAccess kind)
{
	FuMutexLocker *locker = g_new0 (FuMutexLocker, 1);
	locker->mutex = mutex;
	locker->kind = kind;
	fu_mutex_lock (locker->mutex, locker->kind);
	return locker;
}

/**
 * fu_mutex_lock:
 * @self: a #FuMutex
 * @kind: a #FuMutexAccess type
 *
 * Obtain a read or write lock.
 *
 * Since: 1.1.2
 **/
void
fu_mutex_lock (FuMutex *self, FuMutexAccess kind)
{
	if (kind == FU_MUTEX_ACCESS_READ)
		g_rw_lock_reader_lock (&self->rw_lock);
	else if (kind == FU_MUTEX_ACCESS_WRITE)
		g_rw_lock_writer_lock (&self->rw_lock);
}

/**
 * fu_mutex_unlock:
 * @self: a #FuMutex
 * @kind: a #FuMutexAccess type
 *
 * Release a read or write lock.
 *
 * Since: 1.1.2
 **/
void
fu_mutex_unlock (FuMutex *self, FuMutexAccess kind)
{
	if (kind == FU_MUTEX_ACCESS_READ)
		g_rw_lock_reader_unlock (&self->rw_lock);
	else if (kind == FU_MUTEX_ACCESS_WRITE)
		g_rw_lock_writer_unlock (&self->rw_lock);
}

#endif

static void
fu_mutex_finalize (GObject *obj)
{
	FuMutex *self = FU_MUTEX (obj);
	g_rw_lock_clear (&self->rw_lock);
#ifdef FU_MUTEX_DEBUG
	g_free (self->id);
	g_string_free (self->reader, TRUE);
	g_string_free (self->writer, TRUE);
#endif
	G_OBJECT_CLASS (fu_mutex_parent_class)->finalize (obj);
}

static void
fu_mutex_class_init (FuMutexClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_mutex_finalize;
}

static void
fu_mutex_init (FuMutex *self)
{
	g_rw_lock_init (&self->rw_lock);
#ifdef FU_MUTEX_DEBUG
	self->reader = g_string_new (NULL);
	self->writer = g_string_new (NULL);
#endif
}

/**
 * fu_mutex_new:
 * @module: (debugging only) A module to debug; ie G_STRLOC
 * @func: (debugging only) A function debug; ie G_STRFUNC
 *
 * Creates a new RW lock.
 *
 * Returns: (transfer full): a #FuMutex
 *
 * Since: 1.1.2
 **/
FuMutex *
fu_mutex_new (const gchar *module, const gchar *func)
{
	FuMutex *self;
	self = g_object_new (FU_TYPE_MUTEX, NULL);
#ifdef FU_MUTEX_DEBUG
	self->id = g_strdup_printf ("%s(%s)", module, func);
#endif
	return FU_MUTEX (self);
}

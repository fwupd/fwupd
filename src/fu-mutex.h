/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_TYPE_MUTEX (fu_mutex_get_type ())
G_DECLARE_FINAL_TYPE (FuMutex, fu_mutex, FU, MUTEX, GObject)

/**
 * FuMutexAccess:
 * @FU_MUTEX_ACCESS_READ:	If another thread currently holds the write lock or blocks waiting for it, the current thread will block.  Read locks can be taken recursively.
 * @FU_MUTEX_ACCESS_WRITE:	If any thread already holds a read or  write lock, the current thread will block until all other threads have dropped their locks.
 *
 * Type of mutex access to take
 **/
typedef enum {
	FU_MUTEX_ACCESS_READ,
	FU_MUTEX_ACCESS_WRITE
} FuMutexAccess;

/**
 * FuMutexLocker:
 * @mutex:	A #FuMutex
 * @kind:	A #FuMutexAccess
 *
 * A locker for storing a mutex
 **/
typedef struct {
	FuMutex		*mutex;
	FuMutexAccess	 kind;
} FuMutexLocker;

#ifdef FU_MUTEX_DEBUG
#define		 fu_mutex_lock(self,kind)	 fu_mutex_lock_dbg(self,kind,G_STRLOC,G_STRFUNC)
#define		 fu_mutex_unlock(self,kind)	 fu_mutex_unlock_dbg(self,kind,G_STRLOC,G_STRFUNC)
#define		 fu_mutex_locker_new(o,k)	 fu_mutex_locker_new_dbg(o,k,G_STRLOC,G_STRFUNC)
void		 fu_mutex_lock_dbg		(FuMutex	*self,
						 FuMutexAccess	 kind,
						 const gchar	*strloc,
						 const gchar	*strfunc);
void		 fu_mutex_unlock_dbg		(FuMutex	*self,
						 FuMutexAccess	 kind,
						 const gchar	*strloc,
						 const gchar	*strfunc);
FuMutexLocker	*fu_mutex_locker_new_dbg	(FuMutex	*mutex,
						 FuMutexAccess	 kind,
						 const gchar	*strloc,
						 const gchar	*strfunc);
#else
void		 fu_mutex_lock			(FuMutex	*self,
						 FuMutexAccess	 kind);
void		 fu_mutex_unlock		(FuMutex	*self,
						 FuMutexAccess	 kind);
FuMutexLocker	*fu_mutex_locker_new		(FuMutex	*mutex,
						 FuMutexAccess	 kind);
#endif

/* helpers */
#define		 fu_mutex_read_lock(o)		 fu_mutex_lock(o,FU_MUTEX_ACCESS_READ)
#define		 fu_mutex_write_lock(o)		 fu_mutex_lock(o,FU_MUTEX_ACCESS_WRITE)
#define		 fu_mutex_read_unlock(o)	 fu_mutex_unlock(o,FU_MUTEX_ACCESS_READ)
#define		 fu_mutex_write_unlock(o)	 fu_mutex_unlock(o,FU_MUTEX_ACCESS_WRITE)
#define		 fu_mutex_read_locker_new(o)	 fu_mutex_locker_new(o,FU_MUTEX_ACCESS_READ)
#define		 fu_mutex_write_locker_new(o)	 fu_mutex_locker_new(o,FU_MUTEX_ACCESS_WRITE)

FuMutex		*fu_mutex_new			(const gchar	*module,
						 const gchar	*func);
void		 fu_mutex_locker_free		(FuMutexLocker	*locker);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMutexLocker, fu_mutex_locker_free)

G_END_DECLS

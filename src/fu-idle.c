/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuIdle"

#include "config.h"

#include <glib-object.h>

#include "fu-idle.h"
#include "fu-mutex.h"

static void fu_idle_finalize	 (GObject *obj);

struct _FuIdle
{
	GObject			 parent_instance;
	GPtrArray		*items;	/* of FuIdleItem */
	GRWLock			 items_mutex;
	guint			 idle_id;
	guint			 timeout;
	FwupdStatus		 status;
};

enum {
	PROP_0,
	PROP_STATUS,
	PROP_LAST
};

static void
fu_idle_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuIdle *self = FU_IDLE (object);
	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_uint (value, self->status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_idle_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

typedef struct {
	gchar			*reason;
	guint32			 token;
} FuIdleItem;

G_DEFINE_TYPE (FuIdle, fu_idle, G_TYPE_OBJECT)

FwupdStatus
fu_idle_get_status (FuIdle *self)
{
	g_return_val_if_fail (FU_IS_IDLE (self), FWUPD_STATUS_UNKNOWN);
	return self->status;
}

static void
fu_idle_set_status (FuIdle *self, FwupdStatus status)
{
	if (self->status == status)
		return;
	self->status = status;
	g_debug ("status now %s", fwupd_status_to_string (status));
	g_object_notify (G_OBJECT (self), "status");
}

static gboolean
fu_idle_check_cb (gpointer user_data)
{
	FuIdle *self = FU_IDLE (user_data);
	fu_idle_set_status (self, FWUPD_STATUS_SHUTDOWN);
	return G_SOURCE_CONTINUE;
}

static void
fu_idle_start (FuIdle *self)
{
	if (self->idle_id != 0)
		return;
	if (self->timeout == 0)
		return;
	self->idle_id = g_timeout_add_seconds (self->timeout, fu_idle_check_cb, self);
}

static void
fu_idle_stop (FuIdle *self)
{
	if (self->idle_id == 0)
		return;
	g_source_remove (self->idle_id);
	self->idle_id = 0;
}

void
fu_idle_reset (FuIdle *self)
{
	g_return_if_fail (FU_IS_IDLE (self));
	fu_idle_stop (self);
	if (self->items->len == 0)
		fu_idle_start (self);
}

void
fu_idle_uninhibit (FuIdle *self, guint32 token)
{
	g_autoptr(GRWLockWriterLocker) locker = g_rw_lock_writer_locker_new (&self->items_mutex);

	g_return_if_fail (FU_IS_IDLE (self));
	g_return_if_fail (token != 0);
	g_return_if_fail (locker != NULL);

	for (guint i = 0; i < self->items->len; i++) {
		FuIdleItem *item = g_ptr_array_index (self->items, i);
		if (item->token == token) {
			g_debug ("uninhibiting: %s", item->reason);
			g_ptr_array_remove_index (self->items, i);
			break;
		}
	}
	fu_idle_reset (self);
}

guint32
fu_idle_inhibit (FuIdle *self, const gchar *reason)
{
	FuIdleItem *item;
	g_autoptr(GRWLockWriterLocker) locker = g_rw_lock_writer_locker_new (&self->items_mutex);

	g_return_val_if_fail (FU_IS_IDLE (self), 0);
	g_return_val_if_fail (reason != NULL, 0);
	g_return_val_if_fail (locker != NULL, 0);

	g_debug ("inhibiting: %s", reason);
	item = g_new0 (FuIdleItem, 1);
	item->reason = g_strdup (reason);
	item->token = g_random_int_range (1, G_MAXINT);
	g_ptr_array_add (self->items, item);
	fu_idle_reset (self);
	return item->token;
}

void
fu_idle_set_timeout (FuIdle *self, guint timeout)
{
	g_return_if_fail (FU_IS_IDLE (self));
	g_debug ("setting timeout to %us", timeout);
	self->timeout = timeout;
	fu_idle_reset (self);
}

static void
fu_idle_item_free (FuIdleItem *item)
{
	g_free (item->reason);
	g_free (item);
}

FuIdleLocker *
fu_idle_locker_new (FuIdle *self, const gchar *reason)
{
	FuIdleLocker *locker = g_new0 (FuIdleLocker, 1);
	locker->idle = g_object_ref (self);
	locker->token = fu_idle_inhibit (self, reason);
	return locker;
}

void
fu_idle_locker_free (FuIdleLocker *locker)
{
	if (locker == NULL)
		return;
	fu_idle_uninhibit (locker->idle, locker->token);
	g_object_unref (locker->idle);
	g_free (locker);
}

static void
fu_idle_class_init (FuIdleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = fu_idle_finalize;
	object_class->get_property = fu_idle_get_property;
	object_class->set_property = fu_idle_set_property;

	pspec = g_param_spec_uint ("status", NULL, NULL,
				   FWUPD_STATUS_UNKNOWN,
				   FWUPD_STATUS_LAST,
				   FWUPD_STATUS_UNKNOWN,
				   G_PARAM_READABLE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);
}

static void
fu_idle_init (FuIdle *self)
{
	self->status = FWUPD_STATUS_IDLE;
	self->items = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_idle_item_free);
	g_rw_lock_init (&self->items_mutex);
}

static void
fu_idle_finalize (GObject *obj)
{
	FuIdle *self = FU_IDLE (obj);

	fu_idle_stop (self);
	g_ptr_array_unref (self->items);
	g_rw_lock_clear (&self->items_mutex);

	G_OBJECT_CLASS (fu_idle_parent_class)->finalize (obj);
}

FuIdle *
fu_idle_new (void)
{
	FuIdle *self;
	self = g_object_new (FU_TYPE_IDLE, NULL);
	return FU_IDLE (self);
}

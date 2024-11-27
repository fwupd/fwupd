/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuIdle"

#include "config.h"

#include "fu-idle.h"

static void
fu_idle_finalize(GObject *obj);

struct _FuIdle {
	GObject parent_instance;
	GPtrArray *items; /* of FuIdleItem */
	guint idle_id;
	guint timeout;
	FuIdleInhibit inhibit_old;
};

enum { SIGNAL_INHIBIT_CHANGED, SIGNAL_TIMEOUT, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

typedef struct {
	FuIdleInhibit inhibit;
	gchar *reason;
	guint32 token;
} FuIdleItem;

G_DEFINE_TYPE(FuIdle, fu_idle, G_TYPE_OBJECT)

static gboolean
fu_idle_check_cb(gpointer user_data)
{
	FuIdle *self = FU_IDLE(user_data);
	g_signal_emit(self, signals[SIGNAL_TIMEOUT], 0);
	return G_SOURCE_CONTINUE;
}

static void
fu_idle_start(FuIdle *self)
{
	if (self->idle_id != 0)
		return;
	if (self->timeout == 0)
		return;
	self->idle_id = g_timeout_add_seconds(self->timeout, fu_idle_check_cb, self);
}

static void
fu_idle_stop(FuIdle *self)
{
	if (self->idle_id == 0)
		return;
	g_source_remove(self->idle_id);
	self->idle_id = 0;
}

static void
fu_idle_emit_inhibit_changed(FuIdle *self)
{
	FuIdleInhibit inhibit_global = FU_IDLE_INHIBIT_NONE;

	fu_idle_reset(self);
	for (guint i = 0; i < self->items->len; i++) {
		FuIdleItem *item = g_ptr_array_index(self->items, i);
		inhibit_global |= item->inhibit;
	}
	if (self->inhibit_old != inhibit_global) {
		g_autofree gchar *inhibit_str = fu_idle_inhibit_to_string(inhibit_global);
		g_debug("now inhibited: %s", inhibit_str);
		g_signal_emit(self, signals[SIGNAL_INHIBIT_CHANGED], 0, inhibit_global);
		self->inhibit_old = inhibit_global;
	}
}

void
fu_idle_reset(FuIdle *self)
{
	g_return_if_fail(FU_IS_IDLE(self));
	fu_idle_stop(self);
	if (!fu_idle_has_inhibit(self, FU_IDLE_INHIBIT_TIMEOUT))
		fu_idle_start(self);
}

void
fu_idle_uninhibit(FuIdle *self, guint32 token)
{
	g_return_if_fail(FU_IS_IDLE(self));
	g_return_if_fail(token != 0);

	for (guint i = 0; i < self->items->len; i++) {
		FuIdleItem *item = g_ptr_array_index(self->items, i);
		if (item->token == token) {
			g_autofree gchar *inhibit_str = fu_idle_inhibit_to_string(item->inhibit);
			g_debug("uninhibiting: %s by %s", inhibit_str, item->reason);
			g_ptr_array_remove_index(self->items, i);
			break;
		}
	}
	fu_idle_emit_inhibit_changed(self);
}

guint32
fu_idle_inhibit(FuIdle *self, FuIdleInhibit inhibit, const gchar *reason)
{
	FuIdleItem *item;
	g_autofree gchar *inhibit_str = fu_idle_inhibit_to_string(inhibit);

	g_return_val_if_fail(FU_IS_IDLE(self), 0);
	g_return_val_if_fail(inhibit != FU_IDLE_INHIBIT_NONE, 0);

	g_debug("inhibiting: %s by %s", inhibit_str, reason);
	item = g_new0(FuIdleItem, 1);
	item->inhibit = inhibit;
	item->reason = g_strdup(reason);
	item->token = g_random_int_range(1, G_MAXINT); /* nocheck:blocked */
	g_ptr_array_add(self->items, item);

	fu_idle_emit_inhibit_changed(self);
	return item->token;
}

gboolean
fu_idle_has_inhibit(FuIdle *self, FuIdleInhibit inhibit)
{
	g_return_val_if_fail(FU_IS_IDLE(self), FALSE);
	g_return_val_if_fail(inhibit != FU_IDLE_INHIBIT_NONE, FALSE);

	for (guint i = 0; i < self->items->len; i++) {
		FuIdleItem *item = g_ptr_array_index(self->items, i);
		if (item->inhibit & inhibit)
			return TRUE;
	}
	return FALSE;
}

void
fu_idle_set_timeout(FuIdle *self, guint timeout)
{
	g_return_if_fail(FU_IS_IDLE(self));
	g_debug("setting timeout to %us", timeout);
	self->timeout = timeout;
	fu_idle_reset(self);
}

static void
fu_idle_item_free(FuIdleItem *item)
{
	g_free(item->reason);
	g_free(item);
}

FuIdleLocker *
fu_idle_locker_new(FuIdle *self, FuIdleInhibit inhibit, const gchar *reason)
{
	FuIdleLocker *locker = g_new0(FuIdleLocker, 1);
	locker->idle = g_object_ref(self);
	locker->token = fu_idle_inhibit(self, inhibit, reason);
	return locker;
}

void
fu_idle_locker_free(FuIdleLocker *locker)
{
	g_return_if_fail(locker != NULL);
	fu_idle_uninhibit(locker->idle, locker->token);
	g_object_unref(locker->idle);
	g_free(locker);
}

static void
fu_idle_class_init(FuIdleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_idle_finalize;

	signals[SIGNAL_INHIBIT_CHANGED] = g_signal_new("inhibit-changed",
						       G_TYPE_FROM_CLASS(object_class),
						       G_SIGNAL_RUN_LAST,
						       0,
						       NULL,
						       NULL,
						       g_cclosure_marshal_VOID__UINT,
						       G_TYPE_NONE,
						       1,
						       G_TYPE_UINT);
	signals[SIGNAL_TIMEOUT] = g_signal_new("timeout",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					       0);
}

static void
fu_idle_init(FuIdle *self)
{
	self->items = g_ptr_array_new_with_free_func((GDestroyNotify)fu_idle_item_free);
}

static void
fu_idle_finalize(GObject *obj)
{
	FuIdle *self = FU_IDLE(obj);

	fu_idle_stop(self);
	g_ptr_array_unref(self->items);

	G_OBJECT_CLASS(fu_idle_parent_class)->finalize(obj);
}

FuIdle *
fu_idle_new(void)
{
	FuIdle *self;
	self = g_object_new(FU_TYPE_IDLE, NULL);
	return FU_IDLE(self);
}

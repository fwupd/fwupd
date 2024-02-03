/*
 * Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuClientList"

#include "config.h"

#include "fu-client-list.h"

struct _FuClientList {
	GObject parent_instance;
	GPtrArray *array;	     /* (element-type FuClientListItem) */
	GDBusConnection *connection; /* nullable */
};

typedef struct {
	FuClientList *self; /* no-ref */
	FuClient *client;   /* ref */
	guint watcher_id;
} FuClientListItem;

G_DEFINE_TYPE(FuClientList, fu_client_list, G_TYPE_OBJECT)

enum { PROP_0, PROP_CONNECTION, PROP_LAST };

enum { SIGNAL_ADDED, SIGNAL_REMOVED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

static void
fu_client_list_emit_added(FuClientList *self, FuClient *client)
{
	g_debug("client %s added", fu_client_get_sender(client));
	g_signal_emit(self, signals[SIGNAL_ADDED], 0, client);
}

static void
fu_client_list_emit_removed(FuClientList *self, FuClient *client)
{
	g_debug("client %s removed", fu_client_get_sender(client));
	g_signal_emit(self, signals[SIGNAL_REMOVED], 0, client);
}

static void
fu_client_list_sender_name_vanished_cb(GDBusConnection *connection,
				       const gchar *name,
				       gpointer user_data)
{
	FuClientListItem *item = (FuClientListItem *)user_data;
	FuClientList *self = FU_CLIENT_LIST(item->self);
	g_autoptr(FuClient) client = g_object_ref(item->client);
	fu_client_remove_flag(client, FU_CLIENT_FLAG_ACTIVE);
	g_ptr_array_remove(self->array, item);
	fu_client_list_emit_removed(self, client);
}

FuClient *
fu_client_list_register(FuClientList *self, const gchar *sender)
{
	FuClient *client;
	FuClientListItem *item;

	g_return_val_if_fail(FU_IS_CLIENT_LIST(self), NULL);
	g_return_val_if_fail(sender != NULL, NULL);

	/* already exists */
	client = fu_client_list_get_by_sender(self, sender);
	if (client != NULL)
		return client;

	/* create and watch */
	item = g_new0(FuClientListItem, 1);
	item->self = self;
	item->client = fu_client_new(sender);
	if (self->connection != NULL) {
		item->watcher_id =
		    g_bus_watch_name_on_connection(self->connection,
						   sender,
						   G_BUS_NAME_WATCHER_FLAGS_NONE,
						   NULL,
						   fu_client_list_sender_name_vanished_cb,
						   item,
						   NULL);
	}
	g_ptr_array_add(self->array, item);

	/* success */
	fu_client_list_emit_added(self, item->client);
	return g_object_ref(item->client);
}

GPtrArray *
fu_client_list_get_all(FuClientList *self)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_return_val_if_fail(FU_IS_CLIENT_LIST(self), NULL);
	for (guint i = 0; i < self->array->len; i++) {
		FuClientListItem *item = g_ptr_array_index(self->array, i);
		g_ptr_array_add(array, g_object_ref(item->client));
	}
	return g_steal_pointer(&array);
}

FuClient *
fu_client_list_get_by_sender(FuClientList *self, const gchar *sender)
{
	g_return_val_if_fail(FU_IS_CLIENT_LIST(self), NULL);
	for (guint i = 0; i < self->array->len; i++) {
		FuClientListItem *item = g_ptr_array_index(self->array, i);
		if (g_strcmp0(sender, fu_client_get_sender(item->client)) == 0)
			return g_object_ref(item->client);
	}
	return NULL;
}

static void
fu_client_list_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuClientList *self = FU_CLIENT_LIST(object);
	switch (prop_id) {
	case PROP_CONNECTION:
		g_value_set_object(value, self->connection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_client_list_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuClientList *self = FU_CLIENT_LIST(object);
	switch (prop_id) {
	case PROP_CONNECTION:
		self->connection = g_value_dup_object(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_client_list_item_free(FuClientListItem *item)
{
	if (item->watcher_id > 0)
		g_bus_unwatch_name(item->watcher_id);
	g_object_unref(item->client);
	g_free(item);
}

static void
fu_client_list_init(FuClientList *self)
{
	self->array = g_ptr_array_new_with_free_func((GDestroyNotify)fu_client_list_item_free);
}

static void
fu_client_list_finalize(GObject *obj)
{
	FuClientList *self = FU_CLIENT_LIST(obj);
	g_ptr_array_unref(self->array);
	if (self->connection != NULL)
		g_object_unref(self->connection);
	G_OBJECT_CLASS(fu_client_list_parent_class)->finalize(obj);
}

static void
fu_client_list_class_init(FuClientListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_client_list_get_property;
	object_class->set_property = fu_client_list_set_property;
	object_class->finalize = fu_client_list_finalize;

	pspec = g_param_spec_object("connection",
				    NULL,
				    NULL,
				    G_TYPE_DBUS_CONNECTION,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CONNECTION, pspec);

	signals[SIGNAL_ADDED] = g_signal_new("added",
					     G_TYPE_FROM_CLASS(object_class),
					     G_SIGNAL_RUN_LAST,
					     0,
					     NULL,
					     NULL,
					     g_cclosure_marshal_generic,
					     G_TYPE_NONE,
					     1,
					     FU_TYPE_CLIENT);
	signals[SIGNAL_REMOVED] = g_signal_new("removed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_generic,
					       G_TYPE_NONE,
					       1,
					       FU_TYPE_CLIENT);
}

FuClientList *
fu_client_list_new(GDBusConnection *connection)
{
	FuClientList *self;
	g_return_val_if_fail(connection == NULL || G_IS_DBUS_CONNECTION(connection), NULL);
	self = g_object_new(FU_TYPE_CLIENT_LIST, "connection", connection, NULL);
	return FU_CLIENT_LIST(self);
}

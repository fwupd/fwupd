/*
 * Copyright 2026 LXS <support@lxsemicon.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Panel structure */
typedef struct __attribute__((packed)) {
	guint16 x_resolution;
	guint16 y_resolution;
	guint8 x_node;
	guint8 y_node;
} FuStructLxsTouchPanel;

/* Version structure */
typedef struct __attribute__((packed)) {
	guint16 boot_ver;
	guint16 core_ver;
} FuStructLxsTouchVersion;

/* Protocol Setter structure */
typedef struct __attribute__((packed)) {
	guint8 mode;
	guint8 event_trigger_type;
} FuStructLxsTouchProtocolSetter;

/* Protocol Getter structure */
typedef struct __attribute__((packed)) {
	guint8 ready_status;
	guint8 event_ready;
} FuStructLxsTouchProtocolGetter;

/* Flash IAP Command structure */
typedef struct __attribute__((packed)) {
	guint32 addr;
	guint16 size;
	guint8 status;
	guint8 cmd;
} FuStructLxsTouchFlashIAPCmd;

/* Getter functions */
static inline guint16
fu_struct_lxs_touch_panel_get_x_resolution(FuStructLxsTouchPanel *st)
{
	return GUINT16_FROM_LE(st->x_resolution);
}

static inline guint16
fu_struct_lxs_touch_panel_get_y_resolution(FuStructLxsTouchPanel *st)
{
	return GUINT16_FROM_LE(st->y_resolution);
}

static inline guint8
fu_struct_lxs_touch_panel_get_x_node(FuStructLxsTouchPanel *st)
{
	return st->x_node;
}

static inline guint8
fu_struct_lxs_touch_panel_get_y_node(FuStructLxsTouchPanel *st)
{
	return st->y_node;
}

static inline guint16
fu_struct_lxs_touch_version_get_boot_ver(FuStructLxsTouchVersion *st)
{
	return st->boot_ver;
}

static inline guint16
fu_struct_lxs_touch_version_get_core_ver(FuStructLxsTouchVersion *st)
{
	return st->core_ver;
}

static inline guint8
fu_struct_lxs_touch_protocol_setter_get_mode(FuStructLxsTouchProtocolSetter *st)
{
	return st->mode;
}

static inline void
fu_struct_lxs_touch_protocol_setter_set_mode(FuStructLxsTouchProtocolSetter *st, guint8 val)
{
	st->mode = val;
}

static inline void
fu_struct_lxs_touch_protocol_setter_set_event_trigger_type(FuStructLxsTouchProtocolSetter *st,
							    guint8 val)
{
	st->event_trigger_type = val;
}

static inline guint8
fu_struct_lxs_touch_protocol_getter_get_ready_status(FuStructLxsTouchProtocolGetter *st)
{
	return st->ready_status;
}

static inline guint32
fu_struct_lxs_touch_flash_iap_cmd_get_addr(FuStructLxsTouchFlashIAPCmd *st)
{
	return GUINT32_FROM_LE(st->addr);
}

static inline void
fu_struct_lxs_touch_flash_iap_cmd_set_addr(FuStructLxsTouchFlashIAPCmd *st, guint32 val)
{
	st->addr = GUINT32_TO_LE(val);
}

static inline guint16
fu_struct_lxs_touch_flash_iap_cmd_get_size(FuStructLxsTouchFlashIAPCmd *st)
{
	return GUINT16_FROM_LE(st->size);
}

static inline void
fu_struct_lxs_touch_flash_iap_cmd_set_size(FuStructLxsTouchFlashIAPCmd *st, guint16 val)
{
	st->size = GUINT16_TO_LE(val);
}

static inline void
fu_struct_lxs_touch_flash_iap_cmd_set_status(FuStructLxsTouchFlashIAPCmd *st, guint8 val)
{
	st->status = val;
}

static inline void
fu_struct_lxs_touch_flash_iap_cmd_set_cmd(FuStructLxsTouchFlashIAPCmd *st, guint8 val)
{
	st->cmd = val;
}

G_END_DECLS

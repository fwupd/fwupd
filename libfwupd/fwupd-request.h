/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define FWUPD_TYPE_REQUEST (fwupd_request_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdRequest, fwupd_request, FWUPD, REQUEST, GObject)

struct _FwupdRequestClass
{
	GObjectClass			 parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)	(void);
	void (*_fwupd_reserved2)	(void);
	void (*_fwupd_reserved3)	(void);
	void (*_fwupd_reserved4)	(void);
	void (*_fwupd_reserved5)	(void);
	void (*_fwupd_reserved6)	(void);
	void (*_fwupd_reserved7)	(void);
};

/**
 * FwupdRequestKind:
 * @FWUPD_REQUEST_KIND_UNKNOWN:		Unknown kind
 * @FWUPD_REQUEST_KIND_POST:		After the update
 * @FWUPD_REQUEST_KIND_IMMEDIATE:	Immediately
 *
 * The kind of request we are asking of the user.
 **/
typedef enum {
	FWUPD_REQUEST_KIND_UNKNOWN,			/* Since: 1.6.2 */
	FWUPD_REQUEST_KIND_POST,			/* Since: 1.6.2 */
	FWUPD_REQUEST_KIND_IMMEDIATE,			/* Since: 1.6.2 */
	/*< private >*/
	FWUPD_REQUEST_KIND_LAST
} FwupdRequestKind;

/**
 * FWPUD_REQUEST_ID_REMOVE_REPLUG:
 *
 * The user needs to remove and reinsert the device.
 *
 * Since 1.6.2
 */
#define FWPUD_REQUEST_ID_REMOVE_REPLUG			"org.freedesktop.fwupd.request.remove-replug"

/**
 * FWPUD_REQUEST_ID_PRESS_UNLOCK:
 *
 * The user needs to press unlock on the device.
 *
 * Since 1.6.2
 */
#define FWPUD_REQUEST_ID_PRESS_UNLOCK			"org.freedesktop.fwupd.request.press-unlock"

const gchar	*fwupd_request_kind_to_string		(FwupdRequestKind kind);
FwupdRequestKind fwupd_request_kind_from_string		(const gchar	*kind);

FwupdRequest	*fwupd_request_new			(void);
gchar		*fwupd_request_to_string		(FwupdRequest	*self);

const gchar	*fwupd_request_get_id			(FwupdRequest	*self);
void		 fwupd_request_set_id			(FwupdRequest	*self,
							 const gchar	*id);
guint64		 fwupd_request_get_created		(FwupdRequest	*self);
void		 fwupd_request_set_created		(FwupdRequest	*self,
							 guint64	 created);
const gchar	*fwupd_request_get_device_id		(FwupdRequest	*self);
void		 fwupd_request_set_device_id		(FwupdRequest	*self,
							 const gchar	*device_id);
const gchar	*fwupd_request_get_message		(FwupdRequest	*self);
void		 fwupd_request_set_message		(FwupdRequest	*self,
							 const gchar	*message);
const gchar	*fwupd_request_get_image		(FwupdRequest	*self);
void		 fwupd_request_set_image		(FwupdRequest	*self,
							 const gchar	*image);
FwupdRequestKind fwupd_request_get_kind			(FwupdRequest	*self);
void		 fwupd_request_set_kind			(FwupdRequest	*self,
							 FwupdRequestKind	 kind);

FwupdRequest	*fwupd_request_from_variant		(GVariant	*value);

G_END_DECLS

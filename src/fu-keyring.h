/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_KEYRING_H
#define __FU_KEYRING_H

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-keyring-result.h"

G_BEGIN_DECLS

#define FU_TYPE_KEYRING (fu_keyring_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuKeyring, fu_keyring, FU, KEYRING, GObject)

/**
 * FuKeyringVerifyFlags:
 * @FU_KEYRING_VERIFY_FLAG_NONE:		No flags set
 * @FU_KEYRING_VERIFY_FLAG_DISABLE_TIME_CHECKS:	Disable checking of validity periods
 *
 * The flags to use when interacting with a keyring
 **/
typedef enum {
	FU_KEYRING_VERIFY_FLAG_NONE			= 0,
	FU_KEYRING_VERIFY_FLAG_DISABLE_TIME_CHECKS	= 1 << 2,
	/*< private >*/
	FU_KEYRING_VERIFY_FLAG_LAST
} FuKeyringVerifyFlags;

struct _FuKeyringClass
{
	GObjectClass		 parent_class;
	gboolean		 (*setup)		(FuKeyring	*keyring,
							 GError		**error);
	gboolean		 (*add_public_keys)	(FuKeyring	*keyring,
							 const gchar	*path,
							 GError		**error);
	FuKeyringResult		*(*verify_data)		(FuKeyring	*keyring,
							 GBytes		*payload,
							 GBytes		*payload_signature,
							 FuKeyringVerifyFlags flags,
							 GError		**error);
};

gboolean	 fu_keyring_setup			(FuKeyring	*keyring,
							 GError		**error);
gboolean	 fu_keyring_add_public_keys		(FuKeyring	*keyring,
							 const gchar	*path,
							 GError		**error);
FuKeyringResult	*fu_keyring_verify_data			(FuKeyring	*keyring,
							 GBytes		*blob,
							 GBytes		*blob_signature,
							 FuKeyringVerifyFlags flags,
							 GError		**error);
const gchar	*fu_keyring_get_name			(FuKeyring	*self);
void		 fu_keyring_set_name			(FuKeyring	*self,
							 const gchar	*name);

G_END_DECLS

#endif /* __FU_KEYRING_H */

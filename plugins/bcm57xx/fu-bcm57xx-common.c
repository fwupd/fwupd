/*
 * Copyright (C) 2018 Evan Lojewski
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-bcm57xx-common.h"

guint32
fu_bcm57xx_nvram_crc(const guint8 *buf, gsize bufsz)
{
	return fu_crc32(buf, bufsz);
}

gboolean
fu_bcm57xx_verify_crc(GBytes *fw, GError **error)
{
	guint32 crc_actual;
	guint32 crc_file = 0;
	gsize bufsz = 0x0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* expected */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    bufsz - sizeof(guint32),
				    &crc_file,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* reality */
	crc_actual = fu_bcm57xx_nvram_crc(buf, bufsz - sizeof(guint32));
	if (crc_actual != crc_file) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid CRC, expected 0x%08x got: 0x%08x",
			    (guint)crc_file,
			    (guint)crc_actual);
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_bcm57xx_verify_magic(GBytes *fw, gsize offset, GError **error)
{
	guint32 magic = 0;
	gsize bufsz = 0x0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* hardcoded value */
	if (!fu_memread_uint32_safe(buf, bufsz, offset, &magic, G_BIG_ENDIAN, error))
		return FALSE;
	if (magic != BCM_NVRAM_MAGIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid magic, got: 0x%x",
			    (guint)magic);
		return FALSE;
	}

	/* success */
	return TRUE;
}

void
fu_bcm57xx_veritem_free(Bcm57xxVeritem *veritem)
{
	g_free(veritem->branch);
	g_free(veritem->version);
	g_free(veritem);
}

Bcm57xxVeritem *
fu_bcm57xx_veritem_new(const guint8 *buf, gsize bufsz)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(Bcm57xxVeritem) veritem = g_new0(Bcm57xxVeritem, 1);
	struct {
		const gchar *prefix;
		const gchar *branch;
		FwupdVersionFormat verfmt;
	} data[] = {{"5719-v", BCM_FW_BRANCH_UNKNOWN, FWUPD_VERSION_FORMAT_PAIR},
		    {"stage1-", BCM_FW_BRANCH_OSS_FIRMWARE, FWUPD_VERSION_FORMAT_TRIPLET},
		    {NULL, NULL, 0}};

	/* do not assume this is NUL terminated */
	tmp = g_strndup((const gchar *)buf, bufsz);
	if (tmp == NULL || tmp[0] == '\0')
		return NULL;

	/* use prefix to define object */
	for (guint i = 0; data[i].prefix != NULL; i++) {
		if (g_str_has_prefix(tmp, data[i].prefix)) {
			veritem->version = g_strdup(tmp + strlen(data[i].prefix));
			veritem->branch = g_strdup(data[i].branch);
			veritem->verfmt = data[i].verfmt;
			return g_steal_pointer(&veritem);
		}
	}
	veritem->verfmt = FWUPD_VERSION_FORMAT_UNKNOWN;
	veritem->version = g_strdup(tmp);
	return g_steal_pointer(&veritem);
}

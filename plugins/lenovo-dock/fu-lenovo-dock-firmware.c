/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-lenovo-dock-firmware.h"

struct _FuLenovoDockFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuLenovoDockFirmware, fu_lenovo_dock_firmware, FU_TYPE_FIRMWARE)

/* revX is ‘/’ */
typedef struct __attribute__((packed)) {
	guint8 tag[2]; /* '5A' */
	guint8 rev1;
	guint8 ver[4]; /* ‘E104’ */
	guint8 rev2;
	guint8 date[10]; /* ‘2020/10/08’ */
	guint8 rev3;
	guint8 tag1[2]; /* ‘UG' */
	guint8 rev4;
	guint8 vid[4]; /* ’17EF’ */
	guint8 rev5;
	guint8 pid[4]; /* ‘30B4’ */
	guint8 rev6;
	guint8 file_cnt[4]; /* ‘00EF’ */
	guint8 rev7;
} IspLabel;

const gchar * 
fw_selection[] = {
	"FW_NONE",
	"FW_40B1",
	"FW_40B0",
	"FW_LAST"
};

typedef struct {
        FuFirmware *firmware;
} FuLenovoDockFirmwareTokenHelper;

static gboolean
fu_lenovo_dock_firmware_tokenize_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	FuLenovoDockFirmwareTokenHelper *helper = (FuLenovoDockFirmwareTokenHelper *)user_data;
	g_autoptr(GBytes) blob = NULL;

	if (token->len == 0) return TRUE;

	g_autoptr(FuFirmware) img = fu_firmware_new();

	blob = g_bytes_new(token->str, token->len);

	fu_firmware_set_bytes(img, blob);
	fu_firmware_set_idx(img, token_idx);
	fu_firmware_set_id(img, fw_selection[token_idx]);

        /* done */
	fu_firmware_add_image(helper->firmware, img);

	return TRUE;
}

static gboolean
fu_lenovo_dock_firmware_parse(FuFirmware *firmware,
			      GBytes *fw,
			      guint64 addr_start,
			      guint64 addr_end,
			      FwupdInstallFlags flags,
			      GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	gchar tagbuf[3] = {0};
	FuLenovoDockFirmwareTokenHelper helper = {.firmware = firmware};
	gchar label[sizeof(IspLabel)+1] = { 0x0 };

	/* tag */
	if (!fu_memcpy_safe((guint8 *)tagbuf,
                            sizeof(tagbuf),
                            0x0,
                            buf,
                            bufsz,
                            G_STRUCT_OFFSET(IspLabel, tag),
                            2,
                            error))
                 return FALSE;

        if (g_strcmp0(tagbuf, "5A") != 0) {
                        g_set_error(error,
                                    G_IO_ERROR,
                                    G_IO_ERROR_NOT_SUPPORTED,
                                    "got tag %s, expected 5A",
                                    tagbuf);
                 return FALSE;
        }

	/* tag1 */
        if (!fu_memcpy_safe((guint8 *)tagbuf,
                            sizeof(tagbuf),
                            0x0,
                            buf,
                            bufsz,
                            G_STRUCT_OFFSET(IspLabel, tag1),
                            2,
                            error))
                 return FALSE;

	if (g_strcmp0(tagbuf, "UG") != 0) {
	                g_set_error(error,
                                    G_IO_ERROR,
                                    G_IO_ERROR_NOT_SUPPORTED,
                                    "got tag1 %s, expected UG",
                                    tagbuf);
                 return FALSE;
	}

	/* tokenize */
	if (!fu_memcpy_safe((guint8 *) label,
                            sizeof(label),
                            0x0, /* dst */
                            buf,
                            bufsz,
                            0x0, /* src */
                            sizeof(label),
                            error))
                return FALSE;

        if (!fu_common_strnsplit_full(g_bytes_get_data(fw, NULL),
                                      g_bytes_get_size(fw),
                                      (gchar*) &label,
                                      fu_lenovo_dock_firmware_tokenize_cb,
                                      &helper,
                                      error))
                return FALSE;

	/* success */
	return TRUE;
}

static void
fu_lenovo_dock_firmware_init(FuLenovoDockFirmware *self)
{
}

static void
fu_lenovo_dock_firmware_class_init(FuLenovoDockFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_lenovo_dock_firmware_parse;
}

FuFirmware *
fu_lenovo_dock_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LENOVO_DOCK_FIRMWARE, NULL));
}

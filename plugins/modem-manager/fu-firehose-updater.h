/*
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-sahara-loader.h"

#define FU_TYPE_FIREHOSE_UPDATER (fu_firehose_updater_get_type())
G_DECLARE_FINAL_TYPE(FuFirehoseUpdater, fu_firehose_updater, FU, FIREHOSE_UPDATER, GObject)

FuFirehoseUpdater *
fu_firehose_updater_new(const gchar *port, FuSaharaLoader *sahara);

gboolean
fu_firehose_updater_open(FuFirehoseUpdater *self, GError **error);
gboolean
fu_firehose_updater_write(FuFirehoseUpdater *self,
			  XbSilo *silo,
			  GPtrArray *action_nodes,
			  FuProgress *progress,
			  GError **error);
gboolean
fu_firehose_updater_close(FuFirehoseUpdater *self, GError **error);

/* helpers */

gboolean
fu_firehose_validate_rawprogram(GBytes *rawprogram,
				FuArchive *archive,
				XbSilo **out_silo,
				GPtrArray **out_action_nodes,
				GError **error);

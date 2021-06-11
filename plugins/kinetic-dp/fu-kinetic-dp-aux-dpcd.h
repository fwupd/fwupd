/*
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-kinetic-dp-connection.h"

gboolean fu_kinetic_dp_aux_dpcd_read_oui(FuKineticDpConnection *connection,
                                         guint8 *buf,
                                         guint32 buf_size,
                                         GError **error);
gboolean fu_kinetic_dp_aux_dpcd_write_oui(FuKineticDpConnection *connection,
                                          const guint8 *buf,
                                          GError **error)
gboolean fu_kinetic_dp_aux_dpcd_read_branch_id_str(FuKineticDpConnection *connection,
                                                   guint8 *buf,
                                                   guint32 buf_size,
                                                   GError **error)


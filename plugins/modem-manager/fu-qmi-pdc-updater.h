/*
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_QMI_PDC_UPDATER_H
#define __FU_QMI_PDC_UPDATER_H

#include <libqmi-glib.h>

#define FU_TYPE_QMI_PDC_UPDATER (fu_qmi_pdc_updater_get_type ())
G_DECLARE_FINAL_TYPE (FuQmiPdcUpdater, fu_qmi_pdc_updater, FU, QMI_PDC_UPDATER, GObject)

FuQmiPdcUpdater	*fu_qmi_pdc_updater_new		(const gchar		*qmi_port);
gboolean	 fu_qmi_pdc_updater_open	(FuQmiPdcUpdater	*self,
						 GError			**error);
GArray		*fu_qmi_pdc_updater_write	(FuQmiPdcUpdater	*self,
						 const gchar		*filename,
						 GBytes			*blob,
						 GError			**error);
gboolean	 fu_qmi_pdc_updater_activate	(FuQmiPdcUpdater	*self,
						 GArray			*digest,
						 GError			**error);
gboolean	 fu_qmi_pdc_updater_close	(FuQmiPdcUpdater	*self,
						 GError			**error);

#endif /* __FU_QMI_PDC_UPDATER_H */

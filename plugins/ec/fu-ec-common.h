/*
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

/* EC Status Register (see ec/google/chromeec/ec_commands.h) */
#define EC_STATUS_OBF		(1 << 0)	/* o/p buffer full */
#define EC_STATUS_IBF		(1 << 1)	/* i/p buffer full */
#define EC_STATUS_IS_BUSY	(1 << 2)
#define EC_STATUS_IS_CMD	(1 << 3)
#define EC_STATUS_BURST_ENABLE	(1 << 4)
#define EC_STATUS_SCI		(1 << 5)	/* 1 if more events in queue */

/* EC Command Register (see KB3700-ds-01.pdf) */
#define EC_CMD_READ    		0x80
#define EC_CMD_WRITE		0x81

/* what to do with Autoload */
#define AUTOLOAD_NO_ACTION	0
#define AUTOLOAD_DISABLE	1
#define AUTOLOAD_SET_ON		2
#define AUTOLOAD_SET_OFF       	3

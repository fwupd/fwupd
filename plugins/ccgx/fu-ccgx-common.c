/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include <string.h>
#include "fu-ccgx-common.h"

/* ccgx part information */
static CCGxPartInfo ccgx_known_parts[] = {
	{"CCG2",    "CYPD2103-20FNXI",	  0x140011A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2103-14LHXI",	  0x140311A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2104-20FNXI",	  0x140111A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2105-20FNXI",	  0x140211A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2122-24LQXI",	  0x140411A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2122-20FNXI",	  0x140612A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2134-24LQXI",	  0x140511A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2120-24LQXI",	  0x141213A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2119-24LQXI",	  0x140913A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2123-24LQXI",	  0x140711A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2124-24LQXI",	  0x140811A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2121-24LQXI",	  0x141011A4, 0x80,  0x8000},
	{"CCG2",    "CYPD2125-24LQXI",	  0x141111A4, 0x80,  0x8000},
	{"CCG3",    "CYPD3120-40LQXI",	  0x1D0011AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3105-42FNXI",	  0x1D0111AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3121-40LQXI",	  0x1D0211AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3122-40LQXI",	  0x1D0311AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3125-40LQXI",	  0x1D0411AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3135-40LQXI",	  0x1D0511AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3135-16SXQ'",	  0x1D0611AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3126-42FNXI",	  0x1D0711AD, 0x80,  0x20000},
	{"CCG3",    "CYPD3123-40LQXI",	  0x1D0911AD, 0x80,  0x20000},
	{"CCG4",    "CYPD4225-40LQXI",	  0x180011A8, 0x100, 0x20000},
	{"CCG4",    "CYPD4125-40LQXI",	  0x180111A8, 0x100, 0x20000},
	{"CCG4",    "CYPD4235-40LQXI",	  0x180211A8, 0x100, 0x20000},
	{"CCG4",    "CYPD4135-40LQXI",	  0x180311A8, 0x100, 0x20000},
	{"CCG4",    "CYPD4225A0-33FNXIT", 0x181011A8, 0x100, 0x20000},
	{"CCG4",    "CYPD4226-40LQXI",	  0x1F0011AF, 0x100, 0x20000},
	{"CCG4",    "CYPD4126-40LQXI",	  0x1F0111AF, 0x100, 0x20000},
	{"CCG4",    "CYPD4126-24LQXI",	  0x1F0411AF, 0x100, 0x20000},
	{"CCG4",    "CYPD4236-40LQXI",	  0x1F0211AF, 0x100, 0x20000},
	{"CCG4",    "CYPD4136-40LQXI",	  0x1F0311AF, 0x100, 0x20000},
	{"CCG4",    "CYPD4136-24LQXI",	  0x1F0511AF, 0x100, 0x20000},
	{"CCG3PA",  "CYPD3174-24LQXQ",	  0x200011B0, 0x80,  0x10000},
	{"CCG3PA",  "CYPD3174-16SXQ",	  0x200111B0, 0x80,  0x10000},
	{"CCG3PA",  "CYPD3175-24LQXQ",	  0x200211B0, 0x80,  0x10000},
	{"CCG3PA",  "CYPD3171-24LQXQ",	  0x200311B0, 0x80,  0x10000},
	{"CCG3PA",  "CYPD3195-24LDXS",	  0x200511B0, 0x80,  0x10000},
	{"CCG3PA",  "CYPD3196-24LDXS",	  0x200611B0, 0x80,  0x10000},
	{"CCG3PA",  "CYPD3197-24LDXS",	  0x200711B0, 0x80,  0x10000},
	{"CCG3PA2", "CYPDC1185-32LQXQ",   0x240011B4, 0x80,  0x20000},
	{"CCG3PA2", "CYPDC1186-30FNXI",   0x240111B4, 0x80,  0x20000},
	{"CCG3PA2", "CYPDC1186B2-30FNXI", 0x240211B4, 0x80,  0x20000},
	{"CCG5",    "CYPD5225-96BZXI",	  0x210011B1, 0x100, 0x20000},
	{"CCG5",    "CYPD5125-40LQXI",	  0x210111B1, 0x100, 0x20000},
	{"CCG5",    "CYPD5235-96BZXI",	  0x210211B1, 0x100, 0x20000},
	{"CCG5",    "CYPD5236-96BZXI",	  0x210311B1, 0x100, 0x20000},
	{"CCG5",    "CYPD5237-96BZXI",	  0x210411B1, 0x100, 0x20000},
	{"CCG5",    "CYPD5227-96BZXI",	  0x210511B1, 0x100, 0x20000},
	{"CCG5",    "CYPD5135-40LQXI",	  0x210611B1, 0x100, 0x20000},
	{"CCG6",    "CYPD6125-40LQXI",	  0x2A0011BA, 0x100, 0x20000},
	{"CCG6",    "CYPD6126-96BZXI",	  0x2A1011BA, 0x100, 0x20000},
	{"CCG6",    "CYPD5126-40LQXI",	  0x2A0111BA, 0x100, 0x20000},
	{"CCG6",    "CYPD5137-40LQXI",	  0x2A0211BA, 0x100, 0x20000},
	{"CCG6",    "CYPD6137-40LQXI",	  0x2A0311BA, 0x100, 0x20000},
	{"PAG1S",   "CYPAS111-24LQXQ",	  0x2B0111BB, 0x80,  0x10000},
	{"PAG1S",   "CYPD3184-24LQXQ",	  0x2B0011BB, 0x80,  0x10000},
	{"HX3PD",   "CYUSB4347-BZXC_PD",  0x1F8211AF, 0x100, 0x20000},
	{"ACG1F",   "CYAC1126-24LQXI",	  0x2F0011BF, 0x40,  0x4000},
	{"ACG1F",   "CYAC1126-40LQXI",	  0x2F0111BF, 0x40,  0x4000},
	{"CCG6DF",  "CYPD6227-96BZXI",	  0x300011C0, 0x80,  0x10000},
	{"CCG6DF",  "CYPD6127-96BZXI",	  0x300111C0, 0x80,  0x10000},
	{"CCG6SF",  "CYPD6128-96BZXI",	  0x330011C3, 0x80,  0x10000},
	{"CCG6SF",  "CYPD6127-48LQXI",	  0x330111C3, 0x80,  0x10000}
};

/**
 * fu_ccgx_util_init_elapsed_time:
 * @start_time[in,out] start time
 *
 * Initialize timer to measure elapsed time
*/
void
fu_ccgx_util_init_elapsed_time (struct timeval *start_time)
{
	if (start_time) {
		gettimeofday (start_time, NULL);
	}
}

/**
 * fu_ccgx_util_get_elapsed_time_ms:
 * @start_time[in] start time
 *
 * Get elapsed time since start time
 *
 * Return value:  Elapsed time since start time in milisecond
*/
guint64
fu_ccgx_util_get_elapsed_time_ms (struct timeval *start_time)
{
	struct timeval current_time;
	guint64 elapsed_time_ms = 0;
	guint64 current_time_ms;
	guint64 start_time_ms;

	if (start_time != NULL) {
		gettimeofday (&current_time, NULL);

		start_time_ms = start_time->tv_sec * 1000 + start_time->tv_usec/1000;
		current_time_ms = current_time.tv_sec * 1000 + current_time.tv_usec /1000;

		if ( current_time_ms >= start_time_ms ) {
			elapsed_time_ms =  current_time_ms -  start_time_ms;
		} else { /* wrap around, start again */
			gettimeofday (start_time, NULL);
		}
	}
	return elapsed_time_ms;
}

/**
 * fu_ccgx_util_find_ccgx_info:
 *
 * @silicon_id 16 bit silicon ID
 *
 * Read row data in cyacd buffer
 *
 * Return value: CCGxPartInfo pointer if it success, NULL otherwise
 */
CCGxPartInfo *
fu_ccgx_util_find_ccgx_info (guint16 silicon_id)
{
	guint16 tbl_silicon_id;
	guint32 i;

	for (i = 0; i < (sizeof(ccgx_known_parts) / sizeof(CCGxPartInfo)); i++) {
		tbl_silicon_id = (ccgx_known_parts[i].silicon_id) >> 16;
		if (silicon_id == tbl_silicon_id) {
			return &ccgx_known_parts[i];
		}
	}
	return NULL;
}


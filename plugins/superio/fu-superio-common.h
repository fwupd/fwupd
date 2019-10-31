/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

/* for all LDNs */
#define SIO_LDNxx_IDX_LDNSEL		0x07
#define SIO_LDNxx_IDX_CHIPID1		0x20
#define SIO_LDNxx_IDX_CHIPID2		0x21
#define SIO_LDNxx_IDX_CHIPVER		0x22
#define SIO_LDNxx_IDX_SIOCTRL		0x23
#define SIO_LDNxx_IDX_SIOIRQ		0x25
#define SIO_LDNxx_IDX_SIOGP		0x26
#define SIO_LDNxx_IDX_SIOPWR		0x2d
#define SIO_LDNxx_IDX_D2ADR		0x2e
#define SIO_LDNxx_IDX_D2DAT		0x2f

#define SIO_LDNxx_IDX_IOBAD0		0x60 /* 16 bit */
#define SIO_LDNxx_IDX_IOBAD1		0x62 /* 16 bit */

/* these registers are only accessible by EC */
#define GCTRL_ECHIPID1			0x2000
#define GCTRL_ECHIPID2			0x2001
#define GCTRL_ECHIPVER			0x2002

/* to create sub-addresses */
#define SIO_DEPTH2_I2EC_ADDRL		0x10
#define SIO_DEPTH2_I2EC_ADDRH		0x11
#define SIO_DEPTH2_I2EC_DATA		0x12

/*
 * The PMC is a communication channel used between the host and the EC.
 * Compatible mode uses four registers:
 *
 * Name			| EC		| HOST		| ADDR
 * _____________________|_______________|_______________|______
 * PMDIR		| RO		| WO		| 0x62
 * PMDOR		| WO		| RO		| 0x62
 * PMCMDR		| RO		| RO		| 0x66
 * PMSTR		| RO		| RO		| 0x66
 */
#define SIO_EC_PMC_PM1STS		0x00
#define SIO_EC_PMC_PM1DO		0x01
#define SIO_EC_PMC_PM1DOSCI		0x02
#define SIO_EC_PMC_PM1DOCMI		0x03
#define SIO_EC_PMC_PM1DI		0x04
#define SIO_EC_PMC_PM1DISCI		0x05
#define SIO_EC_PMC_PM1CTL		0x06
#define SIO_EC_PMC_PM1IC		0x07
#define SIO_EC_PMC_PM1IE		0x08

/* SPI commands */
#define SIO_SPI_CMD_READ		0x03
#define SIO_SPI_CMD_HS_READ		0x0b
#define SIO_SPI_CMD_FAST_READ_DUAL_OP	0x3b
#define SIO_SPI_CMD_FAST_READ_DUAL_IO	0xbb
#define SIO_SPI_CMD_4K_SECTOR_ERASE	0xd7 /* or 0x20 or 0x52 */
#define SIO_SPI_CMD_64K_BLOCK_ERASE	0xd8
#define SIO_SPI_CMD_CHIP_ERASE		0xc7 /* or 0x60 */
#define SIO_SPI_CMD_PAGE_PROGRAM	0x02
#define SIO_SPI_CMD_WRITE_WORD		0xad
#define SIO_SPI_CMD_RDSR		0x05 /* read status register */
#define SIO_SPI_CMD_WRSR		0x01 /* write status register */
#define SIO_SPI_CMD_WREN		0x06 /* write enable */
#define SIO_SPI_CMD_WRDI		0x04 /* write disable */
#define SIO_SPI_CMD_RDID		0xab
#define SIO_SPI_CMD_JEDEC_ID		0x9f
#define SIO_SPI_CMD_DPD			0xb9 /* deep sleep */
#define SIO_SPI_CMD_RDPD		0xab /* wake from deep sleep */

/* EC Status Register (see ec/google/chromeec/ec_commands.h) */
#define SIO_STATUS_EC_OBF		(1 << 0)	/* o/p buffer full */
#define SIO_STATUS_EC_IBF		(1 << 1)	/* i/p buffer full */
#define SIO_STATUS_EC_IS_BUSY		(1 << 2)
#define SIO_STATUS_EC_IS_CMD		(1 << 3)
#define SIO_STATUS_EC_BURST_ENABLE	(1 << 4)
#define SIO_STATUS_EC_SCI		(1 << 5)	/* 1 if more events in queue */

/* EC Command Register (see KB3700-ds-01.pdf) */
#define SIO_CMD_EC_READ			0x80
#define SIO_CMD_EC_WRITE		0x81
#define SIO_CMD_EC_BURST_ENABLE		0x82
#define SIO_CMD_EC_BURST_DISABLE	0x83
#define SIO_CMD_EC_QUERY_EVENT		0x84
#define SIO_CMD_EC_GET_NAME_STR		0x92
#define SIO_CMD_EC_GET_VERSION_STR	0x93
#define SIO_CMD_EC_DISABLE_HOST_WA	0xdc
#define SIO_CMD_EC_ENABLE_HOST_WA	0xfc

typedef enum {
	SIO_LDN_FDC			= 0x00,	/* IT87 */
	SIO_LDN_UART1			= 0x01,	/* IT87+IT89 */
	SIO_LDN_UART2			= 0x02,	/* IT87+IT89 */
	SIO_LDN_PARALLEL_PORT		= 0x03,	/* IT87 */
	SIO_LDN_SWUC			= 0x04,	/* IT87+IT89 */
	SIO_LDN_KBC_MOUSE		= 0x05,	/* IT87+IT89 */
	SIO_LDN_KBC_KEYBOARD		= 0x06,	/* IT87+IT89 */
	SIO_LDN_GPIO			= 0x07,	/* IT87 */
	SIO_LDN_UART3			= 0x08,	/* IT87 */
	SIO_LDN_UART4			= 0x09,	/* IT87 */
	SIO_LDN_CIR			= 0x0a,	/* IT89 */
	SIO_LDN_SMFI			= 0x0f,	/* IT89 */
	SIO_LDN_RTCT			= 0x10,	/* IT89 */
	SIO_LDN_PM1			= 0x11,	/* IT89 */
	SIO_LDN_PM2			= 0x12,	/* IT89 */
	SIO_LDN_SSSP1			= 0x13,	/* IT89 */
	SIO_LDN_PECI			= 0x14,	/* IT89 */
	SIO_LDN_PM3			= 0x17,	/* IT89 */
	SIO_LDN_PM4			= 0x18,	/* IT89 */
	SIO_LDN_PM5			= 0x19,	/* IT89 */
	SIO_LDN_LAST			= 0x1a
} SioLdn;

const gchar	*fu_superio_ldn_to_text	(guint8		 ldn);

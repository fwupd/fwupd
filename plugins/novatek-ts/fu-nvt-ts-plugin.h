/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

/* plugin type */
#define FU_TYPE_NVT_TS_PLUGIN (fu_nvt_ts_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuNvtTsPlugin, fu_nvt_ts_plugin, FU, NVT_TS_PLUGIN, FuPlugin)

#define NVT_TS_REPORT_ID 0x0B

#ifndef msleep
#define msleep(ms) g_usleep((gulong)(ms) * 1000)
#endif

#define NVT_VID_NUM		 0x0603
#define NT36536_PDID		 0xF203u
#define FLASH_PAGE_SIZE		 256
#define NVT_TRANSFER_LEN	 256
#define SIZE_4KB		 (1024 * 4)
#define SIZE_64KB		 (1024 * 64)
#define SIZE_320KB		 (1024 * 320)
#define BLOCK_64KB_NUM		 4
#define BYTE_PER_POINT		 2
#define FLASH_SECTOR_SIZE	 SIZE_4KB
#define MAX_BIN_SIZE		 SIZE_320KB
#define FLASH_DID_ALL		 0xFFFF
#define HID_FW_BIN_END_NAME_FULL "NVT"
#define BIN_END_FLAG_LEN_FULL	 3
#define BIN_END_FLAG_LEN_MAX	 4

typedef struct {
	uint32_t addr;
	uint8_t mask;
} FuNvtTsReg;

typedef struct {
	uint32_t chip_ver_trim_addr;
	uint32_t swrst_sif_addr;
	uint32_t event_buf_cmd_addr;
	uint32_t event_buf_hs_sub_cmd_addr;
	uint32_t event_buf_reset_state_addr;
	uint32_t event_map_fwinfo_addr;
	uint32_t read_flash_checksum_addr;
	uint32_t rw_flash_data_addr;
	uint32_t enb_casc_addr;
	uint32_t hid_i2c_eng_addr;
	uint32_t gcm_code_addr;
	uint32_t gcm_flag_addr;
	uint32_t flash_cmd_addr;
	uint32_t flash_cmd_issue_addr;
	uint32_t flash_cksum_status_addr;
	uint32_t bld_spe_pups_addr;
	FuNvtTsReg pp4io_en_reg;
	FuNvtTsReg bld_rd_addr_sel_reg;
	FuNvtTsReg bld_rd_io_sel_reg;
	uint32_t q_wr_cmd_addr;
} FuNvtTsMemMap;

typedef struct {
	uint32_t flash_normal_fw_start_addr;
	uint32_t flash_pid_addr;
	uint32_t flash_fw_size;
} FuNvtTsFlashMap;

typedef struct {
	uint8_t flash_cmd;
	uint32_t flash_addr;
	uint16_t flash_checksum;
	uint8_t flash_addr_len;
	uint8_t pem_byte_len;
	uint8_t dummy_byte_len;
	uint8_t *tx_buf;
	uint16_t tx_len;
	uint8_t *rx_buf;
	uint16_t rx_len;
} FuNvtTsGcmXfer;

typedef enum {
	RESET_STATE_INIT = 0xA0,
	RESET_STATE_REK_BASELINE,
	RESET_STATE_REK_FINISH,
	RESET_STATE_NORMAL_RUN,
	RESET_STATE_MAX = 0xAF
} FuNvtTsResetState;

typedef struct {
	uint8_t *bin_data;
	uint32_t bin_size;
	uint32_t flash_start_addr;
} FuNvtTsFwBin;

/* flash manufacturer identification */
typedef enum {
	FLASH_MFR_UNKNOWN = 0x00,
	FLASH_MFR_ESMT = 0x1C,
	FLASH_MFR_PUYA = 0x85,	     /* puya */
	FLASH_MFR_FM = 0xA1,	     /* fm */
	FLASH_MFR_MACRONIX = 0xC2,   /* macronix */
	FLASH_MFR_GIGADEVICE = 0xC8, /* gigadevice */
	FLASH_MFR_WINBOND = 0xEF,    /* winbond */
	FLASH_MFR_MAX = 0xFF
} FuNvtTsFlashMfr;

/* find "QE" or "status register" */
typedef enum {
	QEB_POS_UNKNOWN = 0,
	QEB_POS_SR_1B, /* qe bit in SR 1st byte */
	QEB_POS_OTHER, /* qe bit not in SR 1st byte */
	QEB_POS_MAX
} FuNvtTsQebPos;

/* search "write status register" or "wrsr" */
typedef enum {
	FLASH_WRSR_METHOD_UNKNOWN = 0,
	WRSR_01H1BYTE, /* 01H (S7-S0) */
	WRSR_01H2BYTE, /* 01H (S7-S0) (S15-S8) */
	FLASH_WRSR_METHOD_MAX
} FuNvtTsFlashWrsrMethod;

typedef struct {
	FuNvtTsQebPos qeb_pos; /* qe bit position type, ex. in SR 1st/2nd byte, etc */
	uint8_t qeb_order;     /* in which bit of that byte, start from bit 0 */
} FuNvtTsFlashQebInfo;

/* find "03h" or "read data bytes" */
typedef enum {
	FLASH_READ_METHOD_UNKNOWN = 0,
	SISO_0x03,
	SISO_0x0B,
	SIQO_0x6B,
	QIQO_0xEB,
	FLASH_READ_METHOD_MAX
} FuNvtTsFlashReadMethod;

/* find "page program" */
typedef enum {
	FLASH_PROG_METHOD_UNKNOWN = 0,
	SPP_0x02, /* singalPageProgram_0x02 */
	QPP_0x32, /* quadPageProgram_0x32 */
	QPP_0x38, /* quadPageProgram_0x38 */
	FLASH_PROG_METHOD_MAX
} FuNvtTsFlashProgMethod;

typedef enum {
	FLASH_LOCK_METHOD_UNKNOWN,
	FLASH_LOCK_METHOD_SW_BP_ALL,
	FLASH_LOCK_METHOD_MAX,
} FuNvtTsFlashLockMethod;

typedef struct {
	FuNvtTsFlashMfr mid; /* manufacturer identification */
	uint16_t did;	     /* 2 bytes device identification read by 0x9F RDID */
			     /* command manufacturer ID, memory type, memory density */
	FuNvtTsFlashQebInfo qeb_info;
	FuNvtTsFlashReadMethod rd_method;   /* flash read method */
	FuNvtTsFlashProgMethod prog_method; /* flash program method */
	FuNvtTsFlashWrsrMethod wrsr_method; /* write status register method */
	/* find "rdsr" or "read status register" */
	uint8_t rdsr1_cmd;		    /* cmd for read status register-2 (S15-S8) */
	FuNvtTsFlashLockMethod lock_method; /* block protect position */
	uint8_t sr_bp_bits_all;		    /* bp all protect bits setting in SR for */
					    /* FLASH_LOCK_METHOD_SW_BP_ALL */
} FuNvtTsFlashInfo;

static const FuNvtTsFlashInfo fu_nvt_ts_flash_info_table[] = {
    /* please put flash info items which will use quad mode and is verified */
    /* before those with "did = FLASH_DID_ALL"! */
    {.mid = FLASH_MFR_GIGADEVICE,
     .did = 0x4013,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H2BYTE,
     .rdsr1_cmd = 0x35},
    {.mid = FLASH_MFR_GIGADEVICE,
     .did = 0x6012,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H1BYTE,
     .rdsr1_cmd = 0xFF},
    {.mid = FLASH_MFR_GIGADEVICE,
     .did = 0x6016,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H2BYTE,
     .rdsr1_cmd = 0x35},
    {.mid = FLASH_MFR_PUYA,
     .did = 0x4412,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H1BYTE,
     .rdsr1_cmd = 0xFF},
    {.mid = FLASH_MFR_PUYA,
     .did = 0x6013,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H2BYTE,
     .rdsr1_cmd = 0x35},
    {.mid = FLASH_MFR_PUYA,
     .did = 0x6015,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H2BYTE,
     .rdsr1_cmd = 0x35},
    {.mid = FLASH_MFR_WINBOND,
     .did = 0x3012,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H1BYTE,
     .rdsr1_cmd = 0xFF},
    {.mid = FLASH_MFR_WINBOND,
     .did = 0x6016,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H1BYTE,
     .rdsr1_cmd = 0x35},
    {.mid = FLASH_MFR_MACRONIX,
     .did = 0x2813,
     .qeb_info = {.qeb_pos = QEB_POS_SR_1B, .qeb_order = 6},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H1BYTE,
     .rdsr1_cmd = 0xFF},
    {.mid = FLASH_MFR_FM,
     .did = 0x2813,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 1},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H2BYTE,
     .rdsr1_cmd = 0x35},
    {.mid = FLASH_MFR_WINBOND,
     .did = 0x6012,
     .qeb_info = {.qeb_pos = QEB_POS_OTHER, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = WRSR_01H1BYTE,
     .rdsr1_cmd = 0x35},
    /* please note that the following flash info item should be keep at the last one! Do not move
       it! */
    {.mid = FLASH_MFR_UNKNOWN,
     .did = FLASH_DID_ALL,
     .qeb_info = {.qeb_pos = QEB_POS_UNKNOWN, .qeb_order = 0xFF},
     .rd_method = SISO_0x03,
     .prog_method = SPP_0x02,
     .wrsr_method = FLASH_WRSR_METHOD_UNKNOWN,
     .rdsr1_cmd = 0xFF}};

typedef struct {
	const FuNvtTsMemMap *mmap;
	const FuNvtTsFlashMap *fmap;
	uint8_t fw_ver;
	uint8_t flash_mid;
	uint16_t flash_did;
	uint16_t flash_pid;
	const FuNvtTsFlashInfo *match_finfo;
	uint8_t flash_prog_data_cmd;
	uint8_t flash_read_data_cmd;
	uint8_t flash_read_pem_byte_len;
	uint8_t flash_read_dummy_byte_len;
} FuNvtTsData;

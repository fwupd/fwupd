/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <fwupdplugin.h>

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

/* plugin type */
#define FU_TYPE_NVT_TS_PLUGIN (fu_nvt_ts_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuNvtTsPlugin, fu_nvt_ts_plugin, FU, NVT_TS_PLUGIN, FuPlugin)

/* device type (hidraw-based), declared in the same header */
#define FU_TYPE_NVT_TS_DEVICE (fu_nvt_ts_device_get_type())
G_DECLARE_FINAL_TYPE(FuNvtTsDevice, fu_nvt_ts_device, FU, NVT_TS_DEVICE, FuHidrawDevice)

#define NVT_TS_REPORT_ID 0x0B

#define NVT_DBG(fmt, ...)                                                                          \
	G_STMT_START                                                                               \
	{                                                                                          \
		g_debug("[NVT_TS][Debug] {%s} %s +%d : " fmt,                                      \
			__func__,                                                                  \
			__FILE__,                                                                  \
			__LINE__,                                                                  \
			##__VA_ARGS__);                                                            \
	}                                                                                          \
	G_STMT_END

#define NVT_LOG(fmt, ...)                                                                          \
	G_STMT_START                                                                               \
	{                                                                                          \
		g_info("[NVT_TS][Info ] {%s} %s +%d : " fmt,                                       \
		       __func__,                                                                   \
		       __FILE__,                                                                   \
		       __LINE__,                                                                   \
		       ##__VA_ARGS__);                                                             \
	}                                                                                          \
	G_STMT_END

#define NVT_ERR(fmt, ...)                                                                          \
	G_STMT_START                                                                               \
	{                                                                                          \
		g_warning("[NVT_TS][Error] {%s} %s +%d : " fmt,                                    \
			  __func__,                                                                \
			  __FILE__,                                                                \
			  __LINE__,                                                                \
			  ##__VA_ARGS__);                                                          \
	}                                                                                          \
	G_STMT_END

#define NVT_SET_ERR(fwupd_err, fmt, ...)                                                           \
	G_STMT_START                                                                               \
	{                                                                                          \
		NVT_ERR(fmt, ##__VA_ARGS__);                                                       \
		g_set_error(error, FWUPD_ERROR, fwupd_err, fmt, ##__VA_ARGS__);                    \
	}                                                                                          \
	G_STMT_END

/* shared hex body */
#define NVT_HEX_IMPL(prefix, glib_fn, data, len)                                                   \
	G_STMT_START                                                                               \
	{                                                                                          \
		GString *_s = g_string_new(NULL);                                                  \
		for (gsize _i = 0; _i < (gsize)(len); _i++) {                                      \
			if ((_i % 16) == 0)                                                        \
				g_string_append(_s, prefix);                                       \
			g_string_append_printf(_s, "%02X", ((const guint8 *)(data))[_i]);          \
			if (((_i + 1) % 16) == 0)                                                  \
				g_string_append_c(_s, '\n');                                       \
			else                                                                       \
				g_string_append_c(_s, ' ');                                        \
		}                                                                                  \
		if ((len) % 16 != 0)                                                               \
			g_string_append_c(_s, '\n');                                               \
		glib_fn("%s", _s->str);                                                            \
		g_string_free(_s, TRUE);                                                           \
	}                                                                                          \
	G_STMT_END

#define NVT_DBG_HEX(data, len) NVT_HEX_IMPL("[NVT_TS][Debug] ", g_debug, data, len)

#define NVT_LOG_HEX(data, len) NVT_HEX_IMPL("[NVT_TS][Info ] ", g_info, data, len)

#define NVT_ERR_HEX(data, len) NVT_HEX_IMPL("[NVT_TS][Error] ", g_warning, data, len)

#ifndef msleep
#define msleep(ms) g_usleep((gulong)(ms)*1000)
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

typedef struct nvt_ts_reg {
	uint32_t addr;
	uint8_t mask;
} nvt_ts_reg_t;

struct nvt_ts_mem_map {
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
	nvt_ts_reg_t pp4io_en_reg;
	nvt_ts_reg_t bld_rd_addr_sel_reg;
	nvt_ts_reg_t bld_rd_io_sel_reg;
	uint32_t q_wr_cmd_addr;
};

struct nvt_ts_flash_map {
	uint32_t flash_normal_fw_start_addr;
	uint32_t flash_pid_addr;
	uint32_t flash_fw_size;
};

static const struct nvt_ts_mem_map nt36536_cascade_memory_map = {
    .read_flash_checksum_addr = 0x100000,
    .rw_flash_data_addr = 0x100002,
    .event_buf_cmd_addr = 0x130950,
    .event_buf_hs_sub_cmd_addr = 0x130951,
    .event_buf_reset_state_addr = 0x130960,
    .event_map_fwinfo_addr = 0x130978,
    .chip_ver_trim_addr = 0x1FB104,
    .enb_casc_addr = 0x1FB12C,
    .swrst_sif_addr = 0x1FB43E,
    .hid_i2c_eng_addr = 0x1FB468,
    .bld_spe_pups_addr = 0x1FB535,
    .gcm_code_addr = 0x1FB540,
    .flash_cmd_addr = 0x1FB543,
    .flash_cmd_issue_addr = 0x1FB54E,
    .flash_cksum_status_addr = 0x1FB54F,
    .gcm_flag_addr = 0x1FB553,
};

static const struct nvt_ts_flash_map nt36536_flash_map = {
    .flash_normal_fw_start_addr = 0x2000,
    .flash_pid_addr = 0x3F004,
    .flash_fw_size = 0x3C000,
};

typedef struct gcm_transfer {
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
} gcm_xfer_t;

enum { RESET_STATE_INIT = 0xA0,
       RESET_STATE_REK_BASELINE,
       RESET_STATE_REK_FINISH,
       RESET_STATE_NORMAL_RUN,
       RESET_STATE_MAX = 0xAF };

struct fw_bin {
	uint8_t *bin_data;
	uint32_t bin_size;
	uint32_t flash_start_addr;
};

/* flash manufacturer idenfication */
typedef enum {
	FLASH_MFR_UNKNOWN = 0x00,
	FLASH_MFR_ESMT = 0x1C,
	FLASH_MFR_PUYA = 0x85,	     /* puya */
	FLASH_MFR_FM = 0xA1,	     /* fm */
	FLASH_MFR_MACRONIX = 0xC2,   /* macronix */
	FLASH_MFR_GIGADEVICE = 0xC8, /* gigadevice */
	FLASH_MFR_WINBOND = 0xEF,    /* winbond */
	FLASH_MFR_MAX = 0xFF
} flash_mfr_t;

/* find "QE" or "status register" */
typedef enum {
	QEB_POS_UNKNOWN = 0,
	QEB_POS_SR_1B, /* qe bit in SR 1st byte */
	QEB_POS_OTHER, /* qe bit not in SR 1st byte */
	QEB_POS_MAX
} qeb_pos_t;

/* search "write status register" or "wrsr" */
typedef enum {
	FLASH_WRSR_METHOD_UNKNOWN = 0,
	WRSR_01H1BYTE, /* 01H (S7-S0) */
	WRSR_01H2BYTE, /* 01H (S7-S0) (S15-S8) */
	FLASH_WRSR_METHOD_MAX
} flash_wrsr_method_t;

typedef struct flash_qeb_info {
	qeb_pos_t qeb_pos; /* qe bit position type, ex. in SR 1st/2nd byte, etc */
	uint8_t qeb_order; /* in which bit of that byte, start from bit 0 */
} flash_qeb_info_t;

/* find "03h" or "read data bytes" */
typedef enum {
	FLASH_READ_METHOD_UNKNOWN = 0,
	SISO_0x03,
	SISO_0x0B,
	SIQO_0x6B,
	QIQO_0xEB,
	FLASH_READ_METHOD_MAX
} flash_read_method_t;

/* find "page program" */
typedef enum {
	FLASH_PROG_METHOD_UNKNOWN = 0,
	SPP_0x02, /* singalPageProgram_0x02 */
	QPP_0x32, /* quadPageProgram_0x32 */
	QPP_0x38, /* quadPageProgram_0x38 */
	FLASH_PROG_METHOD_MAX
} flash_prog_method_t;

typedef enum {
	FLASH_LOCK_METHOD_UNKNOWN,
	FLASH_LOCK_METHOD_SW_BP_ALL,
	FLASH_LOCK_METHOD_MAX,
} flash_lock_method_t;

typedef struct flash_info {
	flash_mfr_t mid; /* manufacturer identification */
	uint16_t did;	 /* 2 bytes device identification read by 0x9F RDID */
			 /* command manufacturer ID, memory type, memory density */
	flash_qeb_info_t qeb_info;
	flash_read_method_t rd_method;	 /* flash read method */
	flash_prog_method_t prog_method; /* flash program method */
	flash_wrsr_method_t wrsr_method; /* write status register method */
	/* find "rdsr" or "read status register" */
	uint8_t rdsr1_cmd;		 /* cmd for read status register-2 (S15-S8) */
	flash_lock_method_t lock_method; /* block protect position */
	uint8_t sr_bp_bits_all;		 /* bp all protect bits setting in SR for */
					 /* FLASH_LOCK_METHOD_SW_BP_ALL */
} flash_info_t;

static const flash_info_t flash_info_table[] = {
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

struct nvt_ts_data {
	const struct nvt_ts_mem_map *mmap;
	const struct nvt_ts_flash_map *fmap;
	uint8_t fw_ver;
	uint8_t flash_mid;
	uint16_t flash_did;
	uint16_t flash_pid;
	const flash_info_t *match_finfo;
	uint8_t flash_prog_data_cmd;
	uint8_t flash_read_data_cmd;
	uint8_t flash_read_pem_byte_len;
	uint8_t flash_read_dummy_byte_len;
};

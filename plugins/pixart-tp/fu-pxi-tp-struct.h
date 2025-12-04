// =============================================================
// file: fu-pxi-tp-struct.h  (pure C, no C++ guards, no packed structs)
// =============================================================
#pragma once

#include <glib.h>

/* ---- String constants --------------------------------------------------- */
extern const char PXI_TP_MAGIC[5]; /* "FWHD" + NUL */

/* ---- Fixed sizes & limits (compile-time constants, no macros) ----------- */
enum {
	PXI_TP_HEADER_V1_LEN = 0x0218, /* guint16 */
	PXI_TP_MAX_SECTIONS = 8,
	PXI_TP_SECTION_SIZE = 64
};

/* ---- Update types ------------------------------------------------------- */
typedef enum {
	PXI_TP_UPDATE_TYPE_GENERAL = 0u,    /* standard flash update process */
	PXI_TP_UPDATE_TYPE_FW_SECTION = 1u, /* firmware section update */
	PXI_TP_UPDATE_TYPE_BOOTLOADER = 2u, /* firmware bootloader section update */
	PXI_TP_UPDATE_TYPE_PARAM = 3u,	    /* parameter section update */
	PXI_TP_UPDATE_TYPE_TF_FORCE = 16u   /* TF Force (via DLL) */
} FuPxiTpUpdateType;

/* ---- Update Information bit definitions -------------------------------- */
enum {
	PXI_TP_UI_VALID = (1u << 0),   /* 1 = execute this section */
	PXI_TP_UI_EXTERNAL = (1u << 1) /* 1 = use external file */
};

/* ---- Header field offsets (all LE) -------------------------------------- */
enum {
	PXI_TP_O_MAGIC = 0x00,
	PXI_TP_O_HDRLEN = 0x04,	      /* uint16 */
	PXI_TP_O_HDRVER = 0x06,	      /* uint16 */
	PXI_TP_O_FILEVER = 0x08,      /* uint16 */
	PXI_TP_O_PARTID = 0x0A,	      /* uint16 */
	PXI_TP_O_SECTORS = 0x0C,      /* uint16 */
	PXI_TP_O_TOTALCRC = 0x0E,     /* uint32 over payload (fw.bin) */
	PXI_TP_O_NUMSECTIONS = 0x12,  /* uint16 (count of valid sections) */
	PXI_TP_O_SECTIONS_BASE = 0x14 /* first section (8 * 64 bytes) */
};
/* Replace macro PXI_TP_O_HDRCRC(hlen) with an inline function */
static inline guint32
pxi_tp_o_hdrcrc(guint16 hlen)
{
	/* header CRC32 is stored at (hlen - 4), e.g. 0x214 for v1.0 */
	return (guint32)(hlen - 4u);
}

/* ---- Section field offsets (v1.0, size = 64 bytes, LE) ------------------ */
enum {
	PXI_TP_S_O_TYPE = 0x00,	     /* uint8 */
	PXI_TP_S_O_INFO = 0x01,	     /* uint8 (bitfield) */
	PXI_TP_S_O_FLASHADDR = 0x02, /* uint32 (valid if type==GENERAL) */
	PXI_TP_S_O_INTSTART = 0x06,  /* uint32 (valid if !EXTERNAL) */
	PXI_TP_S_O_INTLEN = 0x0A,    /* uint32 (valid if !EXTERNAL) */
	PXI_TP_S_O_SECTCRC = 0x0E,   /* uint32 (CRC of section data) */
	PXI_TP_S_O_RESERVED = 0x12,
	PXI_TP_S_O_RESERVED_LEN = 0x0c,
	PXI_TP_S_O_EXTNAME = 0x1E, /* ASCII name (reduced) */
	PXI_TP_S_EXTNAME_LEN = 34  /* was 50 -> now 34 */
};

/* ---- Parsed section (convenience for callers) --------------------------- */
typedef struct {
	guint8 update_type;	  /* 0/1/2/3/16 (FuPxiTpUpdateType) */
	guint8 update_info;	  /* raw bitfield */
	gboolean is_valid_update; /* (update_info & PXI_TP_UI_VALID) != 0 */
	gboolean is_external;	  /* (update_info & PXI_TP_UI_EXTERNAL) != 0 */

	guint32 target_flash_start;  /* valid when update_type == GENERAL */
	guint32 internal_file_start; /* valid when !is_external; absolute offset in fw.bin */
	guint32 section_length;	     /* valid when !is_external */
	guint32 section_crc;	     /* CRC of section; 0 for external if unknown */

	guint8 reserved[PXI_TP_S_O_RESERVED_LEN];
	gchar external_file_name[PXI_TP_S_EXTNAME_LEN + 1]; /* NUL-terminated */
} FuPxiTpSection;

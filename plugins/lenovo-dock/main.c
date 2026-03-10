#include <fwupdplugin.h>

#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include "fu-lenovo-dock-struct.h"

#define CONST_UsageTableFlashID		      0xff
#define FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE 0x1000

guint8 CurrentFwVerForGUI[8][4];

static const gint FlashIdUsageLength = 32;
struct FlashIdUsageInformation {
	guint8 PhysicalAddress[4];
	guint8 MaxSize[4];
	guint8 CurrentFwVersion[4];
	guint8 TargetFwVersion[4];
	guint8 TargetFwFileSize[4];
	guint8 TargetFwFileCrc32[4];
	guint8 ComponentID;
	gboolean Flag;
};

static const guint16 DockVid = 0x17ef;
static const guint16 DockPid = 0x111e;

struct FlashIdAttribute {
	gint FLashId;
	FuLenovoDockExternalFlashIdPurpose Purpose;
	gint StorageSize;
	gint EraseSize;
	gint ProgramSize;
};

#define FU_LENOVO_DOCK_DEVICE_IFACE1_LEN 64
#define FU_LENOVO_DOCK_DEVICE_IFACE2_LEN 272

// FlashSize
#define CONST_UsageInfo			    4096
#define FU_LENOVO_DOCK_DEVICE_SIGNATURE_SIZE 256

// Flash Address
#define FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START 0xFFF000

libusb_context *ctx = NULL;
volatile gint device_connected = 1; // 1表示设备连接，0表示设备已断开
libusb_device_handle *devh = NULL;

/*error code*/
static const gint SUCCESS = 0;
static const gint REPORT_DATA_FAILED = 1027;
static const gint COMMAND_FALIURE = 1281;
static const gint COMMAND_TIMEOUT = 1282;
static const gint AP_TOOL_FAILED = 1;
static const gint COMMAND_OVER_RETRY_TIMES = 1283;
static const gint USAGE_INFORMATION_NOT_FOUND = 258;
static const gint USAGE_INFORMATION_FILE_ERROR = 259;
static const gint ARGUMENTS_SETTING_ERROR = 0x300;
static const gint USAGE_INFORMATION_CRC_FAILED = 266;
static const gint USAGE_INFORMATION_PAGE_FAILED = 1030;
static const gint XML_FILE_FORMAT_ERROR = 265;
static const gint UPDATE_USAGE_INFORMATION_PAGE_FAILED = 1030;
static const gint UPDATE_DOCK_FAILED = 0x400;

#define FU_LENOVO_DOCK_DEVICE_DELAY   25000 // ms
#define FU_LENOVO_DOCK_DEVICE_RETRIES 1600

/*Phase-1 Firmware Update Status RC*/
typedef enum {
	FWU_UPDATE_SUCCESS = 0,
	FWU_NO_UPDATE_NEEDED,
	FWU_FAILED_OR_DOCK_NOT_FOUND = -1,
	FWU_PHASE1_LOCKED = 2,
	FWU_OTA_DEPLOYEEING = 3
} FwUpdatgeResult;

struct UsageInformation {
	guint8 Totalnumber;
	guint8 MajorVersion;
	guint8 MinorVersion;
	guint8 Dsa;
	guint8 IoTUpdateFlag;
	guint8 CompositeFwVersion[4];
	guint8 DockPid[2]; // 0x08
	guint8 Crc32[4];
	struct FlashIdUsageInformation *FlashIdList;
};

// G_STATIC_ASSERT(sizeof(FuLenovoDockSignType) == 4);

static guint32
_fu_memread_uintn(guint8 *buf, gint length)
{
	gint size = 0;
	if (length == 2) {
		size = (buf[1] << 8) + buf[0];
	} else if (length == 4) {
		size = (buf[3] << 24) + (buf[2] << 16) + (buf[1] << 8) + buf[0];
	} else
		return AP_TOOL_FAILED;

	return size;
}

static guint8 *
_fu_memwrite_uint32(guint32 size)
{
	static guint8 res[4];
	res[3] = (guint8)((size >> 24) & 0xff);
	res[2] = (guint8)((size >> 16) & 0xff);
	res[1] = (guint8)((size >> 8) & 0xff);
	res[0] = (guint8)(size & 0xff);

	return res;
}

static gboolean
arraysEqual(guint8 *array1, guint8 *array2, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		if (array1[i] != array2[i])
			return FALSE;
	}
	return TRUE;
}

static guint8 *
GetBytes(struct UsageInformation targetUsageInformationTable)
{
	guint8 *table = (guint8 *)g_malloc0(FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE);
	// Header
	table[0] = targetUsageInformationTable.Totalnumber;
	table[1] = (targetUsageInformationTable.MajorVersion << 4) +
		   (targetUsageInformationTable.MinorVersion & 0x0f);
	table[2] = (guint8)targetUsageInformationTable.Dsa;
	table[3] = targetUsageInformationTable.IoTUpdateFlag;
	for (gint i = 0; i < 4; i++)
		table[4 + i] = targetUsageInformationTable.CompositeFwVersion[i];
	for (gint i = 0; i < 2; i++)
		table[8 + i] = targetUsageInformationTable.DockPid[i];
	// Body
	for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		gint flashId = i;
		guint8 flashIdData[FlashIdUsageLength];
		for (gint j = 0; j < FlashIdUsageLength; j++)
			flashIdData[j] = 0;
		for (gint j = 0; j < 4; j++) {
			flashIdData[0 + j] =
			    targetUsageInformationTable.FlashIdList[i].PhysicalAddress[j];
			flashIdData[4 + j] = targetUsageInformationTable.FlashIdList[i].MaxSize[j];
			flashIdData[8 + j] =
			    targetUsageInformationTable.FlashIdList[i].CurrentFwVersion[j];
			flashIdData[12 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwVersion[j];
			flashIdData[16 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwFileSize[j];
			flashIdData[20 + j] =
			    targetUsageInformationTable.FlashIdList[i].TargetFwFileCrc32[j];
			flashIdData[24] = targetUsageInformationTable.FlashIdList[i].ComponentID;
		}
		for (gint j = 0; j < FlashIdUsageLength; j++) {
			gint addr = (flashId * 32) + j;
			table[addr] = flashIdData[j];
			// g_print("%02X ",table[addr]);
		}
	}
	// Crc32
	guint32 tableCrc = fu_crc32(FU_CRC_KIND_B32_STANDARD, table, 4092);
	guint8 *computecrc = _fu_memwrite_uint32(tableCrc);
	for (gint i = 0; i < 4; i++) {
		table[4092 + i] = computecrc[i];
		// g_print("CRC: %02X\n",computecrc[i]);
	}

	return table;
}

static guint8 *
fu_lenovo_dock_device_get_composite_data(gint addr, gint size, char *buf)
{
	guint8 *temp = (guint8 *)g_malloc0(size);
	for (gint i = 0; i < size; i++) {
		temp[i] = (guint8)buf[addr + i];
	}
	return temp;
}

static gboolean
fu_lenovo_dock_device_verify(gint flashId,
			     struct UsageInformation targetUsageInformationTable,
			     char *compositeImageData)
{
	gint maxSize =
	    _fu_memread_uintn(targetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	gint targetFwSize =
	    _fu_memread_uintn(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize, 4);
	if (targetFwSize + FU_LENOVO_DOCK_DEVICE_SIGNATURE_SIZE > maxSize)
		return FALSE;

	gint startAddr =
	    _fu_memread_uintn(targetUsageInformationTable.FlashIdList[flashId].PhysicalAddress, 4) +
	    FU_LENOVO_DOCK_DEVICE_SIGNATURE_SIZE;
	guint8 *flashIdImageData =
	    fu_lenovo_dock_device_get_composite_data(startAddr, targetFwSize, compositeImageData);
	gint a =
	    _fu_memread_uintn(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileCrc32,
			      4);
	guint32 b = fu_crc32(FU_CRC_KIND_B32_STANDARD, flashIdImageData, targetFwSize);
	if ((guint32)a != b)
		return FALSE;

	return TRUE;
}

struct FlashIdAttribute
fu_lenovo_dock_device_get_flash_id_attr(guint8 *buf)
{
	struct FlashIdAttribute fa;
	fa.FLashId = buf[0];
	fa.Purpose = (FuLenovoDockExternalFlashIdPurpose)buf[1];

	gint count = 2;
	guint8 storageBuf[4];
	for (gint i = 0; i < 4; i++)
		storageBuf[i] = buf[count++];
	fa.StorageSize = _fu_memread_uintn(storageBuf, 4);

	guint8 eraseSizeBuf[2];
	for (gint i = 0; i < 2; i++)
		eraseSizeBuf[i] = buf[count++];
	fa.EraseSize = _fu_memread_uintn(eraseSizeBuf, 2);

	guint8 programSizeBuf[2];
	for (gint i = 0; i < 2; i++)
		programSizeBuf[i] = buf[count++];
	fa.ProgramSize = _fu_memread_uintn(programSizeBuf, 2);

	return fa;
}

static void
_fu_hidraw_device_set_feature(guint8 *cmd, gint interface)
{
	gint res;
	switch (interface) {
	case 1:
		res = libusb_control_transfer(devh,
					      0b00100001,
					      0x09,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      FU_LENOVO_DOCK_DEVICE_IFACE1_LEN,
					      0);
		// g_print("set: %d\n",res);
		break;
	case 2:
		res = libusb_control_transfer(devh,
					      0b00100001,
					      0x09,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      FU_LENOVO_DOCK_DEVICE_IFACE2_LEN,
					      0);
		// g_print("set: %d\n",res);
		break;
	default:
		break;
	}

	// if(res < 0)
	//     g_print("_fu_hidraw_device_set_feature failed : %d\n", res);
}

static void
_fu_hidraw_device_get_feature(guint8 *cmd, gint interface)
{
	gint res;
	switch (interface) {
	case 1:
		res = libusb_control_transfer(devh,
					      0b10100001,
					      0x01,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      FU_LENOVO_DOCK_DEVICE_IFACE1_LEN,
					      0);
		// g_print("get: %d\n",res);
		break;
	case 2:
		res = libusb_control_transfer(devh,
					      0b10100001,
					      0x01,
					      0x0300 + cmd[0],
					      interface,
					      cmd,
					      FU_LENOVO_DOCK_DEVICE_IFACE2_LEN,
					      0);
		// g_print("get: %d\n",res);
		break;
	default:
		break;
	}
}

static gint
fu_lenovo_dock_device_function1(FuLenovoDockClassId cmd_class,
				guint8 cmd_id,
				guint8 flash_id,
				guint8 *buf,
				gsize bufsz,
				guint8 *output)
{
	gint interface = 1;
	gint PacketSize = FU_LENOVO_DOCK_DEVICE_IFACE1_LEN;

	guint8 *cmd = (guint8 *)g_malloc0(PacketSize);
	guint8 *cmd2 = (guint8 *)g_malloc0(PacketSize);

	cmd2[0] = 0x00;

	// Header
	cmd[0] = FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_DEFAULT;
	cmd[1] = bufsz;
	cmd[2] = cmd_class;
	cmd[3] = cmd_id;
	cmd[4] = flash_id;
	cmd[5] = 0x00; // reserved

	// Body
	for (gint i = 0; i < bufsz; i++)
		cmd[6 + i] = buf[i];

	_fu_hidraw_device_set_feature(cmd, interface);
	for (gint i = 0; i < PacketSize; i++)
		g_print("%02X ", cmd[i]);
	g_print("\n");
	g_print("_fu_hidraw_device_set_feature for interface 1 PackagetSize = "
		"%d",
		PacketSize);
	g_print("\n");

	gint count = 0;

	do {
		_fu_hidraw_device_get_feature(cmd2, interface);
		for (gint i = 0; i < PacketSize; i++)
			g_print("%02X ", cmd2[i]);
		g_print("\n");

		g_print("_fu_hidraw_device_get_feature for interface 1 PackagetSize "
			"%d",
			PacketSize);
		g_print("\n");
		switch (cmd2[0]) {
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_DEFAULT:
			return REPORT_DATA_FAILED;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_BUSY:
			count++;
			g_usleep(FU_LENOVO_DOCK_DEVICE_DELAY);
			break;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_SUCCESS:
			if (cmd2[4] != flash_id) {
				// g_print("Function error: %d",REPORT_DATA_FAILED);
				return REPORT_DATA_FAILED;
			} else {
				for (gint i = 0; i < PacketSize; i++) {
					output[i] = cmd2[i];
				}
				return SUCCESS;
			}

		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_FALIURE:
			return COMMAND_FALIURE;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_TIMEOUT:
			return COMMAND_TIMEOUT;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_NOT_SUPPORT:
			return AP_TOOL_FAILED;
		}
	} while (count < FU_LENOVO_DOCK_DEVICE_RETRIES);

	return COMMAND_OVER_RETRY_TIMES;
}

static void
fu_lenovo_dock_device_trigger_phase2(gboolean noUnplug)
{
	guint8 DfuCtrl[2] = {0};
	guint8 output1[FU_LENOVO_DOCK_DEVICE_IFACE1_LEN] = {0};
	DfuCtrl[0] = FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_STATUS_LOCKED;
	if (noUnplug) {
		DfuCtrl[1] = FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_NON_UNPLUG;
	} else {
		DfuCtrl[1] = FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_UNPLUG;
	}
	gint r = fu_lenovo_dock_device_function1(
	    FU_LENOVO_DOCK_CLASS_ID_DOCK,
	    FU_LENOVO_DOCK_EXTERNAL_DOCK_CMD_SET_DOCK_FIRMWARE_UPGRADE_CTRL,
	    0,
	    DfuCtrl,
	    2,
	    output1);
}

static guint8 *
GetCommandBody1(guint8 *buf)
{
	gint size = buf[1];
	guint8 *res = (guint8 *)g_malloc0(size);
	for (gint i = 0; i < size; i++)
		res[i] = buf[6 + i];

	return res;
}

static guint8 *
GetCommandBody2(guint8 *buf)
{
	gint size = buf[2];
	guint8 *res = (guint8 *)g_malloc0(size);
	for (gint i = 0; i < size; i++)
		res[i] = buf[7 + i];

	return res;
}

static gint
fu_lenovo_dock_device_function2(guint8 cmd_class,
				guint8 cmd_id,
				guint8 flash_id,
				guint8 *buf,
				gint bufsz,
				guint8 *output)
{
	gint interface = 2;
	gint PacketSize = FU_LENOVO_DOCK_DEVICE_IFACE2_LEN;
	guint8 *cmd = (guint8 *)g_malloc0(PacketSize);
	guint8 *cmd2 = (guint8 *)g_malloc0(PacketSize);

	// Set Report ID
	cmd[0] = 0x10;
	cmd2[0] = 0x10;

	// Header
	cmd[1] = FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_DEFAULT;
	cmd[2] = bufsz;
	cmd[3] = cmd_class;
	cmd[4] = cmd_id;
	cmd[5] = flash_id;
	cmd[6] = 0x00; // reserved

	// Body
	for (gint i = 0; i < bufsz; i++)
		cmd[7 + i] = buf[i];

	_fu_hidraw_device_set_feature(cmd, interface);
	for (gint i = 0; i < PacketSize; i++)
		g_print("%02X ", cmd[i]);
	g_print("\n");
	g_print("_fu_hidraw_device_set_feature for interface 2 PackagetSize = "
		"%d",
		PacketSize);
	g_print("\n");

	gint count = 0;

	do {
		_fu_hidraw_device_get_feature(cmd2, interface);
		for (gint i = 0; i < PacketSize; i++)
			g_print("%02X ", cmd2[i]);
		g_print("\n");
		g_print("_fu_hidraw_device_get_feature for interface 2 "
			"PackagetSize = "
			"%d",
			PacketSize);
		g_print("\n");
		switch (cmd2[0 + 1]) { /* report_id is [0]*/
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_DEFAULT:
			return REPORT_DATA_FAILED;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_BUSY:
			count++;
			g_usleep(FU_LENOVO_DOCK_DEVICE_DELAY);
			break;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_SUCCESS:
			if (cmd2[4 + 1] != flash_id) { /* report_id is [0]*/
				return REPORT_DATA_FAILED;
			} else {
				for (gint i = 0; i < PacketSize; i++) {
					output[i] = cmd2[i];
				}
				return SUCCESS;
			}

		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_FALIURE:
			return COMMAND_FALIURE;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_TIMEOUT:
			return COMMAND_TIMEOUT;
		case FU_LENOVO_DOCK_TARGET_STATUS_COMMAND_NOT_SUPPORT:
			return AP_TOOL_FAILED;
		}
	} while (count < FU_LENOVO_DOCK_DEVICE_RETRIES);
	return COMMAND_OVER_RETRY_TIMES;
}

static gint
fu_lenovo_dock_device_write_usage_information_table(guint8 *UsageInformationData)
{
	gint errorHandle = 0;
	// Get Usage Information Attribute
	guint8 output1[FU_LENOVO_DOCK_DEVICE_IFACE1_LEN] = {0};
	guint8 output2[FU_LENOVO_DOCK_DEVICE_IFACE2_LEN] = {0};

	gint UsageInformationAttributeData =
	    fu_lenovo_dock_device_function1(FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
					    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_ATTRIBUTE,
					    CONST_UsageTableFlashID,
					    0,
					    0,
					    output1);
	if (UsageInformationAttributeData != 0)
		return UsageInformationAttributeData;
	guint8 *UsageInformationAttributeBody = GetCommandBody1(output1);
	struct FlashIdAttribute UsageInformationAttribute =
	    fu_lenovo_dock_device_get_flash_id_attr(UsageInformationAttributeBody);

	// Set Usage Information Memory Access (Erase)
	for (gint readBytes = 0; readBytes < FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE;
	     readBytes += UsageInformationAttribute.EraseSize) {
		gint address = FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START + readBytes;
		guint8 *eraseUsageInformationAddress = _fu_memwrite_uint32(address);
		guint8 setUsageInformationMemoryErase[2 + 4 + 2];
		for (gint i = 0; i < 2 + 4 + 2; i++)
			setUsageInformationMemoryErase[i] = 0;

		setUsageInformationMemoryErase[0] =
		    FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CMD_DOCK_ERASE_WITH_ADDRESS;
		setUsageInformationMemoryErase[1] = 0x00;
		for (gint i = 0; i < 4; i++)
			setUsageInformationMemoryErase[2 + 2 + i] = eraseUsageInformationAddress[i];
		for (gint i = 0; i < 2; i++)
			setUsageInformationMemoryErase[2 + i] =
			    _fu_memwrite_uint32(UsageInformationAttribute.EraseSize)[i];
		memset(output2, 0, FU_LENOVO_DOCK_DEVICE_IFACE2_LEN);
		errorHandle = fu_lenovo_dock_device_function2(
		    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_SET_FLASH_MEMORY_ACCESS,
		    CONST_UsageTableFlashID,
		    setUsageInformationMemoryErase,
		    2 + 4 + 2,
		    output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Usage Information Memory Access (Program)
	for (gint readBytes = 0; readBytes < FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE;
	     readBytes += UsageInformationAttribute.ProgramSize) {
		gint address = FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START + readBytes;
		guint8 *addrBytes = _fu_memwrite_uint32(address);

		gint payloadLength = 2 + 4 + 2;
		guint8 write[UsageInformationAttribute.ProgramSize + payloadLength];
		for (gint i = 0; i < UsageInformationAttribute.ProgramSize + payloadLength; i++)
			write[i] = 0;
		write[0] = FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CMD_DOCK_PROGRAM_WITH_ADDRESS;
		write[1] = 0x00;
		for (gint i = 0; i < 4; i++)
			write[2 + 2 + i] = addrBytes[i];
		for (gint i = 0; i < 2; i++)
			write[2 + i] =
			    _fu_memwrite_uint32(UsageInformationAttribute.ProgramSize)[i];
		for (gint i = 0; i < UsageInformationAttribute.ProgramSize; i++)
			write[payloadLength + i] = UsageInformationData[readBytes + i];

		for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE2_LEN; i++)
			output2[i] = 0;
		errorHandle = fu_lenovo_dock_device_function2(
		    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_SET_FLASH_MEMORY_ACCESS,
		    CONST_UsageTableFlashID,
		    write,
		    (UsageInformationAttribute.ProgramSize + payloadLength),
		    output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Get Usage Information Page Self-Verify
	guint8 writeSelfVerify[1];
	writeSelfVerify[0] = FU_LENOVO_DOCK_FLASH_MEMORY_SELF_VERIFY_TYPE_CRC;
	memset(output1, 0, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN);
	gint pageSelfVerifyData = fu_lenovo_dock_device_function1(
	    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
	    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_MEMORY_SELF_VERIFY,
	    CONST_UsageTableFlashID,
	    writeSelfVerify,
	    1,
	    output1);
	if (pageSelfVerifyData != 0)
		return pageSelfVerifyData;
	guint8 *pageSelfVerifyBody = GetCommandBody1(output1);
	guint8 pageSelfVerifyCrc[4];
	for (gint i = 0; i < 4; i++) {
		pageSelfVerifyCrc[i] = pageSelfVerifyBody[1 + i];
		// g_print("%02X\n",pageSelfVerifyCrc[i]);
	}

	// Usge Information CRC
	guint8 uiCrc[4];
	for (gint i = 0; i < 4; i++) {
		uiCrc[i] = UsageInformationData[4092 + i];
	}
	if (!arraysEqual(uiCrc, pageSelfVerifyCrc, 4))
		return USAGE_INFORMATION_PAGE_FAILED;

	return 0;
}

static gint
fu_lenovo_dock_device_write_flash_id_data(gint flashId,
					  struct FlashIdAttribute flashIdAttribute,
					  struct UsageInformation changeTagetUsageInformationTable,
					  char *compositeImageData)
{
	gint errorHandle = 0;
	guint8 output1[FU_LENOVO_DOCK_DEVICE_IFACE1_LEN];
	guint8 output2[FU_LENOVO_DOCK_DEVICE_IFACE2_LEN];
	// Check FW Data In Host Self-Verify
	if (!fu_lenovo_dock_device_verify(flashId,
					  changeTagetUsageInformationTable,
					  compositeImageData))
		return UPDATE_USAGE_INFORMATION_PAGE_FAILED;

	// Set Flash Memory Access (Erase)
	guint8 *eraseStartAddrBytes =
	    changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress;
	gint targetMaxSize =
	    _fu_memread_uintn(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	gint targetFwSize = _fu_memread_uintn(
	    changeTagetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize,
	    4);
	for (gint eraseBytes = 0; eraseBytes < targetMaxSize;
	     eraseBytes += flashIdAttribute.EraseSize) {
		guint8 setFlashMemoryErase[2 + 2 + 4];
		for (gint i = 0; i < 2 + 2 + 4; i++)
			setFlashMemoryErase[i] = 0;
		setFlashMemoryErase[0] =
		    FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CMD_DOCK_ERASE_WITH_ADDRESS;
		setFlashMemoryErase[1] = 0x00;
		gint eraseAddr = _fu_memread_uintn(eraseStartAddrBytes, 4) + eraseBytes;
		for (gint i = 0; i < 2; i++)
			setFlashMemoryErase[2 + i] =
			    _fu_memwrite_uint32(flashIdAttribute.EraseSize)[i];
		for (gint i = 0; i < 4; i++)
			setFlashMemoryErase[2 + 2 + i] = _fu_memwrite_uint32(eraseAddr)[i];

		for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE2_LEN; i++)
			output2[i] = 0;
		errorHandle = fu_lenovo_dock_device_function2(
		    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_SET_FLASH_MEMORY_ACCESS,
		    flashId,
		    setFlashMemoryErase,
		    2 + 2 + 4,
		    output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Flash Memory Access (program)
	gint flashIdStartAddr =
	    _fu_memread_uintn(changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress,
			      4);
	gint flashIdbufsz =
	    _fu_memread_uintn(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	guint8 *flashIdCompositeData = fu_lenovo_dock_device_get_composite_data(flashIdStartAddr,
										flashIdbufsz,
										compositeImageData);
	for (gint readBytes = 0; readBytes < targetFwSize + FU_LENOVO_DOCK_DEVICE_SIGNATURE_SIZE;
	     readBytes += flashIdAttribute.ProgramSize) {
		gint address = flashIdStartAddr + readBytes;
		guint8 *addrBytes = _fu_memwrite_uint32(address);

		gint payloadLength = 2 + 4 + 2;
		guint8 write[flashIdAttribute.ProgramSize + payloadLength];
		write[0] = FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CMD_DOCK_PROGRAM_WITH_ADDRESS;
		write[1] = 0x00;
		for (gint i = 0; i < 4; i++)
			write[2 + 2 + i] = addrBytes[i];
		for (gint i = 0; i < 2; i++)
			write[2 + i] = _fu_memwrite_uint32(flashIdAttribute.ProgramSize)[i];
		for (gint i = 0; i < flashIdAttribute.ProgramSize; i++)
			write[payloadLength + i] = flashIdCompositeData[readBytes + i];

		for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE2_LEN; i++)
			output2[i] = 0;
		errorHandle = fu_lenovo_dock_device_function2(
		    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_SET_FLASH_MEMORY_ACCESS,
		    flashId,
		    write,
		    flashIdAttribute.ProgramSize + payloadLength,
		    output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Get Flash Usage Information Memory Self-Verify
	guint8 writeFlashUpdateCheckSignature[1];
	writeFlashUpdateCheckSignature[0] = FU_LENOVO_DOCK_FLASH_MEMORY_SELF_VERIFY_TYPE_SIGNATURE;
	for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE1_LEN; i++)
		output1[i] = 0;
	gint flashUpdateCheckSignatureData = fu_lenovo_dock_device_function1(
	    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
	    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_MEMORY_SELF_VERIFY,
	    flashId,
	    writeFlashUpdateCheckSignature,
	    1,
	    output1);
	if (flashUpdateCheckSignatureData != 0)
		return flashUpdateCheckSignatureData;
	guint8 *flashUpdateCheckSignatureBody = GetCommandBody1(output1);
	if (flashUpdateCheckSignatureBody[1] !=
	    (guint8)FU_LENOVO_DOCK_FLASH_MEMORY_SELF_VERIFY_RESULT_PASS)
		return UPDATE_DOCK_FAILED;

	guint8 writeFlashUpdateCheckVerify[1];
	writeFlashUpdateCheckVerify[0] = FU_LENOVO_DOCK_FLASH_MEMORY_SELF_VERIFY_TYPE_CRC;
	for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE1_LEN; i++)
		output1[i] = 0;
	gint flashUpdateCheckVerifyData = fu_lenovo_dock_device_function1(
	    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
	    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_MEMORY_SELF_VERIFY,
	    flashId,
	    writeFlashUpdateCheckVerify,
	    1,
	    output1);
	if (flashUpdateCheckVerifyData != 0)
		return flashUpdateCheckVerifyData;
	guint8 *flashUpdateCheckVerifyBody = GetCommandBody1(output1);
	guint8 flashUpdateCheckVerifyCrc[4];
	for (gint i = 0; i < 4; i++) {
		flashUpdateCheckVerifyCrc[i] = flashUpdateCheckVerifyBody[1 + i];
		// g_print("%02X\n",flashUpdateCheckVerifyCrc[i]);
	}
	if (!arraysEqual(changeTagetUsageInformationTable.FlashIdList[flashId].TargetFwFileCrc32,
			 flashUpdateCheckVerifyCrc,
			 4))
		return UPDATE_DOCK_FAILED;

	return 0;
}

/**
 * @brief Execute firmware update procedure for the dock device.
 *
 * This function performs the full firmware update flow including:
 * - Load composite firmware image and Usage Information Table from file
 * - Request flash memory access from the device
 * - Parse and verify Usage Information Table
 * - Retrieve Flash ID list from the device
 * - Perform firmware programming for each component
 *
 * @param forceUpdate
 *        If TRUE, firmware will be updated even if the current firmware version
 * is => the target version.
 *
 * @param noUnplug
 *        If TRUE, skip unplug detection during the firmware update process.
 *
 * @return SUCCESS on success, otherwise returns an error code defined in the
 * firmware update error definitions.(gint)
 */

static gint
fu_lenovo_dock_device_fw_update(gboolean forceUpdate, gboolean noUnplug)
{
	// gboolean forceUpdate = FALSE;
	// gboolean noUnplug = FALSE;
	char *compositeImageData = NULL;
	gboolean r;

	if (!g_file_get_contents("FW/ldc_u4_composite_image.bin",
				 &compositeImageData,
				 NULL,
				 NULL)) {
		// g_print("Read composite image error: %d\n",r);
		return r;
	}
	// Set Flash Memory Access (Request)
	guint8 setFlashMemoryRequest[2];
	guint8 output1[FU_LENOVO_DOCK_DEVICE_IFACE1_LEN] = {0};
	guint8 output2[FU_LENOVO_DOCK_DEVICE_IFACE2_LEN] = {0};
	setFlashMemoryRequest[0] = FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CMD_ACCCESS_CTRL;
	setFlashMemoryRequest[1] = FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CTRL_REQUEST;
	r = fu_lenovo_dock_device_function2(
	    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
	    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_SET_FLASH_MEMORY_ACCESS,
	    0,
	    setFlashMemoryRequest,
	    2,
	    output2);
	if (r != 0) {
		g_print("Set Flash Memory Access (Request) error: %d\n", r);
		return FWU_OTA_DEPLOYEEING;
	}
	// Read Target Usage Information Table
	struct stat fileInfo;
	struct UsageInformation targetUsageInformationTable;
	guint8 ds[FlashIdUsageLength];
	FILE *fp;
	if (stat("FW/ldc_u4_usage_information_table.bin", &fileInfo) == 0) {
		if (fileInfo.st_size != FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE) {
			return USAGE_INFORMATION_FILE_ERROR;
		} else {
			/*read the file actions*/
			fp = fopen("FW/ldc_u4_usage_information_table.bin", "rb");
			char buffer[FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE];
			size_t bytesRead =
			    fread(&buffer, sizeof(char), FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE, fp);
			targetUsageInformationTable.Totalnumber = (guint8)buffer[0];
			targetUsageInformationTable.MajorVersion =
			    (((guint8)buffer[1] >> 4) & 0x0f);
			targetUsageInformationTable.MinorVersion = ((guint8)buffer[1] & 0x0f);
			targetUsageInformationTable.Dsa = (FuLenovoDockSignType)buffer[2];
			targetUsageInformationTable.IoTUpdateFlag = (guint8)buffer[3];
			for (gint i = 0; i < 4; i++) {
				targetUsageInformationTable.CompositeFwVersion[i] =
				    (guint8)buffer[4 + i];
				targetUsageInformationTable.Crc32[i] =
				    (guint8)buffer[FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE - 4 + i];
			}
			for (gint i = 0; i < 2; i++) {
				targetUsageInformationTable.DockPid[i] = (guint8)buffer[8 + i];
			}
			targetUsageInformationTable.FlashIdList =
			    g_new0(struct FlashIdUsageInformation,
				   targetUsageInformationTable.Totalnumber + 1);
			for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
				for (gint j = 0; j < FlashIdUsageLength; j++) {
					ds[j] = (guint8)buffer[i * 32 + j];
				}
				for (gint j = 0; j < 4; j++) {
					targetUsageInformationTable.FlashIdList[i]
					    .PhysicalAddress[j] = ds[j];
					targetUsageInformationTable.FlashIdList[i].MaxSize[j] =
					    ds[4 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .CurrentFwVersion[j] = ds[8 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwVersion[j] = ds[12 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwFileSize[j] = ds[16 + j];
					targetUsageInformationTable.FlashIdList[i]
					    .TargetFwFileCrc32[j] = ds[20 + j];
					targetUsageInformationTable.FlashIdList[i].ComponentID =
					    ds[24];

					//
					targetUsageInformationTable.FlashIdList[i].Flag = FALSE;
				}
			}
			if (_fu_memread_uintn(targetUsageInformationTable.Crc32, 4) !=
			    fu_crc32(FU_CRC_KIND_B32_STANDARD,
				     buffer,
				     FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE - 4))
				return USAGE_INFORMATION_CRC_FAILED;

			guint16 targetPid =
			    (guint16)((targetUsageInformationTable.DockPid[0] << 8) +
				      targetUsageInformationTable.DockPid[1]);
			if (DockPid != targetPid)
				return XML_FILE_FORMAT_ERROR;
		}
	}
	// Check Flash Id List Count
	memset(output1, 0, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN);
	gint getFlashIdListData =
	    fu_lenovo_dock_device_function1(FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
					    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_ID_LIST,
					    0,
					    0,
					    0,
					    output1);
	guint8 flashIdListTotal = output1[6];
	guint8 flashIdList[flashIdListTotal - 1];
	for (gint i = 0; i < flashIdListTotal - 1; i++) {
		flashIdList[i] = output1[7 + i];
	}
	if ((targetUsageInformationTable.Totalnumber + 1) != flashIdListTotal)
		return USAGE_INFORMATION_CRC_FAILED;

	// Read MCU Usage Information Memory Access (Read)
	const gint readCountByCycle = 256;
	gint mcuUsageInformationCount = 0;
	guint8 mcuUsageInformationData[CONST_UsageInfo];
	do {
		gint payload = 8;
		guint8 setmcuUsageInformationTable[payload];
		setmcuUsageInformationTable[0] =
		    FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CMD_DOCK_READ_WITH_ADDRESS;
		setmcuUsageInformationTable[1] = 0x00;

		guint8 *size = _fu_memwrite_uint32(readCountByCycle);
		setmcuUsageInformationTable[2] = size[0];
		setmcuUsageInformationTable[3] = size[1];

		guint8 *addr = _fu_memwrite_uint32(FU_LENOVO_DOCK_DEVICE_USAGE_INFO_START +
						   mcuUsageInformationCount);
		setmcuUsageInformationTable[4] = addr[0];
		setmcuUsageInformationTable[5] = addr[1];
		setmcuUsageInformationTable[6] = addr[2];
		setmcuUsageInformationTable[7] = addr[3];

		for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE2_LEN; i++)
			output2[i] = 0;
		gint tempBytes = fu_lenovo_dock_device_function2(
		    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_MEMORY_ACCESS,
		    CONST_UsageTableFlashID,
		    setmcuUsageInformationTable,
		    payload,
		    output2);
		for (gint i = 0; i < readCountByCycle; i++) {
			mcuUsageInformationData[mcuUsageInformationCount + i] =
			    output2[7 + payload + i];
		}
		mcuUsageInformationCount += readCountByCycle;

	} while (mcuUsageInformationCount < CONST_UsageInfo);

	// Check & Transfer MCU Usage Information Table
	guint8 mcuUIcrc[4];
	for (gint i = 0; i < 4; i++) {
		mcuUIcrc[i] =
		    mcuUsageInformationData[FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE - 4 + i];
		// g_print("%02X\n",mcuUIcrc[i]);
	}
	guint32 t = fu_crc32(FU_CRC_KIND_B32_STANDARD, mcuUsageInformationData, 4092);
	guint8 *computecrc = _fu_memwrite_uint32(t);
	if (!arraysEqual(mcuUIcrc, computecrc, 4)) {
		forceUpdate = TRUE;
	}

	// Check Package Version to Clean Update List
	gboolean bcdVerUpdateReq = FALSE;
	if (forceUpdate)
		bcdVerUpdateReq = TRUE;

	if (!forceUpdate) {
		memset(output1, 0, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN);
		gint GetBcdData = fu_lenovo_dock_device_function1(
		    FU_LENOVO_DOCK_CLASS_ID_DEVICE_INFORMATION,
		    FU_LENOVO_DOCK_DEVICE_INFORMATION_CMD_GET_FIRMWARE_VERSION,
		    0,
		    0,
		    0,
		    output1);
		guint8 *dockFWPackageVer = GetCommandBody1(output1);
		guint8 *targetFWPackageVer = targetUsageInformationTable.CompositeFwVersion;
		gint dockFWPackageVerInt =
		    (dockFWPackageVer[0] << 16) + (dockFWPackageVer[1] << 8) + dockFWPackageVer[2];
		gint targetFWPackageVerInt = (targetFWPackageVer[1] << 16) +
					     (targetFWPackageVer[2] << 8) + targetFWPackageVer[3];
		if (targetFWPackageVerInt > dockFWPackageVerInt)
			bcdVerUpdateReq = TRUE;
	}
	// Check Dock Component Need to Update
	struct FlashIdUsageInformation *FlashIdUpdateList;
	struct UsageInformation changeTagetUsageInformationTable = targetUsageInformationTable;

	// if (bcdVerUpdateReq) {
	if (forceUpdate) {
		for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			changeTagetUsageInformationTable.FlashIdList[i] =
			    targetUsageInformationTable.FlashIdList[i];
			changeTagetUsageInformationTable.FlashIdList[i].Flag = TRUE;
		}
	}

	// Compare The Component FW Version
	else {
		if (bcdVerUpdateReq) {
			struct UsageInformation mcuUsageInformationTable;
			struct FlashIdUsageInformation *mcuFlashIdList;
			mcuUsageInformationTable.Totalnumber = mcuUsageInformationData[0];
			mcuUsageInformationTable.MajorVersion =
			    ((mcuUsageInformationData[1] >> 4) & 0x0f);
			mcuUsageInformationTable.MinorVersion = (mcuUsageInformationData[1] & 0x0f);
			mcuUsageInformationTable.Dsa =
			    (FuLenovoDockSignType)mcuUsageInformationData[2];
			mcuUsageInformationTable.IoTUpdateFlag = mcuUsageInformationData[3];
			for (gint i = 0; i < 4; i++) {
				mcuUsageInformationTable.CompositeFwVersion[i] =
				    mcuUsageInformationData[4 + i];
				mcuUsageInformationTable.Crc32[i] =
				    mcuUsageInformationData[FU_LENOVO_DOCK_DEVICE_USAGE_INFO_SIZE -
							    4 + i];
			}
			for (gint i = 0; i < 2; i++) {
				mcuUsageInformationTable.DockPid[i] =
				    mcuUsageInformationData[8 + i];
			}
			mcuFlashIdList = g_new0(struct FlashIdUsageInformation,
						mcuUsageInformationTable.Totalnumber);
			for (gint i = 1; i <= mcuUsageInformationTable.Totalnumber; i++) {
				for (gint j = 0; j < FlashIdUsageLength; j++) {
					ds[j] = mcuUsageInformationData[i * 32 + j];
				}
				for (gint j = 0; j < 4; j++) {
					mcuFlashIdList[i].PhysicalAddress[j] = ds[j];
					mcuFlashIdList[i].MaxSize[j] = ds[4 + j];
					mcuFlashIdList[i].CurrentFwVersion[j] = ds[8 + j];
					mcuFlashIdList[i].TargetFwVersion[j] = ds[12 + j];
					mcuFlashIdList[i].TargetFwFileSize[j] = ds[16 + j];
					mcuFlashIdList[i].TargetFwFileCrc32[j] = ds[20 + j];
					mcuFlashIdList[i].ComponentID = ds[24];
				}
			}

			for (gint i = 1; i <= mcuUsageInformationTable.Totalnumber; i++) {
				if (!arraysEqual(
					mcuFlashIdList[i].TargetFwVersion,
					targetUsageInformationTable.FlashIdList[i].TargetFwVersion,
					4)) {
					changeTagetUsageInformationTable.FlashIdList[i] =
					    targetUsageInformationTable.FlashIdList[i];
					changeTagetUsageInformationTable.FlashIdList[i].Flag = TRUE;
				} else {
					changeTagetUsageInformationTable.FlashIdList[i] =
					    mcuFlashIdList[i];
					changeTagetUsageInformationTable.FlashIdList[i].Flag =
					    FALSE;
				}
			}
		}
	}

	// Check MCU in to phase-1 process
	gboolean NeedWriteTable = FALSE;
	for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		if (changeTagetUsageInformationTable.FlashIdList[i].Flag) {
			NeedWriteTable = TRUE;
			break;
		}
	}
	if (NeedWriteTable) {
		// Set Dock FW Update Ctrl
		guint8 mcuUpdateCtrl[2];
		mcuUpdateCtrl[0] = FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_STATUS_NON_LOCK;
		mcuUpdateCtrl[1] = FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_PHASE_CTRL_IN_PHASE1;
		memset(output1, 0, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN);
		r = fu_lenovo_dock_device_function1(
		    FU_LENOVO_DOCK_CLASS_ID_DOCK,
		    FU_LENOVO_DOCK_EXTERNAL_DOCK_CMD_SET_DOCK_FIRMWARE_UPGRADE_CTRL,
		    0,
		    mcuUpdateCtrl,
		    2,
		    output1);

		// Clean Target Fw Version
		for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			for (gint j = 0; j < 4; j++)
				changeTagetUsageInformationTable.FlashIdList[i].TargetFwVersion[j] =
				    0x00;
		}

		// Write Usage Information Table
		guint8 *changeTagetUsageInformationTableBytes =
		    GetBytes(changeTagetUsageInformationTable);
		r = fu_lenovo_dock_device_write_usage_information_table(
		    changeTagetUsageInformationTableBytes);
		if (r != 0)
			return r;
	}

	// Phase-1 Start
	for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		// Flash ID update start
		gint flashId = i;

		// Get Flash ID Attribute
		memset(output1, 0, FU_LENOVO_DOCK_DEVICE_IFACE1_LEN);
		gint flashIdAttributeData = fu_lenovo_dock_device_function1(
		    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_ATTRIBUTE,
		    flashId,
		    0,
		    0,
		    output1);
		if (flashIdAttributeData != 0)
			return flashIdAttributeData;
		guint8 *flashIdAttributeBody = GetCommandBody1(output1);
		struct FlashIdAttribute flashIdAttribute =
		    fu_lenovo_dock_device_get_flash_id_attr(flashIdAttributeBody);
		// Check Component FW File
		if (flashIdAttribute.Purpose !=
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_ID_PURPOSE_FIRMWARE_FILE)
			continue;

		// Check Update Necessary
		if (!changeTagetUsageInformationTable.FlashIdList[flashId]
			 .Flag) // no need to update
			continue;

		// Write Flash ID Data
		r = fu_lenovo_dock_device_write_flash_id_data(flashId,
							      flashIdAttribute,
							      changeTagetUsageInformationTable,
							      compositeImageData);
		if (r != 0)
			return r;

		// Write Target FW Version
		changeTagetUsageInformationTable.FlashIdList[flashId] =
		    targetUsageInformationTable.FlashIdList[flashId];

		// Write Usage Information Table
		r = fu_lenovo_dock_device_write_usage_information_table(
		    GetBytes(changeTagetUsageInformationTable));
		if (r != 0)
			return r;
	}

	// Set Flash Memory Access (Release)
	guint8 SetFlashMemoryAccessRelease[2];
	SetFlashMemoryAccessRelease[0] = FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CMD_ACCCESS_CTRL;
	SetFlashMemoryAccessRelease[1] = FU_LENOVO_DOCK_FLASH_MEMORY_ACCESS_CTRL_RELEASE;
	for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE2_LEN; i++)
		output2[i] = 0;
	r = fu_lenovo_dock_device_function2(
	    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
	    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_SET_FLASH_MEMORY_ACCESS,
	    0,
	    SetFlashMemoryAccessRelease,
	    2,
	    output2);

	// Set Dock FW Update Ctrl
	if (bcdVerUpdateReq) {
		fu_lenovo_dock_device_trigger_phase2(noUnplug);
	}

	return 0;
}

static gboolean
fu_lenovo_dock_device_check_ready_for_phase2()
{
	gboolean rs = FALSE;

	guint8 buffer[65];
	rs = fu_lenovo_dock_device_function1(
	    FU_LENOVO_DOCK_CLASS_ID_DOCK,
	    FU_LENOVO_DOCK_EXTERNAL_DOCK_CMD_GET_DOCK_FIRMWARE_UPGRADE_CTRL,
	    0,
	    0,
	    0,
	    buffer);
	guint8 *DockFirmwareCtrlBody = GetCommandBody1(buffer);
	if (DockFirmwareCtrlBody[0] == FU_LENOVO_DOCK_DOCK_FW_CTRL_UPGRADE_STATUS_LOCKED &&
	    DockFirmwareCtrlBody[1] == 2)
		rs = TRUE;

	return rs;
}

static gint
myclaim(gint interface)
{
	// gint release_interface = libusb_release_interface(devh,interface);
	// g_print("release_interface%d :
	// %s\n",interface,libusb_strerror(release_interface));
	gint r = libusb_claim_interface(devh, interface);
	if (r != 0)
		g_print("interface%d error: %s\n", interface, libusb_strerror(r));

	return r;
}

static gint
init()
{
	libusb_close(devh);
	libusb_exit(ctx);
	// gint r = libusb_init_context(/*ctx=*/NULL, /*options=*/NULL,
	// /*num_options=*/0);
	gint r = libusb_init(&ctx);
	if (r < 0) {
		// fprintf(stderr, "failed to initialize libusb %d - %s\n", r,
		// libusb_strerror(r));
		exit(1);
	}
	devh = libusb_open_device_with_vid_pid(NULL, 0x17ef, 0x111e);
	if (!devh) {
		errno = ENODEV;
		// g_print("open device failed\n");
		return errno;
	}
	// 如果內核驅動佔用，先 detach
	if (libusb_kernel_driver_active(devh, 1) == 1) {
		r = libusb_detach_kernel_driver(devh, 1);
		if (r < 0) {
			// fprintf(stderr, "Detach kernel driver failed on iface %d:
			// %s\n",1,libusb_error_name(r));
			return r;
		}
	}
	if (libusb_kernel_driver_active(devh, 2) == 1) {
		r = libusb_detach_kernel_driver(devh, 2);
		if (r < 0) {
			// fprintf(stderr, "Detach kernel driver failed on iface %d:
			// %s\n",2,libusb_error_name(r));
			return r;
		}
	}
	gint interface1 = myclaim(1);
	gint interface2 = myclaim(2);

	return 0;
}

static gint LIBUSB_CALL
hotplug_callback(struct libusb_context *ctx,
		 struct libusb_device *dev,
		 libusb_hotplug_event event,
		 void *user_data)
{
	struct libusb_device_descriptor desc;
	libusb_get_device_descriptor(dev, &desc);

	if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
		g_print("Device disconnected: VID=0x%04x, PID=0x%04x\n",
			desc.idVendor,
			desc.idProduct);

		// 检查是否是目标设备
		if (desc.idVendor == DockVid && desc.idProduct == DockPid) {
			g_print("Target device disconnected! Terminating program...\n");
			exit(-1);
		}
	}

	return 0; // 返回 0 表示回调未被注销
}

static void *
usb_event_thread(void *arg)
{
	while (device_connected) {
		libusb_handle_events_completed(ctx, NULL); // 检测并触发 USB 热插拔回调事件
	}
	return NULL;
}

struct FlashIdUsageInformation *
fu_lenovo_dock_device_setup()
{
	guint8 output[FU_LENOVO_DOCK_DEVICE_IFACE1_LEN] = {0};
	gint getFlashIdList =
	    fu_lenovo_dock_device_function1(FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
					    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_ID_LIST,
					    0,
					    0,
					    0,
					    output);
	// g_print("getFlashIdList return %d\n",getFlashIdList);
	guint8 totalFlashId = output[6];
	struct FlashIdUsageInformation *Info = g_new0(struct FlashIdUsageInformation, totalFlashId);

	for (gint flashId = 1; flashId < totalFlashId; flashId++) {
		gint getFlashIdAttribute = fu_lenovo_dock_device_function1(
		    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
		    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_ATTRIBUTE,
		    flashId,
		    0,
		    0,
		    output);
		guint8 getPurpose = output[7];
		if (getPurpose == FU_LENOVO_DOCK_EXTERNAL_FLASH_ID_PURPOSE_FIRMWARE_FILE) {
			for (gint i = 0; i < FU_LENOVO_DOCK_DEVICE_IFACE1_LEN; i++)
				output[i] = 0;
			guint8 getFlashIdUsageInformation = fu_lenovo_dock_device_function1(
			    FU_LENOVO_DOCK_CLASS_ID_EXTERNAL_FLASH,
			    FU_LENOVO_DOCK_EXTERNAL_FLASH_CMD_GET_FLASH_ID_USAGE_INFORMATION,
			    flashId,
			    0,
			    0,
			    output);
			for (gint i = 0; i < 4; i++) {
				Info[flashId].PhysicalAddress[i] = output[6 + i];
				Info[flashId].MaxSize[i] = output[10 + i];
				Info[flashId].CurrentFwVersion[i] = output[14 + i];
				Info[flashId].TargetFwVersion[i] = output[18 + i];
				Info[flashId].TargetFwFileSize[i] = output[22 + i];
				Info[flashId].TargetFwFileCrc32[i] = output[26 + i];
				Info[flashId].ComponentID = output[30];
				CurrentFwVerForGUI[flashId][i] = Info[flashId].CurrentFwVersion[i];
			}
		}
	}
	return Info;
}

static gint
GetCompositeVersion(char *out, size_t outSize)
{
	if (!out || outSize == 0)
		return -1;

	guint8 buffer[65] = {0};
	gint rc = fu_lenovo_dock_device_function1(
	    FU_LENOVO_DOCK_CLASS_ID_DEVICE_INFORMATION,
	    FU_LENOVO_DOCK_DEVICE_INFORMATION_CMD_GET_FIRMWARE_VERSION,
	    0,
	    0,
	    0,
	    buffer);
	if (rc != 0) {
		snprintf(out, outSize, "0.0.0.0");
		return -1;
	}

	guint8 *body = GetCommandBody1(buffer);
	if (!body) {
		snprintf(out, outSize, "0.0.0.0");
		return -1;
	}

	snprintf(out, outSize, "%X.%X.%02X", body[0], body[1], body[2]);

	return 0;
}

int
main(int argc, char *argv[])
{
	libusb_context *ctx = NULL;
	volatile gint device_connected = 1; // 1表示设备连接，0表示设备已断开

	gint r = init();
	// 注册热插拔回调
	libusb_hotplug_callback_handle callback_handle;
	gint rc = libusb_hotplug_register_callback(ctx,
						   LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
						       LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
						   0,
						   DockVid,
						   DockPid,
						   LIBUSB_HOTPLUG_MATCH_ANY,
						   hotplug_callback,
						   NULL, // 用户数据传递
						   &callback_handle);
	if (rc != LIBUSB_SUCCESS) {
		fprintf(stderr, "Error registering hotplug callback: %s\n", libusb_strerror(rc));
		libusb_exit(ctx);
		return EXIT_FAILURE;
	}

	// 创建线程处理 libusb 的事件轮询
	pthread_t usb_thread;
	if (pthread_create(&usb_thread, NULL, usb_event_thread, NULL) != 0) {
		fprintf(stderr, "Failed to create USB event thread\n");
		libusb_hotplug_deregister_callback(ctx, callback_handle);
		libusb_exit(ctx);
		return EXIT_FAILURE;
	}

	for (gint i = 1; i < argc; i++) {
		if (strcmp(argv[i], "/c") == 0) {
			g_print("Checking current FW version\n");
			struct FlashIdUsageInformation *Info = fu_lenovo_dock_device_setup();
			char version[64]; // 準備一個足夠大的 buffer

			gint ret = GetCompositeVersion(version, sizeof(version));

			if (ret == 0) {
				g_print("Composite Version = %s\n", version);
			}

			g_print("DMC: ");
			g_print("%d.%d.%02d\n",
				Info[1].CurrentFwVersion[1],
				Info[1].CurrentFwVersion[2],
				Info[1].CurrentFwVersion[3]);
			g_print("DP: ");
			g_print("%d.%02d.%03d\n",
				Info[2].CurrentFwVersion[1],
				Info[2].CurrentFwVersion[2],
				Info[2].CurrentFwVersion[3]);
			g_print("PD: ");
			for (gint i = 0; i < 3; i++)
				g_print("%02X.", Info[3].CurrentFwVersion[i]);
			g_print("%02X\n", Info[3].CurrentFwVersion[3]);
			g_print("USB3: ");
			g_print("%02X%02X\n",
				Info[4].CurrentFwVersion[2],
				Info[4].CurrentFwVersion[3]);
			g_print("USB4: ");
			g_print("%02X%02X\n",
				Info[5].CurrentFwVersion[2],
				Info[5].CurrentFwVersion[3]);

			return 0;
		} else if (strcmp(argv[i], "/u") == 0) {
			gboolean rs = fu_lenovo_dock_device_check_ready_for_phase2();
			// g_print("%s\n", rs ? "TRUE" : "FALSE");
			if (rs == TRUE) {
				g_print("Phase-1 update is already done, by rc %d",
					FWU_PHASE1_LOCKED);
				return FWU_PHASE1_LOCKED;
			}

			g_print("Please DO NOT remove the dock and wait for a few minutes until "
				"the white light stops blinking.\n");
			g_print("Start updating........\n");
			gint r = fu_lenovo_dock_device_fw_update(TRUE, TRUE);
			if (r == 0) {
				/*Notice : rc == 0 includes 2 condition of FW update result as below
				   .
					   1. FW update process is actually be executed because
				   Dock's current composite is < target composite version.
					   2. FW update process doesn't which actually erase & flash
				   FW image since the Dock's current composite version is >= target
				   composite version. *Assume fwupd plugin will cover for the
				   condition with message output*/

				g_print(
				    "Phase-1 finished. Please unplug the cable and wait for 30 "
				    "seconds.\nPhase-2 will start automatically with the orange "
				    "light starts blinking.\n");
				return 0;
			} else {
				g_print("Phase-1 update failed, by error %d", r);
				return r;
			}
		}

		else {
			g_print("Wrong command. Please use /c or /u.\n");
			return 0;
		}
	}

	pthread_join(usb_thread, NULL);
	// 注销回调并释放资源
	libusb_hotplug_deregister_callback(ctx, callback_handle);
	libusb_exit(ctx);

	return 0;
}

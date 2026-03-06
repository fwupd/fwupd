#include <errno.h>
#include <glib.h>
#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

static const gchar *Path_Of_Usage_Information_Table = "FW/ldc_u4_usage_information_table.bin";

static const guint8 UsageTableFlashID = 0xff;
static const gint UsageTableSize = 4096;

typedef enum { Unsigned, RSA2048, RSA3072, ECC256, ECC384 } DSAType;

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

enum Target_Status {
	CommandDefault = 0x00,
	CommandBusy = 0x01,
	CommandSuccess = 0x02,
	CommandFaliure = 0x03,
	CommandTimeout = 0x04,
	CommandNotSupport = 0x05
};

enum ClassID {
	Unnecessary = 0x00,
	Device_Information = 0x00,
	DFU = 0x09,
	Test = 0x0A,
	External_Flash = 0x0C,
	Dock = 0x0E
};

/*External Flash ID*/
static const guint8 Set_Flash_ID_Usage_Information = 0x03;
static const guint8 Set_Flash_Memory_Access = 0x04;
static const guint8 Get_Support_List = 0x80;
static const guint8 Get_Flash_ID_List = 0x81;
static const guint8 Get_Flash_Attribute = 0x82;
static const guint8 Get_Flash_ID_Usage_Information = 0x83;
static const guint8 Get_Flash_Memory_Access = 0x84;
static const guint8 Get_Flash_Memory_Self_Verify = 0x85;
typedef enum { Common, ApplicationData, ImageData, FirmwareFile } ExternalFlashIdPurpose;

struct FlashIdAttribute {
	gint FLashId;
	ExternalFlashIdPurpose Purpose;
	gint StorageSize;
	gint EraseSize;
	gint ProgramSize;
};

struct FlashMemoryAccessCMD {
	enum OPCode {
		AcccessCtrl,
		Erase,
		Program,
		Read,
		Dock_Erase,
		Dock_Program,
		Dock_Read,
		Dock_Erase_With_Address,
		Dock_Program_With_Address,
		Dock_Read_With_Address

	};
	enum AccessCtrl { Release, Request };
};
struct FlashMemorySelfVerify {
	enum Type { Signature, CRC };

	enum VerifyPayload { Fail, Pass };
};

/*Dock Command ID*/
static const guint8 Set_Dock_Port_Ctrl = 0x03;
static const guint8 Set_Dock_Fan_Ctrl = 0x05;
static const guint8 Set_Dock_IoT_Configure = 0x06;
static const guint8 Set_Dock_USB_Container_ID = 0x07;
static const guint8 Set_Dock_Lan_Mac_Address = 0x08;
static const guint8 Set_Dock_Aux_Log = 0x09;
static const guint8 Set_Dock_Firmware_Upgrade_Ctrl = 0x0A;
static const guint8 Get_CMD_Support_List = 0x80;
static const guint8 Get_Dock_Attribute = 0x81;
static const guint8 Get_Dock_Port_Status = 0x82;
static const guint8 Get_Dock_Port_Ctrl = 0x83;
static const guint8 Get_Dock_Port_Connected_Device_Information = 0x84;
static const guint8 Get_Dock_Dock_Fan_Ctrl = 0x85;
static const guint8 Get_Dock_IoT_Configure = 0x86;
static const guint8 Get_Dock_USB_Container_ID = 0x87;
static const guint8 Get_Dock_Lan_Mac_Address = 0x88;
static const guint8 Get_Dock_Aux_Log = 0x89;
static const guint8 Get_Dock_Firmware_Upgrade_Ctrl = 0x8A;

static const gint ReportIdOffset = 1;
static const gint Target_Status_Defult = 0;
static const gint Interface1Length = 64;
static const gint Interface2Length = 272;

// FlashSize
static const gint UsageInfo = 4096;
static const gint SignatureSize = 256;
static const gint EraseSizeByteLength = 2;
static const gint ProgramSizeByteLength = 2;
static const gint AddressByteLength = 4;
static const gint StorageSizeByteLength = 4;
static const gint DfuFilePayLoadLength = 5;

// Flash Address
static const gint ImageStart = 0;
static const gint UsageInfoStart = 16773120;

// Device Information ID
static const guint8 Set_Hardware_Version = 0x02;
static const guint8 Set_Serial_Number = 0x03;
static const guint8 Set_Device_Mode = 0x04;
static const guint8 Set_Device_Edition = 0x06;
static const guint8 Set_Device_Name = 0x08;
static const guint8 Set_Device_Reset = 0x09;
static const guint8 Set_Device_UUID = 0x14;
static const guint8 Get_Command_Support_List = 0x80;
static const guint8 Get_Firmware_Version = 0x81;
static const guint8 Get_Hardware_Version = 0x82;
static const guint8 Get_Serial_Number = 0x83;
static const guint8 Get_Device_Mode = 0x84;
static const guint8 Get_Device_Edition = 0x86;
static const guint8 Get_Device_Name = 0x88;
static const guint8 Get_Device_UUID = 0x9;

libusb_context *ctx = NULL;
volatile gint device_connected = 1; // 1表示设备连接，0表示设备已断开
libusb_device_handle *devh = NULL;

struct DockFwCtrl {
	enum FwUpgradeLocked { nonLock, Locked };
	enum FwUpgradePhaseCtrl {
		NA,
		InPhase1,
		unplug,
		nonUnplug,
		waitForTimer,
	};
};

/*error code*/
static const gint SUCCESS = 0;
static const gint PARAM_ERROR = 1300;
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
static const gint COMPOSITE_IMAGE_FILE_NOT_FOUND = 260;
static const gint COMPOSITE_IMAGE_FILE_ERROR = 261;
static const gint UPDATE_USAGE_INFORMATION_PAGE_FAILED = 1030;
static const gint UPDATE_DOCK_FAILED = 0x400;

static const gint waitMcuBusyTime = 25000000; // 0.025sec
static const gint maxRetry = 1600;

struct timespec req = {0, waitMcuBusyTime};

/*Phase-1 Firmware Update Status RC*/
typedef enum {
	FWU_UPDATE_SUCCESS = 0,
	FWU_NO_UPDATE_NEEDED,
	FWU_FAILED_OR_DOCK_NOT_FOUND = -1,
	FWU_PHASE1_LOCKED = 2,
	FWU_OTA_DEPLOYEEING = 3
} FwUpdatgeResult;

static const guint32 crcTable[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu, 0xe963a535u,
    0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u, 0x09b64c2bu, 0x7eb17cbdu,
    0xe7b82d07u, 0x90bf1d91u, 0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu, 0x1adad47du,
    0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u, 0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu,
    0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u,
    0xa2677172u, 0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu, 0x35b5a8fau, 0x42b2986cu,
    0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u, 0x26d930acu,
    0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
    0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u, 0x2f6f7c87u, 0x58684c11u, 0xc1611dabu,
    0xb6662d3du, 0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu,
    0x9fbfe4a5u, 0xe8b8d433u, 0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu,
    0x086d3d2du, 0x91646c97u, 0xe6635c01u, 0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
    0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau,
    0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u, 0x4db26158u, 0x3ab551ceu,
    0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au,
    0x346ed9fcu, 0xad678846u, 0xda60b8d0u, 0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
    0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u,
    0xce61e49fu, 0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u,
    0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au, 0xead54739u,
    0x9dd277afu, 0x04db2615u, 0x73dc1683u, 0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u,
    0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u, 0xf00f9344u, 0x8708a3d2u, 0x1e01f268u,
    0x6906c2feu, 0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u,
    0x10da7a5au, 0x67dd4accu, 0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u, 0xd6d6a3e8u,
    0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u, 0x316e8eefu,
    0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u, 0xcc0c7795u, 0xbb0b4703u,
    0x220216b9u, 0x5505262fu, 0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u,
    0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du, 0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au,
    0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u, 0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu,
    0x0cb61b38u, 0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
    0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u, 0x88085ae6u,
    0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
    0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u, 0xa7672661u, 0xd06016f7u, 0x4969474du,
    0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u,
    0x47b2cf7fu, 0x30b5ffe9u, 0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u,
    0xcdd70693u, 0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
    0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du};

struct UsageInformation {
	guint8 Totalnumber;
	guint8 MajorVersion;
	guint8 MinorVersion;
	DSAType Dsa;
	guint8 IoTUpdateFlag;
	guint8 CompositeFwVersion[4];
	guint8 DockPid[2];
	guint8 Crc32[4];
	struct FlashIdUsageInformation *FlashIdList;
};

gint
BytesToInt(guint8 *data, gint length)
{
	gint size = 0;
	if (length == 2) {
		size = (data[1] << 8) + data[0];
	} else if (length == 4) {
		size = (data[3] << 24) + (data[2] << 16) + (data[1] << 8) + data[0];
	} else
		return AP_TOOL_FAILED;

	return size;
}
guint8 *
IntToBytes(gint size)
{
	static guint8 res[4];
	res[3] = (guint8)((size >> 24) & 0xff);
	res[2] = (guint8)((size >> 16) & 0xff);
	res[1] = (guint8)((size >> 8) & 0xff);
	res[0] = (guint8)(size & 0xff);

	return res;
}
guint8 *
ToBytes(guint32 size)
{
	static guint8 res[4];
	res[3] = (guint8)((size >> 24) & 0xff);
	res[2] = (guint8)((size >> 16) & 0xff);
	res[1] = (guint8)((size >> 8) & 0xff);
	res[0] = (guint8)(size & 0xff);

	return res;
}

guint32
Compute(guint8 *buffer, size_t bufferlength, gint offset, gint length)
{
	if (!((buffer != NULL) && (offset >= 0) && (length >= 0) &&
	      (offset <= bufferlength - length))) {
		return ARGUMENTS_SETTING_ERROR;
	}

	guint32 crc32 = 0xffffffffU;

	while (--length >= 0) {
		crc32 = crcTable[(crc32 ^ buffer[offset++]) & 0xFF] ^ (crc32 >> 8);
	}
	crc32 ^= 0xffffffffU;
	return crc32;
}

gboolean
arraysEqual(guint8 *array1, guint8 *array2, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		if (array1[i] != array2[i])
			return false;
	}
	return true;
}

static const gchar *Path_Of_Composite_Image = "FW/ldc_u4_composite_image.bin";

gint
ReadCompositeImageFile(char **data)
{
	struct stat fileInfo;
	FILE *fp;
	if (stat(Path_Of_Composite_Image, &fileInfo) == 0) {
		if (fileInfo.st_size > 16773120) {
			return COMPOSITE_IMAGE_FILE_ERROR;
		} else {
			/*read the file actions*/
			fp = fopen(Path_Of_Composite_Image, "rb");
			char *buffer = (char *)calloc(16773120, sizeof(char));
			size_t bytesRead = fread(buffer, sizeof(char), 16773120, fp);
			*data = buffer;
			fclose(fp);
			return SUCCESS;
		}
	}

	else
		return COMPOSITE_IMAGE_FILE_NOT_FOUND;
}

guint8 *
GetBytes(struct UsageInformation targetUsageInformationTable)
{
	guint8 *table = (guint8 *)calloc(UsageTableSize, sizeof(guint8));
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
	guint32 tableCrc = Compute(table, UsageTableSize, 0, 4092);
	guint8 *computecrc = ToBytes(tableCrc);
	for (gint i = 0; i < 4; i++) {
		table[4092 + i] = computecrc[i];
		// g_print("CRC: %02X\n",computecrc[i]);
	}

	return table;
}

#if 0
gint
init();
gint LIBUSB_CALL
hotplug_callback(struct libusb_context *ctx,
		 struct libusb_device *dev,
		 libusb_hotplug_event event,
		 void *user_data);
void *
usb_event_thread(void *arg);
struct FlashIdUsageInformation *
check();
gint
FWUpdate(gboolean forceUpdate, gboolean noUnplug);
gboolean
CheckDockReadyForEnterPhase2Update();
gint
GetCompositeVersion(char *out, size_t outSize);
#endif

guint8 *
GetCompositeData(gint addr, gint size, char *data)
{
	guint8 *temp = (guint8 *)calloc(size, sizeof(guint8));
	for (gint i = 0; i < size; i++) {
		temp[i] = (guint8)data[addr + i];
	}
	return temp;
}

gboolean
CheckFwVerify(gint flashId,
	      struct UsageInformation targetUsageInformationTable,
	      char *compositeImageData)
{
	gint maxSize = BytesToInt(targetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	gint targetFwSize =
	    BytesToInt(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize, 4);
	if (targetFwSize + SignatureSize > maxSize)
		return false;

	gint startAddr =
	    BytesToInt(targetUsageInformationTable.FlashIdList[flashId].PhysicalAddress, 4) +
	    SignatureSize;
	guint8 *flashIdImageData = GetCompositeData(startAddr, targetFwSize, compositeImageData);
	gint a = BytesToInt(targetUsageInformationTable.FlashIdList[flashId].TargetFwFileCrc32, 4);
	guint32 b = Compute(flashIdImageData, targetFwSize, 0, targetFwSize);
	free(flashIdImageData);
	if ((guint32)a != b)
		return false;

	return true;
}

struct FlashIdAttribute
GetFlashIdAttribute(guint8 *data)
{
	struct FlashIdAttribute fa;
	fa.FLashId = data[0];
	fa.Purpose = (ExternalFlashIdPurpose)data[1];

	gint count = 2;
	guint8 storageBuf[StorageSizeByteLength];
	for (gint i = 0; i < StorageSizeByteLength; i++)
		storageBuf[i] = data[count++];
	fa.StorageSize = BytesToInt(storageBuf, StorageSizeByteLength);

	guint8 eraseSizeBuf[EraseSizeByteLength];
	for (gint i = 0; i < EraseSizeByteLength; i++)
		eraseSizeBuf[i] = data[count++];
	fa.EraseSize = BytesToInt(eraseSizeBuf, EraseSizeByteLength);

	guint8 programSizeBuf[ProgramSizeByteLength];
	for (gint i = 0; i < ProgramSizeByteLength; i++)
		programSizeBuf[i] = data[count++];
	fa.ProgramSize = BytesToInt(programSizeBuf, ProgramSizeByteLength);

	return fa;
}

void
SetFeature(guint8 *cmd, gint interface)
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
					      Interface1Length,
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
					      Interface2Length,
					      0);
		// g_print("set: %d\n",res);
		break;
	default:
		break;
	}

	// if(res < 0)
	//     g_print("SetFeature failed : %d\n", res);
}

void
GetFeature(guint8 *cmd, gint interface)
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
					      Interface1Length,
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
					      Interface2Length,
					      0);
		// g_print("get: %d\n",res);
		break;
	default:
		break;
	}

	// if(res < 0)
	//     g_print("GetFeature failed : %d\n", res);
}

gint
Function1(guint8 CmdClass,
	  guint8 CmdId,
	  guint8 FlashId,
	  guint8 *data,
	  gint dataSize,
	  guint8 *output)
{
	gint interface = 1;
	gint PacketSize = Interface1Length;

	guint8 *cmd = (guint8 *)calloc(PacketSize, sizeof(guint8));
	guint8 *cmd2 = (guint8 *)calloc(PacketSize, sizeof(guint8));

	cmd2[0] = 0x00;

	// Header
	cmd[0] = Target_Status_Defult;
	cmd[1] = dataSize;
	cmd[2] = CmdClass;
	cmd[3] = CmdId;
	cmd[4] = FlashId;
	cmd[5] = 0x00; // reserved

	// Body

	if (dataSize < 0 || dataSize > PacketSize - 6) {
		free(cmd);
		free(cmd2);
		return PARAM_ERROR;
	}

	if (dataSize > 0 && data == NULL) {
		free(cmd);
		free(cmd2);
		return PARAM_ERROR;
	}

	for (gint i = 0; i < dataSize; i++)
		cmd[6 + i] = data[i];

	SetFeature(cmd, interface);
	for (gint i = 0; i < PacketSize; i++)
		g_print("%02X ", cmd[i]);
	g_print("\n");
	g_print("******************SetFeature for interface 1 PackagetSize = "
		"%d******************",
		PacketSize);
	g_print("\n");
	free(cmd);

	gint count = 0;

	do {
		GetFeature(cmd2, interface);
		for (gint i = 0; i < PacketSize; i++)
			g_print("%02X ", cmd2[i]);
		g_print("\n");

		g_print("******************GetFeature for interface 1 PackagetSize "
			"%d******************",
			PacketSize);
		g_print("\n");
		switch (cmd2[0]) {
		case CommandDefault:
			// g_print("Function error: %d",REPORT_DATA_FAILED);
			free(cmd2);
			return REPORT_DATA_FAILED;
		case CommandBusy:
			count++;
			nanosleep(&req, NULL);
			break;
		case CommandSuccess:
			if (cmd2[4] != FlashId) {
				free(cmd2);
				// g_print("Function error: %d",REPORT_DATA_FAILED);
				return REPORT_DATA_FAILED;
			} else {
				for (gint i = 0; i < PacketSize; i++) {
					output[i] = cmd2[i];
				}
				free(cmd2);
				return SUCCESS;
			}

		case CommandFaliure:
			// g_print("Function error: %d",COMMAND_FALIURE);
			free(cmd2);
			return COMMAND_FALIURE;
		case CommandTimeout:
			// g_print("Function error: %d",COMMAND_TIMEOUT);
			free(cmd2);
			return COMMAND_TIMEOUT;
		case CommandNotSupport:
			// g_print("Function error: %d",AP_TOOL_FAILED);
			free(cmd2);
			return AP_TOOL_FAILED;
		}
	} while (count < maxRetry);

	// g_print("Function error: %d",COMMAND_OVER_RETRY_TIMES);
	free(cmd2);
	return COMMAND_OVER_RETRY_TIMES;
}

void
TriggerPhase2(gboolean noUnplug)
{
	guint8 DfuCtrl[2] = {0};
	guint8 output1[Interface1Length];
	for (gint i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	if (noUnplug) {
		DfuCtrl[0] = Locked;
		DfuCtrl[1] = nonUnplug;
		gint r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, DfuCtrl, 2, output1);
	} else {
		DfuCtrl[0] = Locked;
		DfuCtrl[1] = unplug;
		gint r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, DfuCtrl, 2, output1);
	}
}

guint8 *
GetCommandBody1(guint8 *data)
{
	gint size = data[1];
	guint8 *res = (guint8 *)calloc(size, sizeof(guint8));
	for (gint i = 0; i < size; i++)
		res[i] = data[6 + i];

	return res;
}
guint8 *
GetCommandBody2(guint8 *data)
{
	gint size = data[2];
	guint8 *res = (guint8 *)calloc(size, sizeof(guint8));
	for (gint i = 0; i < size; i++)
		res[i] = data[7 + i];

	return res;
}

gint
Function2(guint8 CmdClass,
	  guint8 CmdId,
	  guint8 FlashId,
	  guint8 *data,
	  gint dataSize,
	  guint8 *output)
{
	gint interface = 2;
	gint PacketSize = Interface2Length;
	guint8 *cmd = (guint8 *)calloc(PacketSize, sizeof(guint8));
	guint8 *cmd2 = (guint8 *)calloc(PacketSize, sizeof(guint8));

	if (!cmd || !cmd2) {
		if (cmd)
			free(cmd);
		if (cmd2)
			free(cmd2);
		return PARAM_ERROR; // 你可換成你的 error code
	}

	if (dataSize < 0 || dataSize > (PacketSize - 7)) {
		free(cmd);
		free(cmd2);
		return PARAM_ERROR;
	}
	if (dataSize > 0 && data == NULL) {
		free(cmd);
		free(cmd2);
		return PARAM_ERROR;
	}
	if (output == NULL) {
		free(cmd);
		free(cmd2);
		return PARAM_ERROR;
	}

	// Set Report ID
	cmd[0] = 0x10;
	cmd2[0] = 0x10;

	// Header
	cmd[1] = Target_Status_Defult;
	cmd[2] = dataSize;
	cmd[3] = CmdClass;
	cmd[4] = CmdId;
	cmd[5] = FlashId;
	cmd[6] = 0x00; // reserved

	// Body
	for (gint i = 0; i < dataSize; i++)
		cmd[7 + i] = data[i];

	SetFeature(cmd, interface);
	for (gint i = 0; i < PacketSize; i++)
		g_print("%02X ", cmd[i]);
	g_print("\n");
	g_print("******************SetFeature for interface 2 PackagetSize = "
		"%d******************",
		PacketSize);
	g_print("\n");
	free(cmd);

	gint count = 0;

	do {
		GetFeature(cmd2, interface);
		for (gint i = 0; i < PacketSize; i++)
			g_print("%02X ", cmd2[i]);
		g_print("\n");
		g_print("******************GetFeature for interface 2 PackagetSize = "
			"%d******************",
			PacketSize);
		g_print("\n");
		switch (cmd2[0 + ReportIdOffset]) {
		case CommandDefault:
			// g_print("Function error: %d",REPORT_DATA_FAILED);
			free(cmd2);
			return REPORT_DATA_FAILED;
		case CommandBusy:
			count++;
			nanosleep(&req, NULL);
			break;
		case CommandSuccess:
			if (cmd2[4 + ReportIdOffset] != FlashId) {
				// g_print("Function error: %d",REPORT_DATA_FAILED);
				return REPORT_DATA_FAILED;
			} else {
				for (gint i = 0; i < PacketSize; i++) {
					output[i] = cmd2[i];
				}
				free(cmd2);
				return SUCCESS;
			}

		case CommandFaliure:
			// g_print("Function error: %d",COMMAND_FALIURE);
			free(cmd2);
			return COMMAND_FALIURE;
		case CommandTimeout:
			// g_print("Function error: %d",COMMAND_TIMEOUT);
			free(cmd2);
			return COMMAND_TIMEOUT;
		case CommandNotSupport:
			// g_print("Function error: %d",AP_TOOL_FAILED);
			free(cmd2);
			return AP_TOOL_FAILED;
		}
	} while (count < maxRetry);

	free(cmd2);
	// g_print("Function error: %d",COMMAND_OVER_RETRY_TIMES);
	return COMMAND_OVER_RETRY_TIMES;
}

gint
WriteUsageInformationTable(guint8 *usageInformationData)
{
	gint errorHandle = 0;
	// Get Usage Information Attribute
	guint8 output1[Interface1Length];
	guint8 output2[Interface2Length];
	for (gint i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	gint usageInformationAttributeData =
	    Function1(External_Flash, Get_Flash_Attribute, UsageTableFlashID, 0, 0, output1);
	if (usageInformationAttributeData != 0)
		return usageInformationAttributeData;
	guint8 *usageInformationAttributeBody = GetCommandBody1(output1);
	struct FlashIdAttribute usageInformationAttribute =
	    GetFlashIdAttribute(usageInformationAttributeBody);
	free(usageInformationAttributeBody);

	// Set Usage Information Memory Access (Erase)
	for (gint readBytes = 0; readBytes < UsageTableSize;
	     readBytes += usageInformationAttribute.EraseSize) {
		gint address = UsageInfoStart + readBytes;
		guint8 *eraseUsageInformationAddress = IntToBytes(address);
		guint8 setUsageInformationMemoryErase[2 + AddressByteLength + EraseSizeByteLength];
		for (gint i = 0; i < 2 + AddressByteLength + EraseSizeByteLength; i++)
			setUsageInformationMemoryErase[i] = 0;

		setUsageInformationMemoryErase[0] = Dock_Erase_With_Address;
		setUsageInformationMemoryErase[1] = 0x00;
		for (gint i = 0; i < AddressByteLength; i++)
			setUsageInformationMemoryErase[2 + EraseSizeByteLength + i] =
			    eraseUsageInformationAddress[i];
		for (gint i = 0; i < EraseSizeByteLength; i++)
			setUsageInformationMemoryErase[2 + i] =
			    IntToBytes(usageInformationAttribute.EraseSize)[i];
		for (gint i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					UsageTableFlashID,
					setUsageInformationMemoryErase,
					2 + AddressByteLength + EraseSizeByteLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Usage Information Memory Access (Program)
	for (gint readBytes = 0; readBytes < UsageTableSize;
	     readBytes += usageInformationAttribute.ProgramSize) {
		gint address = UsageInfoStart + readBytes;
		guint8 *addrBytes = IntToBytes(address);

		gint payloadLength = 2 + AddressByteLength + ProgramSizeByteLength;
		guint8 write[usageInformationAttribute.ProgramSize + payloadLength];
		for (gint i = 0; i < usageInformationAttribute.ProgramSize + payloadLength; i++)
			write[i] = 0;
		write[0] = Dock_Program_With_Address;
		write[1] = 0x00;
		for (gint i = 0; i < AddressByteLength; i++)
			write[2 + ProgramSizeByteLength + i] = addrBytes[i];
		for (gint i = 0; i < ProgramSizeByteLength; i++)
			write[2 + i] = IntToBytes(usageInformationAttribute.ProgramSize)[i];
		for (gint i = 0; i < usageInformationAttribute.ProgramSize; i++)
			write[payloadLength + i] = usageInformationData[readBytes + i];

		for (gint i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					UsageTableFlashID,
					write,
					(usageInformationAttribute.ProgramSize + payloadLength),
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Get Usage Information Page Self-Verify
	guint8 writeSelfVerify[1];
	writeSelfVerify[0] = CRC;
	for (gint i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	gint pageSelfVerifyData = Function1(External_Flash,
					    Get_Flash_Memory_Self_Verify,
					    UsageTableFlashID,
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
		uiCrc[i] = usageInformationData[4092 + i];
		// g_print("%02X\n",uiCrc[i]);
	}
	if (!arraysEqual(uiCrc, pageSelfVerifyCrc, 4))
		return USAGE_INFORMATION_PAGE_FAILED;

	return 0;
}

gint
WriteFlashIdData(gint flashId,
		 struct FlashIdAttribute flashIdAttribute,
		 struct UsageInformation changeTagetUsageInformationTable,
		 char *compositeImageData)
{
	gint errorHandle = 0;
	guint8 output1[Interface1Length];
	guint8 output2[Interface2Length];
	// Check FW Data In Host Self-Verify
	if (!CheckFwVerify(flashId, changeTagetUsageInformationTable, compositeImageData))
		return UPDATE_USAGE_INFORMATION_PAGE_FAILED;

	// Set Flash Memory Access (Erase)
	guint8 *eraseStartAddrBytes =
	    changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress;
	gint targetMaxSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	gint targetFwSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].TargetFwFileSize, 4);
	for (gint eraseBytes = 0; eraseBytes < targetMaxSize;
	     eraseBytes += flashIdAttribute.EraseSize) {
		guint8 setFlashMemoryErase[2 + EraseSizeByteLength + AddressByteLength];
		for (gint i = 0; i < 2 + EraseSizeByteLength + AddressByteLength; i++)
			setFlashMemoryErase[i] = 0;
		setFlashMemoryErase[0] = Dock_Erase_With_Address;
		setFlashMemoryErase[1] = 0x00;
		gint eraseAddr = BytesToInt(eraseStartAddrBytes, 4) + eraseBytes;
		for (gint i = 0; i < EraseSizeByteLength; i++)
			setFlashMemoryErase[2 + i] = IntToBytes(flashIdAttribute.EraseSize)[i];
		for (gint i = 0; i < AddressByteLength; i++)
			setFlashMemoryErase[2 + EraseSizeByteLength + i] = IntToBytes(eraseAddr)[i];

		for (gint i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					flashId,
					setFlashMemoryErase,
					2 + EraseSizeByteLength + AddressByteLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	// Set Flash Memory Access (program)
	gint flashIdStartAddr =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].PhysicalAddress, 4);
	gint flashIdDataSize =
	    BytesToInt(changeTagetUsageInformationTable.FlashIdList[flashId].MaxSize, 4);
	guint8 *flashIdCompositeData =
	    GetCompositeData(flashIdStartAddr, flashIdDataSize, compositeImageData);
	for (gint readBytes = 0; readBytes < targetFwSize + SignatureSize;
	     readBytes += flashIdAttribute.ProgramSize) {
		gint address = flashIdStartAddr + readBytes;
		guint8 *addrBytes = IntToBytes(address);

		gint payloadLength = 2 + AddressByteLength + ProgramSizeByteLength;
		guint8 write[flashIdAttribute.ProgramSize + payloadLength];
		write[0] = Dock_Program_With_Address;
		write[1] = 0x00;
		for (gint i = 0; i < AddressByteLength; i++)
			write[2 + ProgramSizeByteLength + i] = addrBytes[i];
		for (gint i = 0; i < ProgramSizeByteLength; i++)
			write[2 + i] = IntToBytes(flashIdAttribute.ProgramSize)[i];
		for (gint i = 0; i < flashIdAttribute.ProgramSize; i++)
			write[payloadLength + i] = flashIdCompositeData[readBytes + i];

		for (gint i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		errorHandle = Function2(External_Flash,
					Set_Flash_Memory_Access,
					flashId,
					write,
					flashIdAttribute.ProgramSize + payloadLength,
					output2);
		if (errorHandle != 0)
			return errorHandle;
	}

	free(flashIdCompositeData);

	// Get Flash Usage Information Memory Self-Verify
	guint8 writeFlashUpdateCheckSignature[1];
	writeFlashUpdateCheckSignature[0] = Signature;
	for (gint i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	gint flashUpdateCheckSignatureData = Function1(External_Flash,
						       Get_Flash_Memory_Self_Verify,
						       flashId,
						       writeFlashUpdateCheckSignature,
						       1,
						       output1);
	if (flashUpdateCheckSignatureData != 0)
		return flashUpdateCheckSignatureData;
	guint8 *flashUpdateCheckSignatureBody = GetCommandBody1(output1);
	if (flashUpdateCheckSignatureBody[1] != (guint8)Pass)
		return UPDATE_DOCK_FAILED;

	guint8 writeFlashUpdateCheckVerify[1];
	writeFlashUpdateCheckVerify[0] = CRC;
	for (gint i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	gint flashUpdateCheckVerifyData = Function1(External_Flash,
						    Get_Flash_Memory_Self_Verify,
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
 *        If true, firmware will be updated even if the current firmware version
 * is => the target version.
 *
 * @param noUnplug
 *        If true, skip unplug detection during the firmware update process.
 *
 * @return SUCCESS on success, otherwise returns an error code defined in the
 * firmware update error definitions.(gint)
 */

gint
FWUpdate(gboolean forceUpdate, gboolean noUnplug)
{
	// gboolean forceUpdate = false;
	// gboolean noUnplug = false;
	char *compositeImageData;
	gint r;
	r = ReadCompositeImageFile(&compositeImageData);
	if (r != 0) {
		// g_print("Read composite image error: %d\n",r);
		return r;
	}
	// Set Flash Memory Access (Request)
	guint8 setFlashMemoryRequest[2];
	guint8 output1[Interface1Length];
	guint8 output2[Interface2Length];
	setFlashMemoryRequest[0] = AcccessCtrl;
	setFlashMemoryRequest[1] = Request;
	for (gint i = 0; i < Interface2Length; i++)
		output2[i] = 0;
	r = Function2(External_Flash,
		      Set_Flash_Memory_Access,
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
	if (stat(Path_Of_Usage_Information_Table, &fileInfo) == 0) {
		if (fileInfo.st_size != UsageTableSize) {
			return USAGE_INFORMATION_FILE_ERROR;
		} else {
			/*read the file actions*/
			fp = fopen(Path_Of_Usage_Information_Table, "rb");
			char buffer[UsageTableSize];
			size_t bytesRead = fread(&buffer, sizeof(char), UsageTableSize, fp);
			targetUsageInformationTable.Totalnumber = (guint8)buffer[0];
			targetUsageInformationTable.MajorVersion =
			    (((guint8)buffer[1] >> 4) & 0x0f);
			targetUsageInformationTable.MinorVersion = ((guint8)buffer[1] & 0x0f);
			targetUsageInformationTable.Dsa = (DSAType)buffer[2];
			targetUsageInformationTable.IoTUpdateFlag = (guint8)buffer[3];
			for (gint i = 0; i < 4; i++) {
				targetUsageInformationTable.CompositeFwVersion[i] =
				    (guint8)buffer[4 + i];
				targetUsageInformationTable.Crc32[i] =
				    (guint8)buffer[UsageTableSize - 4 + i];
			}
			for (gint i = 0; i < 2; i++) {
				targetUsageInformationTable.DockPid[i] = (guint8)buffer[8 + i];
			}
			targetUsageInformationTable.FlashIdList =
			    (struct FlashIdUsageInformation *)calloc(
				targetUsageInformationTable.Totalnumber + 1,
				sizeof(struct FlashIdUsageInformation));
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
					targetUsageInformationTable.FlashIdList[i].Flag = false;
				}
			}
			if (BytesToInt(targetUsageInformationTable.Crc32, 4) !=
			    Compute(buffer,
				    sizeof(buffer) / sizeof(buffer[0]),
				    0,
				    UsageTableSize - 4))
				return USAGE_INFORMATION_CRC_FAILED;

			guint16 targetPid =
			    (guint16)((targetUsageInformationTable.DockPid[0] << 8) +
				      targetUsageInformationTable.DockPid[1]);
			if (DockPid != targetPid)
				return XML_FILE_FORMAT_ERROR;
		}
	}
	// Check Flash Id List Count
	for (gint i = 0; i < Interface1Length; i++)
		output1[i] = 0;
	gint getFlashIdListData = Function1(External_Flash, Get_Flash_ID_List, 0, 0, 0, output1);
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
	guint8 mcuUsageInformationData[UsageInfo];
	do {
		gint payload = 8;
		guint8 setmcuUsageInformationTable[payload];
		setmcuUsageInformationTable[0] = Dock_Read_With_Address;
		setmcuUsageInformationTable[1] = 0x00;

		guint8 *size = IntToBytes(readCountByCycle);
		setmcuUsageInformationTable[2] = size[0];
		setmcuUsageInformationTable[3] = size[1];

		guint8 *addr = IntToBytes(UsageInfoStart + mcuUsageInformationCount);
		setmcuUsageInformationTable[4] = addr[0];
		setmcuUsageInformationTable[5] = addr[1];
		setmcuUsageInformationTable[6] = addr[2];
		setmcuUsageInformationTable[7] = addr[3];

		for (gint i = 0; i < Interface2Length; i++)
			output2[i] = 0;
		gint tempBytes = Function2(External_Flash,
					   Get_Flash_Memory_Access,
					   UsageTableFlashID,
					   setmcuUsageInformationTable,
					   payload,
					   output2);
		for (gint i = 0; i < readCountByCycle; i++) {
			mcuUsageInformationData[mcuUsageInformationCount + i] =
			    output2[7 + payload + i];
		}
		mcuUsageInformationCount += readCountByCycle;

	} while (mcuUsageInformationCount < UsageInfo);

	// Check & Transfer MCU Usage Information Table
	guint8 mcuUIcrc[4];
	for (gint i = 0; i < 4; i++) {
		mcuUIcrc[i] = mcuUsageInformationData[UsageTableSize - 4 + i];
		// g_print("%02X\n",mcuUIcrc[i]);
	}
	guint32 t = Compute(mcuUsageInformationData, UsageTableSize, 0, 4092);
	guint8 *computecrc = ToBytes(t);
	if (!arraysEqual(mcuUIcrc, computecrc, 4)) {
		forceUpdate = true;
	}

	// Check Package Version to Clean Update List
	gboolean bcdVerUpdateReq = false;
	if (forceUpdate)
		bcdVerUpdateReq = true;

	if (!forceUpdate) {
		for (gint i = 0; i < Interface1Length; i++)
			output1[i] = 0;
		gint GetBcdData =
		    Function1(Device_Information, Get_Firmware_Version, 0, 0, 0, output1);
		guint8 *dockFWPackageVer = GetCommandBody1(output1);
		guint8 *targetFWPackageVer = targetUsageInformationTable.CompositeFwVersion;
		gint dockFWPackageVerInt =
		    (dockFWPackageVer[0] << 16) + (dockFWPackageVer[1] << 8) + dockFWPackageVer[2];
		gint targetFWPackageVerInt = (targetFWPackageVer[1] << 16) +
					     (targetFWPackageVer[2] << 8) + targetFWPackageVer[3];
		free(dockFWPackageVer);

		if (targetFWPackageVerInt > dockFWPackageVerInt)
			bcdVerUpdateReq = true;
	}
	// Check Dock Component Need to Update
	struct FlashIdUsageInformation *FlashIdUpdateList;
	struct UsageInformation changeTagetUsageInformationTable = targetUsageInformationTable;

	// if (bcdVerUpdateReq) {
	if (forceUpdate) {
		for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			changeTagetUsageInformationTable.FlashIdList[i] =
			    targetUsageInformationTable.FlashIdList[i];
			changeTagetUsageInformationTable.FlashIdList[i].Flag = true;
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
			mcuUsageInformationTable.Dsa = (DSAType)mcuUsageInformationData[2];
			mcuUsageInformationTable.IoTUpdateFlag = mcuUsageInformationData[3];
			for (gint i = 0; i < 4; i++) {
				mcuUsageInformationTable.CompositeFwVersion[i] =
				    mcuUsageInformationData[4 + i];
				mcuUsageInformationTable.Crc32[i] =
				    mcuUsageInformationData[UsageTableSize - 4 + i];
			}
			for (gint i = 0; i < 2; i++) {
				mcuUsageInformationTable.DockPid[i] =
				    mcuUsageInformationData[8 + i];
			}
			mcuFlashIdList = (struct FlashIdUsageInformation *)calloc(
			    mcuUsageInformationTable.Totalnumber,
			    sizeof(struct FlashIdUsageInformation));
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
					changeTagetUsageInformationTable.FlashIdList[i].Flag = true;
				} else {
					changeTagetUsageInformationTable.FlashIdList[i] =
					    mcuFlashIdList[i];
					changeTagetUsageInformationTable.FlashIdList[i].Flag =
					    false;
				}
			}
			free(mcuFlashIdList);
		}
	}

	// Check MCU in to phase-1 process
	gboolean NeedWriteTable = false;
	for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		if (changeTagetUsageInformationTable.FlashIdList[i].Flag) {
			NeedWriteTable = true;
			break;
		}
	}
	if (NeedWriteTable) {
		// Set Dock FW Update Ctrl
		guint8 mcuUpdateCtrl[2];
		mcuUpdateCtrl[0] = nonLock;
		mcuUpdateCtrl[1] = InPhase1;
		for (gint i = 0; i < Interface1Length; i++)
			output1[i] = 0;
		r = Function1(Dock, Set_Dock_Firmware_Upgrade_Ctrl, 0, mcuUpdateCtrl, 2, output1);

		// Clean Target Fw Version
		for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
			for (gint j = 0; j < 4; j++)
				changeTagetUsageInformationTable.FlashIdList[i].TargetFwVersion[j] =
				    0x00;
		}

		// Write Usage Information Table
		guint8 *changeTagetUsageInformationTableBytes =
		    GetBytes(changeTagetUsageInformationTable);
		r = WriteUsageInformationTable(changeTagetUsageInformationTableBytes);
		free(changeTagetUsageInformationTableBytes);
		if (r != 0)
			return r;
	}

	// Phase-1 Start
	for (gint i = 1; i <= targetUsageInformationTable.Totalnumber; i++) {
		// Flash ID update start
		gint flashId = i;

		// Get Flash ID Attribute
		for (gint j = 0; j < Interface1Length; j++)
			output1[j] = 0;
		gint flashIdAttributeData =
		    Function1(External_Flash, Get_Flash_Attribute, flashId, 0, 0, output1);
		if (flashIdAttributeData != 0)
			return flashIdAttributeData;
		guint8 *flashIdAttributeBody = GetCommandBody1(output1);
		struct FlashIdAttribute flashIdAttribute =
		    GetFlashIdAttribute(flashIdAttributeBody);
		free(flashIdAttributeBody);
		// Check Component FW File
		if (flashIdAttribute.Purpose != FirmwareFile)
			continue;

		// Check Update Necessary
		if (!changeTagetUsageInformationTable.FlashIdList[flashId]
			 .Flag) // no need to update
			continue;

		// Write Flash ID Data
		r = WriteFlashIdData(flashId,
				     flashIdAttribute,
				     changeTagetUsageInformationTable,
				     compositeImageData);
		if (r != 0)
			return r;

		// Write Target FW Version
		changeTagetUsageInformationTable.FlashIdList[flashId] =
		    targetUsageInformationTable.FlashIdList[flashId];

		// Write Usage Information Table
		r = WriteUsageInformationTable(GetBytes(changeTagetUsageInformationTable));
		if (r != 0)
			return r;
	}

	// Set Flash Memory Access (Release)
	guint8 SetFlashMemoryAccessRelease[2];
	SetFlashMemoryAccessRelease[0] = AcccessCtrl;
	SetFlashMemoryAccessRelease[1] = Release;
	for (gint i = 0; i < Interface2Length; i++)
		output2[i] = 0;
	r = Function2(External_Flash,
		      Set_Flash_Memory_Access,
		      0,
		      SetFlashMemoryAccessRelease,
		      2,
		      output2);

	// Set Dock FW Update Ctrl
	if (bcdVerUpdateReq) {
		TriggerPhase2(noUnplug);
	}

	return 0;
}

gboolean
CheckDockReadyForEnterPhase2Update()
{
	gboolean rs = false;

	guint8 buffer[65];
	rs = Function1(Dock, Get_Dock_Firmware_Upgrade_Ctrl, 0, 0, 0, buffer);
	guint8 *DockFirmwareCtrlBody = GetCommandBody1(buffer);
	if (DockFirmwareCtrlBody[0] == Locked && DockFirmwareCtrlBody[1] == 2)
		rs = true;

	return rs;
}

gint
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
gint
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
// 回调函数：处理设备的插入和断开事件
gint LIBUSB_CALL
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
void *
usb_event_thread(void *arg)
{
	while (device_connected) {
		libusb_handle_events_completed(ctx, NULL); // 检测并触发 USB 热插拔回调事件
	}
	return NULL;
}

guint8
GetCurrentFwVerForGUI(gint flashId, gint index)
{
	return CurrentFwVerForGUI[flashId][index];
}

struct FlashIdUsageInformation *
check()
{
	guint8 output[Interface1Length];
	for (gint i = 0; i < Interface1Length; i++)
		output[i] = 0;
	gint getFlashIdList = Function1(External_Flash, Get_Flash_ID_List, 0, 0, 0, output);
	// g_print("getFlashIdList return %d\n",getFlashIdList);
	guint8 totalFlashId = output[6];
	struct FlashIdUsageInformation *Info =
	    (struct FlashIdUsageInformation *)calloc(totalFlashId,
						     sizeof(struct FlashIdUsageInformation));

	for (gint flashId = 1; flashId < totalFlashId; flashId++) {
		gint getFlashIdAttribute =
		    Function1(External_Flash, Get_Flash_Attribute, flashId, 0, 0, output);
		guint8 getPurpose = output[7];
		if (getPurpose == FirmwareFile) {
			for (gint i = 0; i < Interface1Length; i++)
				output[i] = 0;
			guint8 getFlashIdUsageInformation =
			    Function1(External_Flash,
				      Get_Flash_ID_Usage_Information,
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

gint
GetCompositeVersion(char *out, size_t outSize)
{
	if (!out || outSize == 0)
		return -1;

	guint8 buffer[65] = {0};
	gint rc = Function1(Device_Information, Get_Firmware_Version, 0, 0, 0, buffer);
	if (rc != 0) {
		snprintf(out, outSize, "0.0.0.0");
		return -1;
	}

	guint8 *body = GetCommandBody1(buffer);
	if (!body) {
		snprintf(out, outSize, "0.0.0.0");
		return -1;
	}

	gint n = snprintf(out, outSize, "%X.%X.%02X", body[0], body[1], body[2]);

	if (n < 0 || (size_t)n >= outSize) {
		return -1;
	}

	return 0;
}

gint
main(gint argc, char *argv[])
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
			struct FlashIdUsageInformation *Info = check();
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
			gboolean rs = CheckDockReadyForEnterPhase2Update();
			// g_print("%s\n", rs ? "true" : "false");
			if (rs == true) {
				g_print("Phase-1 update is already done, by rc %d",
					FWU_PHASE1_LOCKED);
				return FWU_PHASE1_LOCKED;
			}

			g_print("Please DO NOT remove the dock and wait for a few minutes until "
				"the white light stops blinking.\n");
			g_print("Start updating........\n");
			gint r = FWUpdate(true, true);
			if (r == 0) {
				/*Notice : rc == 0 includes 2 condition of FW update result as below
				   .
					   1. FW update process is actually be executed because
				   Dock's current composite is < target composite version.
					   2. FW update process doesn't which actually erase & flash
				   FW image since the Dock's current composite version is >= target
				   composite version. *Assume fwupd pluggin will cover for the
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

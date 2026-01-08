/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"
#include "fu-raydiumtp-common.h"
#include "fu-raydiumtp-hid-device.h"
#include "fu-raydiumtp-firmware.h"
#include "fu-raydiumtp-struct.h"

struct _FuRaydiumtpHidDevice
{
    FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuRaydiumtpHidDevice, fu_raydiumtp_hid_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_raydiumtp_hid_device_to_string(FuDevice *device,
                                  guint idt,
                                  GString *str)
{

}

static gboolean
fu_raydiumtp_hid_device_array_copy(guint8 src[],
                                   gsize src_sz,
                                   gint srcIndex,
                                   guint8 des[],
                                   gsize des_sz,
                                   gint desIndex,
                                   guint length,
                                   GError **error)
{
    return fu_memcpy_safe(des,
                          des_sz,
                          desIndex,
                          src,
                          src_sz,
                          srcIndex,
                          length,
                          error);
}

static gboolean
fu_raydiumtp_check_pid(FuDevice *self, FuRaydiumtpFirmware *ray_fw)
{
    return fu_device_get_pid(self) == fu_raydiumtp_firmware_get_product_id(ray_fw);
}

static gboolean
fu_raydiumtp_check_vid(FuDevice *self, FuRaydiumtpFirmware *ray_fw)
{
    return fu_device_get_vid(self) == fu_raydiumtp_firmware_get_vendor_id(ray_fw);
}

static gboolean
fu_raydiumtp_hid_device_get_report(FuRaydiumtpHidDevice *self,
                                   guint8 *rx,
                                   gsize rxsz,
                                   GError **error)
{
    g_autofree guint8 *rcv_buf = NULL;
    gsize bufsz = rxsz + 1;

    rcv_buf = g_malloc0(bufsz);

    rcv_buf[0] = FU_RAYDIUMTP_CMD2_RID;
    if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
                                      rcv_buf,
                                      bufsz,
                                      FU_IOCTL_FLAG_NONE,
                                      error))
        return FALSE;

    /* success */
    return fu_memcpy_safe(rx,
                          rxsz,
                          0,	/* dst */
                          rcv_buf,
                          bufsz,
                          0,	/* src */
                          rxsz,
                          error);
}

static gboolean
fu_raydiumtp_hid_device_set_report(FuRaydiumtpHidDevice *self,
                                   guint8 *tx,
                                   gsize txsz,
                                   GError **error)
{
    return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
                                        tx,
                                        txsz,
                                        FU_IOCTL_FLAG_NONE,
                                        error);
}

static gboolean
fu_raydiumtp_hid_device_bl_write(FuRaydiumtpHidDevice *self,
                                 guint8 Cmd,
                                 guint8 wBuf[],
                                 guint length,
                                 GError **error)
{
    guint8 retryCnt = 0;
    g_autofree guint8 *outBuf = g_malloc0(I2C_BUF_SIZE);
    gboolean res = FALSE;

    /*check write length*/
    if(length > HIDI2C_WRITE_MAX_LENGTH)
        return FALSE;

    /* Fill HID + I2C header */
    outBuf[0] = FU_RAYDIUMTP_CMD2_WID;
    outBuf[1] = 0x04;
    outBuf[2] = 0x00;
    outBuf[3] = 0x21;
    outBuf[4] = 0x03;
    outBuf[5] = 0x05;
    outBuf[6] = 0x00;
    /* I2C header */
    outBuf[7] = FU_RAYDIUMTP_CMD2_WRT;
    outBuf[8] = 0x00;
    /* HID header */
    outBuf[9] = Cmd;
    outBuf[10] = wBuf[3];
    outBuf[11] = wBuf[4];
    outBuf[12] = wBuf[5];
    /* Length */
    outBuf[13] = (guint8)(length & 0xFF);
    outBuf[14] = (guint8)((length >> 8) & 0xFF);

    /*write data into bootloader*/
    if (!fu_memcpy_safe(outBuf,
                        I2C_BUF_SIZE,
                        15,
                        wBuf,
                        I2C_BUF_SIZE,
                        6,
                        length,
                        error))
        return FALSE;

    do
    {
        res = fu_raydiumtp_hid_device_set_report(self, outBuf, I2C_BUF_SIZE, error);
        if(retryCnt++ > RETRY_NUM)
            break;
        fu_device_sleep(FU_DEVICE(self), 1);
    }
    while (!res);

    if(!res)
        return FALSE;

    memset(outBuf, 0, I2C_BUF_SIZE);
    retryCnt = 0;

    /* Fill HID + I2C header */
    outBuf[0] = FU_RAYDIUMTP_CMD2_WID;
    outBuf[1] = 0x04;
    outBuf[2] = 0x00;
    outBuf[3] = 0x21;
    outBuf[4] = 0x03;
    outBuf[5] = 0x05;
    outBuf[6] = 0x00;
    /* I2C header */
    outBuf[7] = FU_RAYDIUMTP_CMD2_ACK;
    /* Length */
    outBuf[13] = (guint8)(length & 0xFF);
    outBuf[14] = (guint8)((length >> 8) & 0xFF);

    do
    {
        res = fu_raydiumtp_hid_device_set_report(self, outBuf, I2C_BUF_SIZE, error);
        if(retryCnt++ > RETRY_NUM)
            break;
        fu_device_sleep(FU_DEVICE(self), 1);
    }
    while (!res);

    return res;
}

static gboolean
fu_raydiumtp_hid_device_bl_read(FuRaydiumtpHidDevice *self,
                                guint8 rBuf[],
                                guint length,
                                GError **error)
{
    guint8 retryCnt = 0;
    guint8 WaitIdleFlag = 0;
    g_autofree guint8 *outBuf = g_malloc0(I2C_BUF_SIZE);
    g_autofree guint8 *inBuf = g_malloc0(256);
    gboolean res = FALSE;

    if(rBuf[1] == 0xFF)
    {
        WaitIdleFlag = 1;
        rBuf[1] = 0x00;
    }

    /* Fill HID + I2C header */
    outBuf[0] = FU_RAYDIUMTP_CMD2_WID;
    outBuf[1] = 0x04;
    outBuf[2] = 0x00;
    outBuf[3] = 0x12;
    outBuf[4] = 0x02;
    outBuf[5] = 0x05;
    outBuf[6] = 0x00;
    /* I2C header */
    outBuf[7] = rBuf[0];
    outBuf[8] = rBuf[1];
    /* HID header */
    outBuf[9] = rBuf[2];
    outBuf[10] = rBuf[3];
    outBuf[11] = rBuf[4];
    outBuf[12] = rBuf[5];
    /* Length */
    outBuf[13] = (guint8)(length & 0xFF);
    outBuf[14] = (guint8)((length >> 8) & 0xFF);

    do
    {
        if (!fu_raydiumtp_hid_device_set_report(self, outBuf, I2C_BUF_SIZE, error))
            res = FALSE;
        else
            res = fu_raydiumtp_hid_device_get_report(self, inBuf, I2C_BUF_SIZE, error);

        retryCnt++;
        fu_device_sleep(FU_DEVICE(self), 1);
    }
    while (inBuf[HIDI2C_CHK_IDX] != 0xFF && inBuf[0] != 0xFF && retryCnt < RETRY_NUM);

    if(!res || retryCnt >= RETRY_NUM)
    {
        return FALSE;
    }

    if(WaitIdleFlag == 1)
    {
        return fu_memcpy_safe(rBuf,
                              I2C_BUF_SIZE,
                              0,	/* dst */
                              inBuf,
                              256,
                              0,	/* src */
                              I2C_BUF_SIZE,
                              error);
    }
    else
    {
        return fu_memcpy_safe(rBuf,
                              I2C_BUF_SIZE,
                              0,	/* dst */
                              inBuf,
                              256,
                              1,	/* src */
                              I2C_BUF_SIZE - 1,
                              error);
    }
}

static gboolean
fu_raydiumtp_hid_device_tp_write(FuRaydiumtpHidDevice *self,
                                 guint8 Cmd,
                                 guint8 wBuf[],
                                 guint length,
                                 GError **error)
{
    guint8 retryCnt = 0;
    g_autofree guint8 *outBuf = g_malloc0(I2C_BUF_SIZE);
    gboolean res = FALSE;

    /*check write length*/
    if(length > HIDI2C_WRITE_MAX_LENGTH)
        return FALSE;

    /* Fill HID + I2C header */
    outBuf[0] = FU_RAYDIUMTP_CMD2_WID;
    outBuf[1] = 0x04;
    outBuf[2] = 0x00;
    outBuf[3] = 0x21;
    outBuf[4] = 0x03;
    outBuf[5] = 0x05;
    outBuf[6] = 0x00;
    /* I2C header */
    outBuf[7] = FU_RAYDIUMTP_CMD2_WRT;
    outBuf[8] = 0x00;
    /* Length */
    outBuf[9] = (guint8)(length + 1);
    /* Command */
    outBuf[10] = Cmd;

    if (!fu_memcpy_safe(outBuf,
                        I2C_BUF_SIZE,
                        11,
                        wBuf,
                        I2C_BUF_SIZE,
                        0,
                        length,
                        error))
        return FALSE;
    do
    {
        res = fu_raydiumtp_hid_device_set_report(self, outBuf, I2C_BUF_SIZE, error);
        if(retryCnt++ > RETRY_NUM_MAX)
            break;
        fu_device_sleep(FU_DEVICE(self), 1);
    }
    while (!res);

    if(!res)
        return FALSE;

    memset(outBuf, 0, I2C_BUF_SIZE);
    retryCnt = 0;

    /* Fill HID + I2C header */
    outBuf[0] = FU_RAYDIUMTP_CMD2_WID;
    outBuf[1] = 0x04;
    outBuf[2] = 0x00;
    outBuf[3] = 0x21;
    outBuf[4] = 0x03;
    outBuf[5] = 0x05;
    outBuf[6] = 0x00;
    /* I2C header */
    outBuf[7] = FU_RAYDIUMTP_CMD2_ACK;

    do
    {
        res = fu_raydiumtp_hid_device_set_report(self, outBuf, I2C_BUF_SIZE, error);
        if(retryCnt++ > RETRY_NUM_MAX)
            break;
        fu_device_sleep(FU_DEVICE(self), 1);
    }
    while (!res);

    return res;
}

static gboolean
fu_raydiumtp_hid_device_tp_read(FuRaydiumtpHidDevice *self,
                                guint8 Cmd,
                                guint8 rBuf[],
                                GError **error)
{
    guint8 retryCnt = 0;
    g_autofree guint8 *outBuf = g_malloc0(I2C_BUF_SIZE);
    g_autofree guint8 *inBuf = g_malloc0(I2C_BUF_SIZE);
    gboolean res = FALSE;

    /* Fill HID + I2C header */
    outBuf[0] = FU_RAYDIUMTP_CMD2_WID;
    outBuf[1] = 0x04;
    outBuf[2] = 0x00;
    outBuf[3] = 0x12;
    outBuf[4] = 0x02;
    outBuf[5] = 0x05;
    outBuf[6] = 0x00;
    /* I2C header */
    outBuf[7] = FU_RAYDIUMTP_CMD2_READ;
    outBuf[8] = 0x00;
    /* Length */
    outBuf[9] = 0x00;
    outBuf[10] = 0x3C;
    /* Command */
    outBuf[11] = Cmd;

    do
    {
        if (!fu_raydiumtp_hid_device_set_report(self, outBuf, I2C_BUF_SIZE, error))
            res = FALSE;
        else
            res = fu_raydiumtp_hid_device_get_report(self, inBuf, I2C_BUF_SIZE, error);

        retryCnt++;
        fu_device_sleep(FU_DEVICE(self), 1);
    }
    while (inBuf[HIDI2C_CHK_IDX] != 0xFF && inBuf[0] != 0xFF && retryCnt < RETRY_NUM_MAX);

    if(!res || retryCnt >= RETRY_NUM_MAX)
    {
        return FALSE;
    }

    return fu_memcpy_safe(rBuf,
                          I2C_BUF_SIZE,
                          0,	/* dst */
                          inBuf,
                          I2C_BUF_SIZE,
                          1,	/* src */
                          I2C_BUF_SIZE - 1,
                          error);
}

static gboolean
fu_raydiumtp_hid_device_command_write
(
    FuRaydiumtpHidDevice *self,
    guint8 Cmd,
    guint8 buf[],
    guint length,
    GError **error
)
{
    return fu_raydiumtp_hid_device_tp_write(self, Cmd, buf, length, error);
}

static gboolean
fu_raydiumtp_hid_device_command_read
(
    FuRaydiumtpHidDevice *self,
    guint8 Cmd,
    guint8 buf[],
    guint length,
    GError **error
)
{
    return fu_raydiumtp_hid_device_tp_read(self, Cmd, buf, error);
}

static gboolean
fu_raydiumtp_hid_device_write_boot
(
    FuRaydiumtpHidDevice *self,
    guint8 Cmd,
    guint8 buf[],
    guint length,
    GError **error
)
{
    return fu_raydiumtp_hid_device_bl_write(self, Cmd, buf, length, error);
}

static gboolean
fu_raydiumtp_hid_device_read_boot
(
    FuRaydiumtpHidDevice *self,
    guint8 OutBuf[],
    guint length,
    GError **error
)
{
    return fu_raydiumtp_hid_device_bl_read(self, OutBuf, length, error);
}

static gboolean
fu_raydiumtp_hid_device_jump_to_boot
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint8 wdata[I2C_BUF_SIZE] = {0};

    return fu_raydiumtp_hid_device_command_write(self, FU_RAYDIUMTP_CMD_ADDR_JUMP_TO_BOOTLOADER, wdata, 1, error);
}


static guint8
fu_raydiumtp_hid_device_read_status
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint8 i = 0;
    guint8 data[I2C_BUF_SIZE] = {0};

    for(i = 0; i < RETRY_NUM; i++)
    {
        memset(data, 0, sizeof(data));
        data[0] = FU_RAYDIUMTP_CMD2_CHK;
        if(fu_raydiumtp_hid_device_bl_read(self, data, 7, error))
        {
            /* 'f' 'i' 'r' 'm'*/
            if((data[0] == 0x66) && (data[1] == 0x69) && (data[2] == 0x72) && (data[3] == 0x6D))
                return FU_RAYDIUMTP_BOOT_MODE_TS_MAIN;
            /* 'b' 'o' 'o' 't'*/
            else if((data[0] == 0x62) && (data[1] == 0x6F) && (data[2] == 0x6F) && (data[3] == 0x74))
                return FU_RAYDIUMTP_BOOT_MODE_TS_BLDR;
        }
    }
    return FU_RAYDIUMTP_BOOT_MODE_TS_NONE;

}

static gboolean
fu_raydiumtp_hid_device_set_bl_mem
(
    FuRaydiumtpHidDevice *self,
    guint32 addr,
    guint32 value,
    guint size,
    GError **error
)
{
    guint8 wdata[I2C_BUF_SIZE] = {0};

    wdata[6] = (guint8)(addr & 0xFF);
    wdata[7] = (guint8)((addr & 0xFF00) >> 8);
    wdata[8] = (guint8)((addr & 0xFF0000) >> 16);
    wdata[9] = (guint8)((addr & 0xFF000000) >> 24);

    wdata[10] = (guint8)(value & 0xFF);
    wdata[11] = (guint8)((value & 0xFF00) >> 8);
    wdata[12] = (guint8)((value & 0xFF0000) >> 16);
    wdata[13] = (guint8)((value & 0xFF000000) >> 24);

    return fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_WRITEREGISTER, wdata, HIDI2C_WRITE_MAX_LENGTH, error);
}

static gboolean
fu_raydiumtp_hid_device_get_bl_mem
(
    FuRaydiumtpHidDevice *self,
    guint32 addr,
    guint16 length,
    guint8 OutBuf[],
    GError **error
)
{
    guint8 wdata[I2C_BUF_SIZE] = {0};

    wdata[6] = (guint8)addr;
    wdata[7] = (guint8)(addr >> 8);
    wdata[8] = (guint8)(addr >> 16);
    wdata[9] = (guint8)(addr >> 24);
    wdata[10] = (guint8)length;
    wdata[11] = (guint8)(length >> 8);

    if(!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_READ_ADDRESS_MEMORY, wdata, HIDI2C_WRITE_MAX_LENGTH, error))
        return FALSE;

    OutBuf[0] = FU_RAYDIUMTP_CMD2_READ;

    return fu_raydiumtp_hid_device_read_boot(self, OutBuf, length, error);
}

static gboolean
fu_raydiumtp_hid_device_wait_for_idle_boot
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint8 rBuf[I2C_BUF_SIZE] = {0};
    guint8 Cnt = 0, BootMainState = 0x00;
    gboolean res = FALSE;

    do
    {
        memset(rBuf, 0, sizeof(rBuf));

        rBuf[0] = FU_RAYDIUMTP_CMD2_CHK;
        rBuf[1] = 0xFF;
        res = fu_raydiumtp_hid_device_read_boot(self, rBuf, 6, error);
        BootMainState = rBuf[HIDI2C_CHK_IDX];

        if(BootMainState != FU_RAYDIUMTP_CMD_BL_CMD_IDLE || !res)
            fu_device_sleep(FU_DEVICE(self), 10);

        if(Cnt++ > RETRY_NUM_MAX)
            return FALSE;

    }
    while(BootMainState != FU_RAYDIUMTP_CMD_BL_CMD_IDLE);

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_bl_set_wdt
(
    FuRaydiumtpHidDevice *self,
    guint8 enable,
    GError **error
)
{
    guint8 wBuf[I2C_BUF_SIZE] = {0};

    if(enable == 1)
        wBuf[3] = FU_RAYDIUMTP_CMD_BL_WATCHDOG_ENABLE;
    else
        wBuf[3] = FU_RAYDIUMTP_CMD_BL_WATCHDOG_DISABLE;

    return fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_WATCHDOG_FUNCTION_SET, wBuf, HIDI2C_WRITE_MAX_LENGTH, error);
}

static gboolean
fu_raydiumtp_hid_device_bl_dis_wdt_and_unlock_flash
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    gboolean res = TRUE;

    if(!fu_raydiumtp_hid_device_bl_set_wdt(self, 0, error))
        return FALSE;

    res &= fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_KEY_DISABLE_FLASH_PROTECTION, FU_RAYDIUMTP_KEY_DISABLE, 8, error);
    res &= fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_KEY_UNLOCK_PRAM, FU_RAYDIUMTP_KEY_DISABLE, 8, error);
    res &= fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_KEY_FLASH_FLKEY2, FU_RAYDIUMTP_KEY_FLKEY3_KEY, 8, error);
    res &= fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_KEY_FLASH_FLKEY1, FU_RAYDIUMTP_KEY_FLKEY1_KEY, 8, error);
    res &= fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_KEY_FLASH_FLKEY1, FU_RAYDIUMTP_KEY_DISABLE, 8, error);
    res &= fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_KEY_FLASH_FLKEY1, FU_RAYDIUMTP_KEY_FLKEY1_KEY, 8, error);
    res &= fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_KEY_FLASH_FLKEY2, FU_RAYDIUMTP_KEY_DISABLE, 8, error);

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    return res;
}

static gboolean
fu_raydiumtp_hid_device_bl_erase_fw_flash
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint8 wBuf[I2C_BUF_SIZE] = {0};

    wBuf[3] = FU_RAYDIUMTP_CMD_BL_ERASEFLASH_MODE1;
    if(!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_ERASEFLASH, wBuf, HIDI2C_WRITE_MAX_LENGTH, error))
        return FALSE;

    fu_device_sleep(FU_DEVICE(self), 100);

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_bl_erase_flash_sector
(
    FuRaydiumtpHidDevice *self,
    guint32 u32Address,
    guint8 u8Loop,
    GError **error
)
{
    guint8 wBuf[I2C_BUF_SIZE] = {0};

    wBuf[3] = FU_RAYDIUMTP_CMD_BL_ERASEFLASH_MODE4;
    wBuf[7] = (guint8)u32Address;
    wBuf[8] = (guint8)(u32Address >> 8);
    wBuf[9] = (guint8)(u32Address >> 16);
    wBuf[10] = (guint8)(u32Address >> 24);
    wBuf[11] = u8Loop;

    if (!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_ERASEFLASH, wBuf, HIDI2C_WRITE_MAX_LENGTH, error))
        return FALSE;

    fu_device_sleep(FU_DEVICE(self), 1);

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_bl_write_flash
(
    FuRaydiumtpHidDevice *self,
    guint8 InBuf[],
    guint ImageSize,
    GError **error
)
{
    guint8 wBuf[I2C_BUF_SIZE] = {0};
    gint TmpVal = 0, k = 0;
    guint CurWritePageNo = 0;
    guint8 SubPageNo = 0, IsEndOfFWData = 0;
    guint16 RemainWriteData = 0, TotalWritePageNo = 0;
    guint Writelen = ImageSize;
    TmpVal = (4 - (Writelen % 4));
    if(TmpVal != 4)
        Writelen += TmpVal;

    TotalWritePageNo = (guint16)(((float)Writelen / RM_FW_PAGE_SIZE) + 0.9999);

    for(CurWritePageNo = 0; CurWritePageNo < TotalWritePageNo; CurWritePageNo++)
    {
        wBuf[3] = (guint8)(CurWritePageNo & 0xFF);
        wBuf[4] = (guint8)((CurWritePageNo >> 8) & 0xFF);

        for(SubPageNo = 0; SubPageNo < 4; SubPageNo++)
        {
            wBuf[5] = SubPageNo;

            if(((CurWritePageNo * RM_FW_PAGE_SIZE) + (SubPageNo + 1) * HIDI2C_WRITE_SIZE) < Writelen)
            {
                if(!fu_raydiumtp_hid_device_array_copy(InBuf, ImageSize, ((CurWritePageNo * RM_FW_PAGE_SIZE) + (SubPageNo * HIDI2C_WRITE_SIZE)), wBuf, I2C_BUF_SIZE, 6, HIDI2C_WRITE_SIZE, error))
                    return FALSE;
            }
            else if(!IsEndOfFWData)
            {
                RemainWriteData = (guint16)(Writelen - ((CurWritePageNo * RM_FW_PAGE_SIZE) + (SubPageNo * HIDI2C_WRITE_SIZE)));
                if(!fu_raydiumtp_hid_device_array_copy(InBuf, ImageSize, ((CurWritePageNo * RM_FW_PAGE_SIZE) + (SubPageNo * HIDI2C_WRITE_SIZE)), wBuf, I2C_BUF_SIZE, 6, RemainWriteData, error))
                    return FALSE;
                for(k = (RemainWriteData + 6); k < (HIDI2C_WRITE_SIZE + 6); k++)
                    wBuf[k] = 0;
                IsEndOfFWData = 1;
            }
            else
            {
                for(k = 6; k < (HIDI2C_WRITE_SIZE + 6); k++)
                    wBuf[k] = 0;
            }

            if(!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_WRITEHIDI2CFALSH, wBuf, HIDI2C_WRITE_SIZE, error))
                return FALSE;
        }

        if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
            return FALSE;
    }

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_bl_dma_crc
(
    FuRaydiumtpHidDevice *self,
    guint32 base_addr,
    guint32 img_length,
    guint32 image_crc,
    GError **error
)
{
    guint8 readbuf[I2C_BUF_SIZE] = {0};
    guint32 u32Value = 0;
    guint32 calculated_crc = 0;

    if (!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_SADDR, base_addr, 8, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_EADDR, base_addr + img_length - CRC_LEN, 8, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_IER, 5, readbuf, error))
        return FALSE;

    u32Value = (guint32)(readbuf[0]) | (guint32)(readbuf[1] << 8) | (guint32)(readbuf[2] << 16) | (guint32)(readbuf[3] << 24);
    u32Value = (u32Value & 0xFFFEFFFF);

    if (!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_IER, u32Value, 8, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_PRAM_LOCK, 0, 8, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_IER, 5, readbuf, error))
        return FALSE;

    u32Value = (guint32)(readbuf[0]) | (guint32)(readbuf[1] << 8) | (guint32)(readbuf[2] << 16) | (guint32)(readbuf[3] << 24);
    u32Value = ((u32Value & 0xFFFDFFFF) | 0x00020000);

    if (!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_IER, u32Value, 8, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_CR, 5, readbuf, error))
        return FALSE;

    u32Value = (guint32)(readbuf[0]) | (guint32)(readbuf[1] << 8) | (guint32)(readbuf[2] << 16) | (guint32)(readbuf[3] << 24);
    u32Value = ((u32Value & 0xFF7FFFFF) | 0x00800000);

    if (!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_CR, u32Value, 8, error))
        return FALSE;

    do
    {
        fu_device_sleep(FU_DEVICE(self), 100);
        if (!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_IER, 5, readbuf, error))
            return FALSE;
    }
    while((readbuf[2] & 0x80) == 0x80);

    if (!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DMA_RES, 5, readbuf, error))
        return FALSE;

    calculated_crc = (guint32)(readbuf[0]) | (guint32)(readbuf[1] << 8) | (guint32)(readbuf[2] << 16) | (guint32)(readbuf[3] << 24);

    if (image_crc == calculated_crc)
        return TRUE;

    return FALSE;
}

static gboolean
fu_raydiumtp_hid_device_bl_trig_desc_to_flash
(
    FuRaydiumtpHidDevice *self,
    guint32 u32PramAddress,
    guint32 u32FlashAddress,
    guint16 u16Length,
    GError **error
)
{
    guint8 wBuf[I2C_BUF_SIZE] = {0};

    wBuf[3] = FU_RAYDIUMTP_CMD_BL_CMD_WRITERAMFALSH;
    wBuf[4] = 0x05;

    wBuf[8] = (guint8)u32PramAddress;
    wBuf[9] = (guint8)(u32PramAddress >> 8);
    wBuf[10] = (guint8)(u32PramAddress >> 16);
    wBuf[11] = (guint8)(u32PramAddress >> 24);

    wBuf[12] = (guint8)u32FlashAddress;
    wBuf[13] = (guint8)(u32FlashAddress >> 8);
    wBuf[14] = (guint8)(u32FlashAddress >> 16);
    wBuf[15] = (guint8)(u32FlashAddress >> 24);

    wBuf[16] = (guint8)u16Length;
    wBuf[17] = (guint8)(u16Length >> 8);

    if(!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_WRITERAMFALSH, wBuf, HIDI2C_WRITE_MAX_LENGTH, error))
        return FALSE;

    fu_device_sleep(FU_DEVICE(self), 100);

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_bl_trig_pram_to_flash
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint8 wBuf[I2C_BUF_SIZE] = {0};

    wBuf[0] = FU_RAYDIUMTP_CMD2_WRT;
    wBuf[2] = FU_RAYDIUMTP_CMD_BL_CMD_TRIGGER_WRITE_FLASH;

    if(!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_TRIGGER_WRITE_FLASH, wBuf, HIDI2C_WRITE_MAX_LENGTH, error))
        return FALSE;

    fu_device_sleep(FU_DEVICE(self), 100);

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;
    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_bl_software_reset
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint8 retry_cnt = 0;

    do
    {
        if(!fu_raydiumtp_hid_device_set_bl_mem(self, 0x40000004, 0x00000001, 8, error))
            ;
        fu_device_sleep(FU_DEVICE(self), 1000);
    }
    while(fu_raydiumtp_hid_device_read_status(self, error) != FU_RAYDIUMTP_BOOT_MODE_TS_MAIN && (retry_cnt++ < RETRY_NUM));

    if(retry_cnt > RETRY_NUM - 1)
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_set_mem_addr
(
    FuRaydiumtpHidDevice *self,
    guint32 addr,
    guint8 type,
    GError **error
)
{
    guint8 wdata[I2C_BUF_SIZE] = {0};

    wdata[0] = (guint8)(addr & 0xFF);
    wdata[1] = (guint8)((addr & 0xFF00) >> 8);
    wdata[2] = (guint8)((addr & 0xFF0000) >> 16);
    wdata[3] = (guint8)((addr & 0xFF000000) >> 24);
    wdata[4] = type;
    return fu_raydiumtp_hid_device_command_write(self, FU_RAYDIUMTP_CMD_ADDR_MEM_ADDRESS_SET, wdata, 5, error);
}

static gboolean
fu_raydiumtp_hid_device_set_mem_write
(
    FuRaydiumtpHidDevice *self,
    guint32 value,
    GError **error
)
{
    guint8 wdata[I2C_BUF_SIZE] = {0};

    wdata[0] = (guint8)(value & 0xFF);
    wdata[1] = (guint8)((value & 0xFF00) >> 8);
    wdata[2] = (guint8)((value & 0xFF0000) >> 16);
    wdata[3] = (guint8)((value & 0xFF000000) >> 24);
    return fu_raydiumtp_hid_device_command_write(self, FU_RAYDIUMTP_CMD_ADDR_MEM_WRITE, wdata, 4, error);
}

static gboolean
fu_raydiumtp_hid_device_get_mem_read
(
    FuRaydiumtpHidDevice *self,
    guint8 ram[],
    GError **error
)
{
    guint8 i;
    guint8 readbuf[I2C_BUF_SIZE] = {0};

    if(!fu_raydiumtp_hid_device_command_read(self, FU_RAYDIUMTP_CMD_ADDR_MEM_READ, readbuf, 4, error))
        return FALSE;

    for(i = 0; i < 4; i++)
    {
        ram[i] = readbuf[i];
    }
    return TRUE;
}

static guint8
fu_raydiumtp_hid_device_read_flash_protect_status
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint8 readbuf[I2C_BUF_SIZE] = {0};
    guint32 u32Value = 0;

    if(!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_LENGTH, FU_RAYDIUMTP_KEY_FLREAD_STATUS, 8, error))
        return 0xFF;

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return 0xFF;

    if(!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_ISPCTL, 5, readbuf, error))
        return 0xFF;

    u32Value = ((guint32)readbuf[0]) | ((guint32)readbuf[1] << 8) | ((guint32)readbuf[2] << 16) | ((guint32)readbuf[3] << 24);
    u32Value = ((u32Value & 0xFFFFF7FF) | 0x00000800);

    if(!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_ISPCTL, u32Value, 8, error))
        return 0xFF;

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return 0xFF;

    if(!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_DATA, 5, readbuf, error))
        return 0xFF;

    return readbuf[0];
}

static gboolean
fu_raydiumtp_hid_device_write_flash_protect_status
(
    FuRaydiumtpHidDevice *self,
    guint8 status,
    GError **error
)
{
    guint8 readbuf[I2C_BUF_SIZE] = {0};
    guint32 u32Value = 0;

    if (!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_LENGTH, FU_RAYDIUMTP_KEY_FLWRITE_EN, 8, error))
        return FALSE;

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_ISPCTL, 5, readbuf, error))
        return FALSE;

    u32Value = ((guint32)readbuf[0]) | ((guint32)readbuf[1] << 8) | ((guint32)readbuf[2] << 16) | ((guint32)readbuf[3] << 24);
    u32Value = ((u32Value & 0xFFFFF7FF) | 0x00000800);

    if(!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_ISPCTL, u32Value, 8, error))
        return FALSE;

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    if(!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_LENGTH, FU_RAYDIUMTP_KEY_FLWRITE_STATUS, 8, error))
        return FALSE;

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    u32Value = (guint32)(status << 16);

    if(!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_ADDR, u32Value, 8, error))
        return FALSE;

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    if(!fu_raydiumtp_hid_device_get_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_ISPCTL, 5, readbuf, error))
        return FALSE;

    u32Value = ((guint32)readbuf[0]) | ((guint32)readbuf[1] << 8) | ((guint32)readbuf[2] << 16) | ((guint32)readbuf[3] << 24);
    u32Value = ((u32Value & 0xFFFFF7FF) | 0x00000800);

    if(!fu_raydiumtp_hid_device_set_bl_mem(self, FU_RAYDIUMTP_FLASH_CTRL_ISPCTL, u32Value, 8, error))
        return FALSE;

    if(!fu_raydiumtp_hid_device_wait_for_idle_boot(self, error))
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_lock_flash_protect
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    return fu_raydiumtp_hid_device_write_flash_protect_status(self, FU_RAYDIUMTP_PROTECT_ALLOCK, error);
}

static gboolean
fu_raydiumtp_hid_device_unlock_flash_protect
(
    FuRaydiumtpHidDevice *self,
    guint8 mode,
    GError **error
)
{
    switch(mode)
    {
    case 0:
        return fu_raydiumtp_hid_device_write_flash_protect_status(self, FU_RAYDIUMTP_PROTECT_FWUNLOCK, error);
    case 1:
        return fu_raydiumtp_hid_device_write_flash_protect_status(self, FU_RAYDIUMTP_PROTECT_GDUNLOCK, error);
    case 2:
        return fu_raydiumtp_hid_device_write_flash_protect_status(self, FU_RAYDIUMTP_PROTECT_BLUNLOCK, error);
    default:
        return FALSE;
    }
}

static gboolean
fu_raydiumtp_hid_device_read_firmware_info
(
    FuRaydiumtpHidDevice *self,
    GError **error
)
{
    guint32 u32Address = 0;
    guint16 u16Length = 32;
    guint8 buf[I2C_BUF_SIZE] = {0};
    guint8 readbuf[I2C_BUF_SIZE] = {0};
    guint8 readbuf_Desc[I2C_BUF_SIZE] = {0};
    guint8 readbuf_FT[I2C_BUF_SIZE] = {0};
    guint16 u16VID;
    guint8 u8Mode;

    u8Mode = fu_raydiumtp_hid_device_read_status(self, error);

    if (u8Mode == FU_RAYDIUMTP_BOOT_MODE_TS_NONE)
    {
        return FALSE;
    }

    if (u8Mode == FU_RAYDIUMTP_BOOT_MODE_TS_BLDR)
    {
        u32Address = FU_RAYDIUMTP_FLASH_DESC_RECORD_ADDR;
        u16Length = 32;
        buf[6] = (guint8) u32Address;
        buf[7] = (guint8) (u32Address >> 8);
        buf[8] = (guint8) (u32Address >> 16);
        buf[9] = (guint8) (u32Address >> 24);
        buf[10] = (guint8) u16Length;
        buf[11] = (guint8) (u16Length >> 8);

        if(!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_READFLASHADDR, buf, HIDI2C_WRITE_MAX_LENGTH, error))
            return FALSE;

        readbuf_Desc[0] = FU_RAYDIUMTP_CMD2_READ;

        if(!fu_raydiumtp_hid_device_read_boot(self, readbuf_Desc, 40, error))
            return FALSE;

        u32Address = FU_RAYDIUMTP_FLASH_FT_RECORD_ADDR;
        u16Length = 16;
        buf[6] = (guint8) u32Address;
        buf[7] = (guint8) (u32Address >> 8);
        buf[8] = (guint8) (u32Address >> 16);
        buf[9] = (guint8) (u32Address >> 24);
        buf[10] = (guint8) u16Length;
        buf[11] = (guint8) (u16Length >> 8);

        if(!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_READFLASHADDR, buf, HIDI2C_WRITE_MAX_LENGTH, error))
            return FALSE;

        readbuf_FT[0] = FU_RAYDIUMTP_CMD2_READ;

        if(!fu_raydiumtp_hid_device_read_boot(self, readbuf_FT, 20, error))
            return FALSE;

        u16VID = readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_VID_H] << 8 | readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_VID_L];

        if ((u16VID == VENDOR_ID) && ((readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_PID_H] != 0xFF) || (readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_PID_L] != 0xFF)))
        {
            readbuf[9] = readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_PID_H];
            readbuf[10] = readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_PID_L];
            readbuf[16] = readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_VID_L];
            readbuf[17] = readbuf_Desc[FU_RAYDIUMTP_DESC_RECORD_INFO_VID_H];
        }
        else if ((readbuf_FT[FU_RAYDIUMTP_FT_RECORD_INFO_PID_H] != 0xFF) || (readbuf_FT[FU_RAYDIUMTP_FT_RECORD_INFO_PID_L] != 0xFF))
        {
            readbuf[9] = readbuf_FT[FU_RAYDIUMTP_FT_RECORD_INFO_PID_H];
            readbuf[10] = readbuf_FT[FU_RAYDIUMTP_FT_RECORD_INFO_PID_L];
            readbuf[16] = readbuf_FT[FU_RAYDIUMTP_FT_RECORD_INFO_VID_L];
            readbuf[17] = readbuf_FT[FU_RAYDIUMTP_FT_RECORD_INFO_VID_H];
        }
    }
    else if (u8Mode == FU_RAYDIUMTP_BOOT_MODE_TS_MAIN)
    {
        buf[0] = GET_SYS_FW_VERSION_NUM;
        if(!fu_raydiumtp_hid_device_command_write(self, FU_RAYDIUMTP_CMD_ADDR_SYSTEM_INFO_MODE_WRITE, buf, 1, error))
            return FALSE;

        if(!fu_raydiumtp_hid_device_command_read(self, FU_RAYDIUMTP_CMD_ADDR_SYSTEM_INFO_MODE_READ, readbuf, 20, error))
            return FALSE;
    }

    u16VID = (readbuf[17] << 8 | readbuf[16]);

    if (u16VID != VENDOR_ID)
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_update_prepare(FuRaydiumtpHidDevice *self, GError **error)
{
    guint8 retryCnt = 0;

    do
    {
        if(!fu_raydiumtp_hid_device_jump_to_boot(self, error))
            ;
        fu_device_sleep(FU_DEVICE(self), 10);
    }
    while(fu_raydiumtp_hid_device_read_status(self, error) != FU_RAYDIUMTP_BOOT_MODE_TS_BLDR && retryCnt++ < RETRY_NUM);

    if(retryCnt >= RETRY_NUM)
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_write_fwimage(FuRaydiumtpHidDevice *self,
                                      guint8 *img,
                                      guint32 base_addr,
                                      guint32 img_length,
                                      guint32 image_crc,
                                      FuProgress *progress,
                                      GError **error)
{
    //Write Data
    if(!fu_raydiumtp_hid_device_bl_write_flash(self, img, img_length, error))
        return FALSE;

    //Check Data
    if(!fu_raydiumtp_hid_device_bl_dma_crc(self, FU_RAYDIUMTP_RAM_FIRM_BASE, img_length - CRC_LEN, image_crc, error))
        return FALSE;

    //Erase Flash
    if(!fu_raydiumtp_hid_device_bl_erase_fw_flash(self, error))
        return FALSE;

    //Trigger
    if(!fu_raydiumtp_hid_device_bl_trig_pram_to_flash(self, error))
        return FALSE;

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_write_descimage(FuRaydiumtpHidDevice *self,
                                        guint8 *img,
                                        guint32 base_addr,
                                        guint32 img_length,
                                        guint32 image_crc,
                                        FuProgress *progress,
                                        GError **error)
{
    guint8 Sector = (guint8)(img_length / FLASH_SECTOR_SIZE);

    //Write Data
    if(!fu_raydiumtp_hid_device_bl_write_flash(self, img, img_length, error))
        return FALSE;

    //Check Data
    if(!fu_raydiumtp_hid_device_bl_dma_crc(self, FU_RAYDIUMTP_RAM_FIRM_BASE, img_length - CRC_LEN, image_crc, error))
        return FALSE;

    //Erase Flash
    if(!fu_raydiumtp_hid_device_bl_erase_flash_sector(self, base_addr, Sector, error))
        return FALSE;

    //Trigger
    if(!fu_raydiumtp_hid_device_bl_trig_desc_to_flash(self, FU_RAYDIUMTP_RAM_FIRM_BASE, base_addr, img_length, error))
        return FALSE;

    return TRUE;
}


static gboolean
raydiumtp_read_flash_crc(FuRaydiumtpHidDevice *self,
                         guint32 base_addr,
                         guint32 length,
                         guint8 out_crc[4],
                         GError **error)
{
    guint8 rdata[5] = {0};
    guint8 buf[I2C_BUF_SIZE] = {0};
    guint32 addr = base_addr + length - CRC_LEN;
    guint16 crc_length = CRC_LEN;

    if (length < CRC_LEN)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
                    "Component length %u smaller than CRC_LEN %u", length, (guint32)CRC_LEN);
        return FALSE;
    }

    buf[6] = (guint8) addr;
    buf[7] = (guint8) (addr >> 8);
    buf[8] = (guint8) (addr >> 16);
    buf[9] = (guint8) (addr >> 24);
    buf[10] = (guint8) crc_length;
    buf[11] = (guint8) (crc_length >> 8);
    buf[12] = (guint8) (crc_length >> 16);
    buf[13] = (guint8) (crc_length >> 24);

    if (!fu_raydiumtp_hid_device_write_boot(self, FU_RAYDIUMTP_CMD_BL_CMD_READFLASHADDR, buf, HIDI2C_WRITE_MAX_LENGTH, error))
        return FALSE;

    rdata[0] = FU_RAYDIUMTP_CMD2_READ;

    if (!fu_raydiumtp_hid_device_read_boot(self, rdata, sizeof(rdata), error))
        return FALSE;

    return fu_memcpy_safe(out_crc,
                          CRC_LEN,
                          0,
                          rdata,
                          sizeof(rdata),
                          0,
                          CRC_LEN,
                          error);
}

static gboolean
raydiumtp_extract_components(const guint8 *bin_data,
                             gsize bin_size,
                             guint32 image_start,
                             guint32 image_length,
                             guint8 *out_buf,
                             GError **error)
{
    if (bin_data == NULL || bin_size == 0)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "Firmware buffer empty");
        return FALSE;
    }
    if (image_length == 0)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "Invalid component lengths: %u", image_length);
        return FALSE;
    }
    if ((gsize)image_length > bin_size)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
                    "Binary size %zu smaller than %u", bin_size, image_length);
        return FALSE;
    }

    return fu_memcpy_safe(out_buf,
                          image_length,
                          0,				/* dst */
                          bin_data,
                          bin_size,
                          image_start,	/* src */
                          image_length,
                          error);
}

static gboolean
raydiumtp_compare_crc(const guint8 flash_crc[4], const guint8 image_crc[4])
{
    return memcmp(flash_crc, image_crc, CRC_LEN) == 0;
}

static gboolean
fu_raydiumtp_hid_device_verify_status(FuRaydiumtpHidDevice *self,
                                      FuFirmware *firmware,
                                      guint32 fw_start,
                                      guint32 fw_length,
                                      GError **error)
{
    guint8 rdata[4] = {0};
    g_autoptr(GBytes) fw_bytes = NULL;
    const guint8 *fw_data = NULL;
    gsize fw_size = 0;
    guint32 pram_lock_val = 0;
    guint32 image_fw_crc = 0;
    guint32 device_fw_crc = 0;

    fw_bytes = fu_firmware_get_bytes(firmware, error);
    if (fw_bytes == NULL)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "Firmware has no data");
        return FALSE;
    }

    fw_data = g_bytes_get_data(fw_bytes, &fw_size);
    if (fw_data == NULL || fw_size == 0)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "Firmware buffer empty");
        return FALSE;
    }

    image_fw_crc = (guint32)(fw_data[fw_start + fw_length - 4]) |
                   (guint32)(fw_data[fw_start + fw_length - 3] << 8) |
                   (guint32)(fw_data[fw_start + fw_length - 2] << 16) |
                   (guint32)(fw_data[fw_start + fw_length - 1] << 24);

    if (!fu_raydiumtp_hid_device_set_mem_addr(self, FU_RAYDIUMTP_FLASH_CTRL_PRAM_LOCK, MCU_MEM, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_get_mem_read(self, rdata, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_set_mem_addr(self, FU_RAYDIUMTP_FLASH_CTRL_PRAM_LOCK, MCU_MEM, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_set_mem_write(self, pram_lock_val, error))
        return FALSE;

    pram_lock_val = ((guint32)rdata[0]) | ((guint32)rdata[1] << 8) | ((guint32)rdata[2] << 16) | ((guint32)rdata[3] << 24);
    pram_lock_val = (pram_lock_val & (~0x00000004));

    if (!fu_raydiumtp_hid_device_set_mem_addr(self, FU_RAYDIUMTP_FLASH_FIRMCRC_ADDR, MCU_MEM, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_get_mem_read(self, rdata, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_set_mem_addr(self, FU_RAYDIUMTP_FLASH_CTRL_PRAM_LOCK, MCU_MEM, error))
        return FALSE;

    if (!fu_raydiumtp_hid_device_set_mem_write(self, pram_lock_val, error))
        return FALSE;

    device_fw_crc = ((guint32)rdata[0]) | ((guint32)rdata[1] << 8) | ((guint32)rdata[2] << 16) | ((guint32)rdata[3] << 24);

    if (device_fw_crc != image_fw_crc)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
                    "CRC check failed: device=0x%08x image=0x%08x", device_fw_crc, image_fw_crc);
        return FALSE;
    }

    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_write_images(FuRaydiumtpHidDevice *self,
                                     FuFirmware *firmware,
                                     guint32 fw_base,
                                     guint32 desc_base,
                                     guint32 fw_start,
                                     guint32 fw_length,
                                     guint32 desc_start,
                                     guint32 desc_length,
                                     FuProgress *progress,
                                     GError **error)
{
    gboolean update_fw = TRUE, update_desc = TRUE;
    guint8 flash_fw_crc[4] = {0}, flash_desc_crc[4] = {0};
    guint8 image_fw_crc[4] = {0}, image_desc_crc[4] = {0};

    guint32 target_crc = 0;

    g_autoptr(GBytes) bin_bytes = NULL;
    gsize bin_size = 0;
    const guint8 *bin_data = NULL;
    g_autofree guint8 *PRAM = NULL;
    g_autofree guint8 *DESC = NULL;

    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 	5, "prepare-write");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 	5, "erase");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE,  90, "writing");

    bin_bytes = fu_firmware_get_bytes(firmware, error);
    if (bin_bytes == NULL)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "Firmware has no data");
        return FALSE;
    }

    bin_data = g_bytes_get_data(bin_bytes, &bin_size);

    if (fw_length > CRC_LEN)
    {
        PRAM = g_malloc(fw_length);
        if (!raydiumtp_extract_components(bin_data, bin_size, fw_start, fw_length, PRAM, error))
            update_fw = FALSE;
        else
        {
            if (!fu_memcpy_safe(image_fw_crc,
                                sizeof(image_fw_crc),
                                0,
                                PRAM,
                                fw_length,
                                fw_length - CRC_LEN,
                                CRC_LEN,
                                error))
                return FALSE;
        }
    }

    if (desc_length > CRC_LEN)
    {
        DESC = g_malloc(desc_length);
        if (!raydiumtp_extract_components(bin_data, bin_size, desc_start, desc_length, DESC, error))
            update_desc = FALSE;
        else
        {
            if (!fu_memcpy_safe(image_desc_crc,
                                sizeof(image_desc_crc),
                                0,
                                DESC,
                                desc_length,
                                desc_length - CRC_LEN,
                                CRC_LEN,
                                error))
                return FALSE;
        }
    }

    fu_progress_step_done(progress);

    if (!fu_raydiumtp_hid_device_bl_dis_wdt_and_unlock_flash(self, error))
    {
        g_set_error(error,
                    FWUPD_ERROR,
                    FWUPD_ERROR_INTERNAL,
                    "Unlock Flash Failed.");
        return FALSE;
    }

    if (fu_raydiumtp_hid_device_read_flash_protect_status(self, error) != FU_RAYDIUMTP_PROTECT_FWUNLOCK)
    {
        if(!fu_raydiumtp_hid_device_unlock_flash_protect(self, 0, error))
        {
            g_set_error(error,
                        FWUPD_ERROR,
                        FWUPD_ERROR_INTERNAL,
                        "Unlock Flash Protect Failed.");
            return FALSE;
        }

        if (fu_raydiumtp_hid_device_read_flash_protect_status(self, error) != FU_RAYDIUMTP_PROTECT_FWUNLOCK)
        {
            g_set_error(error,
                        FWUPD_ERROR,
                        FWUPD_ERROR_INTERNAL,
                        "Unlock Flash Protect Check Failed.");
            return FALSE;
        }
    }

    if (update_fw)
    {
        if (!raydiumtp_read_flash_crc(self, fw_base, fw_length, flash_fw_crc, error))
            update_fw = FALSE;
        else
            update_fw = !raydiumtp_compare_crc(flash_fw_crc, image_fw_crc);
    }

    if (update_desc)
    {
        if (!raydiumtp_read_flash_crc(self, desc_base, desc_length, flash_desc_crc, error))
            update_desc = FALSE;
        else
            update_desc = !raydiumtp_compare_crc(flash_desc_crc, image_desc_crc);
    }

    fu_progress_step_done(progress);

    if (update_desc)
    {
        target_crc = (guint32)(image_desc_crc[0]) | (guint32)(image_desc_crc[1] << 8) | (guint32)(image_desc_crc[2] << 16) | (guint32)(image_desc_crc[3] << 24);
        if (!fu_raydiumtp_hid_device_write_descimage(self,
                DESC,
                desc_base,
                desc_length,
                target_crc,
                fu_progress_get_child(progress),
                error))
        {
            g_set_error(error,
                        FWUPD_ERROR,
                        FWUPD_ERROR_INTERNAL,
                        "Update Desc Failed.");
            return FALSE;
        }
    }

    if (update_fw)
    {
        target_crc = (guint32)(image_fw_crc[0]) | (guint32)(image_fw_crc[1] << 8) | (guint32)(image_fw_crc[2] << 16) | (guint32)(image_fw_crc[3] << 24);
        if (!fu_raydiumtp_hid_device_write_fwimage(self,
                PRAM,
                fw_base,
                fw_length,
                target_crc,
                fu_progress_get_child(progress),
                error))
        {
            g_set_error(error,
                        FWUPD_ERROR,
                        FWUPD_ERROR_INTERNAL,
                        "Update Firmware Failed.");
            return FALSE;
        }
    }

    if (fu_raydiumtp_hid_device_read_flash_protect_status(self, error) != FU_RAYDIUMTP_PROTECT_ALLOCK)
    {
        if(!fu_raydiumtp_hid_device_lock_flash_protect(self, error))
        {
            g_set_error(error,
                        FWUPD_ERROR,
                        FWUPD_ERROR_INTERNAL,
                        "Lock Flash Protect Failed.");
            return FALSE;
        }

        if (fu_raydiumtp_hid_device_read_flash_protect_status(self, error) != FU_RAYDIUMTP_PROTECT_ALLOCK)
        {
            g_set_error(error,
                        FWUPD_ERROR,
                        FWUPD_ERROR_INTERNAL,
                        "Lock Flash Protect Check Failed.");
            return FALSE;
        }
    }
    fu_progress_step_done(progress);

    return TRUE;
}

static FuFirmware *
fu_raydiumtp_hid_device_prepare_firmware(FuDevice *device,
        GInputStream *stream,
        FuProgress *progress,
        FuFirmwareParseFlags flags,
        GError **error)
{
    g_autoptr(FuFirmware) firmware = fu_raydiumtp_firmware_new();

    if (stream == NULL)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
                    "input stream is NULL");
        return NULL;
    }

    if (!fu_raydiumtp_firmware_parse(
                FU_RAYDIUMTP_FIRMWARE(firmware),
                stream,
                fu_device_get_pid(device),
                error))
        return NULL;
    /* success */
    return g_steal_pointer(&firmware);
}

static gboolean
fu_raydiumtp_hid_device_write_firmware(FuDevice *device,
                                       FuFirmware *firmware,
                                       FuProgress *progress,
                                       FwupdInstallFlags flags,
                                       GError **error)
{
    FuRaydiumtpHidDevice *self = FU_RAYDIUMTP_HID_DEVICE(device);
    FuRaydiumtpFirmware *ray_fw = FU_RAYDIUMTP_FIRMWARE(firmware);
    guint32 fw_base = fu_raydiumtp_firmware_get_fw_base(ray_fw);
    guint32 desc_base = fu_raydiumtp_firmware_get_desc_base(ray_fw);
    guint32 fw_start = fu_raydiumtp_firmware_get_fw_start(ray_fw);
    guint32 fw_len = fu_raydiumtp_firmware_get_fw_len(ray_fw);
    guint32 desc_start = fu_raydiumtp_firmware_get_desc_start(ray_fw);
    guint32 desc_len = fu_raydiumtp_firmware_get_desc_len(ray_fw);

    if (firmware == NULL)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
                    "firmware object is NULL");
        return FALSE;
    }
    if (progress == NULL)
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
                    "missing progress object");
        return FALSE;
    }

    if (!FU_IS_RAYDIUMTP_FIRMWARE(firmware))
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                    "unsupported firmware type");
        return FALSE;
    }

    if (!fu_raydiumtp_check_pid(device, ray_fw) || !fu_raydiumtp_check_vid(device, ray_fw))
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
                    "firmware mismatch");
        return FALSE;
    }

    /* progress definition*/
    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "prepare");
    fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 90, "download");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reload");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, "verify");

    if (!fu_raydiumtp_hid_device_update_prepare(self, error))
        return FALSE;

    fu_progress_step_done(progress);

    if (!fu_raydiumtp_hid_device_write_images(self,
            firmware,
            fw_base,
            desc_base,
            fw_start,
            fw_len,
            desc_start,
            desc_len,
            fu_progress_get_child(progress),
            error))
        return FALSE;

    fu_progress_step_done(progress);

    /* reset IC */
    if (!fu_raydiumtp_hid_device_bl_software_reset(self, NULL))
        return FALSE;

    fu_progress_step_done(progress);

    if (!fu_raydiumtp_hid_device_verify_status(self,
            firmware,
            fw_start,
            fw_len,
            error))
    {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
                    "update firmware unsuccessful");
        return FALSE;
    }

    fu_progress_step_done(progress);

    return TRUE;
}

/***********************************

	Initial Device

************************************/
static void
fu_raydiumtp_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
    fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gboolean
fu_raydiumtp_hid_device_probe(FuDevice *device, GError **error)
{
    if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0)
    {
        g_set_error(error,
                    FWUPD_ERROR,
                    FWUPD_ERROR_NOT_SUPPORTED,
                    "Incorrect subsystem=%s, expected hidraw",
                    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
        return FALSE;
    }

    /* success */
    return TRUE;
}

static gboolean
fu_raydiumtp_hid_device_setup(FuDevice *device, GError **error)
{
    FuRaydiumtpHidDevice *self = FU_RAYDIUMTP_HID_DEVICE(device);

    if (!fu_raydiumtp_hid_device_read_firmware_info(self, error))
    {
        g_prefix_error_literal(error, "read firmware information failed");
        return FALSE;
    }

    return TRUE;
}

static gchar *
fu_raydiumtp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
    return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_raydiumtp_hid_device_init(FuRaydiumtpHidDevice *self)
{
    fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
    fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
    fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
    fu_device_set_summary(FU_DEVICE(self), "Touchpad");
    fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
    fu_device_add_protocol(FU_DEVICE(self), "com.raydium.raydiumtp");
    fu_device_set_name(FU_DEVICE(self), "Touch Controller Sensor");
    fu_device_set_vendor(FU_DEVICE(self), "Raydium");
    fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
    fu_device_set_priority(FU_DEVICE(self), 1);
    fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
    fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
    fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_raydiumtp_hid_device_finalize(GObject *object)
{
    G_OBJECT_CLASS(fu_raydiumtp_hid_device_parent_class)->finalize(object);
}

static void
fu_raydiumtp_hid_device_class_init(FuRaydiumtpHidDeviceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

    object_class->finalize = fu_raydiumtp_hid_device_finalize;
    device_class->to_string = fu_raydiumtp_hid_device_to_string;

    device_class->probe = fu_raydiumtp_hid_device_probe;

    device_class->setup = fu_raydiumtp_hid_device_setup;
    device_class->reload = fu_raydiumtp_hid_device_setup;

    device_class->prepare_firmware = fu_raydiumtp_hid_device_prepare_firmware;
    device_class->write_firmware = fu_raydiumtp_hid_device_write_firmware;

    device_class->set_progress = fu_raydiumtp_hid_device_set_progress;
    device_class->convert_version = fu_raydiumtp_hid_device_convert_version;
}

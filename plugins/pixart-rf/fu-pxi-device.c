/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"
#include "fu-pxi-device.h"
#include "fu-pxi-device-ota.h"
#include "fu-common.h"
#include "fu-pxi-device-common.h"
#include "fu-udev-device.h"
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uhid.h>


#define PXI_HID_DEV_OTA_INPUT_REPORT_ID 0x05
#define PXI_HID_DEV_OTA_OUTPUT_REPORT_ID 0x06
#define PXI_HID_DEV_OTA_FEATURE_REPORT_ID 0x07

G_DEFINE_TYPE (FuPxiDevice, fu_pxi_device, FU_TYPE_UDEV_DEVICE)


static FuFirmware *
fu_pxi_device_prepare_firmware (FuDevice *device,
                                GBytes *fw,
                                FwupdInstallFlags flags,
                                GError **error)
{
    g_autoptr(FuFirmware) firmware = fu_firmware_new ();


    guint32 idx = 0;

    gsize fw_sz = g_bytes_get_size(fw);
    guint8 pos = 0;
    gchar version[5] = {'.', '.', '.', '.', '.'};
    const guint8 *fw_ptr = g_bytes_get_data(fw, &fw_sz);

    if (!fu_firmware_parse (firmware, fw, flags, error))
        return NULL;

    fu_common_dump_raw (G_LOG_DOMAIN, "fw last 32 bytes", &fw_ptr[fw_sz - 32], 32);

    /* find the version tag */
    for(idx = 0; idx < 32; idx++)
    {
        if(fw_ptr[(fw_sz - 32) + idx ] == 'v')
        {
            pos = idx;
            break;
        }
    }

    /* ensure the version */
    if((pos != 31) && (fw_ptr[(fw_sz - 32) + pos + 1] == '_'))
    {
        g_debug("version in bin");
        version[0] = fw_ptr[(fw_sz - 32) + pos + 2];
        version[2] = fw_ptr[(fw_sz - 32) + pos + 4];
        version[4] = fw_ptr[(fw_sz - 32) + pos + 6];
        fu_firmware_set_version(firmware, version);
    }
    else
    {
        /* set the default version if can not find it in fw bin */
        fu_firmware_set_version(firmware, "1.0.0");
    }

    g_debug("firmware ver %s", fu_firmware_get_version(firmware));

    return g_steal_pointer (&firmware);
}

static gboolean
fu_pxi_device_write_firmware (FuDevice *device,
                              FuFirmware *firmware,
                              FwupdInstallFlags flags,
                              GError **error)
{


    g_autoptr(GBytes) fw = NULL;
    g_autoptr(GArray) fw_check_sum_table = g_array_new (FALSE, TRUE, sizeof (gushort));
    const guint8 *fw_ptr;
    const gchar *fw_ver_ptr;
    gsize fw_sz = 0;
    gushort checksum = 0;
    gushort fw_bin_checksum = 0;
    guint32 idx = 0;

    guint8 buf[PXI_OTA_BUF_SZ];

    guint16 write_sz = 0;
    guint32 offset = 0;
    guint32 prn = 0;
    guint16 object_size = 0;

    struct cmd_fw_ota_init_new fw_ota_init_new;
    struct cmd_fw_object_create fw_object_create;
    struct cmd_fw_upgrade fw_upgrade;
    struct cmd_fw_ota_disconnect fw_ota_disconnect;
    struct hci_evt evt;
    struct ret_fw_notify fw_notify_result;



    /* get the default image */
    fw = fu_firmware_get_image_default_bytes (firmware, error);
    if(fw == NULL)
        return FALSE;


    fw_sz = g_bytes_get_size(fw);
    fw_ptr = g_bytes_get_data(fw, &fw_sz);


    /* calculate fw checksum */
    fu_pxi_device_calculate_checksum(&fw_bin_checksum, fw_sz, fw_ptr);

    g_debug("fw bin checksum %x, fw_sz %lu", fw_bin_checksum, fw_sz);
    g_debug("firmware ver %s", fu_firmware_get_version(firmware));

    /* bulid firmware checksum table */
    for(idx = 0 ; idx < fw_sz / MAX_OBJECT_SIZE  ; idx++)
    {

        fu_pxi_device_calculate_checksum(&checksum, MAX_OBJECT_SIZE, &fw_ptr[idx * MAX_OBJECT_SIZE]);
        g_array_append_val (fw_check_sum_table, checksum);
        g_debug("idx %u checksum %x checksum in table %x", idx, checksum, g_array_index (fw_check_sum_table, gushort, idx));

    }

    g_array_append_val (fw_check_sum_table, fw_bin_checksum);

    fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);

    /* initalize fw ota init new command */
    memset((guint8*)&fw_ota_init_new, 0, sizeof(struct cmd_fw_ota_init_new));

    fw_ota_init_new.fw_length = fw_sz;
    fw_ota_init_new.ota_setting = 0x00;



    /* send fw ota init command */
    buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
    buf[1] = CMD_FW_OTA_INIT;
    fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, buf, 2, error);


    /* send fw ota init new command */
    buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
    buf[1] = CMD_FW_OTA_INIT_NEW;
    fu_memcpy_safe(buf, PXI_OTA_BUF_SZ, 2, (guint8*)&fw_ota_init_new, sizeof(struct cmd_fw_ota_init_new), 0, sizeof(struct cmd_fw_ota_init_new), error);
    fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, buf, sizeof(struct cmd_fw_ota_init_new) + 2, error);

    /* delay for read command*/
    g_usleep(30000);


    /* read fw ota init new command */
    buf[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
    buf[1] = CMD_FW_OTA_INIT_NEW;
    fu_pxi_device_get_feature (device, (guint8 *)buf, 20, error);
    fu_memcpy_safe((guint8 *)&evt, sizeof(struct hci_evt), 0, buf,PXI_OTA_BUF_SZ, 0, sizeof(struct hci_evt), error);



    /* write fw into device */
    idx = 0;

    do
    {
        g_clear_error (error);
        object_size = fw_sz > MAX_OBJECT_SIZE ? MAX_OBJECT_SIZE : fw_sz ;

        /* send create fw object command */
        buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
        buf[1] = CMD_FW_OBJECT_CREATE;
        fw_object_create.fw_addr = idx * MAX_OBJECT_SIZE;
        fw_object_create.object_size =  MAX_OBJECT_SIZE;
        fu_memcpy_safe(buf, PXI_OTA_BUF_SZ, 2, (guint8*)&fw_object_create, sizeof(struct cmd_fw_object_create), 0, sizeof(struct cmd_fw_object_create), error);
        fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, buf, sizeof(struct cmd_fw_object_create) + 2, error);

        memset(buf, 0,PXI_OTA_BUF_SZ);
        buf[0] = PXI_HID_DEV_OTA_INPUT_REPORT_ID;
        fu_udev_device_pread_full (FU_UDEV_DEVICE (device), 0, buf, PXI_OTA_BUF_SZ,error);
        g_debug("CMD_FW_OBJECT_CREATE %u",offset);


        fw_sz = fw_sz - object_size;
        g_usleep(30000);
        fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
        while(object_size > 0)
        {

            write_sz = object_size > PXI_OTA_PAYLOAD ? PXI_OTA_PAYLOAD : object_size ;
            memset(buf, 0,PXI_OTA_BUF_SZ);
            buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
            fu_memcpy_safe(buf, PXI_OTA_BUF_SZ, 1, (guint8*)&fw_ptr[offset],write_sz, 0, write_sz, error);

            fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, buf, write_sz + 1, error);
            g_debug("object_size %u write_sz %u ,offset %u", object_size, write_sz, offset);


            offset = offset + write_sz;
            object_size = object_size - write_sz;
            prn++;
            fu_device_set_progress_full(device, (gsize)offset, g_bytes_get_size(fw));
            if(prn >= evt.evt_param.ret_param.fw_ota_init_new.prn_threshold)
            {
                g_debug("wait response");
                g_clear_error (error);
                memset(buf, 0,PXI_OTA_BUF_SZ);

                buf[0] = PXI_HID_DEV_OTA_INPUT_REPORT_ID;


                fu_udev_device_pread_full (FU_UDEV_DEVICE (device), 1, buf, PXI_OTA_BUF_SZ, error);

                fu_memcpy_safe((guint8 *)&fw_notify_result, sizeof(struct ret_fw_notify),0, buf, PXI_OTA_BUF_SZ,1, sizeof(struct ret_fw_notify), error);

                if(fw_notify_result.opcode != CMD_FW_WRITE)
                {
                    g_set_error (error, FWUPD_ERROR,FWUPD_ERROR_READ,"FWUPD_ERROR_READ %d",CMD_FW_WRITE);
                    return FALSE;
                }
                prn = 0;
            }


            if((object_size == 0) && (offset != g_bytes_get_size(fw)))
            {

                g_debug("object clear");
                g_clear_error (error);

                memset(buf, 0, PXI_OTA_BUF_SZ);

                buf[0] = PXI_HID_DEV_OTA_INPUT_REPORT_ID;

                fu_udev_device_pread_full (FU_UDEV_DEVICE (device), 0, buf, PXI_OTA_BUF_SZ,error);

                fu_memcpy_safe((guint8 *)&fw_notify_result, sizeof(struct ret_fw_notify),0, buf, PXI_OTA_BUF_SZ,1, sizeof(struct ret_fw_notify), error);

                g_debug("checksum %x ,table checksum %x", fw_notify_result.checksum,g_array_index (fw_check_sum_table, gushort, idx) );

                if(fw_notify_result.checksum != g_array_index (fw_check_sum_table, gushort, idx))
                {
                    g_set_error (error, FWUPD_ERROR,FWUPD_ERROR_READ,"checksum fail %d",CMD_FW_WRITE);
                    return FALSE;
                }
                prn = 0;
                break;

            }

        }


        idx++;



    }
    while( fw_sz > 0 );

    fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);

    /* prepare send fw updgrad command */
    g_clear_error (error);
    memset(buf, 0,PXI_OTA_BUF_SZ);
    buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
    buf[1] = CMD_FW_UPGRADE;

    fw_ver_ptr = fu_firmware_get_version(firmware);

    /* set fw updgrad info */
    fw_upgrade.sz = g_bytes_get_size(fw);
    fw_upgrade.checksum = fw_bin_checksum;

    fw_upgrade.version[0] = fw_ver_ptr[0];
    fw_upgrade.version[1] = '.';
    fw_upgrade.version[2] = fw_ver_ptr[2];
    fw_upgrade.version[3] = '.';
    fw_upgrade.version[4] = fw_ver_ptr[4];

    /* send fw updgrad command */
    g_debug("send fw updgrad command");
    fu_memcpy_safe(buf, PXI_OTA_BUF_SZ, 2, (guint8*)&fw_upgrade,sizeof(struct cmd_fw_upgrade), 0, sizeof(struct cmd_fw_upgrade), error);
    fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, buf, sizeof(struct cmd_fw_upgrade) + 2, error);

    /* read fw updgrad command result */
    g_clear_error (error);
    memset(buf, 0,PXI_OTA_BUF_SZ);
    buf[0] = PXI_HID_DEV_OTA_INPUT_REPORT_ID;

    fu_udev_device_pread_full (FU_UDEV_DEVICE (device), 1, buf, PXI_OTA_BUF_SZ,error);
    fu_common_dump_raw (G_LOG_DOMAIN, "buf", (guint8 *)buf, PXI_OTA_BUF_SZ);
    fu_memcpy_safe((guint8 *)&fw_notify_result, sizeof(struct ret_fw_notify), 0, buf,PXI_OTA_BUF_SZ,1, sizeof(struct ret_fw_notify), error);

    if(fw_notify_result.opcode != CMD_FW_UPGRADE)
    {
        g_set_error (error, FWUPD_ERROR,FWUPD_ERROR_READ, "FWUPD_ERROR_READ %d", CMD_FW_WRITE);
        return FALSE;
    }

    fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
    /* send device reset command */
    g_clear_error (error);
    memset(buf, 0,PXI_OTA_BUF_SZ);
    buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
    buf[1] = CMD_FW_MCU_RESET;

    fw_ota_disconnect.reason = g_bytes_get_size(fw);
    fu_memcpy_safe(buf, PXI_OTA_BUF_SZ, 2, (guint8*)&fw_ota_disconnect, sizeof(struct cmd_fw_ota_disconnect), 0, sizeof(struct cmd_fw_ota_disconnect), error);
    fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, buf, sizeof(struct cmd_fw_ota_disconnect) + 2, error);


    return TRUE;


}


static gboolean
fu_pxi_device_probe (FuDevice *device, GError **error)
{
    /* set the physical ID */
    if (!fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "hid", error))
        return FALSE;

    return TRUE;
}

static gboolean
fu_pxi_device_setup (FuDevice *device, GError **error)
{


    guint8 buf[64];

    g_autofree gchar *current_version = NULL;

    struct hci_evt evt;


    buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
    buf[1] = CMD_FW_OTA_INIT;
    fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, (guint8 *)buf, 2, error);


    buf[0] = PXI_HID_DEV_OTA_OUTPUT_REPORT_ID;
    buf[1] = CMD_FW_GET_INFO;
    fu_udev_device_pwrite_full (FU_UDEV_DEVICE (device), 0, (guint8 *)buf, 2, error);


    buf[0] = PXI_HID_DEV_OTA_FEATURE_REPORT_ID;
    buf[1] = CMD_FW_GET_INFO;
    fu_pxi_device_get_feature (device, buf, 64, error);

    fu_common_dump_raw (G_LOG_DOMAIN, "buf", (guint8 *)buf, 64);

    fu_memcpy_safe((guint8 *)&evt, sizeof(struct hci_evt), 0, buf, 64, 0, sizeof(struct hci_evt), error);

    if(evt.evt_param.opcode != CMD_FW_GET_INFO)
        return FALSE;


    current_version = g_strndup ((gchar*)&evt.evt_param.ret_param.fw_info_get.version, 5);

    g_debug("checksum %x",evt.evt_param.ret_param.fw_info_get.checksum);
    g_debug("version %s",(gchar*)evt.evt_param.ret_param.fw_info_get.version);
    g_debug("current_version %s",current_version);
    fu_device_set_version (device, current_version);

    fu_device_add_checksum (device, g_strdup_printf("0x%4x",evt.evt_param.ret_param.fw_info_get.checksum));

    return TRUE;
}


static void
fu_pxi_device_init (FuPxiDevice *self)
{

    fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
    fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
    fu_device_set_vendor_id(FU_DEVICE (self),"USB:0x093A");
    fu_device_set_protocol (FU_DEVICE (self), "com.pixart.rf");


}



static void
fu_pxi_device_class_init (FuPxiDeviceClass *klass)
{

    FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
    klass_device->probe = fu_pxi_device_probe;
    klass_device->setup = fu_pxi_device_setup;
    klass_device->write_firmware = fu_pxi_device_write_firmware;
    klass_device->prepare_firmware = fu_pxi_device_prepare_firmware;

}


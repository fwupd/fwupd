/*
 * Copyright (C) 2024 Mario Limonciello <superm1@gmail.com
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/* defaults changed here will also be reflected in the fwupd.conf man page */
#define FU_DAEMON_CONFIG_DEFAULT_UPDATE_MOTD	       TRUE
#define FU_DAEMON_CONFIG_DEFAULT_IGNORE_POWER	       FALSE
#define FU_DAEMON_CONFIG_DEFAULT_ONLY_TRUSTED	       TRUE
#define FU_DAEMON_CONFIG_DEFAULT_SHOW_DEVICE_PRIVATE   TRUE
#define FU_DAEMON_CONFIG_DEFAULT_ALLOW_EMULATION       FALSE
#define FU_DAEMON_CONFIG_DEFAULT_ENUMERATE_ALL_DEVICES TRUE
#define FU_DAEMON_CONFIG_DEFAULT_TRUSTED_UIDS	       NULL
#define FU_DAEMON_CONFIG_DEFAULT_HOST_BKC	       NULL
#define FU_DAEMON_CONFIG_DEFAULT_RELEASE_DEDUPE	       TRUE
#define FU_DAEMON_CONFIG_DEFAULT_TEST_DEVICES	       FALSE
#define FU_DAEMON_CONFIG_DEFAULT_DISABLED_PLUGINS      ""
#define FU_DAEMON_CONFIG_DEFAULT_URI_SCHEMES	       "file;https;http;ipfs"
#define FU_DAEMON_CONFIG_DEFAULT_TRUSTED_REPORTS       "VendorId=$OEM"
#define FU_DAEMON_CONFIG_DEFAULT_RELEASE_PRIORITY      "local"
#define FU_DAEMON_CONFIG_DEFAULT_IDLE_TIMEOUT	       7200

#define FWUPD_REMOTE_CONFIG_DEFAULT_REFRESH_INTERVAL 86400 /* 24h */

#define FU_THUNDERBOLT_CONFIG_DEFAULT_MINIMUM_KERNEL_VERSION "4.13.0"
#define FU_THUNDERBOLT_CONFIG_DEFAULT_DELAYED_ACTIVATION     FALSE

#define FU_MSR_CONFIG_DEFAULT_MINIMUM_SME_KERNEL_VERSION "5.18.0"

#define FU_REDFISH_CONFIG_DEFAULT_MANAGER_RESET_TIMEOUT	   "1800"
#define FU_REDFISH_CONFIG_DEFAULT_CA_CHECK		   FALSE
#define FU_REDFISH_CONFIG_DEFAULT_IPMI_DISABLE_CREATE_USER FALSE

#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_GRUB_CHAIN_LOAD	      FALSE
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_SHIM_FOR_SECURE_BOOT   FALSE
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_REQUIRE_ESP_FREE_SPACE	      "0" /* in MB */
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_CAPSULE_UPDATE_ON_DISK FALSE
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_EFI_DEBUGGING	      FALSE
#define FU_UEFI_CAPSULE_CONFIG_DEFAULT_REBOOT_CLEANUP		      TRUE

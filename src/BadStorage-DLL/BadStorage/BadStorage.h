// Copyright © Eaton Works 2025. All rights reserved.
// License: https://github.com/EatonZ/BadStorage/blob/main/LICENSE

#pragma once

#define BADSTORAGE_VERSION 1 //Keep this in sync with BadStorage.xml and BASE_VER & UPDATE_VER environment variables.
#define BSTOR_INDICATOR    0x4253544F52414745 //FATXplorer adds "BSTORAGE" to offset 0x858 in the boot sector so it can properly identify this type of storage device.
#define BADSTORAGE_LOG_PATH "GAME:\\BadStorage.log" //Every Print of an execution is saved here (overwritten each run), since there is usually no debugger attached this early.

/*
	The RDFC patch updates FatxProcessBootSector to always use a value of 1 for the root directory first cluster.
	FATXplorer formats Bad Storage Content Partitions with a value of 0, which will cause the retail kernel to think the root directory is corrupt. This is important because we don't want the retail kernel to add anything to the partition before Bad Storage is executed.
	The original design was to use a larger sectors per cluster to get the retail kernel to think the partition is "Unformatted" but this resulted in the system not loading the avatar partitions.

	The DID patch temporarily overrides XContent::DeviceAssignDeviceId to give the HDD its original device ID value (1) when XContent::DeviceProcessAddRemove (add) is called.
	This is important to fix the issue where the avatar update would not install.
*/
#ifdef _XDK
#define BADSTORAGE_SUPPORTED_FLASH_VERSION 17489

#define BADSTORAGE_FATX_RDFC_PATCH_OFFSET 0x800D46B0
//li %r27, 1
const UCHAR BADSTORAGE_FATX_RDFC_PATCH_DATA[4] = { 0x3B, 0x60, 0x00, 0x01 };

#define BADSTORAGE_DID_PATCH_OFFSET 0x8172DE54
//li %r30, 1
//b (0x8172DEBC - 0x8172DE58)
const UCHAR BADSTORAGE_DID_PATCH_DATA[8] = { 0x3B, 0xC0, 0x00, 0x01, 0x48, 0x00, 0x00, 0x64 };
#else
#define BADSTORAGE_SUPPORTED_FLASH_VERSION 17559

#define BADSTORAGE_FATX_RDFC_PATCH_OFFSET 0x800A5FAC
//li %r28, 1
const UCHAR BADSTORAGE_FATX_RDFC_PATCH_DATA[4] = { 0x3B, 0x80, 0x00, 0x01 };

#define BADSTORAGE_DID_PATCH_OFFSET 0x8167B3DC
//li %r30, 1
//b (0x8167B444 - 0x8167B3E0)
const UCHAR BADSTORAGE_DID_PATCH_DATA[8] = { 0x3B, 0xC0, 0x00, 0x01, 0x48, 0x00, 0x00, 0x64 };
#endif

const PCHAR PHYSICAL_DISK_PATH =    "\\Device\\Harddisk0\\PhysicalDisk";
const PCHAR PARTITION_0_PATH =      "\\Device\\Harddisk0\\Partition0";
const PCHAR PARTITION_1_PATH =      "\\Device\\Harddisk0\\Partition1";
const PCHAR PARTITION_1_PATH_FILE = "\\Device\\Harddisk0\\Partition1\\";
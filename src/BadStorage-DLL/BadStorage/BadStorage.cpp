// Copyright © Eaton Works 2025. All rights reserved.
// License: https://github.com/EatonZ/BadStorage/blob/main/LICENSE

#include "stdafx.h"
#include "BadStorage.h"
#include "UnauthDisk.h"

CHAR LogBuffer[0x2000];
ULONG LogLength = 0;

VOID Print(const PCHAR Format, ...)
{
	CHAR formatStr[200];
	va_list args;
	va_start(args, Format);
	vsprintf(formatStr, Format, args);
	va_end(args);

	BOOLEAN am;
	SYSTEMTIME time;
	GetLocalTime(&time);
	if (time.wHour > 12)
	{
		time.wHour = time.wHour - 12;
		am = FALSE;
	}
	else if (time.wHour == 12) am = FALSE;
	else
	{
		time.wHour = time.wHour == 0 ? 12 : time.wHour;
		am = TRUE;
	}

	DbgPrint("[%d/%d/%d %02d:%02d:%02d %s] %s\n", time.wMonth, time.wDay, time.wYear, time.wHour, time.wMinute, time.wSecond, am ? "AM" : "PM", formatStr);

	SIZE_T length = strlen(formatStr);
	if (LogLength + length + 2 <= sizeof(LogBuffer))
	{
		memcpy(LogBuffer + LogLength, formatStr, length);
		LogBuffer[LogLength + length] = '\r';
		LogBuffer[LogLength + length + 1] = '\n';
		LogLength += length + 2;
	}
}

VOID SaveLog()
{
	HANDLE file = CreateFile(BADSTORAGE_LOG_PATH, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return;
	DWORD written;
	WriteFile(file, LogBuffer, LogLength, &written, NULL);
	CloseHandle(file);
}

HANDLE OpenDisk(const PSZ Path, BOOLEAN ReadOnly)
{
	ANSI_STRING str;
	RtlInitAnsiString(&str, Path);

	OBJECT_ATTRIBUTES oa;
	InitializeObjectAttributes(&oa, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);

	HANDLE handle;
	IO_STATUS_BLOCK iosb;
	//For disks, NtOpenFile must be used (CreateFile doesn't work).
	NTSTATUS status = NtOpenFile(&handle, GENERIC_READ | (ReadOnly ? 0 : GENERIC_WRITE) | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
	if (!NT_SUCCESS(status))
	{
		Print("OpenDisk (%s) failed: 0x%08X", Path, status);
		return INVALID_HANDLE_VALUE;
	}
	else return handle;
}

BOOLEAN CheckBSTOR(HANDLE Handle)
{
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER offset;
	offset.QuadPart = 0; //We're always at the boot sector position.
	UCHAR bootSector[0x1000];
	NTSTATUS status = NtReadFile(Handle, NULL, NULL, NULL, &iosb, &bootSector, sizeof(bootSector), &offset);
	if (!NT_SUCCESS(status))
	{
		Print("Failed to read boot sector from disk: 0x%08X", status);
		return FALSE;
	}
	else
	{
		Print("BSTOR format: %s", *(PULONGLONG)(bootSector + 0x858) == BSTOR_INDICATOR ? "yes" : "no");
		return *(PULONGLONG)(bootSector + 0x858) == BSTOR_INDICATOR;
	}
}

//This is just for tesing purposes. Kill the FATX boot sector signature, making it unmountable, but recoverable.
//Remember to open the disk with write access!
BOOLEAN KillBootSector(HANDLE Handle, BOOLEAN Restore)
{
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER offset;
	offset.QuadPart = 0; //We're always at the boot sector position.
	UCHAR bootSector[0x1000];
	NTSTATUS status = NtReadFile(Handle, NULL, NULL, NULL, &iosb, &bootSector, sizeof(bootSector), &offset);
	if (NT_SUCCESS(status))
	{
		*(PULONG)bootSector = Restore ? FAT_VOLUME_SIGNATURE : 0;
		status = NtWriteFile(Handle, NULL, NULL, NULL, &iosb, &bootSector, sizeof(bootSector), &offset);
		if (!NT_SUCCESS(status))
		{
			Print("Failed to write boot sector to disk: 0x%08X", status);
			return FALSE;
		}
		else
		{
			Print("Boot sector %s.", Restore ? "restored" : "killed");
			return TRUE;
		}
	}
	else
	{
		Print("Failed to read boot sector from disk: 0x%08X", status);
		return FALSE;
	}
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    return TRUE;
}

BOOLEAN ExecuteInternal(PBOOLEAN IsRetailFormatted)
{
	Print("Bad Storage");
	Print("Created by Eaton");
	#ifdef _DEBUG
	Print("Version: %d Debug, built: %s %s", BADSTORAGE_VERSION, __DATE__, __TIME__);
	#else
	Print("Version: %d Release, built: %s %s", BADSTORAGE_VERSION, __DATE__, __TIME__);
	#endif

	if (IsRetailFormatted != NULL) *IsRetailFormatted = FALSE;

	BOOLEAN ret = FALSE;
	DWORD statusFlags;
	XeKeysGetStatus(&statusFlags);
	#ifdef _XDK
	if ((statusFlags & 0x40000) != 0x40000)
	{
		Print("Build/console type mismatch.");
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Can't run the XDK version on retail.", 0);
		return FALSE;
	}
	#else
	if ((statusFlags & 0x40000) == 0x40000)
	{
		Print("Build/console type mismatch.");
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Can't run the retail version on XDK.", 0);
		return FALSE;
	}
	#endif

	//We have hardcoded offsets and thus are tied to a specific flash version.
	if (XboxKrnlVersion->Build != BADSTORAGE_SUPPORTED_FLASH_VERSION)
	{
		Print("Kernel version mismatch. Supported: %d, Detected: %d", BADSTORAGE_SUPPORTED_FLASH_VERSION, XboxKrnlVersion->Build);
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Kernel update required.", 0);
		return FALSE;
	}

	if ((XboxHardwareInfo->Flags & XBOX_HW_FLAG_HDD) != XBOX_HW_FLAG_HDD)
	{
		//If this value got filled in, it means there is a disk.
		if (*(PULONG)SataDiskUserAddressableSectors_Offset == 0)
		{
			Print("No disk.");
			XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: No internal disk connected.", 0);
		}
		else
		{
			#ifdef UNAUTH_DISK_SUPPORTED
			//No security sector: initialize and announce the disk ourselves (see UnauthDisk.h).
			//On a later execution in the same boot the HDD flag is set, so the regular path below runs and reports "not BSTOR" (skipped).
			Print("Disk not genuine. Trying unauthenticated disk support.");
			return UnauthDiskActivate();
			#else
			Print("Disk not genuine.");
			XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Disk not genuine/flashed. Flash using FATXplorer.", 0);
			#endif
		}
		return FALSE;
	}
	
	PDEVICE_OBJECT phyDiskDeviceObject = NULL;
	PDEVICE_OBJECT p0DeviceObject = NULL;
	PDEVICE_OBJECT p1DeviceObject = NULL;

	OBJECT_STRING phyDiskStr;
	RtlInitAnsiString(&phyDiskStr, PHYSICAL_DISK_PATH);
	NTSTATUS status = ObReferenceObjectByName(&phyDiskStr, 0, IoDeviceObjectType, NULL, (PVOID*)&phyDiskDeviceObject);
	if (!NT_SUCCESS(status))
	{
		Print("ObReferenceObjectByName failed on PhysicalDisk: 0x%08X", status);
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: PhysicalDisk kernel device object not found.", 0);
		return FALSE;
	}

	OBJECT_STRING p0Str;
	RtlInitAnsiString(&p0Str, PARTITION_0_PATH);
	status = ObReferenceObjectByName(&p0Str, 0, IoDeviceObjectType, NULL, (PVOID*)&p0DeviceObject);
	if (!NT_SUCCESS(status))
	{
		Print("ObReferenceObjectByName failed on Partition0: 0x%08X", status);
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Partition0 kernel device object not found.", 0);
		goto cleanupAndExit;
	}

	OBJECT_STRING p1Str;
	RtlInitAnsiString(&p1Str, PARTITION_1_PATH);
	status = ObReferenceObjectByName(&p1Str, 0, IoDeviceObjectType, NULL, (PVOID*)&p1DeviceObject);
	if (!NT_SUCCESS(status))
	{
		Print("ObReferenceObjectByName failed on Partition1: 0x%08X", status);
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Partition1 kernel device object not found.", 0);
		goto cleanupAndExit;
	}

	PPARTITION_INFORMATION phyDiskPartitionInfo = &((PSATA_DISK_EXTENSION)phyDiskDeviceObject->DeviceExtension)->PartitionInformation;
	PPARTITION_INFORMATION p0DiskPartitionInfo = &((PSATA_DISK_EXTENSION)p0DeviceObject->DeviceExtension)->PartitionInformation;
	PPARTITION_INFORMATION p1DiskPartitionInfo = &((PSATA_DISK_EXTENSION)p1DeviceObject->DeviceExtension)->PartitionInformation;

	HANDLE diskHandle = OpenDisk(PARTITION_1_PATH, TRUE);
	if (diskHandle == INVALID_HANDLE_VALUE)
	{
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Failed to open disk handle.", 0);
		goto cleanupAndExit;
	}

	if (!CheckBSTOR(diskHandle))
	{
		if (IsRetailFormatted != NULL) *IsRetailFormatted = TRUE;
		//Opting to not show this to avoid it being annoying.
		//XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Disk is not formatted for Bad Storage. Reformat using FATXplorer.", 0);
		goto cleanupAndExit;
	}

	#ifndef _DEBUG
	//A quick and easy way to check if Bad Storage has already executed is to check if Partition0's size is updated.
	if (p0DiskPartitionInfo->PartitionLength.QuadPart == phyDiskPartitionInfo->PartitionLength.QuadPart)
	{
		Print("Bad Storage already completed successfully.");
		ret = TRUE;
		goto cleanupAndExit;
	}
	#endif

	XContent_DEVICEADDREMOVETASK task;
	task.pszDevicePath = PARTITION_1_PATH;
	task.Action = DEVICESTATE_REMOVE;
	task.DeviceType = XCONTENTDEVICETYPE_HDD;
	if (!((pfnXContentDeviceProcessAddRemove)XContentDeviceProcessAddRemove_Offset)(&task))
	{
		Print("XContent::DeviceProcessAddRemove (remove) returned FALSE.");
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Failed to process HDD removal.", 0);
		goto cleanupAndExit;
	}

	//If you want to spoof disk size for testing, do it here.
	ULONG diskLBA = *(PULONG)SataDiskUserAddressableSectors_Offset;
	LONGLONG diskSize = (LONGLONG)diskLBA * 0x200;

	//These are the 4 places the disk LBA or size is stored.
	SetMemory((PVOID)SataDiskUserAddressableSectors_Offset, &diskLBA, sizeof(diskLBA));
	phyDiskPartitionInfo->PartitionLength.QuadPart = diskSize;
	//Only these 2 are actually required.
	p0DiskPartitionInfo->PartitionLength.QuadPart = diskSize;
	p1DiskPartitionInfo->PartitionLength.QuadPart = diskSize - 0x130EB0000;

	//Now remount..

	//Temporarily patch FatxProcessBootSector so the next mount will not be "corrupt".
	BYTE patchB4RDFC[sizeof(BADSTORAGE_FATX_RDFC_PATCH_DATA)] = {};
	memcpy(patchB4RDFC, (PVOID)BADSTORAGE_FATX_RDFC_PATCH_OFFSET, sizeof(patchB4RDFC));
	SetMemory((PVOID)BADSTORAGE_FATX_RDFC_PATCH_OFFSET, BADSTORAGE_FATX_RDFC_PATCH_DATA, sizeof(BADSTORAGE_FATX_RDFC_PATCH_DATA));

	//Temporarily patch XContent::DeviceAssignDeviceId to give the HDD the correct device ID.
	BYTE patchB4DID[sizeof(BADSTORAGE_DID_PATCH_DATA)] = {};
	memcpy(patchB4DID, (PVOID)BADSTORAGE_DID_PATCH_OFFSET, sizeof(patchB4DID));
	SetMemory((PVOID)BADSTORAGE_DID_PATCH_OFFSET, BADSTORAGE_DID_PATCH_DATA, sizeof(BADSTORAGE_DID_PATCH_DATA));

	task.Action = DEVICESTATE_ADD;
	BOOLEAN addResult = ((pfnXContentDeviceProcessAddRemove)XContentDeviceProcessAddRemove_Offset)(&task);
	SetMemory((PVOID)BADSTORAGE_FATX_RDFC_PATCH_OFFSET, patchB4RDFC, sizeof(patchB4RDFC));
	SetMemory((PVOID)BADSTORAGE_DID_PATCH_OFFSET, patchB4DID, sizeof(patchB4DID));
	if (!addResult)
	{
		Print("XContent::DeviceProcessAddRemove (add) returned FALSE.");
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Failed to process HDD addition.", 0);
		goto cleanupAndExit;
	}

	((pfnBroadcastStorageDevicesChanged)BroadcastStorageDevicesChanged_Offset)();

	ret = TRUE;
	Print("BadStorage completed successfully.");

	cleanupAndExit:
	if (phyDiskDeviceObject != NULL) ObDereferenceObject(phyDiskDeviceObject);
	if (p0DeviceObject != NULL) ObDereferenceObject(p0DeviceObject);
	if (p1DeviceObject != NULL) ObDereferenceObject(p1DeviceObject);
	return ret;
}

EXTERN_C BOOLEAN Execute(PBOOLEAN IsRetailFormatted)
{
	LogLength = 0;
	BOOLEAN ret = ExecuteInternal(IsRetailFormatted);
	Print("Result: %s", ret ? "applied" : (IsRetailFormatted != NULL && *IsRetailFormatted) ? "skipped" : "failed");
	SaveLog();
	return ret;
}
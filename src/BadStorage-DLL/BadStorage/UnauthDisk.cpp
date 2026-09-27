// License: https://github.com/EatonZ/BadStorage/blob/main/LICENSE

#include "stdafx.h"
#include "BadStorage.h"
#include "UnauthDisk.h"

#ifdef UNAUTH_DISK_SUPPORTED

#define SATA_DISK_INITIALIZE_OFFSET       0x8015DB18
#define SATA_DISK_INITIALIZE_LENGTH       0x5FC
#define SATA_DISK_POWER_DOWN_REGISTRATION 0x80170B90
#define POWER_DOWN_LIST_HEAD              0x801704C8
#define XAM_PROLOGUE_MFLR_R12             0x7D8802A6
#define DO_DEVICE_INITIALIZING            0x10
#define CONTENT_PARTITION_OFFSET          0x130EB0000ULL
#define OBJECT_DIRECTORY_BUCKETS          13
#define MAX_DISK_DEVICES                  16
#define MAX_REFERENCES                    4

//SHA-256 of SataDiskInitialize (0x8015DB18..0x8015E114) on a retail 17559 kernel after XeUnshackle.
const UCHAR SATA_DISK_INITIALIZE_SHA256[32] = {
	0x3B, 0x52, 0xFF, 0x82, 0x27, 0x34, 0xEB, 0x7D, 0x4B, 0x17, 0x4D, 0x6F, 0x7F, 0x76, 0x09, 0x6C,
	0xC3, 0x39, 0x4D, 0x81, 0x62, 0x9C, 0xB9, 0x65, 0x0A, 0x2E, 0xB0, 0xC3, 0x5C, 0x8B, 0x3D, 0xB4
};

//Not guaranteed to be in the XDK import libraries, so they are resolved by ordinal and checked against the known 17559 addresses.
const struct { PCHAR Name; DWORD Ordinal; DWORD Expected; } UNAUTH_KERNEL_EXPORTS[] = {
	{ "ObDirectoryObjectType",            262, 0x80042660 },
	{ "DumpGetRawDumpInfo",                 5, 0x800B90C8 },
	{ "HalRegisterPowerDownNotification",  38, 0x80067A58 }
};
enum { EXPORT_OB_DIRECTORY_OBJECT_TYPE, EXPORT_DUMP_GET_RAW_DUMP_INFO, EXPORT_HAL_REGISTER_POWER_DOWN_NOTIFICATION, EXPORT_COUNT };

typedef DWORD (*pfnDumpGetRawDumpInfo)(PVOID Buffer, DWORD Unknown);
typedef VOID (*pfnHalRegisterPowerDownNotification)(PVOID Registration, BOOLEAN Register);

//The partition map SataDiskInitialize builds (0x8015DF28..0x8015E0D0). PhysicalDisk/Partition0/Partition1 depend on the disk size.
const struct { PCHAR Name; ULONGLONG Offset; ULONGLONG Length; BOOLEAN OverlapsDumpPartition; } FIXED_PARTITIONS[] = {
	{ "Cache0",                  0x80000ULL,     0x80000000ULL, FALSE },
	{ "Cache1",                  0x80080000ULL,  0x80000000ULL, FALSE },
	{ "DumpPartition",           0x100080000ULL, 0x20E30000ULL, FALSE },
	{ "SystemURLCachePartition", 0x100080000ULL, 0x6000000ULL,  TRUE  },
	{ "TitleURLCachePartition",  0x106080000ULL, 0x2000000ULL,  TRUE  },
	{ "SystemExtPartition",      0x10C080000ULL, 0xCE30000ULL,  TRUE  },
	{ "SystemAuxPartition",      0x118EB0000ULL, 0x8000000ULL,  TRUE  },
	{ "SystemPartition",         0x120EB0000ULL, 0x10000000ULL, FALSE },
	{ "WindowsPartition",        0,              0,             FALSE }
};

//Object directory entry as seen on 17559; the object body follows at +0x20.
typedef struct _OBJECT_DIRECTORY_ENTRY {
	struct _OBJECT_DIRECTORY_ENTRY* Next;
	ULONG Unknown;
	OBJECT_STRING Name;
} OBJECT_DIRECTORY_ENTRY, *POBJECT_DIRECTORY_ENTRY;
#define OBJECT_BODY(Entry) ((PDEVICE_OBJECT)((PUCHAR)(Entry) + 0x20))

typedef struct _DISK_DEVICE {
	CHAR Name[32];
	PDEVICE_OBJECT DeviceObject;
} DISK_DEVICE, *PDISK_DEVICE;

const PCHAR HARDDISK_DIRECTORY_PATH = "\\Device\\Harddisk0";

#pragma region

const ULONG SHA256_K[64] = {
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

VOID Sha256Block(PULONG State, const UCHAR* Block)
{
	ULONG w[64];
	for (int i = 0; i < 16; i++) w[i] = ((ULONG)Block[i * 4] << 24) | ((ULONG)Block[i * 4 + 1] << 16) | ((ULONG)Block[i * 4 + 2] << 8) | Block[i * 4 + 3];
	for (int i = 16; i < 64; i++)
	{
		ULONG s0 = ROTR32(w[i - 15], 7) ^ ROTR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		ULONG s1 = ROTR32(w[i - 2], 17) ^ ROTR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	ULONG a = State[0], b = State[1], c = State[2], d = State[3], e = State[4], f = State[5], g = State[6], h = State[7];
	for (int i = 0; i < 64; i++)
	{
		ULONG t1 = h + (ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25)) + ((e & f) ^ (~e & g)) + SHA256_K[i] + w[i];
		ULONG t2 = (ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	State[0] += a; State[1] += b; State[2] += c; State[3] += d; State[4] += e; State[5] += f; State[6] += g; State[7] += h;
}

VOID Sha256(const UCHAR* Data, ULONG Length, UCHAR Digest[32])
{
	ULONG state[8] = { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };
	ULONG full = Length & ~63UL;
	for (ULONG i = 0; i < full; i += 64) Sha256Block(state, Data + i);

	UCHAR tail[128];
	ULONG rest = Length - full;
	ULONG tailLength = rest < 56 ? 64 : 128;
	ULONGLONG bits = (ULONGLONG)Length * 8;
	memset(tail, 0, sizeof(tail));
	memcpy(tail, Data + full, rest);
	tail[rest] = 0x80;
	for (int i = 0; i < 8; i++) tail[tailLength - 1 - i] = (UCHAR)(bits >> (i * 8));
	for (ULONG i = 0; i < tailLength; i += 64) Sha256Block(state, tail + i);

	for (int i = 0; i < 8; i++)
	{
		Digest[i * 4] = (UCHAR)(state[i] >> 24);
		Digest[i * 4 + 1] = (UCHAR)(state[i] >> 16);
		Digest[i * 4 + 2] = (UCHAR)(state[i] >> 8);
		Digest[i * 4 + 3] = (UCHAR)state[i];
	}
}

#pragma endregion SHA-256

//Write kernel memory (XeUnshackle already lifted the protection, like SetMemory on retail) and read it back.
BOOLEAN WriteChecked(PVOID Dest, const VOID* Source, SIZE_T Size)
{
	__try
	{
		memcpy(Dest, Source, Size);
		return memcmp(Dest, Source, Size) == 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return FALSE;
	}
}

BOOLEAN PowerDownRegistered(PULONG Count)
{
	ULONG node = *(PULONG)POWER_DOWN_LIST_HEAD, seen = 0;
	BOOLEAN registered = FALSE;
	while (node != POWER_DOWN_LIST_HEAD && node != 0 && seen < 64)
	{
		if (node >= SATA_DISK_POWER_DOWN_REGISTRATION && node < SATA_DISK_POWER_DOWN_REGISTRATION + 0x10) registered = TRUE;
		node = *(PULONG)node;
		seen++;
	}
	*Count = seen;
	return registered;
}

BOOLEAN GetPartitionGeometry(const PCHAR Name, ULONGLONG DiskSize, PPARTITION_INFORMATION Geometry, PBOOLEAN OverlapsDumpPartition)
{
	*OverlapsDumpPartition = FALSE;
	if (strcmp(Name, "PhysicalDisk") == 0 || strcmp(Name, "Partition0") == 0)
	{
		Geometry->StartingOffset.QuadPart = 0;
		Geometry->PartitionLength.QuadPart = DiskSize;
		return TRUE;
	}
	if (strcmp(Name, "Partition1") == 0)
	{
		Geometry->StartingOffset.QuadPart = CONTENT_PARTITION_OFFSET;
		Geometry->PartitionLength.QuadPart = DiskSize - CONTENT_PARTITION_OFFSET;
		return TRUE;
	}
	for (ULONG i = 0; i < ARRAYSIZE(FIXED_PARTITIONS); i++)
	{
		if (strcmp(Name, FIXED_PARTITIONS[i].Name) == 0)
		{
			Geometry->StartingOffset.QuadPart = FIXED_PARTITIONS[i].Offset;
			Geometry->PartitionLength.QuadPart = FIXED_PARTITIONS[i].Length;
			*OverlapsDumpPartition = FIXED_PARTITIONS[i].OverlapsDumpPartition;
			return TRUE;
		}
	}
	return FALSE;
}

//Everything before the first write only reads and validates; any mismatch aborts with nothing changed.
BOOLEAN UnauthDiskActivateInternal(PVOID* References, PULONG ReferenceCount, PULONGLONG DiskSizeOut)
{
	#define FAIL(Notice, ...) { Print(__VA_ARGS__); XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, Notice, 0); return FALSE; }
	#define REFERENCE(Path, Type, Out) \
	{ \
		OBJECT_STRING str; \
		RtlInitAnsiString(&str, Path); \
		status = ObReferenceObjectByName(&str, 0, Type, NULL, (PVOID*)&Out); \
		if (NT_SUCCESS(status) && Out != NULL && *ReferenceCount < MAX_REFERENCES) References[(*ReferenceCount)++] = Out; \
		else if (!NT_SUCCESS(status)) Out = NULL; \
	}

	NTSTATUS status;
	HANDLE kernel = NULL;
	if (!NT_SUCCESS(XexGetModuleHandle((PSZ)"xboxkrnl.exe", &kernel)) || kernel == NULL) FAIL(L"BadStorage FAILURE: xboxkrnl.exe not found.", "XexGetModuleHandle(xboxkrnl.exe) failed.");
	DWORD exports[EXPORT_COUNT];
	for (int i = 0; i < EXPORT_COUNT; i++)
	{
		exports[i] = 0;
		XexGetProcedureAddress(kernel, UNAUTH_KERNEL_EXPORTS[i].Ordinal, &exports[i]);
		if (exports[i] != UNAUTH_KERNEL_EXPORTS[i].Expected)
			FAIL(L"BadStorage FAILURE: Unexpected kernel exports.", "%s (ordinal %d) is at 0x%08X, expected 0x%08X.", UNAUTH_KERNEL_EXPORTS[i].Name, UNAUTH_KERNEL_EXPORTS[i].Ordinal, exports[i], UNAUTH_KERNEL_EXPORTS[i].Expected);
	}

	UCHAR digest[32];
	Sha256((const UCHAR*)SATA_DISK_INITIALIZE_OFFSET, SATA_DISK_INITIALIZE_LENGTH, digest);
	if (memcmp(digest, SATA_DISK_INITIALIZE_SHA256, sizeof(digest)) != 0)
	{
		CHAR hex[65];
		for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
		FAIL(L"BadStorage FAILURE: SataDiskInitialize differs. Nothing was changed.", "SataDiskInitialize SHA-256 mismatch: %s", hex);
	}

	ULONG sectors = *(PULONG)SataDiskUserAddressableSectors_Offset;
	ULONGLONG diskSize = (ULONGLONG)sectors * 0x200;
	*DiskSizeOut = diskSize;
	Print("Disk: %u sectors. XboxHardwareInfo->Flags: 0x%08X", sectors, XboxHardwareInfo->Flags);
	if (diskSize <= CONTENT_PARTITION_OFFSET) FAIL(L"BadStorage FAILURE: Internal disk is too small.", "Disk is too small.");

	PVOID directory = NULL;
	PDEVICE_OBJECT phyDiskDeviceObject = NULL;
	REFERENCE(HARDDISK_DIRECTORY_PATH, (PVOID)exports[EXPORT_OB_DIRECTORY_OBJECT_TYPE], directory);
	if (directory == NULL) FAIL(L"BadStorage FAILURE: Harddisk0 kernel directory not found.", "ObReferenceObjectByName failed on Harddisk0: 0x%08X", status);
	REFERENCE(PHYSICAL_DISK_PATH, IoDeviceObjectType, phyDiskDeviceObject);
	if (phyDiskDeviceObject == NULL) FAIL(L"BadStorage FAILURE: PhysicalDisk kernel device object not found.", "ObReferenceObjectByName failed on PhysicalDisk: 0x%08X", status);

	DISK_DEVICE devices[MAX_DISK_DEVICES];
	ULONG deviceCount = 0;
	BOOLEAN layoutConfirmed = FALSE;
	for (int bucket = 0; bucket < OBJECT_DIRECTORY_BUCKETS; bucket++)
	{
		POBJECT_DIRECTORY_ENTRY entry = ((POBJECT_DIRECTORY_ENTRY*)directory)[bucket];
		for (int depth = 0; entry != NULL && depth < 32; depth++, entry = entry->Next)
		{
			if (deviceCount == MAX_DISK_DEVICES) FAIL(L"BadStorage FAILURE: Unexpected Harddisk0 layout.", "More than %d entries in Harddisk0.", MAX_DISK_DEVICES);
			PDISK_DEVICE device = &devices[deviceCount++];
			USHORT length = entry->Name.Length;
			if (length >= sizeof(device->Name)) length = 0;
			memcpy(device->Name, entry->Name.Buffer, length);
			device->Name[length] = '\0';
			device->DeviceObject = OBJECT_BODY(entry);
			if (strcmp(device->Name, "PhysicalDisk") == 0) layoutConfirmed = device->DeviceObject == phyDiskDeviceObject;
		}
	}
	if (!layoutConfirmed) FAIL(L"BadStorage FAILURE: Unexpected Harddisk0 layout.", "Harddisk0 layout differs (entry + 0x20 != PhysicalDisk).");
	Print("Harddisk0 layout confirmed: %u entries.", deviceCount);

	ULONG powerDownCount;
	BOOLEAN powerDownRegistered = PowerDownRegistered(&powerDownCount);
	UCHAR dumpInfo[0x100];
	memset(dumpInfo, 0, sizeof(dumpInfo));
	DWORD rawDump = ((pfnDumpGetRawDumpInfo)exports[EXPORT_DUMP_GET_RAW_DUMP_INFO])(dumpInfo, 0);
	Print("Power-down list: %u entries, SATA disk %s. DumpGetRawDumpInfo: 0x%X", powerDownCount, powerDownRegistered ? "registered" : "not registered", rawDump);

	//From here on, memory is written. Same order as the kernel: geometry first, then clear DO_DEVICE_INITIALIZING.
	for (ULONG i = 0; i < deviceCount; i++)
	{
		PDEVICE_OBJECT deviceObject = devices[i].DeviceObject;
		PARTITION_INFORMATION geometry;
		BOOLEAN overlapsDumpPartition;
		if ((deviceObject->Flags & DO_DEVICE_INITIALIZING) == 0 || deviceObject->DriverObject != phyDiskDeviceObject->DriverObject) continue;
		if (!GetPartitionGeometry(devices[i].Name, diskSize, &geometry, &overlapsDumpPartition)) continue;
		if (rawDump != 0 && overlapsDumpPartition)
		{
			Print("%s skipped (overlaps DumpPartition and a raw dump exists).", devices[i].Name);
			continue;
		}

		ULONG flags = deviceObject->Flags & ~DO_DEVICE_INITIALIZING;
		if (!WriteChecked(&((PSATA_DISK_EXTENSION)deviceObject->DeviceExtension)->PartitionInformation, &geometry, sizeof(geometry)) ||
			!WriteChecked(&deviceObject->Flags, &flags, sizeof(flags)))
			FAIL(L"BadStorage FAILURE: Failed to initialize the partitions.", "%s: write did not stick.", devices[i].Name);
		Print("%s: offset 0x%I64X, length 0x%I64X, flags 0x%X.", devices[i].Name, geometry.StartingOffset.QuadPart, geometry.PartitionLength.QuadPart, flags);
	}

	//Optional, like in the kernel's own sequence: without it the disk still mounts.
	ULONG hardwareFlags = XboxHardwareInfo->Flags | XBOX_HW_FLAG_HDD;
	if (WriteChecked(&XboxHardwareInfo->Flags, &hardwareFlags, sizeof(hardwareFlags))) Print("XboxHardwareInfo->Flags: 0x%08X", hardwareFlags);
	else Print("Warning: could not set XBOX_HW_FLAG_HDD; continuing.");

	if (!powerDownRegistered)
	{
		((pfnHalRegisterPowerDownNotification)exports[EXPORT_HAL_REGISTER_POWER_DOWN_NOTIFICATION])((PVOID)SATA_DISK_POWER_DOWN_REGISTRATION, TRUE);
		if (!PowerDownRegistered(&powerDownCount)) FAIL(L"BadStorage FAILURE: Failed to register the disk shutdown routine.", "HalRegisterPowerDownNotification did not register.");
		Print("SATA disk power-down routine registered (%u entries).", powerDownCount);
	}

	PDEVICE_OBJECT p0DeviceObject = NULL;
	PDEVICE_OBJECT p1DeviceObject = NULL;
	REFERENCE(PARTITION_0_PATH, IoDeviceObjectType, p0DeviceObject);
	if (p0DeviceObject == NULL) FAIL(L"BadStorage FAILURE: Partition0 did not initialize.", "Partition0 still does not resolve: 0x%08X", status);
	REFERENCE(PARTITION_1_PATH, IoDeviceObjectType, p1DeviceObject);
	if (p1DeviceObject == NULL) FAIL(L"BadStorage FAILURE: Partition1 did not initialize.", "Partition1 still does not resolve: 0x%08X", status);

	if (*(PULONG)XContentDeviceProcessAddRemove_Offset != XAM_PROLOGUE_MFLR_R12 || *(PULONG)BroadcastStorageDevicesChanged_Offset != XAM_PROLOGUE_MFLR_R12)
		FAIL(L"BadStorage FAILURE: Unexpected xam version.", "xam functions do not start with mflr r12.");

	XContent_DEVICEADDREMOVETASK task;
	task.pszDevicePath = PARTITION_1_PATH;
	task.Action = DEVICESTATE_ADD;
	task.DeviceType = XCONTENTDEVICETYPE_HDD;
	task.DeviceID = 0;
	if (!((pfnXContentDeviceProcessAddRemove)XContentDeviceProcessAddRemove_Offset)(&task))
		FAIL(L"BadStorage FAILURE: Failed to process HDD addition.", "XContent::DeviceProcessAddRemove (add) returned FALSE.");
	Print("XContent::DeviceProcessAddRemove (add) succeeded. Device ID: %u", task.DeviceID);

	((pfnBroadcastStorageDevicesChanged)BroadcastStorageDevicesChanged_Offset)();
	return TRUE;

	#undef REFERENCE
	#undef FAIL
}

BOOLEAN UnauthDiskActivate()
{
	PVOID references[MAX_REFERENCES];
	ULONG referenceCount = 0;
	ULONGLONG diskSize = 0;
	BOOLEAN ret;

	__try
	{
		ret = UnauthDiskActivateInternal(references, &referenceCount, &diskSize);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Print("Exception 0x%08X during unauthenticated disk activation.", GetExceptionCode());
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, L"BadStorage FAILURE: Exception while activating the disk.", 0);
		ret = FALSE;
	}

	for (ULONG i = 0; i < referenceCount; i++) ObDereferenceObject(references[i]);

	if (ret)
	{
		WCHAR notice[64];
		_snwprintf(notice, ARRAYSIZE(notice) - 1, L"BadStorage: Unauthenticated disk enabled (%u GB).", (ULONG)(diskSize / 1000000000));
		notice[ARRAYSIZE(notice) - 1] = L'\0';
		XNotifyQueueUI(XNOTIFYUI_TYPE_AVOID_REVIEW, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_HIGH, notice, 0);
		Print("Unauthenticated disk activated successfully.");
	}
	return ret;
}

#endif

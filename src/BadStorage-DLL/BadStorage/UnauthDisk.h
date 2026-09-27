// License: https://github.com/EatonZ/BadStorage/blob/main/LICENSE

#pragma once

/*
	Unauthenticated disk support (retail 17559 only).

	A disk without an Xbox 360 security sector fails SataDiskAuthenticateDevice during boot, before any exploit runs.
	SataDiskInitialize then skips the block at 0x8015DF28..0x8015E10C, which would have:
	  - filled in the PartitionInformation of every \Device\Harddisk0 device and cleared DO_DEVICE_INITIALIZING;
	  - left out the partitions that overlap DumpPartition when DumpGetRawDumpInfo reports a raw dump;
	  - called HalRegisterPowerDownNotification(0x80170B90, TRUE), whose routine sends FLUSH CACHE + STANDBY IMMEDIATE on shutdown;
	  - set XBOX_HW_FLAG_HDD in XboxHardwareInfo->Flags.
	UnauthDiskActivate redoes all of that in RAM, then announces the disk with XContent::DeviceProcessAddRemove (add) and
	BroadcastStorageDevicesChanged, like the regular Bad Storage path. Nothing is written to flash; a cold boot undoes it.

	While DO_DEVICE_INITIALIZING is set, the partition devices do not resolve by name (STATUS_NO_SUCH_DEVICE), so they are found
	by walking the \Device\Harddisk0 object directory. The offsets were checked on a live 17559 console, and SataDiskInitialize
	is hashed before anything is written.

	Known limits: avatars on the disk do not work (the disk key is set in the hypervisor once per boot, before the exploit),
	and the disk cannot be used as an exploit entry point.
*/
#ifndef _XDK
#define UNAUTH_DISK_SUPPORTED

BOOLEAN UnauthDiskActivate();
#endif

VOID Print(const PCHAR Format, ...);

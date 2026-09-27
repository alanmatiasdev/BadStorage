# Xbox 360 Bad Storage

## Standalone boot path without the Xbox SDK

The experimental OpenXeChain title in [src/BadStorage-OpenXe](src/BadStorage-OpenXe)
activates an unauthenticated internal disk locally and attempts to launch Aurora.
The SSD activation and Aurora launch were verified on retail 17559, but the
console froze when returning from GTA San Andreas to Aurora. The cause is not
yet isolated. Do not set this title as DashLaunch's `Default` until the full
game/return flow is validated.
Once stable, no PC or network will be needed at boot. See
[docs/openxe-boot.md](docs/openxe-boot.md) for build, validation, and recovery
instructions. This is separate from the upstream
`BadStorage.xex.dll` entry point; SynthXEX does not yet support DLL exports.

For more information, check out the official project page: https://fatxplorer.eaton-works.com/bad-storage/

Bad Storage comes in 2 flavors: XEX and DLL. The XEX version is for when you want to have a convenient, launchable executable. The DLL is for developers who want to add Bad Storage to their homebrew apps or launchers.

The DLL is not a system module. It should be loaded, executed, and then unloaded from memory once. The changes stick until the console is cold rebooted or shutdown. For example usage of the DLL, <a href="https://github.com/Byrom90/XeUnshackle/pull/62">check out the XeUnshackle implementation</a>.

**For those who do not code and just want to download/use Bad Storage:** it is built into the latest version of <a href="https://github.com/Byrom90/XeUnshackle">XeUnshackle</a>, so just download that and it is all you need. Make sure to format your drive using FATXplorer as well.

# Unauthenticated disk support (this fork)

The retail DLL can also enable an internal disk **without** an Xbox 360 security sector (e.g. an SSD the FATXplorer SSD Maker does not support), on retail kernel 17559. When the disk is identified but not genuine, instead of failing with "Disk not genuine/flashed", `Execute` does in RAM what `SataDiskInitialize` skipped at boot: it fills in the partition geometry, clears `DO_DEVICE_INITIALIZING`, sets `XBOX_HW_FLAG_HDD`, registers the disk's shutdown routine (FLUSH CACHE + STANDBY IMMEDIATE), and announces the disk with `XContent::DeviceProcessAddRemove`. Details are in `UnauthDisk.h`.

- Retail 17559 only. Every address is checked first (kernel export addresses, a SHA-256 of `SataDiskInitialize`, the `\Device\Harddisk0` directory layout, and the xam function prologues). On any mismatch, it aborts before writing anything.
- Nothing is written to flash. A cold boot undoes it, so it runs again on every boot (XeUnshackle does this automatically).
- The disk is used as the console formats it (Settings > System > Storage), not the Bad Storage (BSTOR) format.
- Avatars on the disk do not work: the disk key is set in the hypervisor once per boot, before the exploit. The disk cannot be used as an exploit entry point either.
- Each execution writes its log to `GAME:\BadStorage.log` (the `BadUpdatePayload` folder when loaded by XeUnshackle).
- Genuine and BSTOR-formatted disks follow the original path unchanged.

# Compiling

The recommended development environment is:
- Windows 7 SP1 (a VM is perfectly fine)
- Microsoft Visual Studio 2010 SP1
- Microsoft Xbox 360 SDK (any version should do)

Bad Storage should compile out of the box. There are a few build configurations:
- **Debug:** Builds with debug libraries and settings. Use this for testing/development on XDK kernels only. This will not launch on retail.
- **Debug_Retail:** Builds with debug libraries and settings. Use this for testing/development on retail kernels only.
- **Release:** Builds a fully optimized version. Use this to build a version for normal use.

If you need to debug anything, that will be slightly tricky because until Bad Storage is executed, the internal drive will not be usable, and Visual Studio will always want to deploy to the internal drive.

# Support

The <a href="https://github.com/EatonZ/XDON/issues">GitHub issue tracker</a> is open for your questions.

# Legal

The <a href="https://choosealicense.com/licenses/mit/">MIT License</a> applies to this project. You are welcome to add the Bad Storage DLL or code to your projects - just give credit where appropriate.

No XDK installers or individual components/dlls from the XDK will be provided here. Please don't ask.

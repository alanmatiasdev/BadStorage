# Local SSD boot without the Xbox SDK

This path does not use the Windows 7 VM, the Microsoft SDK, JRPC, or a network
connection during boot. `BadStorageBoot.xex` is an Xbox 360 title compiled with
OpenXeChain. It checks retail kernel 17559, restores the initialization of the
unauthenticated disk partitions in RAM, announces the HDD to `xam`, and then
launches Aurora. The patch is temporary: a cold boot runs the title again.
Nothing is written to flash or to the SSD by the patch.

**Status: experimental and manually validated.** Version v5 enabled the SSD,
loaded games, and passed the game/return-to-Aurora cycle with GTA V. The
`launch.ini` was then configured to start v5 automatically; the original
configuration is backed up at `USB0:\launch.ini.aurora-backup`. A cold boot with
the automatic configuration should still be confirmed after deployment.

The XeUnshackle `BadStorage.xex.dll` slot cannot load this file: SynthXEX does
not yet generate DLL exports, while that slot requires ordinal 1. This XEX is a
second stage launched through DashLaunch's `Default` entry after XeUnshackle.
An Aurora Lua script cannot perform this patch either because its scripting API
does not expose the required memory access and native calls.

## Building on Linux

Install Clang, CMake, Ninja, Git, and C/C++ build tools on the build computer.
The Windows 7 VM can remain powered off. The full official OpenXeChain build
is documented at <https://github.com/OpenXeChain/buildscript>; the minimal build
below was used to produce this freestanding XEX without Newlib. Reserve several
GB of disk space and enough time to build LLVM.

```sh
git clone --filter=blob:none --sparse https://github.com/OpenXeChain/llvm.git /tmp/badstorage-openxe-llvm
git -C /tmp/badstorage-openxe-llvm sparse-checkout set llvm clang lld cmake third-party libunwind
git clone https://github.com/OpenXeChain/SynthXEX.git /tmp/badstorage-openxe-synthxex
cmake -S /tmp/badstorage-openxe-llvm/llvm -B /tmp/badstorage-openxe-compiler-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_INSTALL_PREFIX=/tmp/badstorage-openxe-sdk \
  '-DLLVM_ENABLE_PROJECTS=clang;lld' -DLLVM_TARGETS_TO_BUILD=PowerPC \
  -DLLVM_DEFAULT_TARGET_TRIPLE=ppc32-xbox360 \
  -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON -DLLVM_BUILD_TESTS=OFF
cmake --build /tmp/badstorage-openxe-compiler-build \
  --target clang lld llvm-dlltool llvm-ar -j 8
for part in clang clang-resource-headers lld llvm-ar llvm-dlltool; do
  cmake --install /tmp/badstorage-openxe-compiler-build --component "$part"
done
cmake -S /tmp/badstorage-openxe-synthxex -B /tmp/badstorage-openxe-synthxex-build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/badstorage-openxe-sdk
cmake --build /tmp/badstorage-openxe-synthxex-build
cmake --install /tmp/badstorage-openxe-synthxex-build
OPENXE_PREFIX=/tmp/badstorage-openxe-sdk bash src/BadStorage-OpenXe/build.sh
```

Output: `src/BadStorage-OpenXe/build/BadStorageBoot.xex`. The tested commits
were OpenXeChain/llvm `890b83f6c8259a8899e182a5f7d9cf39c64131cc` and SynthXEX
`4bda05e21e3f6384ac447f8db3103a0a15b96887`. Recheck the generated file when
using newer revisions. A base address of `0x82000000` produced an invalid image
hash and the console did not execute that XEX; `0x92000000` produced a valid
hash and ran correctly. The exact reason the first XEX was rejected by the
loader was not isolated. Run the host logic tests with:

```sh
clang -std=c11 -O2 -Wall -Wextra -Werror \
  src/BadStorage-OpenXe/test_boot.c -o /tmp/badstorage-openxe-test
/tmp/badstorage-openxe-test
```

## Installation and testing

On the test console, Aurora is located at `USB0:\Apps\Aurora\Aurora.xex` and
the original `launch.ini` uses `Default = Usb:\Apps\Aurora\Aurora.xex`. The
XEX launches `GAME:\Aurora.xex` and writes diagnostics to
`GAME:\BadStorageBoot.log`; keep it in the same directory as Aurora.

1. Save a copy of `USB0:\launch.ini`. The transfer utility is only used during
   installation; it is not needed at boot.

   Define `XBOX_HOST` only on the local computer; do not commit its value:

   ```sh
   export XBOX_HOST=<xbox-address>
   ```

   ```sh
   python3 tools/xbdm-file.py --host "$XBOX_HOST" get \
     'USB0:\launch.ini' /tmp/badstorage-launch.ini.backup
   ```

2. Upload the XEX under a new name. The utility refuses to replace an existing
   remote file and compares every byte after upload:

   ```sh
   python3 tools/xbdm-file.py --host "$XBOX_HOST" put-new \
     src/BadStorage-OpenXe/build/BadStorageBoot.xex \
     'USB0:\Apps\Aurora\BadStorageBoot.xex'
   ```

3. With the SSD disabled after a cold boot, launch `BadStorageBoot.xex`
   manually and confirm the complete cycle: `Hdd1` is accessible, a game runs,
   and the console returns to Aurora reliably. Read `GAME:\BadStorageBoot.log`:
   `OK` only means activation completed; it does **not** prove that the title
   switch succeeded. `E01` through `E18` identify a failed precondition in
   `activate()`. If the screen goes black or crashes, power-cycle the console;
   the original `Default` entry is the recovery path. Do not enable automatic
   boot in that case.

4. After repeatedly validating activation **and** returning to Aurora, set the
   `Default` line in `launch.ini` to:

   ```ini
   Default = Usb:\Apps\Aurora\BadStorageBoot.xex
   ```

   Preserve every other line; no XBDM/JRPC plugin is required at boot. Keep a
   backup of the previous configuration on the USB drive. Power the Xbox off
   completely, turn it on, follow ABadAvatar → XeUnshackle → DashLaunch, and
   confirm that the XEX applies the patch and opens Aurora without an external
   connection. Verify that `Hdd1` appears and that the games are accessible.

5. To revert, restore `Default = Usb:\Apps\Aurora\Aurora.xex` from the backup
   or edit the USB drive on a computer. If the new title fails to launch, the
   USB remains editable outside the Xbox; nothing is written to NAND.

The code validates export addresses, the kernel build, a SHA-256 digest of
`SataDiskInitialize`, the `Harddisk0` directory layout, and `xam` function
prologues before its first write. Geometry is written before clearing
`DO_DEVICE_INITIALIZING`, in the same order as the kernel. Do not use this XEX
on another kernel without porting and validating every offset. Even with the
disk mounted, the hypervisor disk-key/avatar limitation remains outside this
patch's scope.

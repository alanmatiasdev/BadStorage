#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")" && pwd)"
: "${OPENXE_PREFIX:?Set OPENXE_PREFIX to the installed OpenXeChain prefix}"
compiler="$OPENXE_PREFIX/bin/clang"
linker="$OPENXE_PREFIX/bin/lld-link"
dlltool="$OPENXE_PREFIX/bin/llvm-dlltool"
converter="$OPENXE_PREFIX/bin/synthxex"

for program in "$compiler" "$linker" "$dlltool" "$converter"; do
    if [[ ! -x "$program" ]]; then
        echo "Missing OpenXeChain tool: $program" >&2
        exit 1
    fi
done

mkdir -p "$project_dir/build"
"$dlltool" -m xbox360 -d "$project_dir/xboxkrnl.def" -l "$project_dir/build/xboxkrnl.a"
"$compiler" --target=ppc32-xbox360 -std=c11 -Os -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fno-stack-protector -nostdlib \
    -c "$project_dir/boot.c" -o "$project_dir/build/boot.obj"
"$linker" /subsystem:xbox360 /base:0x92000000 /entry:main \
    /out:"$project_dir/build/BadStorageBoot.exe" \
    "$project_dir/build/boot.obj" "$project_dir/build/xboxkrnl.a"
"$converter" -i "$project_dir/build/BadStorageBoot.exe" \
    -o "$project_dir/build/BadStorageBoot.xex"
echo "Built $project_dir/build/BadStorageBoot.xex"

# Inicialização local do SSD sem XDK

Este caminho não usa a VM Windows 7, SDK da Microsoft, JRPC ou rede durante o
boot. `BadStorageBoot.xex` é um título Xbox 360 compilado com OpenXeChain. Ele
confere o kernel retail 17559, restaura em RAM a inicialização das partições
do disco não autenticado, anuncia o HDD ao `xam` e tenta abrir o Aurora. O
patch é efêmero: um boot frio exige executá-lo novamente. Nada é gravado no
flash ou no SSD pelo patch.

**Estado: experimental, validado manualmente.** A v5 ativou o SSD, carregou os
jogos e passou pelo ciclo de jogo/retorno ao Aurora com GTA V. O `launch.ini`
foi então configurado para iniciar a v5 automaticamente; o backup original
está em `USB0:\launch.ini.aurora-backup`. Ainda falta confirmar um boot frio
com essa configuração automática.

O slot `BadStorage.xex.dll` do XeUnshackle não pode receber este arquivo:
SynthXEX ainda não gera exports DLL (o slot requer ordinal 1). O XEX é uma
segunda etapa, depois do XeUnshackle, através do `Default` do DashLaunch.
Um script Lua do Aurora também não serve para este patch: a API de scripts
não expõe as leituras/escritas e as chamadas nativas necessárias.

## Compilação no Linux

Instale Clang, CMake, Ninja, Git e compiladores C/C++ no computador de build.
A VM Windows 7 pode ficar desligada. O build oficial completo da OpenXeChain
está em <https://github.com/OpenXeChain/buildscript>; o build mínimo abaixo
foi usado para gerar este XEX sem Newlib, pois `boot.c` é freestanding.
Reserve alguns GB e tempo para a compilação do LLVM.

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

Saída: `src/BadStorage-OpenXe/build/BadStorageBoot.xex`. Foram testados os
commits OpenXeChain/llvm `890b83f6c8259a8899e182a5f7d9cf39c64131cc` e
SynthXEX `4bda05e21e3f6384ac447f8db3103a0a15b96887`. Se usar revisões
futuras, confira novamente o arquivo gerado. Com endereço-base `0x82000000`,
o verificador de XEX acusou hash inválido e o console não executou o código;
com `0x92000000`, o hash validou e o código rodou. A causa exata da rejeição
do primeiro XEX pelo loader não foi isolada. Para os testes de lógica no host:

```sh
clang -std=c11 -O2 -Wall -Wextra -Werror \
  src/BadStorage-OpenXe/test_boot.c -o /tmp/badstorage-openxe-test
/tmp/badstorage-openxe-test
```

## Instalação e teste

No console em estudo, o Aurora fica em `USB0:\Apps\Aurora\Aurora.xex`; o
`launch.ini` atual diz `Default = Usb:\Apps\Aurora\Aurora.xex`. O XEX usa
`GAME:\Aurora.xex` para voltar ao Aurora e grava o diagnóstico em
`GAME:\BadStorageBoot.log`; mantenha-o na mesma pasta do Aurora.

1. Guarde uma cópia de `USB0:\launch.ini`. O utilitário de transferência abaixo
   funciona apenas durante a instalação; ele não é usado no boot:

   Defina `XBOX_HOST` apenas no computador local (não o versione):

   ```sh
   export XBOX_HOST=<endereco-do-xbox>
   ```

   ```sh
   python3 tools/xbdm-file.py --host "$XBOX_HOST" get \
     'USB0:\launch.ini' /tmp/badstorage-launch.ini.backup
   ```

2. Envie o XEX com um nome novo; o utilitário recusa substituir um arquivo
   remoto existente e compara todos os bytes após o upload:

   ```sh
   python3 tools/xbdm-file.py --host "$XBOX_HOST" put-new \
     src/BadStorage-OpenXe/build/BadStorageBoot.xex \
     'USB0:\Apps\Aurora\BadStorageBoot.xex'
   ```

3. Com o SSD desativado após boot frio, inicie `BadStorageBoot.xex`
   manualmente e confirme o ciclo completo: `Hdd1` acessível, jogo funcionando
   e retorno estável ao Aurora. Leia `GAME:\BadStorageBoot.log`: `OK` indica somente
   que a ativação terminou, **não** que a troca de título funcionou. `E01` a
   `E18` indicam a pré-condição que falhou em `activate()`. Se houver tela
   preta ou crash, desligue e ligue o console; o `Default` original preserva
   o caminho de recuperação. Não configure o boot automático nesse caso.

4. Depois de validar repetidamente a ativação **e** o retorno ao Aurora, a
   linha `Default` do `launch.ini` deve ser:

   ```ini
   Default = Usb:\Apps\Aurora\BadStorageBoot.xex
   ```

   Preserve todas as demais linhas; não é preciso `plugin` XBDM/JRPC. Guarde
   no USB também uma cópia da configuração anterior. Desligue totalmente o
   Xbox, ligue-o e percorra ABadAvatar → XeUnshackle → DashLaunch. O XEX deve
   aplicar o patch e abrir Aurora sem conexão externa. Confirme que `Hdd1`
   aparece e que os jogos estão acessíveis.

5. Para reverter, restaure `Default = Usb:\Apps\Aurora\Aurora.xex` usando a
   cópia do `launch.ini` (ou edite o USB num computador). Se o título novo
   falhar ao abrir, o pendrive continua editável fora do Xbox; não há escrita
   na NAND.

O código valida endereços de exports, build do kernel, SHA-256 de
`SataDiskInitialize`, layout do diretório `Harddisk0` e prólogos das funções
do `xam` antes da primeira gravação. A geometria é escrita antes de limpar
`DO_DEVICE_INITIALIZING`, na ordem do kernel. Não use o XEX em outro kernel
sem portar e validar todos os offsets. Mesmo com o disco montado, o problema
de avatars/chave de disco no hypervisor permanece fora do escopo deste patch.

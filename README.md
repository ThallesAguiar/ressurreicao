# Ressurreição

Fork da decompilação open-source de *Castlevania: Symphony of the Night*
([sotn-decomp](https://github.com/Xeeynamo/sotn-decomp)) que reimagina a
história original como o confronto entre **Jesus e a Morte**: o prólogo
jogável foi reescrito para contar a vitória sobre a morte e a ressurreição,
mantendo a jogabilidade e a engine originais.

## O que mudou

- Diálogo e narração do prólogo reescritos (Richter/Drácula → Jesus/Morte)
- Scroll de abertura com novo texto
- Port nativo para Windows (via [psyz](https://github.com/Xeeynamo/psyz) +
  SDL3), sem emulador
- Renderização de texto no PC (fonte Shinonome 14dot, domínio público)
- Menu de debug in-game (**R2**) com God Mode, One-Hit-Kill, No-Clip,
  Corações Infinitos e Dar Todos os Itens — além das flags equivalentes
  por linha de comando

## Como jogar e usar

### Jeito rápido — o prólogo

Dê dois cliques em [`run-prologo.cmd`](run-prologo.cmd) (na pasta raiz do
projeto). Ele abre a janela direto no prólogo jogável.

### Rodar manualmente

```cmd
cd /d C:\www\rea\sotn-decomp
build\pc\sotn.exe
```

Sem argumentos, o jogo liga normal: tela de título → prólogo → fase
jogável. Para música e vozes (áudio XA do CD), aponte `--disk` para o
`.cue` do disco:

```cmd
build\pc\sotn.exe --disk "disks\sotn.us.cue"
```

### Flags de debug úteis

| Opção | O que faz |
|---|---|
| `--god-mode` | Jogador não recebe dano |
| `--one-hit-kill` | Qualquer acerto mata o alvo |
| `--no-clip` | Liga o modo voador/no-clip (alterna com **L2** no jogo) |
| `--infinite-hearts` | Sub-armas nunca gastam corações |
| `--give-all-items` | Começa com todos os itens/equipamentos |

**Menu de debug in-game:** aperte **R2** durante o jogo para abrir um menu
com os mesmos toggles. **Cima/Baixo** navega, **X (Cross)** liga/desliga,
**R2** de novo fecha. Funciona independente das flags de linha de comando.

### Controles (teclado, jogador 1)

| Tecla | Botão do PS1 | Ação |
|---|---|---|
| Setas | D-Pad | Mover / navegar menus |
| **X** | ✕ CROSS | Pular |
| **Z** | □ SQUARE | Atacar |
| **Enter** | START | Pausa / confirmar |
| **Backspace** | SELECT | Menu |

Qualquer controle reconhecido pelo SDL3 (Xbox, DualShock, DualSense,
Switch Pro) funciona com o mapeamento padrão.

### Como compilar (build)

Precisa de **Git, Go, Python 3.12+, MinGW-w64 (GCC), CMake e Rust/cargo**
instalados, além de uma cópia própria do disco do jogo (`.cue` + `.bin`).

```cmd
git clone --recurse-submodules https://github.com/Xeeynamo/sotn-decomp.git
:: coloque o disco como disks\sotn.us.cue (+ as trilhas .bin)
sotn-disk extract disks/sotn.us.cue disks/us
sotn-assets extract-assets config/assets.us.yaml
sotn-assets build-assets config/assets.us.yaml
cmake -S . -B build/pc -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl
cmake --build build/pc
```

O executável gerado fica em `build\pc\sotn.exe`. Depois de editar código,
basta rodar `cmake --build build/pc` de novo pra recompilar.

Guia completo — som, exportar o jogo compilado, recompilar do zero e
editar o texto do prólogo — em [TUTORIAL.md](TUTORIAL.md).

## Créditos

**Castlevania: Symphony of the Night** e todos os seus personagens,
gráficos, músicas e fases são propriedade da **Konami**. Este projeto é um
fork não-oficial da decompilação, feito para a **REA**, sem fins
lucrativos — não há venda, monetização ou distribuição comercial de
nenhum tipo. É um projeto de fã, educacional/ministerial, criado para uso
próprio da comunidade.

## Licença e aviso legal

O **código** desta decompilação é licenciado sob **AGPL-3.0-or-later**.
Os **assets originais do jogo** (gráficos, música, fases) pertencem à
Konami e não são redistribuídos por este repositório — cada pessoa precisa
da sua própria cópia do disco original para gerar o executável.

---

## Castlevania: Symphony of the Night Decompilation (projeto original)

A work-in-progress decompilation of Castlevania Symphony of the Night for Sony PlayStation 1, Sony PlayStation Portable and Sega Saturn. It aims to recreate the source code from the existing binaries using static and/or dynamic analysis. The code compiles byte-for-byte to the same binaries of the game, effectively being a matching decompilation.

It currently supports the following versions of the game:

* `us` the reference build with the serial number SLUS-00067
* `hd` an unreleased PS1 Japanese build found in Castlevania: Dracula X Chronicles game data
* `pspeu` European build from Castlevania: Dracula X Chronicles
* `saturn` the port created by an external development team

This repo does not include any assets or assembly code necessary for compiling the binaries. A prior copy of the game is required to extract the required assets.

## Useful links

* [Build guide](https://github.com/Xeeynamo/sotn-decomp/wiki/Build)
* [Decompilation guide](https://github.com/Xeeynamo/sotn-decomp/wiki/Decompilation)
* [Progress report](https://sotn.xee.dev/)
* [decomp.dev progress](https://decomp.dev/Xeeynamo/sotn-decomp)

Decompiling a game is a mastodontic task. If you have some basic programming skills, please join us in this journey. Any contribution will be very appreciated!

[![Join to our Discord server](https://discord.com/api/guilds/1079389589950705684/widget.png?style=banner2)](https://sotn-discord.xee.dev/)

## Special thanks

This project is possible thanks to the hard work of tools provided by the Decompilation community:

* [mips2c](https://github.com/matt-kempster/m2c) from @matt-kempster to decompile MIPS assembly into C. This has proven to be more accurate than Hexrays IDA and Ghidra.
* [splat](https://github.com/ethteck/splat) from @ethteck to disassemble code and extract data with a symbol map. This tool provides the fundamentals of the SOTN decomp.
* [asm-differ](https://github.com/simonlindholm/asm-differ) from @simonlindholm to know how the decompiled code compares to the original binary.
* [decomp-permuter](https://github.com/simonlindholm/decomp-permuter) from @simonlindholm to pick different versions of the same code that better matches the original binary.
* [maspsx](https://github.com/mkst/maspsx) by @mkst to replicate the customized assembler used in the official PSX SDK.
* [decomp.me](https://github.com/decompme/decomp.me/) by @ethteck, @nanaian and @mkst to provide a collaborative decompilation site to share and contribute to work-in-progress decompiled functions.
* [frogress](https://github.com/decompals/frogress) by @ethteck to store and retrieve progression data.
* [esa-new](https://github.com/mkst/esa-new) by @mkst as an inspiration on how to set-up a PS1 decompilation project.
* [oot](https://github.com/zeldaret/oot) as an inspiration of what it is possible to achieve with a complete decompiled video game.

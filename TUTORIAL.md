# Tutorial — Castlevania: Symphony of the Night (port nativo para PC)

Este tutorial ensina a **usar**, **configurar sons/controles**, **exportar** e **compartilhar** a versão nativa do jogo que foi compilada a partir da decompilação ([sotn-decomp](https://github.com/Xeeynamo/sotn-decomp)) com a camada [psyz](https://github.com/Xeeynamo/psyz) + SDL3.

**O que é:** o jogo roda **nativamente no Windows**, sem emulador de PS1. O código C do jogo foi reconstruído e compilado para x86-64.

---

## 1. Como usar (rodar o jogo)

### 1.1 Jeito rápido — o prólogo (Richter vs Drácula)

Dê dois cliques em [`run-prologo.cmd`](run-prologo.cmd) (que está na pasta raiz do projeto). Ele abre a janela direto no prólogo.

### 1.2 Iniciar normalmente (título → história → jogo)

Abra um terminal **dentro da pasta do projeto** (`C:\www\rea\sotn-decomp`) e rode:

```cmd
cd /d C:\www\rea\sotn-decomp
build\pc\sotn.exe
```

Sem argumentos, o jogo liga normal: tela de título → **texto do prólogo (a história do Richter/Belmont)** → fase jogável.

> ⚠️ O jogo precisa ser executado a partir da pasta `C:\www\rea\sotn-decomp`, porque ele lê os arquivos do disco de `disks\us\` (caminho relativo).

### 1.3 Com música (trilha XA)

A música e as vozes são áudio **XA** do CD. O `--disk` deve apontar para o arquivo **`.cue`** do disco (não o `.bin` cru):

```cmd
cd /d C:\www\rea\sotn-decomp
build\pc\sotn.exe --disk "disks\sotn.us.cue"
```

### 1.4 Opções de linha de comando

| Opção | O que faz |
|---|---|
| `--disk <caminho>` | Arquivo **`.cue`** do disco (música/vozes XA) |
| `--stage <nome>` | Começa numa fase específica (ex.: `st0`, `no3`, `nz0`, `sel`) |
| `--player <nome>` | Personagem: `ric` (Richter), `alu` (Alucard), `mar` (Maria) |
| `--scale <n>` | Escala da resolução interna (1–8). Ex.: `--scale 3` |
| `--demo <n>` | Reproduz um demo de atração |
| `--record <arquivo>` | Grava os comandos do controle num arquivo |
| `--replay <arquivo>` | Reproduz um arquivo de comandos gravado |
| `--replay-and-exit` | Sai sozinho quando o replay/demo terminar |
| `--replay-fast` | Desliga o limite de quadros durante replay |
| `--spawn-point <nome>` | Começa num ponto específico |
| `--god-mode` | Debug: jogador não recebe dano |
| `--one-hit-kill` | Debug: qualquer acerto mata o alvo |
| `--no-clip` | Debug: liga o modo voador/no-clip original do jogo (alterna com **L2** durante o jogo) |
| `--infinite-hearts` | Debug: sub-armas nunca gastam corações |
| `--give-all-items` | Debug: começa com todos os itens/equipamentos no inventário |
| `--help` | Mostra a ajuda |

**Menu de debug in-game:** aperte **R2** durante o jogo pra abrir um menu com os mesmos toggles acima (God Mode, One-Hit-Kill, No-Clip, Infinite Hearts, Give All Items). Use **cima/baixo** pra navegar e **X (Cross)** pra ligar/desligar. R2 de novo fecha. Funciona independente das flags de linha de comando — pode usar os dois juntos ou só o menu.

**Exemplos:**

```cmd
:: Prólogo jogável (Richter vs Drácula)
build\pc\sotn.exe --stage st0 --player ric

:: Jogo do Alucard (início)
build\pc\sotn.exe --stage no3 --player alu

:: Janela maior (3x)
build\pc\sotn.exe --scale 3

:: Prólogo com todas as opções de debug ligadas
build\pc\sotn.exe --stage st0 --player ric --god-mode --one-hit-kill --no-clip --infinite-hearts --give-all-items
```

---

## 2. Controles

### 2.1 Teclado (jogador 1)

| Tecla | Botão do PS1 | Ação no jogo |
|---|---|---|
| Setas ↑ ↓ ← → | D-Pad | Mover / navegar menus |
| **X** | ✕ CROSS | Pular |
| **Z** | □ SQUARE | Atacar (chicote do Richter / espada do Alucard) |
| **D** | ○ CIRCLE | (ação) |
| **S** | △ TRIANGLE | (ação) |
| **Q** | L1 | — |
| **W** | L2 | — |
| **R** | R1 | — |
| **E** | R2 | — |
| **Enter** | START | Pausa / confirmar |
| **Backspace** | SELECT | Menu |
| **1** | L3 | — |
| **2** | R3 | — |

### 2.2 Teclas especiais (do porte)

| Tecla | Função |
|---|---|
| **ESC** | Fecha o jogo |
| **F4** | Alterna tela cheia / janela |
| **F6** | Liga/desliga a visualização de VRAM (debug) |

### 2.3 Controle (gamepad)

Qualquer controle reconhecido pelo SDL3 funciona: **Xbox (360/One/Series), DualShock 4, DualSense, Switch Pro** e genéricos. O mapeamento é o padrão:

- **D-Pad / analógico esquerdo** = mover
- **Face buttons** (✕/○/△/□) = pular/atacar/etc.
- **LB / RB** = L1 / R1
- **LT / RT** = L2 / R2
- **Start / Select** = START / SELECT
- **Clique dos analógicos** = L3 / R3

O teclado sempre controla o **jogador 1**; um controle conectado também assume o jogador 1.

---

## 3. Som

### 3.1 Efeitos sonoros

Funcionam **automaticamente**. O SPU é emulado pelo psyz e os bancos de som (`.VH`/`.VB`) são lidos de `disks\us\VAB\` (ex.: `SD_RIH.VB` = Richter, `SD_ALK.VB` = Alucard). A saída usa WASAPI/DirectSound do Windows.

### 3.2 Música (trilha XA)

A música **e as vozes das conversas** são áudio XA do CD. O `--disk` precisa receber o arquivo **`.cue`** (que descreve as trilhas) — e **não** o `.bin` cru. Rode com:

```cmd
build\pc\sotn.exe --disk "disks\sotn.us.cue"
```

> O `.cue` aponta para as trilhas 1 e 2 (`.bin`). A música e as vozes ficam na trilha 2.

### 3.3 Problemas comuns

| Sintoma | Causa provável | Solução |
|---|---|---|
| Sem música e sem vozes | `--disk` não foi passado, ou apontou para o `.bin` em vez do `.cue` | Passar `--disk "disks\sotn.us.cue"` |
| Sem efeitos sonoros | Falta `disks\us\VAB\` | Conferir se a pasta `disks\us` está completa |
| Aviso `no track found for sector …` no log | `.cue` ausente ou sem a trilha 2 | Usar o `.cue` completo (com trilha 1 e 2) |

---

## 4. Exportar (empacotar o jogo para outro PC)

O jogo compilado **não precisa de nada instalado** no PC de destino — só Windows 10/11 64 bits (o SDL já está embutido no `.exe` e o runtime Universal C faz parte do Windows).

### 4.1 O que precisa ir no pacote

| Arquivo/Pasta | Necessário? | Tamanho aprox. |
|---|---|---|
| `build\pc\sotn.exe` | Sim | ~7 MB |
| `build\pc\*.dll` (66 overlays) | Sim | ~varias dezenas de MB |
| `disks\us\` (pasta inteira) | Sim | ~500 MB |
| `.cue` + trilhas 1 e 2 (`.bin`) | Só se quiser música/vozes | ~560 MB |

### 4.2 Montando o pacote (PowerShell)

```powershell
$dist = "C:\sotn-pc-port"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "C:\www\rea\sotn-decomp\build\pc\sotn.exe" $dist
Copy-Item "C:\www\rea\sotn-decomp\build\pc\*.dll" $dist
Copy-Item "C:\www\rea\sotn-decomp\disks\us" "$dist\disks\us" -Recurse
# Música e vozes (opcional) — o .cue + as duas trilhas:
Copy-Item "C:\www\rea\Castlevania - Symphony of the Night (USA)\Castlevania - Symphony of the Night (USA) (Track 1).bin" "$dist\Track 1.bin"
Copy-Item "C:\www\rea\Castlevania - Symphony of the Night (USA)\Castlevania - Symphony of the Night (USA) (Track 2).bin" "$dist\Track 2.bin"
Set-Content "$dist\disc.cue" @'
FILE "Track 1.bin" BINARY
  TRACK 01 MODE2/2352
    INDEX 01 00:00:00
FILE "Track 2.bin" BINARY
  TRACK 02 AUDIO
    INDEX 00 00:00:00
    INDEX 01 00:02:00
'@
```

Depois crie um `run.cmd` **dentro de `C:\sotn-pc-port`** com este conteúdo:

```cmd
@echo off
cd /d "%~dp0"
sotn.exe --disk "disc.cue"
```

O outro PC só precisa abrir esse `run.cmd`. (Se não copiou as trilhas, remova o `--disk ...`.)

> Dica: o script [exportar.ps1](exportar.ps1) já monta tudo isso automaticamente.

### 4.3 Gerando um ZIP

```powershell
Compress-Archive -Path "C:\sotn-pc-port\*" -DestinationPath "C:\sotn-pc-port.zip"
```

---

## 5. Compartilhar (atenção à parte legal)

**O que é livre:** o **código-fonte** da decompilação (o `sotn-decomp`) é licenciado sob **AGPL-3.0**. Você pode compartilhar o código e o tutorial livremente (mantendo a licença).

**O que NÃO é livre:** os **assets do jogo** — gráficos, músicas, sons, fases — pertencem à **Konami**. Isso inclui tudo que está em `disks\us\` e a trilha 2 do CD.

> Regra prática: **não distribua o jogo compilado (com os assets)** publicamente. O jeito certo de compartilhar é distribuir o **código + o tutorial + as instruções de build**, e cada pessoa usa a **própria cópia do disco** (que ela já possui). Compartilhar o pacote pronto **privadamente** com quem tem o jogo é a prática comum, mas continua sendo uma área cinzenta — use seu critério.

### 5.1 O que compartilhar com outra pessoa

1. O link do repositório: <https://github.com/Xeeynamo/sotn-decomp>
2. Este tutorial (`TUTORIAL.md`).
3. O `run-prologo.cmd`.
4. (Opcional) as suas edições de texto em `src\st\st0\prologue_cutscene_script.h`.

---

## 6. Recompilar do zero (resumo)

Para quem quiser refazer o build noutra máquina:

1. Instalar: **Git**, **Go**, **Python 3.12+**, **MinGW-w64 (GCC)**, **CMake**, **Rust/cargo**.
2. `git clone --recurse-submodules https://github.com/Xeeynamo/sotn-decomp.git`
3. Colocar o disco como `disks\sotn.us.cue` + `.bin` (trilha 1 e 2).
4. Extrair: `sotn-disk extract disks/sotn.us.cue disks/us`
5. Extrair assets: `sotn-assets extract-assets config/assets.us.yaml` e `build-assets config/assets.us.yaml`
6. Compilar: `cmake -S . -B build/pc -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DPSYZ_RENDERER=sdl3_gl` e depois `cmake --build build/pc` (o `sdl3_gl` usa OpenGL e é o mais compatível no Windows).

O guia oficial está em: <https://github.com/Xeeynamo/sotn-decomp/wiki/Build>

---

## 7. Editar o texto do prólogo

O texto da história (a parte do "Belmont sumiu") fica em:

```
src\st\st0\prologue_cutscene_script.h
```

A versão do disco USA é o bloco `#else` (perto do final do arquivo). A passagem do desaparecimento é:

```c
"However, one night 4 years later,\x01\x0C"
"under the glare of a full moon,\x01\x0B"
"Richter mysteriously vanished. (EDITED)\x02\x0C"
```

- `\x01` = quebra de linha; `\x02` + byte = fim de página/posição de rolagem.
- É só reescrever o texto (ASCII), manter os `\x01`/`\x02`, recompilar e rodar de novo:

```cmd
cmake --build build/pc --parallel 8
```

---

## 8. Limitações conhecidas do porte para PC

- **Fonte do texto.** O porte PC originalmente não tinha renderização de fontes. Nesta versão foi adicionada uma implementação: a fonte **Shinonome 14dot** (domínio público, do OpenBIOS) é embutida em `src\pc\krom_font.c` e usada pelo `func_80106A28` (reabilitado em `src\dra\66590.c`). O texto do prólogo e diálogos agora aparece, mas com a aparência dessa fonte (não a fonte original do PS1).
- **`FLAGS DCP` no log** — aviso inofensivo do parser de `.cue` (ignora essa linha).

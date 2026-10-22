# Ressurreição

Projeto original que reimagina a história de *Castlevania: Symphony of
the Night* como o confronto entre **Jesus e a Morte**: o prólogo jogável
foi reescrito para contar a vitória sobre a morte e a ressurreição,
construído sobre a engine recriada pela decompilação open-source
[sotn-decomp](https://github.com/Xeeynamo/sotn-decomp).

## O que mudou

- Diálogo e narração do prólogo reescritos (Richter/Drácula → Jesus/Morte)
- Scroll de abertura com novo texto
- Port nativo para Windows (via [psyz](https://github.com/Xeeynamo/psyz) +
  SDL3), sem emulador
- Jesus ferido jogável, com 197 quadros em pixel art e paleta própria;
  as sprites são incorporadas automaticamente ao recompilar o port de PC.
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
gráficos, músicas e fases são propriedade da **Konami**. Este é um
projeto original, feito para a **REA**, sem fins lucrativos — não há
venda, monetização ou distribuição comercial de nenhum tipo. É um projeto
de fã, educacional/ministerial, criado para uso próprio da comunidade.

## Licença e aviso legal

O **código** desta decompilação é licenciado sob **AGPL-3.0-or-later**.
Os **assets originais do jogo** (gráficos, música, fases) pertencem à
Konami e não são redistribuídos por este repositório — cada pessoa precisa
da sua própria cópia do disco original para gerar o executável.

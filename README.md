# Da Cruz à Glória

Um jogo curto de ação em pixel art sobre Jesus e seu confronto com Morte.
Desenvolvido com o **REA**, para fins de estudo e sem fins lucrativos.

## Jogar no Windows

Dê dois cliques em **[run-prologo.cmd](run-prologo.cmd)**, na pasta do
projeto. O jogo abre diretamente no prólogo, com música e vozes.

## Jogar no navegador

1. Dê dois cliques em **[run-web.cmd](run-web.cmd)**.
2. Abra **<http://localhost:8080>** e clique em **Jogar**.
3. Mantenha a janela do servidor aberta enquanto joga.

Para abrir essa versão local, é necessário ter o **Python** instalado.
Na versão hospedada, basta acessar o link do jogo, sem instalar nada.

## Controles

| Tecla | Ação |
|---|---|
| Setas | Mover e agachar |
| Z | Atacar |
| X | Pular após a transformação |
| Enter | Pausar e confirmar |

Jesus ferido não consegue pular. O salto fica disponível na forma glorificada.
No celular, use os botões na tela e prefira jogar na horizontal.

## Primeira execução ou recompilação

Os atalhos precisam dos arquivos compilados em `build/pc` ou `build/web`.
Essas pastas não acompanham uma cópia recém-clonada do repositório.

- **Windows:** siga o [guia de preparação e compilação](TUTORIAL.md).
  Essa versão usa os dados extraídos do jogo em `disks/us` e o disco
  configurado em `disks/sotn.us.cue`, obtidos de uma cópia própria.
- **Navegador:** siga o [guia da versão web](docs/WEB.md) para compilar
  e gerar o ZIP de publicação.

## Créditos e licença

Projeto de fã, não oficial e sem vínculo com a **Konami**. Créditos à
Konami por *Castlevania: Symphony of the Night* e pelos personagens,
gráficos, músicas e demais elementos originais utilizados.

Baseado em [sotn-decomp](https://github.com/Xeeynamo/sotn-decomp) e
[psyz](https://github.com/Xeeynamo/psyz). O código segue a licença
[AGPL-3.0-or-later](LICENSE); os elementos originais do jogo pertencem
aos seus respectivos titulares.

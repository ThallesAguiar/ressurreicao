# Versão para navegador

O port web é compilado em `Release`, sem menu de debug. O build de Windows
continua em `build/pc`; a versão web usa `build/web` e contém o prólogo ST0.

## Compilar no Windows

Além das ferramentas do build de PC, instale o Emscripten uma vez, na raiz:

```powershell
git clone --depth 1 https://github.com/emscripten-core/emsdk.git build/tools/emsdk
.\build\tools\emsdk\emsdk.bat install 6.0.13
.\build\tools\emsdk\emsdk.bat activate 6.0.13
```

Depois execute:

```powershell
powershell -ExecutionPolicy Bypass -File build-web.ps1
```

O pacote final fica em **`build/web/site`**, e o ZIP para enviar fica em
**`build/da-cruz-a-gloria-web.zip`**. As ferramentas e os arquivos
intermediários ficam fora dessa pasta e não precisam ser publicados.

## Testar localmente

Abra `run-web.cmd` e acesse <http://localhost:8080>. Ou execute:

```powershell
python -m http.server 8080 --directory build/web/site
```

Abra <http://localhost:8080> e clique em **Jogar**. Não abra o HTML com
duplo clique: o navegador precisa carregar os arquivos por HTTP.

Setas movem, Z ataca, X pula após a transformação e Enter pausa. Em telas
de toque, os controles aparecem na própria página. O som começa após o
clique em Jogar, conforme as regras dos navegadores.

## Publicar

Hospede todos os arquivos de `build/web/site` juntos, preservando os nomes.
O servidor deve servir `.wasm` como `application/wasm` e usar HTTPS.
O jogo não exige um backend nem cabeçalhos de isolamento entre origens.

No itch.io, crie um projeto do tipo **HTML**, envie um ZIP com `index.html`
na raiz e marque que o arquivo deve ser jogado no navegador. Habilite
suporte a celular e tela cheia. Também é possível hospedar o mesmo pacote
em um site estático próprio.

O empacotador copia somente os dados necessários e conserva os setores de
áudio usados pelo prólogo, com seus endereços e cabeçalhos originais. Os
arquivos do disco de origem permanecem intactos.

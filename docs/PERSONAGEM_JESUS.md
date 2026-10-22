# Jesus — descrição do personagem e direção para sprites

Este documento define a aparência, a postura e a expressão do protagonista de **Ressurreição**. Serve como referência para a criação posterior dos sprites e das animações de Jesus em duas formas: **Jesus ferido**, no início do jogo, e **Jesus glorificado**, após sua derrota para a Morte e a intervenção do Espírito Santo.

## 1. Identidade visual comum às duas formas

Jesus é um homem adulto, de cabelos castanhos longos e ondulados, barba castanha cheia e pele de tonalidade quente. Seu rosto deve manter os mesmos traços nas duas formas: nariz marcado, sobrancelhas expressivas e olhar reconhecível. A transformação precisa mostrar que se trata da mesma pessoa.

- **Cabelos:** longos, com ondas e mechas bem definidas, caindo sobre os ombros e pelas costas.
- **Barba:** cheia, acompanhando o maxilar e o queixo; bigode integrado à barba.
- **Fisionomia:** preservar as proporções do rosto e a identidade entre retratos, sprites e animações.
- **Estilo:** pixel art inspirada nas duas imagens de referência, com volumes definidos por luz e sombra e silhueta legível durante o jogo.

As imagens fornecidas são referências de retrato. A parte inferior das roupas, os calçados e as proporções do sprite de corpo inteiro precisam ser definidos na etapa de produção, mantendo coerência com esta descrição e com a escala do jogo.

## 2. Forma inicial — Jesus ferido

**Referência visual: Imagem #1, com coroa de espinhos, ferimentos e vestes ensanguentadas.**

### Estado e presença

Jesus inicia o jogo **machucado, cansado e abatido**. Seu corpo demonstra dor, desgaste e dificuldade para continuar. Sua presença transmite sofrimento e resistência: ele segue em frente apesar da exaustão.

### Aparência

- **Cabeça:** coroa de espinhos de galhos marrons, irregulares e pontiagudos, pressionando a testa e se misturando aos cabelos.
- **Rosto:** expressão cansada, olhar pesado e sério, sinais de dor e marcas de ferimentos. Sangue escorre da testa pelo rosto, especialmente pelas laterais e pela região dos olhos e das bochechas, conforme a referência.
- **Pele:** ferida e marcada; sangue visível também no pescoço e nas áreas expostas junto à roupa.
- **Cabelos e barba:** desalinhados, com aparência de desgaste, preservando a identidade do personagem.
- **Vestes:** túnica de tecido simples, originalmente clara, agora suja, rasgada e manchada de sangue. Usar branco encardido, bege e cinza nas áreas de tecido, com manchas de terra e sangue vermelho escuro.
- **Acabamento do tecido:** bordas gastas, rasgos irregulares e dobras pesadas. Os rasgos e as manchas devem ser perceptíveis no sprite, sem depender dos detalhes do retrato.

### Postura e movimento

- Tronco levemente curvado, ombros caídos e cabeça com tendência a baixar.
- Respiração visível na animação de repouso, sugerindo esforço e cansaço.
- Passos pesados e gestos que comuniquem dor e desgaste.
- Expressões e poses de sofrimento nas reações a dano e na derrota.

Essas orientações descrevem a atuação visual. Velocidade de movimento, controles e duração das animações devem respeitar as necessidades da jogabilidade.

### Leitura de cor

Predominam castanhos, bege, branco sujo e sombras acinzentadas. O sangue vermelho escuro funciona como destaque. A coroa de espinhos e as roupas deterioradas são os principais sinais de identificação desta forma.

## 3. Evento de transformação

A sequência narrativa obrigatória é:

1. Jesus enfrenta a Morte em sua forma ferida.
2. Jesus é derrotado pela Morte.
3. O Espírito Santo se manifesta e lhe concede poder.
4. Jesus recebe a glória de Deus e assume sua forma glorificada.

A transformação deve comunicar a recuperação completa do corpo e a chegada do poder divino. Os ferimentos desaparecem, o sangue e a sujeira somem, as vestes passam a ser finas e a coroa de espinhos dá lugar à coroa de **Rei dos Reis**. A postura deixa de demonstrar exaustão e passa a demonstrar vigor, força e autoridade.

A luz pode ser usada como recurso visual de transição, preservando a leitura do rosto e da silhueta. O efeito exato e a quantidade de quadros serão definidos na produção das animações.

## 4. Forma final — Jesus glorificado

**Referência visual: Imagem #2, com coroa dourada, pedras azuis e vestes brancas com acabamento dourado.**

### Estado e presença

Após receber a glória de Deus, Jesus está **completamente curado, bem disposto, poderoso, forte e invencível**. Sua presença transmite majestade, serenidade e autoridade divina. O rosto demonstra determinação e plena vitalidade.

### Aparência

- **Cabeça:** coroa real de ouro, com pontas altas e ornamentadas e pedras azuis, conforme a referência. Ela representa sua condição de **Rei dos Reis**.
- **Rosto:** saudável, sem cortes, hematomas, sangue ou sinais de exaustão; olhar firme, atento e confiante.
- **Pele:** íntegra, com iluminação quente e aspecto recuperado. Nenhum ferimento permanece visível.
- **Cabelos e barba:** os mesmos cabelos castanhos longos e a mesma barba da forma inicial, agora com aparência cuidada e mechas iluminadas.
- **Vestes:** roupas finas, brancas e limpas, com bordados e acabamentos dourados. O tecido deve transmitir qualidade, leveza e dignidade real.
- **Acabamento do tecido:** dobras elegantes, contornos íntegros e detalhes dourados legíveis. Sem rasgos, sujeira ou manchas de sangue.
- **Constituição:** corpo vigoroso e presença forte, mantendo a anatomia reconhecível da forma inicial.

### Postura e movimento

- Tronco ereto, ombros firmes e cabeça erguida.
- Repouso estável e sereno, com respiração tranquila.
- Movimentos seguros e decididos, com poses que transmitam força e domínio.
- Ataques com expressão de poder divino e gestos firmes.
- A atuação desta forma deve comunicar invencibilidade; evitar poses de dor ou abatimento que contradigam esse estado.

**Invencibilidade é uma característica de jogabilidade solicitada para esta forma**, além de uma intenção visual. Este documento orienta a representação; a regra deverá ser implementada no código do jogo posteriormente.

### Leitura de cor

Predominam branco luminoso, ouro e tons quentes da pele e dos cabelos. As pedras azuis contrastam com a coroa dourada. A coroa real, as vestes impecáveis e a postura ereta são os principais sinais de identificação desta forma.

## 5. Diretrizes para a criação dos sprites

- Manter o mesmo rosto, cabelo, barba e proporções básicas nas duas formas. A diferença deve vir do estado físico, da coroa, das vestes, da postura e da atuação.
- Adaptar os detalhes dos retratos à escala real do jogo. Priorizar silhueta, contraste e manchas de cor que continuem legíveis em movimento.
- Representar a coroa de espinhos e a coroa real com contornos distintos, reconhecíveis mesmo em baixa resolução.
- Manter a posição dos rasgos, das manchas de sangue, dos bordados e das pedras da coroa consistente entre quadros e direções.
- Usar enquadramento e pontos de apoio compatíveis entre as formas, para que a transformação preserve a posição do personagem na cena.
- Planejar repouso, deslocamento, salto, queda e ataque para ambas as formas, conforme as ações disponíveis no jogo; incluir dano e derrota para a forma ferida e a sequência de transformação.
- Definir resolução, quantidade de quadros, limites de paleta e organização das folhas de sprites a partir dos requisitos da engine na etapa de produção.
- Não acrescentar armas, armaduras, asas ou outros acessórios como requisitos deste personagem sem uma definição posterior.

## 6. Critérios de aprovação visual

- **Forma ferida:** reconhecida imediatamente como Jesus machucado, cansado e abatido, com coroa de espinhos e vestes sujas, rasgadas e ensanguentadas.
- **Transformação:** acontece após a derrota para a Morte e a manifestação do Espírito Santo; comunica o recebimento da glória de Deus e a recuperação completa.
- **Forma glorificada:** reconhecida imediatamente como o mesmo Jesus, agora saudável, forte, bem disposto, poderoso e invencível, com vestes finas e coroa de Rei dos Reis.
- **Consistência:** as duas formas mantêm a identidade do personagem e podem ser distinguidas durante o jogo sem depender de texto explicativo.

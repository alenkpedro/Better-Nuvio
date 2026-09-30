# Motores refeitos — 30/09/2026

A substituição foi concluída nos caminhos de execução: o extrator Matroska
antigo, a pré-busca que interrompia o vídeo e a fila de frames ASS foram apagados.
A montagem do continuar assistindo saiu da descoberta de catálogos e passou a
um serviço próprio. Os dados de progresso antigos são importados sem apagar a conta.

## Referências

- NuvioTVSmart `48a94b347837965e2e052f82707820c0dd7c8028`: extrator oficial
  `services/webos/src/bitmapSubtitles.js`, com adaptação do transporte HTTP para o serviço
  local; contrato de progresso, seleção por título e retomada.
- NuvioTV dev `fa6614384e23ff5808bf39c53dd3fa602055c54e`: tempo do Media3,
  cues e ajuste manual com compensação de reação ao controle.
- Nuvio Enhanced WebOS: seleção com token, tempo do vídeo menos atraso manual,
  montagem em fases e identidade própria dos arcos.

## Implementações

- `subtitle_engine.c/h`: seleção, pedidos de janela ao extrator oficial,
  descarte de respostas antigas e transferência do renderer nativo somente
  depois de instalar uma janela válida. A reprodução começa imediatamente.
- `legenda.c/h`: documentos imutáveis SRT/VTT/ASS, codificação, busca temporal,
  download limitado e proteção contra seleções antigas.
- `assrender.c`: libass renderiza diretamente o tempo de apresentação no thread
  gráfico. Sem fila de frames, relógio próprio ou conversão automática de FPS.
- `media_clock.c/h`: amostra do backend com extrapolação limitada, pausa e seek.
- `progresso.c/h`: estado por perfil, revisões, exclusões persistentes e metadados.
- `syncprog.c/h`: aplica o snapshot recebido antes de enviar alterações locais;
  confirmação por revisão e perfil. `syncvisto.c` transporta itens assistidos.
- `continuar_motor.c/h` e `watch_service.c/h`: último estado por título, próximos
  episódios e enriquecimento por identidade exata, publicados independentemente
  do catálogo. Cards do serviço não são sobrescritos pela projeção antiga.
- `descoberta.c`: preserva `id` próprio antes do alias `imdb_id` de fontes.
- `player.c`/`faixas.c`: retomada absoluta, checkpoints a cada cinco segundos e
  na pausa/saída; ID e temporada da conta separados da consulta de fontes.
- Android: Media3 conserva a reprodução e as legendas embutidas; amostras de
  tempo a cada 50 ms. O núcleo compartilhado contém os novos serviços.

## Verificação executada

```sh
SANITIZE=1 bash tests/engines_rebuilt.sh
SANITIZE=1 bash tests/continuar_pipeline.sh
bash tests/faixas_grupos.sh
bash tests/subtitle_playback.sh
bash tests/watch_playback.sh
bash tests/mkv_legendas.sh
bash tests/ass_fontes.sh
bash tests/legenda_ass_shot.sh /tmp/bn-rebuilt-ass
```

`subtitle_playback` reproduz arquivos MKV de teste SRT e ASS por HTTP Range,
passando pelo serviço oficial, parser e libass. Verifica início de fala,
pausa, seek nos dois sentidos, atraso manual e troca de seleção. O início foi
observado dentro de 2 ms do timestamp nas execuções locais. Essa medida é dos
fixtures no Mac, não de todas as fontes disponíveis nas TVs.

`watch_playback` usa o backend FFmpeg real: retoma progresso remoto com duração
zero, faz seek, pausa, salva, fecha e reabre na posição persistida. Mantém
`tmdb:299939`, capa de Lizzie e temporada 1 na conta, com temporada 4 para a fonte.
Os testes do serviço cobrem metadados incorretos, catálogo republicado, próximo
episódio, duração desconhecida, perfis, snapshots vazios e exclusões. O próximo
episódio confirmado continua visível durante atualização e falha de metadados.

## Distribuição

APK 0.1.27 (versionCode 28) e IPK 1.5.35 compilados com o mesmo núcleo.
A validação física no Fire TV e na LG ainda está pendente. O teste Mac usa
FFmpeg/libass e o mesmo serviço de extração incluído no IPK.

## Navegação (1.5.34 / 0.1.26)

A barra superior agora usa uma cápsula que expande por hover ou D-pad. O vidro
lê somente a faixa atrás dela e aplica duas passadas de desfoque em 280×32,
com alvos reutilizados durante a animação. Os nomes são revelados com recorte
e o ajuste de movimento reduzido continua respeitado. A verificação GL no Mac
cobriu transparência, mouse, clique, D-pad, recolhimento, resize e restauração
do framebuffer. O comportamento nas TVs ainda depende do teste físico.

## Continuar assistindo completo (1.5.35 / 0.1.27)

O histórico da conta Nuvio passa inteiro pela seleção e ordenação. Foram removidos os cortes de 50 cards no snapshot e 64 candidatos na montagem. A Home começa com 12 cards e libera os próximos 12 ao chegar ao fim, até o último título disponível. O contador mostra o total completo; a renderização mantém só os cards próximos à tela ativos. A posição acompanha a identidade do título quando o histórico atualiza. A fileira de próximos episódios usa a mesma paginação.

Verificação: 137 títulos, todos os modos de ordenação, publicação assíncrona, passagem pelas fronteiras de página, último card, restauração após reordenação e 70 próximos episódios, com AddressSanitizer e UndefinedBehaviorSanitizer no Mac. Renderização real verificada com D-pad nos cards 12, 65 e 137, sem erros GL.

## Menu de legendas e áudio — IPK 1.5.36 / APK 0.1.28

A consulta e a apresentação seguem o `subtitleRepository` e os métodos 49/50
do Nuvio Enhanced WebOS. O nome do addon aparece no selo, o idioma como título
e o ID original como informação secundária. Nomes de idioma completos são
normalizados antes do filtro; IDs iguais ao idioma não são repetidos. Opções
ficam na ordem do ID, preservando a ordem de origem em empates.

O pedido inclui `videoHash`, `videoSize` e `filename` quando fornecidos pela
fonte. Uma troca de arquivo refaz a busca e invalida respostas antigas. O
limite de 32 resultados por addon foi removido; o catálogo tem um teto de
proteção de 1024 opções. Headers de download também são preservados.

No Mac, o backend FFmpeg enumera as faixas de áudio e troca o decoder e o
resampler na posição atual, inclusive durante a pausa. LG e Android continuam
usando suas listas e seleções nativas.

Verificações: respostas capturadas e anonimizadas de Idiotas (17 opções PT-BR),
80 opções de um mesmo addon, URL com os três hints, invalidação por fonte,
headers, ordenação e badge único. Reprodução de duas faixas AAC (440/880 Hz)
confirma o áudio decodificado após cada troca. SRT e ASS continuam surgindo em
2,100 s do vídeo com erro medido abaixo de 4 ms nos arquivos de teste.

## Estilo das legendas embutidas — IPK 1.5.38 / APK 0.1.30

A lista de faixas passou de 32 para 128 e o cabeçalho Matroska comporta 256
faixas totais, incluindo vídeo e áudio. O corte em 32 fazia o casamento por
ordinal recusar um arquivo de 43 legendas; sem codec/ordinal, a seleção ficava
no desenho nativo da TV e não chegava ao estilo do app.

O extrator entrega VTT para texto SubRip, que usa o mesmo desenho das
legendas externas SRT/VTT. ASS/SSA continuam no libass para conservar placas
e efeitos. A legenda nativa fica visível durante a preparação; a transição
para o overlay ocorre quando a janela extraída está pronta.

Verificação: um MKV com 43 legendas, seleção da última faixa, extração real
pelo serviço oficial, pausa, seek e troca de seleção. As imagens de texto
embutido e externo foram iguais pixel a pixel com estilos padrão e
personalizado. O primeiro texto apareceu em 2,100–2,103 s para o timestamp
de 2,100 s nos fixtures do Mac. O arquivo filmado e a LG precisam da validação
física após instalar este pacote.

## Serviço de legendas na LG — IPK 1.5.39

A validação física de 1.5.38 ainda mostrou o estilo nativo. Os testes anteriores
usavam Node 26 no Mac e não cobriam o runtime do serviço na LG. A falha foi
reproduzida em Node 0.12.2: o serviço enviado como fonte não iniciava por sintaxe
incompatível. Em Node 8.17.0, o extrator rejeitava a URL por depender de `URL`
global ausente. As respostas HTTP também usavam encadeamento de `writeHead`,
indisponível nesses runtimes.

O IPK passa a incluir um bundle ES5 com polyfills, compilado durante o
empacotamento. O extrator oficial usa a assinatura HTTP compatível e o motor
opcional de plugins só é inicializado quando necessário. O Mac executa o mesmo
bundle que a TV. O estilo do texto extraído continua no renderer de SRT/VTT
do app, conforme os testes da versão anterior.

Verificação: o pacote real foi carregado pelo `main` de package.json em Node
0.12.2 e 8.17.0. Ambos extraíram SRT, ASS e a última de 43 faixas em MKVs reais,
seguiram redirect/HTTP Range e continuaram disponíveis após erro. O teste
físico da LG permanece necessário para confirmar a instalação e a transição.

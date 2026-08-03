# BCD3000 no Windows 11 25H2 — Design da Solução

Data: 2026-07-27

## Como ler este documento (nota acrescentada em 2026-07-29)

Este é o **registro histórico do desenho**, escrito em 2026-07-27. O texto abaixo é
mantido como foi decidido na época, **inclusive onde o desenho errou** — as decisões
erradas e o que as derrubou são a parte mais útil deste arquivo, e reescrevê-las para
combinar com o código de hoje apagaria a evidência.

Onde o que foi construído divergiu do que o desenho previa, o parágrafo afetado carrega
logo abaixo uma linha marcada com `> **ATUALIZAÇÃO (2026-07-29):**`, com o fato medido e
onde ele foi medido. A marca fica **no ponto da afirmação** de propósito: quem lê só uma
seção tem de ver a correção sem depender de chegar ao fim do arquivo.

Estado em 2026-07-29, medido e não presumido:

- **Fase 0, Fase 1, Fase 3a, Passo 2.1 e Passo 2.2: concluídos e validados no hardware.**
  O passo 2.2 fechou os **11** critérios de aceite (os 8 originais mais 3 que nasceram no
  caminho). Corrida mais longa: **82 min 56 s contínuos, 44.095,7 amostras/s, 0 underruns,
  0 overruns, 0 fomes de entrada**.
- **Fase 3 (empacotamento): em andamento**, e com duas portas fechadas por cláusula de
  licença e pela regra de custo zero do dono — ver as notas nas linhas da Fase 3 e da
  Fase 3a.
- A fonte da verdade deste projeto é, nesta ordem: **o código no disco** > o ledger
  (`.superpowers/sdd/2026-07-27-bcd3000-audio-asio/progress.md`) > este documento.

## Diagnóstico (com evidência)

- Código 39 / `CM_PROB_DRIVER_FAILED_LOAD`, Kernel-PnP 219, status `0xC000026C` = `STATUS_DRIVER_UNABLE_TO_LOAD` (imagem recusada pelo Code Integrity antes de rodar; NÃO é falha do DriverEntry).
- Causa: `bcd3000.sys` (2010) assinado por cert VeriSign SHA-1 expirado (2011), cross-sign era Win7 que o Win11 25H2 não honra mais. Não é toggle de UI. Imports do driver: todas as APIs ainda existem no Win11 → não é "API removida".

## Descoberta chave (dump USB)

BCD3000 (`VID_1397/PID_00BF`) é dispositivo classe-padrão, 4 interfaces numa **função única** (`MI_00`):

| IF | Classe | Função | Endpoint | Direção |
|----|--------|--------|----------|---------|
| 0 | Audio Control | controle | ctrl | — |
| 1 | Audio Streaming | playback 4ch/16/44.1k | `0x02` ISO OUT | PC→dev |
| 2 | Audio Streaming | captura 4ch/16/44.1k | `0x83` ISO IN | dev→PC |
| 3 | MIDI Streaming | controles + LEDs | `0x81` BULK IN / `0x01` BULK OUT | dev→PC / PC→dev |

## Resultado da tentativa com drivers embutidos (Plano A)

Removidos `oem133.inf` (bcd3000.inf) e `oem127.inf` (bcd3000wdm.inf). Windows assumiu via `usbccgp`+`usbaudio`. Resultado:

- ✅ Áudio de SAÍDA funciona (testado no VirtualDJ, WASAPI, 4 canais: master 1&2, fones 3&4).
- ✅ MIDI OUT (LEDs) existe.
- ❌ MIDI IN (controles) NÃO existe — `midiInGetNumDevs=0`. Controles mudos no VDJ (confirmado).
- ❌ Áudio de ENTRADA (captura) não exposto.

**Causa raiz do lado "dev→PC":** descritor MIDI do firmware tem `wTotalLength` errado (9 em vez de ~37); `usbaudio.sys` moderno para de parsear antes do jack de MIDI-in. Linux precisa de quirk específico para este device (confirma que é o aparelho, não a máquina). Como as 4 interfaces são **uma função só (MI_00)**, não dá para separar o MIDI e conectar só ele — é tudo-ou-nada por um único driver.

## Decisão final: driver completo em user-mode (sem comprometer segurança)

Assumir o aparelho inteiro com componentes user-mode + peças assinadas pela Microsoft:

```
BCD3000 (USB) → WinUSB.sys (MS-signed) → Serviço user-mode (nosso "cérebro")
                                          ├─ controles: lê BULK IN 0x81 → porta MIDI virtual → VDJ/Traktor/Mixxx
                                          ├─ LEDs/VU: porta MIDI virtual → BULK OUT 0x01
                                          ├─ áudio: DLL ASIO (user-mode!) streaming ISO 0x02/0x83
                                          └─ mic/phono switch: comando MIDI/control
```

Por que respeita 100% a segurança: WinUSB é da Microsoft (já assinado); DLL ASIO e serviço são user-mode (não precisam assinatura de kernel). Secure Boot/HVCI/blocklist permanecem ligados.

> **ATUALIZAÇÃO (2026-07-29):** a premissa de UM serviço user-mode fazendo áudio **e**
> controles ao mesmo tempo **caiu no hardware**, e a razão está medida: no WinUSB só um
> processo por vez segura este aparelho (`CreateFile` erro 5 = acesso negado com o outro
> ativo). O desenho final tem **dois** processos e uma passagem de bastão:
> - **`BcdAsio.dll`** (dentro do software de DJ) assume o aparelho **inteiro** — IF1, IF2
>   **e IF3** — enquanto o áudio roda: faz o áudio e **repassa** os bytes de MIDI;
> - **`BCD3000Bridge.exe`** é o **dono permanente** da porta MIDI virtual "BCD3000": cria
>   uma vez ao iniciar e **nunca** fecha. O driver **não cria porta nenhuma** — ele é
>   cliente de um canal local (`\\.\pipe\BCD3000MidiRelay`, modo mensagem, 4 bytes fixos),
>   e o bridge é o servidor;
> - a passagem do **aparelho** é por evento nomeado, em escopo duplo (local +
>   `Global\BCD3000_DriverWantsDevice`), porque o bridge roda **sem** elevação e o software
>   de DJ **com** elevação.
>
> Por que a porta é do bridge e não do driver, e isto foi determinado **por eliminação**:
> um teste do dono (trocar de placa de som no VirtualDJ, transição única e limpa) mostrou
> que **uma vez que o software de DJ perde o controlador com ele aberto, ele não volta a
> procurá-lo até ser reiniciado**. Logo qualquer desenho em que o driver seja dono da porta
> adia o mesmo evento fatal em vez de evitá-lo. Detalhe completo, com as medições, nos
> Adendos 1 e 2 de `docs/superpowers/specs/2026-07-27-bcd3000-audio-asio-design.md`.
>
> A garantia de segurança do parágrafo acima **não mudou**: nada de kernel foi instalado,
> Secure Boot/HVCI/blocklist seguem ligados, e não existe carga em tempo de boot.

Alvos: VirtualDJ, Traktor, Mixxx. Reversível: driver original em `Drivers originais/BCD3000_1.3.4/` + setup.exe.

> **ATUALIZAÇÃO (2026-07-29):** `Drivers originais/` **saiu do rastreamento do git** em
> `56d84bb` (segue no disco desta máquina, e continua sendo o caminho de volta aqui). Quem
> clonar o repositório **não recebe** essa pasta: o pacote de 2010 é material da Behringer
> e não é redistribuído. Dos três alvos, **só o VirtualDJ foi exercitado** (v2026 b9482);
> Traktor e Mixxx seguem **não testados** — o driver é ASIO e a porta MIDI tem o mesmo nome
> para qualquer host, mas isso é raciocínio, não medição.

## Plano em fases

- **Fase 0 — Prova de conceito (spike):** bind WinUSB via Zadig + Python/pyusb lê `0x81`. Prova acesso user-mode e captura o protocolo dos controles. Reversível. Ferramentas: Python+pyusb+libusb, Zadig.
- **Fase 1 — Controles → MIDI virtual:** serviço lê controles e injeta em porta MIDI virtual; LEDs via `0x01`. Mapear no VDJ/Mixxx. Entrega: controlar o software.
- **Fase 2 — Áudio via ASIO:** DLL ASIO user-mode com streaming ISO in/out; comando mic/phono. Ferramentas: Visual Studio C++, ASIO SDK. Entrega: áudio in/out baixa latência.
- **Fase 3 — Empacotamento:** instalador (bind WinUSB + registra ASIO + serviço auto-start). Entrega: solução permanente 1-clique.

> **ATUALIZAÇÃO (2026-07-29) — duas coisas destas duas linhas não se sustentaram:**
>
> 1. **O SDK da Steinberg saiu do produto.** A Fase 2 foi construída com ele e depois
>    cortada (Caminho C): as declarações da interface ASIO passaram a ser nossas, em
>    `native/bcdasio/asioapi.h`, com **prova de compatibilidade binária** — `build.bat
>    abicheck`, **426 verificações** (309 de layout/constantes, 74 de vtable em tempo de
>    execução, 43 de travessia), 24 métodos (3 do IUnknown + 21 do IASIO), 0 falhas. A prova
>    tem **prazo**: só roda enquanto `native/ASIOSDK/` estiver no disco, e vale para x64 com
>    MSVC. Quem clonar o repositório **compila sem baixar SDK nenhum**.
> 2. **O comando mic/phono nunca foi implementado**, e segue fora de escopo. O que existe é
>    a *leitura* das entradas: a chave PHONO/LINE da entrada B foi usada como variável de
>    teste para provar o mapeamento de canal, não como algo que o driver comande.
> 3. **O instalador NÃO faz o bind do WinUSB, e isso agora é decisão tomada e não
>    pendência.** A Microsoft exige catálogo **assinado** para instalar um pacote WinUSB
>    ("Create a signed catalog file for the package. This file is required to install WinUSB
>    on Windows"), e o caminho legítimo de assinar (Partner Center, certificado EV, taxa
>    anual) foi descartado pela regra de **custo zero** do dono. Logo o **Zadig continua
>    sendo passo manual do usuário**. O que o instalador faz é registrar o ASIO, instalar o
>    driver e o serviço de controles, e **conduzir** o usuário nos pré-requisitos —
>    detectando o que falta e explicando. "1-clique" não foi alcançado: a instalação numa
>    máquina virgem tem **seis passos**, e dois deles são de terceiros (loopMIDI/teVirtualMIDI
>    e Zadig). Desenho em
>    `docs/superpowers/specs/2026-07-29-bcd3000-installer-wizard-design.md`.

## Fase 0 — RESULTADO (concluída, sucesso)

- WinUSB aplicado via Zadig na função MI_00. libusb NÃO consegue reivindicar IF3 (limitação com função única) → usamos a **API WinUSB direta** (`WinUsb_Initialize` + `WinUsb_GetAssociatedInterface(idx=2 → IF3)` + `WinUsb_ReadPipe(0x81)`). Funciona. Script: `poc/winusb_bcd.py`.
- **Controles lidos com sucesso em user-mode.** Viabilidade do projeto PROVADA.
- Protocolo (dev→PC, canal MIDI 0, pacotes USB-MIDI 1.0 padrão de 4 bytes):
  - **Botões:** Note On (0x90), notas ~0x00–0x24 (~35 botões). Press=vel 0x7F, release=0x00.
  - **Faders/Knobs:** Control Change (0xB0), CC 0x00–0x0D, valor absoluto 0x00–0x7F.
  - **Jog wheels:** CC 0x12/0x13, relativo em torno de 0x40 (0x41=CW, 0x3F=CCW).
- **Insight de arquitetura:** não precisamos decodificar o significado de cada controle — o software de DJ já tem mapeamento da BCD3000. Nosso serviço é uma **ponte MIDI transparente**: encaminha bytes de `0x81` → porta MIDI virtual, e porta virtual → `0x01` (LEDs).

## Fase 1 — RESULTADO (núcleo concluído, sucesso)

- Ponte ao vivo `poc/bridge.py` (**POC removido em `36f7b46`** — provou o caminho e foi substituído pelo `poc/bridge_service.py`): lê EP 0x81 (WinUSB) → encaminha USB-MIDI para porta virtual loopMIDI "BCD3000" (via python-rtmidi). 995 msgs encaminhadas no teste; loopMIDI acusou tráfego.
- **VirtualDJ reconheceu o controlador "BCD3000" e os controles movem o software.** Confirmado pelo usuário.
- **LEDs/feedback concluído.** Migrado de loopMIDI para **teVirtualMIDI** (porta bidirecional "BCD3000" criada pelo próprio `poc/bridge2.py`, **POC removido em `36f7b46`** — o que ele provou vive hoje no `poc/bridge_service.py`): controles IN + LEDs OUT numa porta só, como controlador real. RX callback → `write_led` → EP 0x01. Teste: 14.626 controles→PC e 5.655 LEDs←PC. VirtualDJ controla e acende luzes. **Fase 1 COMPLETA.**
- Dependência loopMIDI removida (usamos a DLL teVirtualMIDI direto). loopMIDI só serviu p/ instalar o driver teVirtualMIDI.
- Nota: enquanto o device está no WinUSB, o áudio embutido fica OFF (volta na Fase 2). A ponte roda como script Python no venv do agente — **enquanto não empacotarmos (Fase 3), os controles só funcionam com o `bridge2.py` rodando.**

> **ATUALIZAÇÃO (2026-07-29):** os dois POCs desta fase (`poc/bridge.py` e
> `poc/bridge2.py`) foram **removidos do repositório em `36f7b46`**, depois de medido que
> nenhum código vivo os referenciava. Eles não são perda: provaram o encaminhamento
> `0x81` → porta virtual e o retorno dos LEDs por `0x01`, e essa função vive hoje no
> `poc/bridge_service.py` (empacotado como `BCD3000Bridge.exe`). Os quatro arquivos Python
> **vivos** do repositório são `poc/bridge_service.py`, `poc/winusb_bcd.py` e os dois arnês
> versionados `poc/arnes_canal.py` e `poc/arnes_invariante.py`.
>
> A frase final desta seção também não vale mais: os controles **não** dependem de script
> nenhum rodando à mão desde a Fase 3a, e desde o passo 2.2 a porta virtual pertence
> permanentemente ao `BCD3000Bridge.exe`.

## Fase 3a — RESULTADO (standalone controles+LEDs, concluída)

- `poc/bridge_service.py`: versão robusta (reconexão automática, log). Usa só API nativa do Windows (WinUSB + teVirtualMIDI) — sem pyusb/rtmidi.
- Empacotado com PyInstaller (`--onefile --noconsole`) → `poc/dist/BCD3000Bridge.exe` (**`poc/dist/` é saída de build e não é rastreada no git**; o binário que roda vive em `%LOCALAPPDATA%\BCD3000Bridge\`).
- Instalado em `%LOCALAPPDATA%\BCD3000Bridge\BCD3000Bridge.exe` + atalho na pasta Inicializar (auto-start no login). Log: `%LOCALAPPDATA%\BCD3000Bridge\bridge.log`.
- **Controles+LEDs agora permanentes e independentes.** Dependência restante: teVirtualMIDI64.dll (instalada via loopMIDI) — o instalador final (Fase 3) deve embuti-la + WinUSB INF (dispensar Zadig).
- Nota USB: device é USB 1.1 FullSpeed; áudio isócrono pode falhar em portas xHCI 3.0 → recomendar USB 2.0 ou hub 2.0. Não corrigível por software.

> **ATUALIZAÇÃO (2026-07-29) — a última linha do segundo item é a única coisa deste
> documento que o desenho previa e que se provou PROIBIDA, não apenas difícil:**
>
> - **embutir a teVirtualMIDI é vedado por cláusula**, e a cláusula aparece em dois lugares
>   independentes: a página do autor ("The software referenced here may not be distributed
>   via any means without prior written consent by the author") e o **EULA que vem dentro do
>   próprio `loopMIDISetup.exe`** ("Distribution in any form without prior written
>   permission by the author is prohibited!"). Ou seja: a cláusula que proíbe embutir viaja
>   dentro do arquivo que seria embutido. Existe um módulo MSI redistribuível
>   (`teVM64BurnEV.msi`), mas **só para licenciados**;
> - **o WinUSB INF dispensando o Zadig** está fora pelo motivo já escrito na nota da Fase 3
>   (catálogo assinado + regra de custo zero);
> - o que fica, e é entregável: o instalador **conduz** — detecta, explica, abre a página
>   oficial de cada dependência e reconfere depois. Nada de terceiro dentro do nosso
>   arquivo.
>
> Duas notas de fato sobre este mesmo bloco: a `teVirtualMIDI64.dll` é a interface de um
> **driver de kernel** (`teVirtualMIDI64.sys`, provado pela tabela de arquivos do MSI), e é
> por isso que o passo do loopMIDI é o que **pode pedir reinício** — nada que o **nosso**
> instalador faz pede reiniciar o Windows. E o `BcdAsio.dll` **perdeu por completo** a
> dependência da teVirtualMIDI no passo 2.2 (verificado por dentro do binário: as cadeias
> `teVirtualMIDI64.dll` e `virtualMIDICreatePortEx2` estão **ausentes**); ela é hoje
> requisito de **um único** programa, o `BCD3000Bridge.exe`.

## Fase 2 — spike de áudio (VALIDADO, ambas direções)

- Isócrono via WinUSB (Win8.1+ API) funciona em user-mode. Scripts: `poc/iso_test2.py` (playback), `poc/iso_capture.py` (captura) — **os dois, mais `poc/iso_test.py`, foram removidos em `36f7b46`** depois de o motor em C++ (`native/bcdasio/audioengine.cpp`) substituí-los; o que eles provaram está registrado nos dois itens abaixo.
- **Playback (EP 0x02, IF1 alt 1):** tom de 440Hz saiu no fone (usuário confirmou). `WinUsb_SetCurrentAlternateSetting`+`RegisterIsochBuffer`+`WriteIsochPipeAsap`. Nota: `GetOverlappedResult` reporta 0 bytes em iso ASAP (`ovlOk=True`, e1=997 IO_PENDING) — é quirk de reporte, o áudio flui.
- **Captura (EP 0x83, IF2 alt 1):** `ReadIsochPipeAsap` + array USBD_ISO_PACKET_DESCRIPTOR. 90 pacotes/chunk, 31760 bytes, amostras -10..7 (noise floor do ADC = correto sem sinal).
- Soluços ocasionais no loop serial de Python (sem double-buffer) → resolver no C++ com buffers múltiplos em voo.
- **Conclusão: áudio in/out viável. Próximo: driver ASIO em C++** (precisa Visual Studio C++ + ASIO SDK Steinberg). Formatos: 4ch/16-bit/44.1kHz fixos, EP OUT 0x02 (adaptive), EP IN 0x83 (async), 360 bytes/pkt.

> **ATUALIZAÇÃO (2026-07-29):** os soluços foram resolvidos como previsto — **3
> transferências em voo por direção**, blocos de 3.528 bytes (441 frames = 10 ms exatos).
> O `wMaxPacketSize` de **360 bytes** foi confirmado no hardware nos dois endpoints. O
> "precisa ASIO SDK Steinberg" **deixou de valer** (ver a nota da Fase 3): o SDK foi cortado
> do produto.

## Fase 2 — Passo 2.1 CONCLUÍDO (driver ASIO carrega no VirtualDJ)

- Toolchain pronto: MSVC (VS BuildTools 2026, `vcvars64.bat` em `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\`) + Windows SDK + WinUSB (testado com `native/test_toolchain.cpp`). ASIO SDK em `native/ASIOSDK/`.
- Driver ASIO esqueleto em `native/bcdasio/` (baseado em `driver/asiosample`): CLSID próprio `{B0D3000A-51E7-4C2B-9F3A-1234ABCD5678}`, nome de registro "Behringer BCD3000", DLL `BcdAsio.dll`, `getDriverName`="BCD3000". Fontes: `asiosmpl.cpp` (customizado), `asiosmpl.h`, `wintimer.cpp`, `dllmain.cpp` (ponte DllMain→DllEntryPoint), `BcdAsio.def`. `makesamp.cpp` REMOVIDO da build (era Mac-only).
- Build: `native/bcdasio/build.bat` (tem `cd /d "%~dp0"`; invocar por caminho completo). Registro: `register.bat`/`unregister.bat` via regsvr32 (precisa admin/UAC).
- Registrado em `HKLM\SOFTWARE\ASIO\Behringer BCD3000`. **VirtualDJ carrega sem erro.** Ainda usa áudio "fake" do exemplo (16 canais genéricos "Sample"). ASIO antigo de terceiros "BCD3000 ASIO" removido.
- Gotcha: rebuild falha se VirtualDJ estiver com o DLL carregado (fechar VDJ antes). cmd nesta máquina não herda cwd → usar caminho completo / `cd /d %~dp0`. vcvars imprime "vswhere não reconhecido" (inofensivo).

> **ATUALIZAÇÃO (2026-07-29) — nenhum dos arquivos citados neste bloco existe mais no
> repositório, e a razão é diferente em cada caso:**
>
> - `native/test_toolchain.cpp` — **removido em `36f7b46`**. Era teste de fumaça do
>   toolchain; nenhum `build.bat` o mencionava. Ele provou o que existia para provar: MSVC +
>   Windows SDK + WinUSB compilando e ligando nesta máquina;
> - `asiosmpl.cpp` / `asiosmpl.h` — **renomeados** para `bcdasio.cpp` / `bcdasio.h` no passo
>   2.2, e reescritos por dentro (a classe `AsioSample` virou `BcdAsioDriver`). O CLSID e o
>   nome `BcdAsio.dll` foram **preservados de propósito**, e é por isso que o registro feito
>   aqui, no 2.1, continuou valendo — o teste do passo 2.2 no VirtualDJ **não precisou de
>   UAC**;
> - `wintimer.cpp` e `makesamp.cpp` — vinham do exemplo da Steinberg e **saíram junto com o
>   SDK**. A única função que ainda importava, `getNanoSeconds()`, foi reescrita como nossa
>   em `native/bcdasio/nanoclock.cpp`;
> - `native/ASIOSDK/` — **fora do rastreamento** desde `56d84bb` (segue no disco desta
>   máquina, e é o que permite rodar `build.bat abicheck`). O texto BSD-3 do exemplo que deu
>   origem ao `bcdasio.cpp` está preservado em `native/bcdasio/LICENSE-asiosample.txt`.

## Passo 2.2 — áudio real: CONCLUÍDO E VALIDADO NO HARDWARE

**O que o desenho previa** (escrito em 2026-07-27, mantido como registro):

> Reescrever o miolo do `asiosmpl.cpp`: reportar **4 saídas** (Master L/R, Phones L/R) + **4 entradas** com nomes reais (`getChannels`, `getChannelInfo`), formato 16-bit/44.1kHz; integrar backend WinUSB isócrono (EP 0x02 OUT via IF1 alt1, EP 0x83 IN via IF2 alt1) com **double-buffer** e sincronizado ao clock do device; converter entre buffers ASIO e o formato do device. Referência de streaming: `poc/iso_test2.py` e `poc/iso_capture.py`. Handles: `WinUsb_GetAssociatedInterface(base, 0/1/2)` = IF1/IF2/IF3.

Os três arquivos citados nessa passagem — `asiosmpl.cpp`, `poc/iso_test2.py` e
`poc/iso_capture.py` — **não existem mais**; o que aconteceu com cada um está nas notas das
duas seções anteriores.

**O que foi construído** (2026-07-28 e 2026-07-29). O previsto acima foi entregue, e mais
quatro coisas que o desenho **não** previa — as três primeiras porque um teste de hardware
derrubou uma premissa:

1. **Canais.** Saídas: `Master L`, `Master R`, `Phones L`, `Phones R`. Entradas: `Phono A L`,
   `Phono A R`, `Phono/Line B L`, `Phono/Line B R` — o mapeamento foi **provado por três
   testes cruzados** (fio no pino central do conector A L saturou o ch1 em 32768 com
   vazamento decrescente ch2 > ch3 > ch4; fio em B L saturou o ch3 com ch4 > ch1 > ch2; a
   chave PHONO/LINE de B mexeu em ch3/ch4 e não tocou ch1/ch2). Os nomes moram numa **tabela
   de perfil de aparelho** no topo de `native/bcdasio/usbdev.cpp`, não em constantes
   espalhadas, e um teste unitário os compara literalmente.
2. **O driver assume o aparelho INTEIRO** — IF1, IF2 **e IF3** — e devolve depois, por
   passagem de bastão. Ele **não cria porta MIDI**: repassa os bytes por um canal local para
   o `BCD3000Bridge.exe`, que é o dono permanente da porta. Ver a atualização da seção
   "Decisão final", acima.
3. **Correção de deriva de relógio**, que o desenho tinha adiado para o passo 2.4: acima de
   uma marca d'água no anel de entrada (4 blocos do host), o motor **descarta 1 frame por
   transferência** — 1 amostra por vez em vez de um pacote de 44, de propósito, porque 44 de
   uma vez produziriam um clique periódico. Sem ela o anel de entrada enchia em ~12 min e os
   overruns começavam.
4. **Perfil de aparelho em tabela**, com a **BCD2000 como entrada experimental** (VID:PID
   1397:00BD; o lado do áudio é idêntico ao da BCD3000, o dos controles é outro aparelho —
   framing MIDI proprietário e sequência de inicialização de 52 bytes, nada disso
   implementado). **Ninguém no projeto tem uma BCD2000**: esse caminho sai com falha limpa e
   log claro, e é a única parte do projeto que nunca passou pelo hardware.

**Medições que fecham o passo** (todas com o binário atual, que já contém o corte do SDK e
o perfil de aparelho):

| O que | Medido |
|---|---|
| Corrida mais longa | **82 min 56 s contínuos**, 219.423.519 frames |
| Taxa média | **44.095,7 amostras/s** (exigido 44.100 ± 50) |
| Contadores | underruns=**0**, overruns=**0**, fomes de entrada=**0** |
| Correção de deriva | 48.441 correções = **77,9 B/s** de compensação, contra **79 B/s** de deriva medidos em outra corrida; nível do anel **não subiu** em 83 min |
| MIDI na mesma corrida | pacotes=1670, injetados=1670, leds=18903, perdidos=**0**, conexões do canal=**1** |
| Porta MIDI virtual | criada **1 vez**, atravessando 20 h, 5 cargas do driver, 2 desconexões de cabo e 2 instalações |
| Cabo arrancado tocando | aparelho retomado na **tentativa 1**, áudio e controles voltaram **sem reabrir o software de DJ** |
| Critérios de aceite | **11 de 11** (os 8 originais mais 3 que nasceram no caminho) |

**O que continua fora**, e é honesto dizer: a latência informada ao host é **estimada, sem
medição de loopback** (a medição definitiva é passo 2.4), e o ajuste de latência mínima
também. Traktor e Mixxx nunca foram testados. Detalhe completo, com cada divergência e a
medição que a causou, em `docs/superpowers/specs/2026-07-27-bcd3000-audio-asio-design.md`
(corpo + três adendos).

## Estado do ambiente (2026-07-27)

Python 3.11 ✅, git ✅, pip ✅, pyusb/libusb ✅, python-rtmidi ✅. Falta p/ Fase 2: Visual Studio C++/ASIO SDK. Fase 1 usa porta MIDI virtual (loopMIDI/teVirtualMIDI).

> **ATUALIZAÇÃO (2026-07-29):** nada falta mais para a Fase 2 — ela está concluída. O
> toolchain (MSVC + Windows SDK + WinUSB) está no lugar e o **ASIO SDK deixou de ser
> requisito** do produto. `pyusb`/`libusb` e `python-rtmidi` foram usados só nos POCs e **não
> são dependência de nada vivo**: o `poc/bridge_service.py` usa apenas API nativa do Windows
> por `ctypes`. A porta MIDI virtual é da teVirtualMIDI (instalada via loopMIDI) e pertence
> **permanentemente** ao `BCD3000Bridge.exe`.

// Testes unitarios das pecas puras do driver BCD3000.
// Rodar: build.bat tests  &&  tests.exe
//
// Ha tambem um MODO ARNES, que nao roda teste nenhum:
//   tests.exe rele-cliente <pacotes> <ms>
// Ele usa o RelayLink real como cliente do canal de MIDI, para provar o contrato
// contra o servidor em Python do BCD3000Bridge.exe. Ver o fim do arquivo.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include "log.h"
#include "format.h"
#include "ringbuf.h"
#include "handoff.h"
#include "midibridge.h"
// So pelas funcoes inline inHighWaterBytes/ringBytesFor e por
// kInHighWaterBlocks. Nada aqui usa a classe AudioEngine, que vive no
// audioengine.cpp e nao entra no alvo `tests` do build.bat.
#include "audioengine.h"
#include "nanoclock.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            printf("  FALHOU linha %d: %s\n", __LINE__, #cond);            \
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

//----------------------------------------------------------------------
static void test_log(void)
{
    printf("test_log\n");

    char path[512];

    // Sem logInit, logWrite nao pode quebrar e o caminho tem de vir vazio.
    bcd::logWrite("isso deve ser ignorado");
    bcd::logPath(path, sizeof(path));
    CHECK(strlen(path) == 0);

    CHECK(bcd::logInit("teste_unitario.log"));
    bcd::logPath(path, sizeof(path));
    CHECK(strlen(path) > 0);
    // O caminho tem de apontar para a pasta do BCD3000Bridge.
    CHECK(strstr(path, "BCD3000Bridge") != 0);
    CHECK(strstr(path, "teste_unitario.log") != 0);

    bcd::logWrite("linha de teste %d", 42);
    bcd::logClose();
    bcd::logPath(path, sizeof(path));
    CHECK(strlen(path) == 0);

    // logPath nunca pode estourar o buffer do chamador.
    char tiny[4];
    bcd::logInit("teste_unitario.log");
    memset(tiny, 0x7F, sizeof(tiny));
    bcd::logPath(tiny, (int)sizeof(tiny));
    CHECK(strlen(tiny) <= 3);

    // O arquivo tem de existir e conter o texto escrito.
    bcd::logPath(path, sizeof(path));
    bcd::logClose();

    FILE* f = fopen(path, "rb");
    CHECK(f != 0);
    if (f) {
        char buf[1024];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = 0;
        fclose(f);
        CHECK(strstr(buf, "linha de teste 42") != 0);
        remove(path);
    }
}

//----------------------------------------------------------------------
static void test_format_constantes(void)
{
    printf("test_format_constantes\n");

    CHECK(bcd::kChannels == 4);
    CHECK(bcd::kBytesPerFrame == 8);
    CHECK(bcd::kSampleRate == 44100);

    // O bloco tem de dar exatamente 10 ms de audio a 44100 Hz.
    CHECK(bcd::kFramesPerBlock == 441);
    CHECK(bcd::kBlockBytes == 3528);
    CHECK(bcd::kFramesPerBlock * 100 == bcd::kSampleRate);
    CHECK(bcd::kBlockBytes == bcd::kFramesPerBlock * bcd::kBytesPerFrame);
    CHECK(bcd::kUsbFramesPerBlock == 10);
}

//----------------------------------------------------------------------
// Um nome de canal nunca pode sugerir um conector que ninguem verificou. Nome
// generico e honesto; nome inventado manda o usuario ligar o cabo no lugar errado
// confiando num rotulo que nao foi medido.
static bool sugereConector(const char* nome)
{
    static const char* const proibidas[] = { "Master", "Phones", "Phono",
                                             "Line", "Mic", "Booth" };
    for (int i = 0; i < (int)(sizeof(proibidas) / sizeof(proibidas[0])); i++)
        if (strstr(nome, proibidas[i]) != 0)
            return true;
    return false;
}

//----------------------------------------------------------------------
// A TABELA DE PERFIS e a escolha de perfil sao dado puro e logica pura, e por isso
// cabem aqui inteiras: nenhuma linha deste teste toca em registro, USB ou hardware.
//
// E e onde ha de ser: ninguem neste projeto tem uma BCD2000, e o caminho dela vai
// ser publicado sem nunca ter rodado. O que da para provar sem o aparelho e
// exatamente isto - a tabela, a chave montada, a ordem de busca e a recusa dos
// controles -, e o que nao da esta marcado como suposicao na propria tabela.
static void test_perfis_de_aparelho(void)
{
    printf("test_perfis_de_aparelho\n");

    // ---- forma da tabela ----
    CHECK(bcd::profileCount() == 2);
    CHECK(bcd::profileAt(0) != 0);
    CHECK(bcd::profileAt(1) != 0);
    // Fora da tabela e 0, e nao um ponteiro para o vizinho.
    CHECK(bcd::profileAt(-1) == 0);
    CHECK(bcd::profileAt(2) == 0);
    CHECK(bcd::profileAt(1000) == 0);

    const bcd::DeviceProfile* const p3000 = bcd::profileAt(0);
    const bcd::DeviceProfile* const p2000 = bcd::profileAt(1);

    // ---- A ORDEM DE BUSCA. Esta assercao e o contrato do item 2 da tarefa: a
    // BCD3000 e procurada PRIMEIRO. Ela e o unico modelo validado no hardware, e na
    // maquina do dono ela casa na primeira volta - o perfil experimental nunca
    // chega a ser consultado. Inverter a ordem poria um perfil que nunca rodou na
    // frente do que esta em producao.
    CHECK(strcmp(p3000->model, "BCD3000") == 0);
    CHECK(strcmp(p2000->model, "BCD2000") == 0);
    CHECK(p3000->provenOnHardware);
    CHECK(!p2000->provenOnHardware);
    // O perfil padrao - o que responde quando nenhuma busca casou - e o validado.
    CHECK(&bcd::defaultProfile() == p3000);

    // ---- busca por PID ----
    CHECK(bcd::profileForPid(0x00BF) == p3000);
    CHECK(bcd::profileForPid(0x00BD) == p2000);
    // PID desconhecido nao pode cair num perfil "parecido".
    CHECK(bcd::profileForPid(0x0000) == 0);
    CHECK(bcd::profileForPid(0x00BE) == 0);
    CHECK(bcd::profileForPid(0x00B0) == 0);
    CHECK(bcd::profileForPid(0xFFFF) == 0);
    // E nenhum PID pode aparecer duas vezes: profileForPid seria ambigua.
    for (int i = 0; i < bcd::profileCount(); i++)
        CHECK(bcd::profileForPid(bcd::profileAt(i)->pid) == bcd::profileAt(i));
    CHECK(p3000->pid != p2000->pid);
    // Mesmo fabricante nos dois.
    CHECK(p3000->vid == 0x1397);
    CHECK(p2000->vid == 0x1397);

    // ---- A CHAVE DO REGISTRO, MONTADA, CONTRA A CONSTANTE HISTORICA ----
    //
    // Esta e a assercao que protege o caminho validado no hardware. O literal
    // abaixo e, caractere por caractere, a constante `kEnumKey` que existia em
    // usbdev.cpp antes de a tabela de perfis existir. Se a montagem divergir dela em
    // um unico byte - caixa do hexadecimal, separador, ordem -, o driver deixa de
    // achar a BCD3000 na maquina do dono.
    const char* const kChaveHistorica3000 =
        "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_1397&PID_00BF&MI_00";

    char chave[bcd::kEnumKeyMax];
    CHECK(bcd::profileEnumKey(*p3000, chave, (int)sizeof(chave)));
    CHECK(strcmp(chave, kChaveHistorica3000) == 0);

    // A da BCD2000 difere SO no PID - o resto e a mesma convencao do enumerador USB.
    CHECK(bcd::profileEnumKey(*p2000, chave, (int)sizeof(chave)));
    CHECK(strcmp(chave,
                 "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_1397&PID_00BD&MI_00") == 0);

    // Todo perfil da tabela tem de caber em kEnumKeyMax; um que nao caiba seria uma
    // chave pela metade indo para o RegOpenKeyEx.
    for (int i = 0; i < bcd::profileCount(); i++) {
        char k[bcd::kEnumKeyMax];
        CHECK(bcd::profileEnumKey(*bcd::profileAt(i), k, (int)sizeof(k)));
        CHECK(strlen(k) > 0);
    }

    // Buffer justo: cabe com exatamente strlen+1.
    {
        const int exato = (int)strlen(kChaveHistorica3000) + 1;
        char justo[128];
        CHECK(exato <= (int)sizeof(justo));
        CHECK(bcd::profileEnumKey(*p3000, justo, exato));
        CHECK(strcmp(justo, kChaveHistorica3000) == 0);
    }
    // Buffer curto: false E buffer VAZIO. Meia chave de registro seria pior que
    // nenhuma - ela apontaria para outro no.
    {
        char curto[16];
        memset(curto, 0x7F, sizeof(curto));
        CHECK(!bcd::profileEnumKey(*p3000, curto, (int)sizeof(curto)));
        CHECK(curto[0] == 0);
    }
    // Argumentos degenerados nao podem escrever em lugar nenhum.
    CHECK(!bcd::profileEnumKey(*p3000, 0, 64));
    {
        char um[1];
        um[0] = 0x7F;
        CHECK(!bcd::profileEnumKey(*p3000, um, 0));
        CHECK(um[0] == 0x7F);          // size 0: nem o terminador
    }

    // ---- OS ENDPOINTS E INDICES DA BCD3000, CONTRA AS CONSTANTES HISTORICAS ----
    //
    // Os nove literais abaixo sao os valores que estavam em usbdev.h e que foram
    // compilados no DLL validado no hardware (7min54s de musica, underruns=0,
    // overruns=0, MIDI e LEDs vivos). Mudar qualquer um deles nao da erro de
    // compilacao em lugar nenhum - da silencio, ou audio que nao sobe.
    CHECK(p3000->epPlayback   == 0x02);   // era kEpPlayback
    CHECK(p3000->epCapture    == 0x83);   // era kEpCapture
    CHECK(p3000->epControls   == 0x81);   // era kEpControls
    CHECK(p3000->epLeds       == 0x01);   // era kEpLeds
    CHECK(p3000->altStreaming == 1);      // era kAltStreaming
    CHECK(p3000->altIdle      == 0);      // era kAltIdle
    CHECK(p3000->assocPlayback == 0);     // era kAssocPlayback -> IF1
    CHECK(p3000->assocCapture  == 1);     // era kAssocCapture  -> IF2
    CHECK(p3000->assocMidi     == 2);     // era kAssocMidi     -> IF3
    CHECK(strcmp(p3000->functionId, "MI_00") == 0);

    // Os tres indices de associacao tem de ser distintos: dois iguais entregariam
    // o MESMO handle de interface para dois papeis diferentes.
    CHECK(p3000->assocPlayback != p3000->assocCapture);
    CHECK(p3000->assocCapture  != p3000->assocMidi);
    CHECK(p3000->assocPlayback != p3000->assocMidi);
    // Sentido dos endpoints: bit 7 ligado = IN (aparelho -> PC).
    CHECK((p3000->epPlayback & 0x80) == 0);      // OUT
    CHECK((p3000->epCapture  & 0x80) == 0x80);   // IN
    CHECK((p3000->epControls & 0x80) == 0x80);   // IN
    CHECK((p3000->epLeds     & 0x80) == 0);      // OUT

    // A BCD2000 herda os MESMOS endpoints, e isso e FATO pesquisado (driver Linux
    // anyc/snd-bcd2000): 0x02/0x83 para audio, 0x81/0x01 para controles e LEDs.
    CHECK(p2000->epPlayback == p3000->epPlayback);
    CHECK(p2000->epCapture  == p3000->epCapture);
    CHECK(p2000->epControls == p3000->epControls);
    CHECK(p2000->epLeds     == p3000->epLeds);

    // ---- O PROTOCOLO DE CONTROLES, E A RECUSA ----
    //
    // A BCD3000 fala USB-MIDI 1.0 e e suportada. A BCD2000 tem enquadramento
    // proprietario (prefixo + tamanho) e exige 52 bytes de inicializacao, nada disso
    // implementado - o perfil dela TEM de marcar o protocolo como nao suportado, e a
    // ponte TEM de recusar. Sem esta recusa a ponte leria bytes de enquadramento
    // como pacotes de 4 bytes e injetaria mensagens aleatorias na porta MIDI.
    CHECK(p3000->controlProtocol == bcd::kCtrlUsbMidi10);
    CHECK(p2000->controlProtocol == bcd::kCtrlBcd2000Proprietary);
    CHECK(p3000->controlProtocol != p2000->controlProtocol);
    CHECK(bcd::bridgeSupportsProfile(*p3000));
    CHECK(!bcd::bridgeSupportsProfile(*p2000));
    // Todo perfil nao suportado tem de ser tambem nao validado: um modelo cujos
    // controles nao funcionam nao pode ser anunciado como provado.
    for (int i = 0; i < bcd::profileCount(); i++) {
        const bcd::DeviceProfile* const p = bcd::profileAt(i);
        CHECK(bcd::bridgeSupportsProfile(*p) || !p->provenOnHardware);
    }
    // Nome do protocolo: nunca nulo, nunca vazio, e diferente entre os dois - ele
    // vai para o log e e o que explica a recusa a quem le.
    CHECK(bcd::controlProtocolName(bcd::kCtrlUsbMidi10) != 0);
    CHECK(strlen(bcd::controlProtocolName(bcd::kCtrlUsbMidi10)) > 0);
    CHECK(strlen(bcd::controlProtocolName(bcd::kCtrlBcd2000Proprietary)) > 0);
    CHECK(strcmp(bcd::controlProtocolName(bcd::kCtrlUsbMidi10),
                 bcd::controlProtocolName(bcd::kCtrlBcd2000Proprietary)) != 0);
    CHECK(strlen(bcd::controlProtocolName(12345)) > 0);   // valor de fora da enum

    // ---- OS NOMES DE CANAL DA BCD3000: EXATAMENTE OS PROVADOS NO HARDWARE ----
    //
    // Estes oito literais custaram TRES TESTES CRUZADOS no aparelho:
    //  - saidas, com o driver embutido do Windows: 1 e 2 = Master, 3 e 4 = fone;
    //  - entradas 1 e 2: fio no pino central do conector A L saturou o ch1 com
    //    vazamento decrescente em ch2 > ch3 > ch4;
    //  - entradas 3 e 4: a chave PHONO/LINE da entrada B alterou ch3 e ch4 e nao
    //    tocou em ch1/ch2.
    //
    // ESTE BLOCO EXISTE CONTRA UM DEFEITO ESPECIFICO: alguem "arrumar" os nomes
    // (ordem, grafia, "melhorar" o rotulo) e quebrar o mapeamento. O sintoma seria o
    // usuario ligar o master no fone e nao entender por que, sem nenhum erro em
    // lugar nenhum.
    CHECK(strcmp(p3000->outNames[0], "Master L") == 0);
    CHECK(strcmp(p3000->outNames[1], "Master R") == 0);
    CHECK(strcmp(p3000->outNames[2], "Phones L") == 0);
    CHECK(strcmp(p3000->outNames[3], "Phones R") == 0);
    CHECK(strcmp(p3000->inNames[0], "Phono A L") == 0);
    CHECK(strcmp(p3000->inNames[1], "Phono A R") == 0);
    CHECK(strcmp(p3000->inNames[2], "Phono/Line B L") == 0);
    CHECK(strcmp(p3000->inNames[3], "Phono/Line B R") == 0);

    // ---- OS NOMES DA BCD2000: GENERICOS, PORQUE O MAPEAMENTO E DESCONHECIDO ----
    for (int i = 0; i < bcd::kChannels; i++) {
        CHECK(!sugereConector(p2000->outNames[i]));
        CHECK(!sugereConector(p2000->inNames[i]));
    }
    // E genericos nao quer dizer vazios nem repetidos: cada canal tem de ser
    // distinguivel na lista do software de DJ.
    for (int i = 0; i < bcd::profileCount(); i++) {
        const bcd::DeviceProfile* const p = bcd::profileAt(i);
        for (int a = 0; a < bcd::kChannels; a++) {
            CHECK(p->outNames[a] != 0 && strlen(p->outNames[a]) > 0);
            CHECK(p->inNames[a]  != 0 && strlen(p->inNames[a])  > 0);
            for (int b = a + 1; b < bcd::kChannels; b++) {
                CHECK(strcmp(p->outNames[a], p->outNames[b]) != 0);
                CHECK(strcmp(p->inNames[a],  p->inNames[b])  != 0);
            }
        }
    }
    // Todo nome tem de caber nos 32 bytes do ASIOChannelInfo::name, com o
    // terminador. Este e o teste que o desenho da tabela deixou como UNICO limite dos
    // nomes, e por isso ele continua aqui - mas ele deixou de ser a unica barreira: o
    // getChannelInfo() passou a copiar com limite em vez de strcpy, entao um nome
    // comprido acrescentado por quem nao roda os testes chega TRUNCADO ao software de DJ
    // em vez de estourar o buffer do host. Esta assercao e que reprova o nome ANTES de
    // alguem publicar; a copia limitada e a rede para quando ela nao rodar.
    for (int i = 0; i < bcd::profileCount(); i++) {
        const bcd::DeviceProfile* const p = bcd::profileAt(i);
        for (int c = 0; c < bcd::kChannels; c++) {
            CHECK(strlen(p->outNames[c]) < 32);
            CHECK(strlen(p->inNames[c])  < 32);
        }
    }

    // ---- O NOME DA FONTE DE RELOGIO, MONTADO ----
    // getClockSources() monta "<modelo> Internal" num campo de 32 bytes. Para a
    // BCD3000 tem de dar o MESMO texto que era constante antes desta tarefa.
    for (int i = 0; i < bcd::profileCount(); i++) {
        char nome[32];
        const int n = _snprintf(nome, sizeof(nome) - 1, "%s Internal",
                                bcd::profileAt(i)->model);
        nome[sizeof(nome) - 1] = 0;
        CHECK(n > 0);                       // nao truncou
        CHECK(strlen(nome) < sizeof(nome));
        if (i == 0)
            CHECK(strcmp(nome, "BCD3000 Internal") == 0);
    }

    // ---- O PERFIL EXPOSTO PELO UsbDevice ----
    //
    // Um UsbDevice recem-construido nao procurou nada: matchedProfile() e 0, e
    // profile() cai no perfil validado. Esta segunda parte e load-bearing e nao
    // conveniencia - getChannelInfo() do ASIO responde ANTES de o aparelho ser
    // encontrado, e a resposta correta nesse caso e a mesma que este metodo dava
    // antes de existir tabela nenhuma: a da BCD3000.
    //
    // Roda sem hardware: um UsbDevice construido nao abre nada.
    {
        bcd::UsbDevice dev;
        CHECK(!dev.isOpen());
        CHECK(dev.matchedProfile() == 0);
        CHECK(&dev.profile() == p3000);
        CHECK(strcmp(dev.profile().model, "BCD3000") == 0);
        CHECK(strcmp(dev.profile().outNames[0], "Master L") == 0);
    }
}

//----------------------------------------------------------------------
// AS MENSAGENS QUE CHEGAM AO SOFTWARE DE DJ, E O LIMITE DELAS
//
// POR QUE ESTE TESTE EXISTE, e ele nasceu de um defeito de CAMPO e nao de uma revisao.
// Em 2026-07-29 a mensagem de "interface inativa" chegou a 217 caracteres. O
// getErrorMessage() do ASIO copiava com `strcpy` para um buffer DO HOST cujo contrato
// (metodo 4 de asioapi.h) e "no maximo 124 bytes com o terminador": quatro bytes alem do
// fim, dentro do processo do software de DJ, quatro vezes no mesmo dia. E o corte caiu no
// meio da SEGUNDA causa - o dono leu "outro programa pode estar com o aparelh" enquanto a
// causa verdadeira, cabo mal encaixado, era apagada da tela E do log.
//
// Limitar a copia sozinho trocaria estouro por MENTIRA SILENCIOSA. O que impede a
// reincidencia e a assercao daqui: toda mensagem que a busca do usbdev pode produzir e
// renderizada, com pior caso de argumento, e tem de caber no contrato. A proxima mensagem
// longa quebra o teste antes de chegar ao dono.
//
// O ACOPLAMENTO E ESTRUTURAL, e nao uma lista copiada a mao: o laco percorre a FAIXA da
// enum FindStage. Um estagio novo, com mensagem nova, entra no laco sozinho.
//
// O QUE ESTE TESTE NAO ALCANCA, dito por escrito: as mensagens do audioengine.cpp, que
// nao entra no alvo `tests`, e as do fail() do usbdev, cujos textos sao literais em
// pontos de chamada e nao uma tabela. As 31 mensagens que podem chegar ao host foram
// MEDIDAS uma a uma nesta rodada - 16 do usbdev, 15 do motor - e a mais longa depois da
// correcao tem 116 caracteres. Para o que sobra, o BcdAsioDriver::setError() registra uma
// linha de ATENCAO no log quando uma mensagem nao cabe.
//
// E ELE COBRE DUAS FORMAS, e nao uma: a CURTA, que vai ao host e tem de caber nos 124
// bytes, e a LONGA, que vai ao LOG e NAO tem esse limite - o log e a ferramenta de
// diagnostico deste projeto e nao tem contrato com ninguem. Uma assercao de 124 sobre a
// forma longa destruiria o objetivo dela, entao ela nao existe aqui de proposito. O que
// se afirma sobre a longa e a LEI DE COMPOSICAO e o limite do buffer em que o findPath a
// renderiza.

// TODO byte de TODA mensagem tem de estar em 0x20..0x7E.
//
// ASCII sem acento e DECISAO e nao descuido: getErrorMessage e `char*` sem codificacao
// declarada, e um host que renderize como UTF-8 mostraria "nao" acentuado como mojibake
// exatamente na tela que existe para nao enganar. A mesma faixa serve para o log, onde um
// byte de controle vindo do registro FORJA LINHA (ver o bloco do kStageBadGuid abaixo).
//
// A funcao tem autoteste logo no inicio do teste: sem ele, um erro aqui tornaria vazias
// todas as assercoes que dependem dela, que e a forma classica de um teste passar sem
// verificar nada.
static bool soAsciiImprimivel(const char* s)
{
    if (!s)
        return true;
    for (const char* p = s; *p; p++) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7E)
            return false;
    }
    return true;
}

static void test_mensagens_do_host(void)
{
    printf("test_mensagens_do_host\n");

    // ---- autoteste do verificador de ASCII ----
    // Sem estas quatro linhas, uma soAsciiImprimivel() que devolvesse sempre true faria
    // ~40 assercoes deste teste passarem sem verificar nada.
    {
        CHECK(soAsciiImprimivel("abc 0123 ~!/:()?"));
        CHECK(!soAsciiImprimivel("a\nb"));
        // `unsigned char` e conversao do PONTEIRO, e nao `char` com cast do valor: 0x80
        // nao cabe num char assinado e o cast trunca, o que e o aviso C4310 - que o alvo
        // strict trata como ERRO. Montar em unsigned char diz a mesma coisa sem truncar.
        unsigned char alto[3];
        alto[0] = 'a'; alto[1] = 0x7F; alto[2] = 0;
        CHECK(!soAsciiImprimivel((const char*)alto));
        alto[1] = 0x80;
        CHECK(!soAsciiImprimivel((const char*)alto));
    }

    // ---- o limite, e a fronteira exata dele ----
    CHECK(bcd::kAsioErrorMax == 124);          // metodo 4 de asioapi.h
    CHECK(bcd::diagnosticFitsAsio(0));         // nada a copiar
    CHECK(bcd::diagnosticFitsAsio(""));
    {
        char justo[200];
        // 123 caracteres + terminador = 124 = o contrato inteiro: cabe.
        memset(justo, 'x', sizeof(justo));
        justo[bcd::kAsioErrorMax - 1] = 0;
        CHECK((int)strlen(justo) == bcd::kAsioErrorMax - 1);
        CHECK(bcd::diagnosticFitsAsio(justo));
        // 124 caracteres + terminador = 125: um byte alem, e e exatamente esse byte que
        // o defeito escrevia na memoria do host.
        memset(justo, 'x', sizeof(justo));
        justo[bcd::kAsioErrorMax] = 0;
        CHECK((int)strlen(justo) == bcd::kAsioErrorMax);
        CHECK(!bcd::diagnosticFitsAsio(justo));
    }

    // ---- TODA mensagem de busca, TODO perfil, PIOR CASO de argumento ----
    //
    // O buffer de saida e GRANDE de proposito. Com um buffer do tamanho do contrato a
    // mensagem sairia truncada e a assercao passaria SEMPRE - o teste tem de medir o
    // tamanho VERDADEIRO da mensagem, nao o do buffer.
    //
    // O `detail` de pior caso tem 200 caracteres porque ele NAO vem da nossa tabela: e o
    // valor que o Zadig gravou no registro, e HKLM e editavel por administrador.
    char pior[201];
    memset(pior, 'G', sizeof(pior) - 1);
    pior[sizeof(pior) - 1] = 0;

    int produzidas = 0;
    int semMensagem = 0;
    int comDica = 0;
    int semDica = 0;
    int maiorCurta = 0;
    int maiorLonga = 0;
    for (int i = 0; i < bcd::profileCount(); i++) {
        const bcd::DeviceProfile* const p = bcd::profileAt(i);
        // Um antes do primeiro estagio e um depois do ultimo: valor fora da faixa nao
        // pode inventar texto nem devolver o texto de outro estagio.
        for (int stage = bcd::kStageHardError - 1; stage <= bcd::findStageCount(); stage++) {
            char msg[512];
            memset(msg, 0x7F, sizeof(msg));
            const bool got = bcd::findStageMessage(stage, *p, pior,
                                                   msg, (int)sizeof(msg));
            // A FORMA LONGA, no MESMO laco e pelo MESMO motivo: um estagio novo entra
            // aqui sozinho, sem ninguem lembrar de acrescentar linha nenhuma.
            //
            // O buffer e maior que bcd::kDiagnosticLogMax de proposito - o mesmo cuidado
            // do buffer da forma curta, uma camada acima. Medindo dentro de um buffer do
            // tamanho do limite, a mensagem sairia truncada e a assercao passaria SEMPRE:
            // o teste tem de medir o tamanho VERDADEIRO da mensagem, nao o do buffer.
            char longa[2048];
            memset(longa, 0x7F, sizeof(longa));
            const bool gotLong = bcd::findStageMessageLong(stage, *p, pior,
                                                           longa, (int)sizeof(longa));
            // As duas formas existem ou faltam JUNTAS. Sem isto, um estagio poderia
            // chegar ao host com texto e ao log sem nenhum, ou o contrario.
            CHECK(gotLong == got);
            if (!got) {
                // Sem mensagem, o buffer tem de ficar VAZIO - nem o lixo de antes, nem
                // meia mensagem. Vale para as duas formas.
                semMensagem++;
                CHECK(msg[0] == 0);
                CHECK(longa[0] == 0);
                continue;
            }
            produzidas++;
            CHECK(strlen(msg) > 0);
            CHECK(bcd::diagnosticFitsAsio(msg));

            // ---- A LEI DE COMPOSICAO, e e ela que torna o fallback ESTRUTURAL ----
            //
            //     longa == curta                  quando nao ha dica
            //     longa == curta + " " + dica     quando ha
            //
            // Reconstruir a longa a partir das duas pecas PUBLICAS, em vez de congelar
            // uma terceira cadeia, e o que faz esta assercao valer para o estagio que
            // ainda nao existe: se alguem acrescentar um estagio e esquecer a dica, a lei
            // continua valendo (a longa vira a curta, que e o pior caso aceitavel e esta
            // escrito no codigo), e o denominador de dicas mais abaixo e que denuncia a
            // falta.
            const char* const dica = bcd::findStageHint(stage);
            char esperada[2048];
            if (dica)
                _snprintf(esperada, sizeof(esperada) - 1, "%s %s", msg, dica);
            else
                _snprintf(esperada, sizeof(esperada) - 1, "%s", msg);
            esperada[sizeof(esperada) - 1] = 0;
            CHECK(strcmp(longa, esperada) == 0);
            if (dica) {
                comDica++;
                CHECK(strlen(longa) > strlen(msg));
                // A dica e LITERAL: um '%' nela viraria argumento faltando no logWrite
                // do findPath, porque a longa e CONCATENADA e nao interpolada.
                CHECK(strchr(dica, '%') == 0);
            } else {
                semDica++;
                CHECK(strcmp(longa, msg) == 0);
            }
            // O UNICO limite da forma longa. NAO e 124 - o contrato de 124 e do host, e
            // uma assercao dele aqui destruiria o objetivo desta forma. O que ela nao
            // pode e ser truncada EM SILENCIO no log pelo buffer do findPath, que seria
            // repetir o defeito de 2026-07-29 com outro numero.
            CHECK((int)strlen(longa) + 1 <= bcd::kDiagnosticLogMax);
            CHECK(soAsciiImprimivel(msg));
            CHECK(soAsciiImprimivel(longa));
            if ((int)strlen(msg) > maiorCurta)
                maiorCurta = (int)strlen(msg);
            if ((int)strlen(longa) > maiorLonga)
                maiorLonga = (int)strlen(longa);
        }
    }
    // DENOMINADORES, publicados: 5 estagios de "nao achei" (kStageNoNode..kStageNotPresent)
    // e 3 sem mensagem (kStageHardError - 1, kStageHardError, kStageFound, mais o
    // findStageCount() que e um alem do ultimo = 4), para cada um dos 2 perfis.
    CHECK(bcd::findStageCount() == 6);
    CHECK(produzidas  == 5 * bcd::profileCount());
    CHECK(semMensagem == 4 * bcd::profileCount());
    // DENOMINADOR DAS DICAS: hoje os 5 estagios com mensagem TEM dica. Um estagio novo
    // sem dica cai aqui - e o comportamento dele continua CORRETO (a lei acima garante
    // que a longa vira a curta), so deixa de ser completo. E a diferenca entre "faltou" e
    // "quebrou", e este teste ja obrigava a decisao de qualquer forma: os dois
    // denominadores acima tambem mudam quando um estagio entra.
    CHECK(comDica == 5 * bcd::profileCount());
    CHECK(semDica == 0);
    // A forma longa E MAIOR que o contrato do host, e e para isso que ela existe. Se
    // algum dia ela couber em 124, alguem a encurtou de volta.
    CHECK(maiorCurta + 1 <= bcd::kAsioErrorMax);
    CHECK(maiorLonga + 1 >  bcd::kAsioErrorMax);
    CHECK(bcd::kDiagnosticLogMax > bcd::kAsioErrorMax);

    // Os estagios sem mensagem, nomeados um a um - o laco acima conta, este bloco diz
    // QUAIS. kStageFound achou o aparelho e nao tem o que explicar; kStageHardError ja
    // escreveu a mensagem dele no ponto exato da falha, e sobrescrever aqui apagaria o
    // "(erro N)" que e a unica pista daquele caminho.
    {
        const bcd::DeviceProfile* const p3000 = bcd::profileAt(0);
        char m[512];
        CHECK(!bcd::findStageMessage(bcd::kStageFound,     *p3000, 0, m, (int)sizeof(m)));
        CHECK(!bcd::findStageMessage(bcd::kStageHardError, *p3000, 0, m, (int)sizeof(m)));
        CHECK(!bcd::findStageMessage(9999,                 *p3000, 0, m, (int)sizeof(m)));
        // A forma LONGA recusa os mesmos tres, e a dica deles e nula. Sem estas linhas,
        // um estagio sem mensagem curta poderia ganhar uma linha de log com so a dica.
        CHECK(!bcd::findStageMessageLong(bcd::kStageFound,     *p3000, 0, m, (int)sizeof(m)));
        CHECK(!bcd::findStageMessageLong(bcd::kStageHardError, *p3000, 0, m, (int)sizeof(m)));
        CHECK(!bcd::findStageMessageLong(9999,                 *p3000, 0, m, (int)sizeof(m)));
        CHECK(bcd::findStageHint(bcd::kStageFound)     == 0);
        CHECK(bcd::findStageHint(bcd::kStageHardError) == 0);
        CHECK(bcd::findStageHint(9999)                 == 0);
        // Argumentos degenerados nao podem escrever em lugar nenhum.
        CHECK(!bcd::findStageMessage(bcd::kStageNoNode, *p3000, 0, 0, 64));
        CHECK(!bcd::findStageMessageLong(bcd::kStageNoNode, *p3000, 0, 0, 64));
        char um[1];
        um[0] = 0x7F;
        CHECK(!bcd::findStageMessage(bcd::kStageNoNode, *p3000, 0, um, 0));
        CHECK(um[0] == 0x7F);              // size 0: nem o terminador
        CHECK(!bcd::findStageMessageLong(bcd::kStageNoNode, *p3000, 0, um, 0));
        CHECK(um[0] == 0x7F);
    }

    // ---- OS TEXTOS, CONGELADOS ----
    //
    // Quatro destes cinco literais sao os que rodaram no hardware; o do kStageNotPresent
    // e o que esta rodada REESCREVEU, e o motivo esta escrito no usbdev.cpp. Congelar o
    // CONTEUDO importa tanto quanto o comprimento: e a ORDEM DAS CAUSAS que faz o dono
    // conferir o cabo antes de refazer o vinculo do Zadig, que e a operacao mais perigosa
    // de toda a instalacao. Um "melhoramento" de texto que reordene as causas passa no
    // teste de comprimento e quebra aqui.
    {
        const bcd::DeviceProfile* const p3000 = bcd::profileAt(0);
        char m[512];

        CHECK(bcd::findStageMessage(bcd::kStageNotPresent, *p3000, 0, m, (int)sizeof(m)));
        CHECK(strcmp(m,
              "a BCD3000 nao responde e ja funcionou aqui. Nesta ordem: "
              "1) reencaixe o cabo USB 2) feche outro programa/VM 3) Zadig") == 0);
        // Medida, e nao estimada: 116 caracteres, 117 bytes com o terminador, contra os
        // 124 do contrato. A versao que estourava tinha 217.
        CHECK(strlen(m) == 116);
        CHECK((int)strlen(m) + 1 <= bcd::kAsioErrorMax);
        // O cabo vem ANTES do Zadig no texto. E a assercao que carrega a licao do
        // incidente: o corte apagava justamente o cabo.
        CHECK(strstr(m, "cabo") != 0);
        CHECK(strstr(m, "Zadig") != 0);
        CHECK(strstr(m, "cabo") < strstr(m, "Zadig"));
        // *** "ja funcionou aqui" NAO E ENFEITE DE REDACAO. *** E a informacao
        // diagnostica que separa este estagio do kStageNoInterface - houve vinculo WinUSB
        // nesta maquina contra NUNCA houve. Era o que a versao anterior dizia com "tem
        // WinUSB aqui", e a troca so foi aceitavel porque diz a mesma coisa em portugues
        // comum. Uma "melhoria" de texto que remova a frase perde o proposito do estagio,
        // passa nas assercoes de comprimento e de ordem, e quebra aqui.
        CHECK(strstr(m, "ja funcionou aqui") != 0);

        // ---- E A FORMA LONGA DO MESMO ESTAGIO, que e a que vai ao LOG ----
        //
        // Este e o estagio que motivou a forma longa existir: a rodada que fez a mensagem
        // caber no host encurtou tambem o log, de 217 para 117 caracteres, e o log passou
        // a dizer MENOS sobre o cenario que custou uma hora de investigacao em
        // 2026-07-29. Duas frases se perderam palavra por palavra, e as duas voltam aqui.
        char L[2048];
        CHECK(bcd::findStageMessageLong(bcd::kStageNotPresent, *p3000, 0, L, (int)sizeof(L)));
        // A longa COMECA pela curta - o dono do aparelho le a mesma primeira frase no log
        // e na tela do software de DJ.
        CHECK(strncmp(L, m, strlen(m)) == 0);
        CHECK(strlen(L) > strlen(m));
        // "(maquina virtual?)" era uma das duas frases perdidas, e o VMware pegou este
        // aparelho duas vezes no mesmo dia nesta maquina.
        CHECK(strstr(L, "MAQUINA VIRTUAL") != 0);
        // A outra. E ela vem acompanhada do que FAZER, porque "3) Zadig" sozinho nao diz.
        CHECK(strstr(L, "o vinculo do Zadig se perdeu") != 0);
        CHECK(strstr(L, "reaplicar o WinUSB") != 0);
        // Medida: 553 caracteres. E ela NAO cabe no contrato do host, o que e o objetivo
        // desta forma e nao um defeito dela.
        CHECK(strlen(L) == 553);
        CHECK(!bcd::diagnosticFitsAsio(L));
        CHECK((int)strlen(L) + 1 <= bcd::kDiagnosticLogMax);
        CHECK(soAsciiImprimivel(L));

        CHECK(bcd::findStageMessage(bcd::kStageNoNode, *p3000, 0, m, (int)sizeof(m)));
        CHECK(strcmp(m, "aparelho nao encontrado no registro (a BCD3000 esta conectada "
                        "e com WinUSB aplicado pelo Zadig?)") == 0);

        CHECK(bcd::findStageMessage(bcd::kStageNoInterface, *p3000, 0, m, (int)sizeof(m)));
        CHECK(strcmp(m, "nenhuma interface WinUSB presente (a BCD3000 esta conectada?)")
              == 0);

        CHECK(bcd::findStageMessage(bcd::kStageNoGuid, *p3000, 0, m, (int)sizeof(m)));
        CHECK(strcmp(m, "DeviceInterfaceGUID ausente (o Zadig aplicou o WinUSB na funcao "
                        "MI_00?)") == 0);

        // O GUID de verdade sai inteiro: a limitacao do formato existe contra valor
        // absurdo, e nao contra o caso legitimo.
        CHECK(bcd::findStageMessage(bcd::kStageBadGuid, *p3000,
                                    "{12345678-1234-1234-1234-123456789ABC}",
                                    m, (int)sizeof(m)));
        CHECK(strcmp(m, "GUID invalido no registro: "
                        "{12345678-1234-1234-1234-123456789ABC}") == 0);
        // E o valor absurdo para em 90 caracteres: 27 do prefixo + 90 = 117, que cabe.
        CHECK(bcd::findStageMessage(bcd::kStageBadGuid, *p3000, pior,
                                    m, (int)sizeof(m)));
        CHECK(strlen(m) == 27 + 90);
        CHECK(bcd::diagnosticFitsAsio(m));
        // detail nulo nao pode virar leitura de ponteiro nulo dentro do _snprintf.
        CHECK(bcd::findStageMessage(bcd::kStageBadGuid, *p3000, 0, m, (int)sizeof(m)));
        CHECK(strcmp(m, "GUID invalido no registro: ") == 0);
    }

    // ---- OS BYTES DO REGISTRO NAO ENTRAM CRUS NUMA LINHA DE LOG ----
    //
    // O `detail` do kStageBadGuid e o unico argumento destas mensagens que vem de FORA:
    // e o valor que o Zadig gravou em HKLM, e HKLM e editavel por administrador. O tipo
    // ja e conferido na leitura e o comprimento ja esta limitado no formato (%.90s), entao
    // NAO havia estouro. O que passava eram os BYTES: um '\n' no valor produz uma LINHA
    // FORJADA no asio.log, e 90 bytes bastam para escrever uma inteira.
    //
    // Isto nao e hipotese de laboratorio. O log e a BASE DE PROVA deste projeto: os
    // criterios de teste de hardware leem cinco cadeias dele como evidencia, e a primeira
    // delas cabe folgada em 90 caracteres. Prova forjavel nao e prova.
    {
        const bcd::DeviceProfile* const p3000 = bcd::profileAt(0);
        char m[512];
        char L[2048];

        // O exemplo E o caso concreto, e nao um valor inventado: esta cadeia tem 60
        // caracteres e e um dos cinco marcadores que os portoes de hardware procuram.
        const char* const forjada =
            "\n12:00:00.000 usbdev: aberto (IF1 e IF2 obtidas, IF3 obtida)";
        CHECK(bcd::findStageMessage(bcd::kStageBadGuid, *p3000, forjada,
                                    m, (int)sizeof(m)));
        CHECK(strchr(m, '\n') == 0);
        CHECK(strchr(m, '\r') == 0);
        CHECK(soAsciiImprimivel(m));
        // O '\n' virou '.', entao o marcador deixa de comecar uma linha: o texto todo
        // continua sendo a MESMA linha "usbdev: GUID invalido no registro: ...".
        CHECK(strstr(m, "GUID invalido no registro: .12:00:00.000 usbdev: aberto") == m);
        // A forma LONGA sai pelo mesmo caminho e herda a defesa - e ela e justamente a
        // que vai para o log.
        CHECK(bcd::findStageMessageLong(bcd::kStageBadGuid, *p3000, forjada,
                                        L, (int)sizeof(L)));
        CHECK(strchr(L, '\n') == 0);
        CHECK(soAsciiImprimivel(L));

        // A REGRA E POR FAIXA E NAO POR LISTA. Uma correcao que trocasse so '\r' e '\n'
        // deixaria passar o \t, o ESC (que num terminal move o cursor e reescreve o que ja
        // esta na tela), o 0x7F e os bytes >= 0x80, que num log lido como UTF-8 saem como
        // sequencia invalida. Sao sete bytes sujos entre 'A' e 'Z', e tem de sair sete
        // pontos.
        unsigned char sujo[16];
        sujo[0] = 'A';
        sujo[1] = '\t'; sujo[2] = '\r'; sujo[3] = '\n'; sujo[4] = 0x1B;
        sujo[5] = 0x7F; sujo[6] = 0x80; sujo[7] = 0xFF;
        sujo[8] = 'Z';  sujo[9] = 0;
        CHECK(bcd::findStageMessage(bcd::kStageBadGuid, *p3000, (const char*)sujo,
                                    m, (int)sizeof(m)));
        CHECK(strcmp(m, "GUID invalido no registro: A.......Z") == 0);
        CHECK(soAsciiImprimivel(m));

        // A FAIXA E FECHADA NOS DOIS EXTREMOS: 0x20 (o espaco) e 0x7E (~) FICAM. Trocar um
        // `<=` por `<` no saneamento apagaria o espaco de todo valor legitimo, e essa e a
        // forma de o "conserto" corromper o dado que ele veio proteger.
        char limites[4];
        limites[0] = 0x20; limites[1] = 0x7E; limites[2] = 0x21; limites[3] = 0;
        CHECK(bcd::findStageMessage(bcd::kStageBadGuid, *p3000, limites,
                                    m, (int)sizeof(m)));
        CHECK(strcmp(m, "GUID invalido no registro:  ~!") == 0);
    }

    // ---- E o mesmo para o perfil EXPERIMENTAL ----
    // O modelo entra em quatro das cinco mensagens. Um modelo comprido acrescentado a
    // tabela empurraria a mensagem de interface inativa para fora do contrato, e o laco
    // do inicio deste teste ja cobre isso para todo perfil - este bloco existe para
    // deixar visivel que o nome do modelo aparece no texto de quem nao e a BCD3000.
    {
        const bcd::DeviceProfile* const p2000 = bcd::profileAt(1);
        char m[512];
        CHECK(bcd::findStageMessage(bcd::kStageNotPresent, *p2000, 0, m, (int)sizeof(m)));
        CHECK(strstr(m, "BCD2000") != 0);
        CHECK(strstr(m, "BCD3000") == 0);
        CHECK(bcd::diagnosticFitsAsio(m));
    }
}

//----------------------------------------------------------------------
static void test_blockBytesFor(void)
{
    printf("test_blockBytesFor\n");

    // O caso real do aparelho: 360 bytes = 45 frames.
    // 3528 = 9 pacotes de 360 + 1 de 288; ambos multiplos de 8; 10 pacotes.
    CHECK(bcd::blockBytesFor(360) == 3528);

    // Tambem valido: 368 bytes -> 9 de 368 (3312) + 1 de 216; ambos multiplos de 8.
    CHECK(bcd::blockBytesFor(368) == 3528);

    // Pacote que nao e multiplo de 8 partiria um frame de audio entre pacotes.
    CHECK(bcd::blockBytesFor(361) == 0);

    // Pacote pequeno demais: 3528/176 daria 21 pacotes, mais que os 10 quadros
    // USB disponiveis em 10 ms -- o bloco levaria mais tempo que o devido.
    CHECK(bcd::blockBytesFor(176) == 0);

    // Valores absurdos.
    CHECK(bcd::blockBytesFor(0) == 0);
    CHECK(bcd::blockBytesFor(-8) == 0);

    // Pacote grande o bastante para caber o bloco inteiro de uma vez seria
    // 3528 bytes num quadro so -- impossivel no full speed, mas a regra e a mesma:
    // 1 pacote <= 10, multiplo de 8, resto zero.
    CHECK(bcd::blockBytesFor(3528) == 3528);
}

//----------------------------------------------------------------------
static void test_interleave_deinterleave(void)
{
    printf("test_interleave_deinterleave\n");

    const int frames = 3;
    short c0[frames] = { 1, 2, 3 };
    short c1[frames] = { 10, 20, 30 };
    short c2[frames] = { 100, 200, 300 };
    short c3[frames] = { 1000, 2000, 3000 };

    const short* src[4] = { c0, c1, c2, c3 };
    short packed[frames * 4];
    memset(packed, 0xAA, sizeof(packed));

    bcd::interleave4(src, packed, frames);

    // Ordem intercalada: ch0 ch1 ch2 ch3, frame a frame.
    CHECK(packed[0] == 1);    CHECK(packed[1] == 10);
    CHECK(packed[2] == 100);  CHECK(packed[3] == 1000);
    CHECK(packed[4] == 2);    CHECK(packed[5] == 20);
    CHECK(packed[6] == 200);  CHECK(packed[7] == 2000);
    CHECK(packed[8] == 3);    CHECK(packed[9] == 30);
    CHECK(packed[10] == 300); CHECK(packed[11] == 3000);

    // Ida e volta tem de devolver o original.
    short d0[frames], d1[frames], d2[frames], d3[frames];
    memset(d0, 0, sizeof(d0)); memset(d1, 0, sizeof(d1));
    memset(d2, 0, sizeof(d2)); memset(d3, 0, sizeof(d3));
    short* dst[4] = { d0, d1, d2, d3 };

    bcd::deinterleave4(packed, dst, frames);
    for (int i = 0; i < frames; i++) {
        CHECK(d0[i] == c0[i]);
        CHECK(d1[i] == c1[i]);
        CHECK(d2[i] == c2[i]);
        CHECK(d3[i] == c3[i]);
    }
}

//----------------------------------------------------------------------
static void test_canais_nulos(void)
{
    printf("test_canais_nulos\n");

    // O software de DJ pode abrir so parte dos canais. Canal nulo na saida
    // tem de virar silencio, e canal nulo na entrada tem de ser ignorado
    // sem escrever em lugar nenhum.
    const int frames = 2;
    short c0[frames] = { 7, 8 };
    const short* src[4] = { c0, 0, 0, 0 };

    short packed[frames * 4];
    memset(packed, 0x7F, sizeof(packed));
    bcd::interleave4(src, packed, frames);

    CHECK(packed[0] == 7); CHECK(packed[1] == 0);
    CHECK(packed[2] == 0); CHECK(packed[3] == 0);
    CHECK(packed[4] == 8); CHECK(packed[5] == 0);
    CHECK(packed[6] == 0); CHECK(packed[7] == 0);

    short d2[frames];
    memset(d2, 0, sizeof(d2));
    short* dst[4] = { 0, 0, d2, 0 };
    bcd::deinterleave4(packed, dst, frames);   // nao pode falhar
    CHECK(d2[0] == 0);
    CHECK(d2[1] == 0);
}

//----------------------------------------------------------------------
static void test_ring_basico(void)
{
    printf("test_ring_basico\n");

    bcd::ByteRing r;
    // 100 nao e potencia de dois: tem de arredondar para 128.
    CHECK(r.init(100));
    CHECK(r.capacity() == 128);
    CHECK(r.used() == 0);
    CHECK(r.space() == 128);

    const char* msg = "abcdefgh";
    CHECK(r.write(msg, 8) == 8);
    CHECK(r.used() == 8);
    CHECK(r.space() == 120);

    char out[16];
    memset(out, 0, sizeof(out));
    CHECK(r.read(out, 8) == 8);
    CHECK(memcmp(out, "abcdefgh", 8) == 0);
    CHECK(r.used() == 0);

    r.reset();
    CHECK(r.used() == 0);
    CHECK(r.space() == 128);
}

//----------------------------------------------------------------------
static void test_ring_da_a_volta(void)
{
    printf("test_ring_da_a_volta\n");

    bcd::ByteRing r;
    CHECK(r.init(16));
    CHECK(r.capacity() == 16);

    char in[16], out[16];
    for (int i = 0; i < 16; i++)
        in[i] = (char)(i + 1);

    // Enche, esvazia quase tudo, e escreve de novo para forcar a volta.
    CHECK(r.write(in, 12) == 12);
    CHECK(r.read(out, 10) == 10);
    CHECK(r.used() == 2);

    CHECK(r.write(in, 12) == 12);      // agora atravessa o fim do buffer
    CHECK(r.used() == 14);

    memset(out, 0, sizeof(out));
    CHECK(r.read(out, 2) == 2);
    CHECK(out[0] == 11);               // sobras da primeira escrita
    CHECK(out[1] == 12);

    memset(out, 0, sizeof(out));
    CHECK(r.read(out, 12) == 12);
    for (int i = 0; i < 12; i++)
        CHECK(out[i] == (char)(i + 1));
    CHECK(r.used() == 0);
}

//----------------------------------------------------------------------
static void test_ring_limites(void)
{
    printf("test_ring_limites\n");

    bcd::ByteRing r;
    CHECK(r.init(8));

    char in[16];
    for (int i = 0; i < 16; i++)
        in[i] = (char)i;

    // Escrever mais do que cabe: grava so o que cabe e informa quanto gravou.
    CHECK(r.write(in, 16) == 8);
    CHECK(r.used() == 8);
    CHECK(r.space() == 0);
    CHECK(r.write(in, 1) == 0);

    // Ler mais do que tem: le so o que tem.
    char out[16];
    memset(out, 0x55, sizeof(out));
    CHECK(r.read(out, 16) == 8);
    CHECK(r.used() == 0);
    CHECK(out[8] == 0x55);             // nao escreveu alem do disponivel
    CHECK(r.read(out, 1) == 0);

    // discard nunca passa do que existe.
    CHECK(r.write(in, 4) == 4);
    CHECK(r.discard(10) == 4);
    CHECK(r.used() == 0);
    CHECK(r.discard(1) == 0);
}

//----------------------------------------------------------------------
static void test_ring_uso_real(void)
{
    printf("test_ring_uso_real\n");

    // O caso do driver: escreve em blocos de 512 frames (ASIO) e le em
    // blocos de 441 frames (USB), sem nunca perder nem repetir byte.
    bcd::ByteRing r;
    CHECK(r.init(4096 * bcd::kBytesPerFrame));

    const int asioBytes = 512 * bcd::kBytesPerFrame;
    const int usbBytes  = bcd::kBlockBytes;

    unsigned char* wbuf = new unsigned char[asioBytes];
    unsigned char* rbuf = new unsigned char[usbBytes];

    unsigned char nextWrite = 0;
    unsigned char nextRead  = 0;
    long long     totalRead = 0;
    bool          ok = true;

    for (int iter = 0; iter < 500 && ok; iter++) {
        if (r.space() >= asioBytes) {
            for (int i = 0; i < asioBytes; i++)
                wbuf[i] = nextWrite++;
            if (r.write(wbuf, asioBytes) != asioBytes)
                ok = false;
        }
        if (r.used() >= usbBytes) {
            if (r.read(rbuf, usbBytes) != usbBytes) {
                ok = false;
            } else {
                for (int i = 0; i < usbBytes; i++) {
                    if (rbuf[i] != nextRead++) { ok = false; break; }
                }
                totalRead += usbBytes;
            }
        }
    }
    CHECK(ok);
    CHECK(totalRead > 100000);         // realmente moveu dados

    delete[] wbuf;
    delete[] rbuf;
}

//----------------------------------------------------------------------
static void test_ring_init_limites(void)
{
    printf("test_ring_init_limites\n");

    // Os LIMITES de ByteRing::init, que uma revisao anterior conferiu A MAO e
    // confirmou corretos - era lacuna de COBERTURA, e nao defeito suspeito, e por
    // isso ficou de fora das rodadas de correcao da epoca. A faixa valida (1 a
    // 16 MiB) e o arredondamento para a proxima potencia de dois estao escritos na
    // interface publica, em ringbuf.h; daqui em diante estao tambem travados.
    //
    // O caso NEGATIVO nao e simetria de teste: com ele aceito, o laco
    // `while (cap < capacityBytes) cap <<= 1;` nem rodaria, e a alocacao seria de
    // um byte com mask_ = 0 - um anel que aceita escrita e nunca a devolve.
    bcd::ByteRing r;
    CHECK(!r.init(0));                   // zero nao e capacidade
    CHECK(!r.init(-8));                  // negativo idem
    CHECK(r.init(1));                    // o menor anel valido que existe
    CHECK(r.init(1 << 24));              // exatamente o teto: 16 MiB
    CHECK(!r.init((1 << 24) + 1));       // um byte acima do teto: recusado
}

//----------------------------------------------------------------------
static void test_aritmetica_dos_aneis(void)
{
    printf("test_aritmetica_dos_aneis\n");

    // Duas invariantes da correcao de deriva de relogio, para os quatro
    // tamanhos de bloco que o driver aceita (getBufferSize: 256 a 2048):
    //
    //  1. a marca d'agua tem de ficar ABAIXO da capacidade do anel. Se chegasse
    //     a capacidade, inRing_.used() nunca a passaria, a correcao de deriva
    //     nunca dispararia, e o transbordo de entrada voltaria sem aviso;
    //  2. a marca d'agua menos um bloco do host - que e a amplitude natural de
    //     oscilacao do anel - tem de continuar positiva. Se fosse a zero, a
    //     propria correcao passaria a esvaziar o anel e causaria fome na
    //     entrada.
    //
    // As duas contas vem de inHighWaterBytes/ringBytesFor, as MESMAS funcoes
    // que o motor usa; sem essa fonte unica, o teste poderia passar enquanto o
    // codigo real divergisse.
    const int blocos[] = { 256, 512, 1024, 2048 };

    for (int i = 0; i < 4; i++) {
        const int bf = blocos[i];
        bcd::ByteRing r;
        CHECK(r.init(bcd::ringBytesFor(bf)));
        CHECK(bcd::inHighWaterBytes(bf) < r.capacity());
        CHECK(bcd::inHighWaterBytes(bf) - bf * bcd::kBytesPerFrame > 0);
    }
}

//----------------------------------------------------------------------
static void test_pre_carga_da_entrada(void)
{
    printf("test_pre_carga_da_entrada\n");

    // A pre-carga de silencio do anel de entrada, feita uma vez por sessao em
    // AudioEngine::start(). O motor chama
    // primeRingWithSilence(inRing_, inHighWaterBytes(blockFrames_)) — as MESMAS
    // funcoes usadas aqui, para o teste nao poder passar enquanto o codigo real
    // divergisse.
    //
    // A invariante que este teste protege e o ALINHAMENTO DE FRAME: se a
    // pre-carga deixasse o ponteiro do anel fora da fronteira de 8 bytes, os
    // canais rotacionariam de forma permanente e silenciosa (o canal 1 passaria
    // a sair no canal 2).
    CHECK(bcd::kPrimeChunkBytes % bcd::kBytesPerFrame == 0);
    CHECK(bcd::kPrimeChunkBytes > 0);
    CHECK(bcd::kPrimeChunkBytes == bcd::kPrimeChunkFrames * bcd::kBytesPerFrame);

    const int blocos[] = { 256, 512, 1024, 2048 };
    for (int i = 0; i < 4; i++) {
        const int bf     = blocos[i];
        const int target = bcd::inHighWaterBytes(bf);

        // A quantidade pedida e multiplo de frame por construcao: a conta e
        // kInHighWaterBlocks * blockFrames * kBytesPerFrame, e o ultimo fator
        // e o proprio tamanho do frame.
        CHECK(target % bcd::kBytesPerFrame == 0);

        bcd::ByteRing r;
        CHECK(r.init(bcd::ringBytesFor(bf)));
        CHECK(bcd::primeRingWithSilence(r, target) == target);
        CHECK(r.used() == target);
        CHECK(r.used() % bcd::kBytesPerFrame == 0);

        // O que entrou e silencio de verdade, e nao lixo de pilha.
        unsigned char buf[64];
        memset(buf, 0xAA, sizeof(buf));
        CHECK(r.read(buf, (int)sizeof(buf)) == (int)sizeof(buf));
        bool tudoZero = true;
        for (int k = 0; k < (int)sizeof(buf); k++)
            if (buf[k] != 0)
                tudoZero = false;
        CHECK(tudoZero);

        // Reinicio (stop seguido de start): o anel volta a zero e uma nova
        // pre-carga da exatamente o mesmo nivel, sem acumular o da sessao
        // anterior.
        r.reset();
        CHECK(r.used() == 0);
        CHECK(bcd::primeRingWithSilence(r, target) == target);
        CHECK(r.used() == target);

        // A EQUIVALENCIA em que o laco de aquecimento de threadMain() se apoia:
        // um bloco INTEIRO do host, lido deste anel recem-pre-carregado, e
        // silencio byte a byte. E por isso que o aquecimento pode entregar um
        // clientIn_ zerado sem ler o anel — pumpBlock(false) — e o cliente
        // receber exatamente os mesmos bytes que receberia lendo. Sem esta
        // igualdade, nao consumir mudaria o audio entregue, e a economia da
        // pre-carga sairia caro.
        const int blockBytes = bf * bcd::kBytesPerFrame;
        CHECK(target >= blockBytes);          // ha um bloco inteiro para ler
        unsigned char* bloco = new unsigned char[blockBytes];
        memset(bloco, 0xAA, blockBytes);
        CHECK(r.read(bloco, blockBytes) == blockBytes);
        bool blocoTodoZero = true;
        for (int k = 0; k < blockBytes; k++)
            if (bloco[k] != 0)
                blocoTodoZero = false;
        CHECK(blocoTodoZero);
        delete[] bloco;
    }

    // Anel sem espaco: escreve o que couber, devolve o total real (para o motor
    // poder registrar a falha) e nao entra em laco infinito.
    {
        bcd::ByteRing r;
        CHECK(r.init(8));
        CHECK(r.capacity() == 8);
        CHECK(bcd::primeRingWithSilence(r, 4096) == 8);
        CHECK(r.used() == 8);
        CHECK(r.used() % bcd::kBytesPerFrame == 0);
    }

    // Pedidos que nao sao multiplos do pedaco, e pedidos invalidos. Um pedido
    // que nao seja multiplo de frame e truncado para baixo: assim o anel nunca
    // sai da fronteira de frame, seja qual for o argumento.
    {
        bcd::ByteRing r;
        CHECK(r.init(4096));
        CHECK(bcd::primeRingWithSilence(r, 520) == 520);      // 512 + 8
        r.reset();
        CHECK(bcd::primeRingWithSilence(r, 8) == 8);
        r.reset();
        CHECK(bcd::primeRingWithSilence(r, 20) == 16);        // truncado
        CHECK(r.used() % bcd::kBytesPerFrame == 0);
        r.reset();
        CHECK(bcd::primeRingWithSilence(r, 0) == 0);
        CHECK(bcd::primeRingWithSilence(r, -8) == 0);
        CHECK(r.used() == 0);
    }
}

//----------------------------------------------------------------------
static void test_latencia_de_entrada(void)
{
    printf("test_latencia_de_entrada\n");

    // A latencia de ENTRADA informada ao software de DJ - o numero com que ele
    // alinha gravacao e monitoracao. Ele ja esteve errado DUAS VEZES e nos DOIS
    // sentidos (primeiro por copiar a semantica do lado da saida, depois por medir o
    // anel na marca d'agua em vez do meio da oscilacao), e ate esta rodada era a
    // UNICA aritmetica de anel do motor escrita em linha no ponto de chamada e sem
    // teste nenhum. Todo o resto vive em audioengine.h e e travado aqui.
    //
    // A funcao exercitada e a MESMA que getLatencies() chama: repetir a expressao
    // aqui faria o teste passar enquanto o numero informado ao host divergisse.
    //
    // Os quatro valores sao a tabela escrita no comentario de getLatencies(), em
    // bcdasio.cpp: bloco do host + nivel de regime do anel + UM bloco de 10 ms do
    // USB. Eles NAO sao medicao: sao um modelo do caminho de dados, e a medicao
    // definitiva e por loopback, pendente no passo 2.4. O que este teste protege e
    // que o codigo continue informando o numero que o comentario promete.
    CHECK(bcd::inputLatencyFrames(256)  == 1849);
    CHECK(bcd::inputLatencyFrames(512)  == 3257);
    CHECK(bcd::inputLatencyFrames(1024) == 6073);
    CHECK(bcd::inputLatencyFrames(2048) == 11705);

    const int blocos[] = { 256, 512, 1024, 2048 };
    for (int i = 0; i < 4; i++) {
        const int bf = blocos[i];

        // A forma fechada: 5,5 blocos do host mais um bloco de 10 ms do USB. Uma
        // mudanca em kInHighWaterBlocks passaria em branco pelos quatro literais
        // acima se alguem os "corrigisse" junto; aqui ela tem de ser deliberada.
        CHECK(bcd::inputLatencyFrames(bf) == bf * 11 / 2 + bcd::kFramesPerBlock);

        // O termo do anel fica ACIMA da marca d'agua: a faixa medida no hardware
        // (16.900 a 19.800 bytes com bloco de 512) esta inteira acima dela, e
        // informar a marca sozinha subnotificaria a latencia real.
        CHECK(bcd::inRingSteadyBytes(bf) > bcd::inHighWaterBytes(bf));
        // E ABAIXO do pico da oscilacao, que e a marca mais um bloco do host.
        // Acima dele o driver informaria latencia que o anel nunca chega a ter.
        CHECK(bcd::inRingSteadyBytes(bf) <
              bcd::inHighWaterBytes(bf) + bf * bcd::kBytesPerFrame);
        // E e multiplo de frame: senao a divisao por kBytesPerFrame truncaria, e o
        // erro dependeria do tamanho de bloco de um jeito invisivel na tabela.
        CHECK(bcd::inRingSteadyBytes(bf) % bcd::kBytesPerFrame == 0);
    }
}

//----------------------------------------------------------------------
// O CANAL LOCAL, exercitado de verdade: um servidor falso de named pipe no
// proprio processo de teste, contra o RelayLink REAL - o mesmo codigo que roda
// dentro do software de DJ. Nao precisa de USB, nem do BCD3000Bridge.exe, nem da
// teVirtualMIDI.
//
// Nao precisa de thread nenhum, e isso nao e sorte: o cliente pode abrir um pipe
// que esta em estado de ESCUTA antes de o servidor chamar ConnectNamedPipe, e
// nesse caso o ConnectNamedPipe devolve na hora com ERROR_PIPE_CONNECTED. Todas as
// trocas abaixo sao ordenadas de proposito - escreve-se antes de ler, sempre -,
// entao nenhuma leitura bloqueia e o teste nao pode travar.
//
// Este teste cobre a metade C++ do contrato entre as duas linguagens. A outra
// metade - o servidor de VERDADE, em Python, injetando na porta virtual - se prova
// com o arnes de duas linguagens descrito no plano (tests.exe rele-cliente contra
// bridge_service.servidor_do_canal).
static HANDLE criarServidorFalsoDoCanal(void)
{
    return CreateNamedPipeW(bcd::kRelayPipeName,
                            PIPE_ACCESS_DUPLEX,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                            PIPE_REJECT_REMOTE_CLIENTS,
                            1, 4096, 4096, 0, 0);
}

static void test_relay_link(void)
{
    printf("test_relay_link\n");

    // O nome do canal e contrato com o outro processo E com a outra linguagem.
    // Mudar aqui sem mudar poc/bridge_service.py nao daria erro de compilacao em
    // lugar nenhum - so faria os controles ficarem mudos. Que quebre aqui.
    CHECK(wcscmp(bcd::kRelayPipeName, L"\\\\.\\pipe\\BCD3000MidiRelay") == 0);

    // Tamanho fixo nos dois sentidos: o pacote USB-MIDI 1.0.
    CHECK(bcd::kRelayPacketBytes == 4);
    // O buffer de leitura tem de caber varias mensagens e, sobretudo, tem de ser
    // multiplo do pacote.
    CHECK(bcd::kRelayReadBufBytes >= 16 * bcd::kRelayPacketBytes);
    CHECK((bcd::kRelayReadBufBytes % bcd::kRelayPacketBytes) == 0);
    // Prazos: a escrita no canal nao pode dominar o laco da ponte, e a reconexao
    // nao pode ser tao rara que o usuario perceba o bridge subindo.
    CHECK(bcd::kRelayWriteMs >= 50);
    CHECK(bcd::kRelayWriteMs <= 1000);
    CHECK(bcd::kRelayRetryMs >= 250);
    CHECK(bcd::kRelayRetryMs <= 3000);
    // Recuo graduado: o passo da PARTIDA tem de ser bem menor que o de regime, e a
    // janela curta tem de cobrir a ordem de grandeza da tempestade de reset do
    // software de DJ (um par stop/start a cada ~0,5 a 1 s). Com passo unico de 1 s,
    // uma instancia que nasca nessa janela pode morrer antes de conectar.
    CHECK(bcd::kRelayFirstRetryMs >= 50);
    CHECK(bcd::kRelayFirstRetryMs * 4 <= bcd::kRelayRetryMs);
    CHECK(bcd::kRelayFirstTries >= 5);
    // A fase curta inteira nao pode passar do passo de regime por muito: ela existe
    // para cobrir o arranque, nao para virar o comportamento normal.
    CHECK(bcd::kRelayFirstTries * bcd::kRelayFirstRetryMs >= 500);
    CHECK(bcd::kRelayFirstTries * bcd::kRelayFirstRetryMs <= 3 * bcd::kRelayRetryMs);

    // ---- daqui para baixo o teste cria um servidor com o NOME REAL do canal ----
    //
    // Se o BCD3000Bridge.exe estiver rodando com o codigo desta tarefa, o nome ja
    // existe: a criacao falharia e, muito pior, o connect() abaixo acharia o
    // servidor DE VERDADE e injetaria pacotes de teste na porta MIDI do usuario -
    // LEDs acendendo sozinhos no aparelho no meio de um set. Mesma decisao do
    // test_handoff: PULAR, nao falhar. As assercoes de contrato acima ja rodaram.
    //
    // WaitNamedPipeW nao conecta nem consome instancia: so pergunta se existe.
    // Qualquer erro diferente de ERROR_FILE_NOT_FOUND ja quer dizer que o nome
    // existe (ERROR_SEM_TIMEOUT, por exemplo, e "existe e esta ocupado").
    if (WaitNamedPipeW(bcd::kRelayPipeName, 1) ||
        GetLastError() != ERROR_FILE_NOT_FOUND) {
        printf("  PULANDO a parte que cria o canal: ja existe um servidor em '%ls' "
               "(BCD3000Bridge.exe rodando?). Injetar pacotes de teste nele mexeria "
               "na porta MIDI do usuario.\n", bcd::kRelayPipeName);
        return;
    }

    bcd::RelayLink link;
    DWORD e = 0;

    // ---- MODO DE FALHA 1: o bridge nao esta rodando ----
    // Tem de ser detectado com codigo de erro e NAO pode ser fatal: quem chama
    // repete mais tarde e o audio nem sabe que isto aconteceu.
    CHECK(!link.isConnected());
    CHECK(!link.connect(&e));
    CHECK(e == ERROR_FILE_NOT_FOUND);
    const unsigned char um[4] = { 0x0B, 0xB0, 0x05, 0x40 };
    CHECK(!link.send(um, 4, 0, &e));
    CHECK(e == ERROR_INVALID_HANDLE);
    CHECK(!link.armRead(&e));
    CHECK(!link.readPending());
    // Sem canal, finishRead nao inventa dado nem acusa erro: nao havia leitura.
    CHECK(link.finishRead(&e) == 0);

    // ---- com servidor ----
    //
    // A falha de criacao NAO conta como verificacao falhada, e a diferenca importa:
    // a guarda acima nao e atomica. Entre o WaitNamedPipeW dizer "nao existe" e este
    // CreateNamedPipeW, o BCD3000Bridge.exe pode subir e criar o nome - e nesse caso
    // o certo e PULAR, exatamente como na guarda. Na versao anterior o CHECK contava
    // FALHA e so depois imprimia "PULANDO", o que dava veredito errado (e vermelho no
    // console) numa corrida benigna. A propriedade de seguranca continua valendo do
    // mesmo jeito: sem servidor proprio nada e injetado, e o connect() abaixo nao
    // acontece.
    HANDLE srv = criarServidorFalsoDoCanal();
    if (srv == INVALID_HANDLE_VALUE) {
        printf("  PULANDO o resto: nao consegui criar o servidor falso em '%ls' "
               "(erro %lu). Se for 231/183, o BCD3000Bridge.exe subiu entre a guarda "
               "e esta linha - corrida benigna, nao defeito.\n",
               bcd::kRelayPipeName, GetLastError());
        return;
    }

    // SEGUNDA CORRIDA BENIGNA, documentada aqui para as falhas nao parecerem
    // misteriosas: com o bridge PARADO e um driver deste projeto RODANDO (audio
    // ligado), o thread da ponte dele tenta conectar neste nome a cada poucas
    // centenas de ms. Ele pode pegar a UNICA instancia (nMaxInstances = 1) do
    // servidor que acabamos de criar antes de nos, e ai o connect() daqui falha com
    // ERROR_PIPE_BUSY e o resto do teste falha em cascata. Nao ha estrago - o driver
    // recebe pacotes de teste que ninguem injeta em porta nenhuma, porque o servidor
    // e este processo -, so ruido. NAO se poe guarda para isso: a janela e de
    // microssegundos, e a unica forma de fechar seria pedir mais instancias, o que
    // divergiria do servidor de verdade e enfraqueceria o teste.
    CHECK(link.connect(&e));
    CHECK(link.isConnected());

    // O cliente ja esta do outro lado, entao isto devolve na hora.
    const BOOL conectou = ConnectNamedPipe(srv, 0);
    CHECK(conectou || GetLastError() == ERROR_PIPE_CONNECTED);

    // ---- driver -> bridge: DOIS pacotes numa transferencia, UMA mensagem ----
    // E o caso real: o EP 0x81 entrega varios pacotes de 4 bytes por
    // transferencia, e o driver repassa a transferencia inteira como uma mensagem.
    // O segundo pacote e o ENCHIMENTO 00 00 00 00 do aparelho, e ele atravessa o
    // canal DE PROPOSITO - o filtro (tabela de CIN mais o teste do bit de status)
    // mora do outro lado, num caminho unico compartilhado com o leitor do
    // aparelho do bridge. Descartar aqui criaria uma segunda copia da tabela.
    const unsigned char dois[8] = { 0x09, 0x90, 0x0A, 0x7F,
                                    0x00, 0x00, 0x00, 0x00 };
    CHECK(link.send(dois, 8, 0, &e));

    unsigned char lido[64];
    DWORD n = 0;
    CHECK(ReadFile(srv, lido, sizeof(lido), &n, 0) != 0);
    CHECK(n == 8);
    CHECK(memcmp(lido, dois, 8) == 0);

    // Tamanho que nao e multiplo de 4 e recusado ANTES de tocar no canal: e este
    // invariante que faz o enquadramento ser desnecessario.
    CHECK(!link.send(dois, 3, 0, &e));
    CHECK(e == ERROR_INVALID_PARAMETER);
    CHECK(!link.send(0, 4, 0, &e));
    CHECK(e == ERROR_INVALID_PARAMETER);
    CHECK(!link.send(dois, 0, 0, &e));
    CHECK(e == ERROR_INVALID_PARAMETER);

    // ---- bridge -> driver: uma mensagem de LED ----
    const unsigned char led[4] = { 0x09, 0x90, 0x20, 0x7F };
    n = 0;
    CHECK(WriteFile(srv, led, 4, &n, 0) != 0);
    CHECK(n == 4);
    CHECK(link.armRead(&e));
    CHECK(link.readPending());
    CHECK(WaitForSingleObject(link.readEvent(), 2000) == WAIT_OBJECT_0);
    CHECK(link.finishRead(&e) == 4);
    CHECK(!link.readPending());
    CHECK(memcmp(link.readBuffer(), led, 4) == 0);

    // ---- modo MENSAGEM: duas mensagens NAO se colam ----
    // E esta a propriedade que elimina o enquadramento, e ela nao e automatica: o
    // modo de leitura de um cliente de pipe nasce em BYTES, e e o
    // SetNamedPipeHandleState dentro de connect() que o troca. Em modo de bytes as
    // duas escritas abaixo poderiam voltar juntas numa leitura de 8 - o que ainda
    // daria certo - ou PARTIDAS em 3 + 5, e ai o fluxo desincronizaria de vez.
    const unsigned char led2[4] = { 0x09, 0x90, 0x21, 0x00 };
    CHECK(WriteFile(srv, led,  4, &n, 0) != 0);
    CHECK(WriteFile(srv, led2, 4, &n, 0) != 0);
    CHECK(link.armRead(&e));
    CHECK(WaitForSingleObject(link.readEvent(), 2000) == WAIT_OBJECT_0);
    CHECK(link.finishRead(&e) == 4);
    CHECK(memcmp(link.readBuffer(), led, 4) == 0);
    CHECK(link.armRead(&e));
    CHECK(WaitForSingleObject(link.readEvent(), 2000) == WAIT_OBJECT_0);
    CHECK(link.finishRead(&e) == 4);
    CHECK(memcmp(link.readBuffer(), led2, 4) == 0);

    // ---- mensagem mal formada: truncada no pacote, sem contaminar a seguinte ----
    // Um servidor que mandasse 6 bytes seria um defeito do outro lado. O que
    // importa e que ele nao consegue desalinhar este lado: os 2 bytes sobrando
    // morrem com a mensagem deles.
    const unsigned char torta[6] = { 0x09, 0x90, 0x22, 0x7F, 0xAA, 0xBB };
    CHECK(WriteFile(srv, torta, 6, &n, 0) != 0);
    CHECK(WriteFile(srv, led2,  4, &n, 0) != 0);
    CHECK(link.armRead(&e));
    CHECK(WaitForSingleObject(link.readEvent(), 2000) == WAIT_OBJECT_0);
    CHECK(link.finishRead(&e) == 4);
    CHECK(memcmp(link.readBuffer(), torta, 4) == 0);
    CHECK(link.armRead(&e));
    CHECK(WaitForSingleObject(link.readEvent(), 2000) == WAIT_OBJECT_0);
    CHECK(link.finishRead(&e) == 4);
    CHECK(memcmp(link.readBuffer(), led2, 4) == 0);

    // ---- armRead e idempotente ----
    // O laco da ponte chama armRead em TODA volta, e na maioria delas ja ha leitura
    // pendente. Se isto submetesse uma segunda leitura, o mesmo OVERLAPPED seria
    // usado por duas operacoes ao mesmo tempo.
    CHECK(link.armRead(&e));
    CHECK(link.readPending());
    CHECK(link.armRead(&e));
    CHECK(link.readPending());

    // ---- MODO DE FALHA 2: o bridge morre com o driver segurando o aparelho ----
    // Fechar o servidor com uma leitura ARMADA e exatamente o que acontece. O canal
    // tem de ser detectado como quebrado, e close() tem de devolver TRUE: a leitura
    // cancelada assenta, e so por isso o thread da ponte pode reconectar em vez de
    // vazar o estado e sair.
    CloseHandle(srv);
    CHECK(WaitForSingleObject(link.readEvent(), 2000) == WAIT_OBJECT_0);
    CHECK(link.finishRead(&e) < 0);
    CHECK(e != 0);
    CHECK(!link.send(um, 4, 0, &e));
    CHECK(link.close());
    CHECK(!link.isConnected());

    // Fechar duas vezes e inofensivo - e obrigatorio, porque o destrutor chama de
    // novo.
    CHECK(link.close());
}

//----------------------------------------------------------------------
// A propria classe MidiBridge tem uma parte testavel sem USB e sem a
// teVirtualMIDI: os caminhos que recusam trabalho ANTES de tocar em qualquer
// recurso. Sao justamente os riscos de ciclo de vida - stop() sem start(),
// stop() duas vezes, start() sem aparelho -, e nenhum deles precisa de hardware.
static void test_midibridge_ciclo_de_vida(void)
{
    printf("test_midibridge_ciclo_de_vida\n");

    // O nome da PORTA MIDI virtual nao e mais afirmado aqui, e a razao esta no
    // topo do midibridge.h: o driver nao cria porta nenhuma desde esta tarefa. O
    // contrato que ele ainda tem com o mundo de fora - e que quebraria em silencio
    // se mudasse - passou a ser o nome do CANAL, travado em test_relay_link.

    bcd::MidiBridge midi;

    // Estado inicial: nada rodando, nenhum contador, nenhum erro.
    CHECK(!midi.isRunning());
    CHECK(midi.packetsForwarded() == 0);
    CHECK(midi.ledsForwarded() == 0);
    CHECK(midi.ledErrors() == 0);
    CHECK(midi.packetsDropped() == 0);
    CHECK(midi.relayConnects() == 0);
    CHECK(strlen(midi.lastError()) == 0);

    // stop() sem start(), e stop() repetido, tem de ser inofensivos: o destrutor
    // chama stop() e o software de DJ tambem.
    midi.stop();
    midi.stop();
    CHECK(!midi.isRunning());

    // start(0) recusa sem tocar em nada e explica o motivo.
    CHECK(!midi.start(0));
    CHECK(!midi.isRunning());
    CHECK(strlen(midi.lastError()) > 0);

    // Aparelho fechado tambem e recusado, e sem abrir nada - e por isso que
    // isto roda em teste de unidade. Um UsbDevice recem-construido nao tem
    // handle nenhum, nem de arquivo nem de interface.
    bcd::UsbDevice dev;
    CHECK(!dev.isOpen());
    CHECK(dev.midiIf() == 0);
    CHECK(!midi.start(&dev));
    CHECK(!midi.isRunning());

    // stop() depois de um start() que falhou tambem tem de ser inofensivo.
    midi.stop();
    CHECK(!midi.isRunning());

    // NAO testado aqui, de proposito: a recusa por midiIf() == 0 com o aparelho
    // ABERTO (a Correcao C do briefing). Exigiria tomar o aparelho de verdade,
    // e um teste de unidade que abre hardware nao e teste de unidade. Fica
    // coberto pela linha de log que usbdev::open() escreve quando a IF3 falta.
}

//----------------------------------------------------------------------
// Observador independente, com o MESMO direito de acesso do lado Python e o handle
// FECHADO depois de consultar. E ele que prova que o lado Python vai encontrar o
// que o lado C++ criou - nenhum teste que so chame requestDevice() e olhe o
// retorno prova isso.
static HANDLE abrirEventoComoOBridge(void)
{
    HANDLE h = OpenEventA(SYNCHRONIZE, FALSE, bcd::kEventName);
    if (!h)
        h = OpenEventA(SYNCHRONIZE, FALSE, bcd::kEventNameLocal);
    return h;
}

static bool eventoSinalizado(HANDLE h)
{
    return h != 0 && WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
}

// O OU sobre os DOIS nomes, igual ao driver_quer_aparelho() de
// poc/bridge_service.py: pedido vivo e QUALQUER um dos dois escopos sinalizado.
// Olhar so o primeiro nome que se consegue abrir responderia "nao ha pedido" com o
// pedido vivo no outro escopo - e e essa a decisao que este teste usa para nao
// mexer em estado de uma sessao de audio viva.
static bool pedidoVivoComoOBridge(void)
{
    const char* const nomes[2] = { bcd::kEventName, bcd::kEventNameLocal };
    bool vivo = false;
    for (int i = 0; i < 2; i++) {
        HANDLE h = OpenEventA(SYNCHRONIZE, FALSE, nomes[i]);
        if (!h)
            continue;
        if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0)
            vivo = true;
        CloseHandle(h);
    }
    return vivo;
}

//----------------------------------------------------------------------
static void test_handoff(void)
{
    printf("test_handoff\n");

    // ATENCAO: a SEGUNDA METADE deste teste mexe no estado REAL da passagem de
    // bastao, que e um evento nomeado do Windows visivel a toda a sessao. Com o
    // BCD3000Bridge.exe rodando, ela faz o bridge soltar o APARELHO por um instante
    // e retomar depois (~0,84 s, medido; a porta virtual dele nao e afetada). Por
    // isso todo caminho dela termina com releaseDevice() - e por isso ela e PULADA
    // quando ha um pedido vivo (ver a guarda no meio da funcao).

    // Os dois nomes, congelados. Mudar qualquer um deles sem mudar
    // poc/bridge_service.py quebra a passagem de bastao SEM ERRO em lugar nenhum:
    // o driver pediria num nome e o bridge escutaria em outro, e o sintoma seria
    // "os controles nao voltam". Que quebre aqui, em vez de no hardware.
    CHECK(strcmp(bcd::kEventName, "Global\\BCD3000_DriverWantsDevice") == 0);
    CHECK(strcmp(bcd::kEventNameLocal, "BCD3000_DriverWantsDevice") == 0);
    // O nome local tem de ser o global sem o prefixo de escopo - se divergirem,
    // um lado pode criar no Global e o outro achar o local de outro programa.
    CHECK(strcmp(bcd::kEventName + strlen("Global\\"), bcd::kEventNameLocal) == 0);

    // Parametros da repeticao: passo curto o suficiente para nao dominar a espera
    // (o bridge leva algumas centenas de ms para soltar) e teto da ordem de 3 s.
    CHECK(bcd::kHandoffRetryMs >= 100);
    CHECK(bcd::kHandoffRetryMs <= 250);
    CHECK(bcd::kHandoffTries >= 5);
    CHECK((unsigned)bcd::kHandoffTries * bcd::kHandoffRetryMs >= 2000);
    CHECK((unsigned)bcd::kHandoffTries * bcd::kHandoffRetryMs <= 4000);

    // ---- daqui para baixo, o teste MEXE em estado real do sistema ----
    //
    // A condicao perigosa NAO e "o evento existe". Depois do primeiro start() ele
    // existe pelo resto da vida do processo do software de DJ, com o audio PARADO
    // inclusive: o driver mantem o handle aberto de proposito. A condicao perigosa
    // e "existe E esta SINALIZADO", que quer dizer sessao de audio VIVA agora.
    //
    // Seguir em frente nesse caso faria dois estragos, os dois em silencio:
    //  (1) o releaseDevice() daqui zeraria o pedido da sessao viva, e o
    //      BCD3000Bridge.exe sairia do braco de espera e passaria a disputar o
    //      aparelho com o driver pelo resto da sessao;
    //  (2) quando o driver finalmente parasse, o releaseDevice() DELE veria o
    //      evento nao sinalizado e NAO escreveria "bastao: aparelho devolvido" -
    //      justamente o marcador que o portao de hardware usa como evidencia. O
    //      teste sabotaria a prova do portao seguinte.
    //
    // E nao e hipotetico: no plano, o step de rodar os testes vem ANTES do step de
    // fechar o software de DJ. Por isso a saida e PULAR, nao falhar - nao ha
    // defeito nenhum em rodar os testes com o audio ligado, so nao se pode mexer.
    //
    // As assercoes que provam o contrato entre as duas linguagens - os nomes, o
    // prefixo de escopo e os parametros de repeticao - nao dependem de evento
    // nenhum e JA RODARAM acima.
    if (pedidoVivoComoOBridge()) {
        printf("  PULANDO a parte que mexe no evento: ha um pedido VIVO nesta "
               "sessao (software de DJ com o audio ligado pelo driver). Zerar esse "
               "pedido faria o programa de controles disputar o aparelho com o "
               "driver e apagaria o marcador de devolucao no log. As assercoes de "
               "contrato acima rodaram.\n");
        return;
    }

    // Estado inicial: NAO se afirma nada por CHECK. O evento pode existir sem estar
    // sinalizado - um driver ASIO deste projeto que ja rodou start() nesta sessao e
    // cujo processo continua vivo. Nao invalida nada do que vem abaixo.
    HANDLE pre = abrirEventoComoOBridge();
    if (pre) {
        printf("  nota: o evento do bastao JA existe (nao sinalizado) - um driver "
               "ASIO deste projeto ja rodou start() nesta sessao\n");
        CloseHandle(pre);
    }

    // ---- pedir ----
    // requestDevice() NAO pode dormir. A primeira versao desta receita fazia
    // Sleep(2000) incondicional aqui dentro, o que travava TODO start() de audio
    // por 2 s - inclusive quando nao ha bridge nenhum para soltar o aparelho, que
    // e justamente o caso deste teste. A espera mudou de lugar: agora esta no laco
    // de tentativas do device.open(), que se auto-temporiza.
    const DWORD t0 = GetTickCount();
    CHECK(bcd::requestDevice());
    const DWORD dt = GetTickCount() - t0;
    CHECK(dt < 250);

    // O observador independente encontra o evento e o ve SINALIZADO. Estas duas
    // linhas sao o contrato com o lado Python.
    HANDLE obs = abrirEventoComoOBridge();
    CHECK(obs != 0);
    CHECK(eventoSinalizado(obs));

    // O ESCOPO DUPLO: o nome LOCAL tem de existir e estar sinalizado SEMPRE, com ou
    // sem privilegio para o Global. E ele que faz um bridge da mesma sessao achar o
    // pedido mesmo quando o software de DJ roda ELEVADO - medido nesta maquina, um
    // processo elevado criava so no Global, e um bridge sem elevacao pode nao
    // conseguir abrir aquele objeto. Sem esta linha, uma regressao que voltasse a
    // criar "o primeiro nome que der" passaria no teste toda vez que ele rodasse
    // elevado, que e exatamente quando o defeito existe.
    HANDLE obsLocal = OpenEventA(SYNCHRONIZE, FALSE, bcd::kEventNameLocal);
    CHECK(obsLocal != 0);
    CHECK(eventoSinalizado(obsLocal));
    if (obsLocal)
        CloseHandle(obsLocal);

    // Pedir duas vezes seguidas nao pode falhar, nem travar, nem mudar o estado.
    const DWORD t1 = GetTickCount();
    CHECK(bcd::requestDevice());
    CHECK(GetTickCount() - t1 < 250);
    CHECK(eventoSinalizado(obs));

    // ---- devolver ----
    bcd::releaseDevice();
    // O evento CONTINUA existindo - o driver mantem o handle aberto de proposito -
    // mas deixa de estar sinalizado, e e assim que o bridge sabe que pode retomar.
    CHECK(!eventoSinalizado(obs));
    // E TODOS os escopos foram zerados, nao so o primeiro encontrado: um nome que
    // sobrasse sinalizado seguraria para sempre um bridge que olhasse justo aquele.
    // Este OU e o mesmo que o lado Python faz.
    CHECK(!pedidoVivoComoOBridge());
    HANDLE obs2 = abrirEventoComoOBridge();
    CHECK(obs2 != 0);
    CHECK(!eventoSinalizado(obs2));
    if (obs2)
        CloseHandle(obs2);

    // Soltar repetido tem de ser inofensivo: a casca ASIO chama releaseDevice() no
    // stop(), no destrutor e no caminho de erro do start(), e disposeBuffers()
    // chama stop() de novo.
    bcd::releaseDevice();
    bcd::releaseDevice();
    CHECK(!eventoSinalizado(obs));

    // ---- pedir de novo depois de soltar ----
    // O ciclo tem de ser reutilizavel: o software de DJ liga e desliga o audio
    // varias vezes na mesma sessao, e o VirtualDJ ainda retenta sozinho a cada
    // ~60 s quando falha.
    const DWORD t2 = GetTickCount();
    CHECK(bcd::requestDevice());
    CHECK(GetTickCount() - t2 < 250);
    CHECK(eventoSinalizado(obs));

    bcd::releaseDevice();
    CHECK(!eventoSinalizado(obs));

    // O ciclo inteiro - 3 pedidos e 4 devolucoes - nao pode ter dormido nada. Um
    // unico Sleep(2000) reintroduzido em qualquer um dos dois lados cai aqui.
    CHECK(GetTickCount() - t0 < 1000);

    if (obs)
        CloseHandle(obs);
}

//----------------------------------------------------------------------
// MODO ARNES (nao e teste de unidade): exercita o RelayLink REAL contra o
// servidor de VERDADE do BCD3000Bridge.exe, que e escrito em Python. E a unica
// forma de provar o contrato entre as duas linguagens sem hardware nenhum -
// nenhum teste que fique dentro de uma das duas linguagens prova que o outro lado
// entende os bytes.
//
// Uso:  tests.exe rele-cliente <pacotes> <ms_de_espera_por_LED>
//
// Envia <pacotes> pacotes de 4 bytes previsiveis (Control Change no CC 0x05, com
// o valor andando de 1 em 1) e depois fica <ms> lendo LEDs, imprimindo cada um em
// hexadecimal. A ultima linha e legivel por script.
//
// Fica fora da contagem de verificacoes de proposito: `tests.exe` sem argumento
// nenhum roda exatamente os mesmos testes de antes.
static int modo_rele_cliente(int pacotes, int esperaMs)
{
    bcd::RelayLink link;
    DWORD e = 0;

    printf("ARNES: conectando em '%ls'\n", bcd::kRelayPipeName);
    const DWORD t0 = GetTickCount();
    while (!link.connect(&e)) {
        if (GetTickCount() - t0 > 5000) {
            printf("ARNES_FALHA: nao conectei em 5 s (erro %lu)\n", e);
            return 2;
        }
        Sleep(100);
    }
    printf("ARNES: conectado\n");

    int enviados = 0;
    for (int i = 0; i < pacotes; i++) {
        // Uma mensagem com DOIS pacotes: o util e o enchimento do aparelho. O
        // outro lado tem de injetar exatamente um dos dois.
        unsigned char msg[8];
        msg[0] = 0x0B; msg[1] = 0xB0; msg[2] = 0x05; msg[3] = (unsigned char)(i + 1);
        msg[4] = 0x00; msg[5] = 0x00; msg[6] = 0x00; msg[7] = 0x00;
        if (!link.send(msg, 8, 0, &e)) {
            printf("ARNES_FALHA: send #%d falhou (erro %lu)\n", i, e);
            link.close();
            return 3;
        }
        enviados += 2;
    }
    printf("ARNES: enviados %d pacotes (%d mensagens)\n", enviados, pacotes);

    int recebidos = 0;
    const DWORD t1 = GetTickCount();
    while (GetTickCount() - t1 < (DWORD)esperaMs) {
        if (!link.armRead(&e)) {
            printf("ARNES_FALHA: armRead falhou (erro %lu)\n", e);
            break;
        }
        if (WaitForSingleObject(link.readEvent(), 100) != WAIT_OBJECT_0)
            continue;
        const int got = link.finishRead(&e);
        if (got < 0) {
            printf("ARNES_FALHA: canal quebrou na leitura (erro %lu)\n", e);
            break;
        }
        const unsigned char* p = link.readBuffer();
        for (int i = 0; i + 4 <= got; i += 4) {
            printf("ARNES_LED %02X %02X %02X %02X\n", p[i], p[i+1], p[i+2], p[i+3]);
            recebidos++;
        }
    }

    const bool limpo = link.close();
    printf("ARNES_FIM enviados=%d recebidos=%d close=%d\n",
           enviados, recebidos, limpo ? 1 : 0);
    return 0;
}

//----------------------------------------------------------------------
//----------------------------------------------------------------------
// O relogio de nanossegundos (nanoclock.cpp), que substituiu o wintimer.cpp do
// exemplo da Steinberg.
//
// POR QUE ISTO TEM TESTE: e aritmetica, e aritmetica errada aqui nao quebra nada
// visivelmente. O carimbo de tempo vai para o software de DJ uma vez por bloco, e
// um erro nele aparece como sincronia estranha entre audio e MIDI, ou como
// velocidade calculada errada - nunca como uma falha. E o mesmo motivo pelo qual a
// aritmetica dos aneis e a latencia de entrada tem teste: as duas ja erraram.
//
// A conversao esta numa funcao PURA de proposito (ticksToNanoSeconds), separada da
// leitura do relogio. So assim ela pode ser exercitada com valores escolhidos - e o
// valor que mais importa e um que nao acontece nesta sessao: 49 dias de maquina
// ligada, onde a forma ingenua da conta estoura em 64 bits.
static void test_nanoclock(void)
{
    printf("test_nanoclock\n");

    const unsigned long long kNs   = 1000000000ull;
    const unsigned long long qpc10 = 10000000ull;       // 10 MHz, o desta maquina
    const unsigned long long qpcHpet = 3579545ull;      // o valor classico do PC

    // Os dois unicos valores de entrada sem resposta possivel.
    CHECK(bcd::ticksToNanoSeconds(12345, 0) == 0);
    CHECK(bcd::ticksToNanoSeconds(12345, 18446744074ull) == 0);   // acima do limite
    CHECK(bcd::ticksToNanoSeconds(12345, 18446744073ull) != 0);   // o limite ainda vale

    // Zero e um segundo, nas duas frequencias.
    CHECK(bcd::ticksToNanoSeconds(0, qpc10) == 0);
    CHECK(bcd::ticksToNanoSeconds(qpc10, qpc10) == kNs);
    CHECK(bcd::ticksToNanoSeconds(qpcHpet, qpcHpet) == kNs);

    // Meio segundo e um tick. Um tick a 10 MHz vale 100 ns - e a resolucao que o
    // timeGetTime() do wintimer.cpp nao tinha (ele dava 1 ms, na melhor das
    // hipoteses, e ~15,6 ms na pratica).
    CHECK(bcd::ticksToNanoSeconds(qpc10 / 2, qpc10) == kNs / 2);
    CHECK(bcd::ticksToNanoSeconds(1, qpc10) == 100ull);

    // Com resto: 10.000.003 ticks a 10 MHz sao 1.000.000.300 ns exatos. Se a conta
    // fosse feita so com a divisao inteira dos segundos, o resto se perderia e este
    // valor viraria 1.000.000.000.
    CHECK(bcd::ticksToNanoSeconds(10000003ull, qpc10) == 1000000300ull);

    // *** O caso que a forma ingenua nao aguenta. ***
    // 49 dias a 10 MHz sao 4,2336e13 ticks. `ticks * 1000000000` daria 4,2336e22, que
    // NAO cabe em 64 bits (o teto e 1,8447e19): a forma ingenua nao daria um numero
    // impreciso, daria lixo. A resposta certa e 49 * 86400 * 1e9 ns.
    const unsigned long long ticks49d = 49ull * 86400ull * qpc10;
    CHECK(bcd::ticksToNanoSeconds(ticks49d, qpc10) == 49ull * 86400ull * kNs);

    // E o caso que o wintimer.cpp perdia por outro motivo: em 49,7 dias o
    // timeGetTime() dava a volta ao zero. Aqui 100 anos ainda cabem.
    const unsigned long long ticks100y = 100ull * 365ull * 86400ull * qpc10;
    CHECK(bcd::ticksToNanoSeconds(ticks100y, qpc10) == 100ull * 365ull * 86400ull * kNs);

    // - - - - o relogio de verdade - - - -
    const unsigned long long a = bcd::nanoSecondsNow();
    CHECK(a != 0);

    // A EPOCA. O carimbo tem de continuar sendo "nanossegundos desde a partida da
    // maquina", que era a do timeGetTime(): trocar a epoca faria o host achar que o
    // audio esta atrasado em horas. O GetTickCount64() conta milissegundos desde a
    // partida e e a testemunha independente disso. A tolerancia e larga de proposito -
    // o GetTickCount64 tem passo de ~15,6 ms e os dois relogios nao sao lidos no mesmo
    // instante -, e larga ela ainda pega o erro que importa, que seria de ordens de
    // grandeza.
    const unsigned long long tickMs = (unsigned long long)GetTickCount64();
    const unsigned long long ourMs  = a / 1000000ull;
    const unsigned long long diffMs = (ourMs > tickMs) ? (ourMs - tickMs)
                                                       : (tickMs - ourMs);
    CHECK(diffMs < 2000ull);

    // Nao anda para tras.
    const unsigned long long b = bcd::nanoSecondsNow();
    CHECK(b >= a);
    CHECK(b - a < kNs);          // duas leituras seguidas nao levam um segundo

    // A PARTIDA em hi/lo, que e a forma que o ASIO exige. Trocar os dois campos e o
    // erro mais silencioso deste arquivo - nada falha, o host so le o instante
    // multiplicado por 2^32.
    //
    // O teste nao compara com um valor fixo (o relogio anda): ele recompoe e confere a
    // GRANDEZA. Com hi e lo trocados, `lo` (que vai a 4,29e9) iria para a metade alta e
    // o valor recomposto passaria de 1e19 ns, ou seja mais de 500 anos - a folga entre
    // "maquina ligada ha mais de um segundo" e "menos de dez anos" e de dez ordens de
    // grandeza, e nenhuma maquina cai no meio por acidente.
    ASIOTimeStamp ts;
    ts.hi = 0xdeadbeef;
    ts.lo = 0xfeedface;
    bcd::getNanoSeconds(&ts);
    const unsigned long long joined =
        ((unsigned long long)ts.hi << 32) | (unsigned long long)ts.lo;
    CHECK(joined > kNs);                                   // mais de 1 s de maquina
    CHECK(joined < 10ull * 365ull * 86400ull * kNs);        // menos de 10 anos

    // E o valor partido tem de ser o MESMO instante que nanoSecondsNow devolve.
    const unsigned long long c = bcd::nanoSecondsNow();
    CHECK(c >= joined);
    CHECK(c - joined < kNs);

    // Ponteiro nulo nao pode quebrar: isto e chamado do thread de audio.
    bcd::getNanoSeconds(0);
    CHECK(true);
}

//----------------------------------------------------------------------
int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "rele-cliente") == 0) {
        const int pacotes  = (argc > 2) ? atoi(argv[2]) : 4;
        const int esperaMs = (argc > 3) ? atoi(argv[3]) : 1500;
        return modo_rele_cliente(pacotes, esperaMs);
    }

    printf("== testes unitarios BCD3000 ==\n");
    test_log();
    test_format_constantes();
    test_perfis_de_aparelho();
    test_mensagens_do_host();
    test_blockBytesFor();
    test_interleave_deinterleave();
    test_canais_nulos();
    test_ring_basico();
    test_ring_da_a_volta();
    test_ring_limites();
    test_ring_uso_real();
    test_ring_init_limites();
    test_aritmetica_dos_aneis();
    test_pre_carga_da_entrada();
    test_latencia_de_entrada();
    test_nanoclock();
    test_relay_link();
    test_midibridge_ciclo_de_vida();
    test_handoff();
    printf("== %d verificacoes, %d falhas ==\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

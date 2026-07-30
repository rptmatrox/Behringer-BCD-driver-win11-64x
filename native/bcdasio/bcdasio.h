#ifndef _bcdasio_
#define _bcdasio_

// asioapi.h e a NOSSA declaracao da interface ASIO, provada binariamente identica a do
// SDK da Steinberg pelo alvo `build.bat abicheck` (426 verificacoes, 24 slots de vtable).
// Ela substitui os `asiosys.h` + `iasiodrv.h` que estavam aqui, e traz sozinha o
// preambulo de COM (por <unknwn.h>), que era o que os includes de rpc.h, rpcndr.h,
// windows.h e ole2.h faziam manualmente.
#include "asioapi.h"
#include "comserver.h"
#include "audioengine.h"
#include "midibridge.h"
#include "usbdev.h"

enum {
    kNumInputs  = 4,
    kNumOutputs = 4
};

// A ORDEM DAS CLASSES-BASE E ABI, e nao ha aqui uma terceira por acaso.
//
// IASIO vem PRIMEIRO, e tem de vir: o ponteiro que o host recebe do QueryInterface e o
// ponteiro para o subobjeto IASIO, e so com IASIO na primeira posicao ele coincide com o
// endereco do objeto completo - que e tambem o que faz o mesmo ponteiro servir como
// IUnknown, porque IASIO deriva de IUnknown no deslocamento 0. Trocada a ordem, o driver
// compila e o host chama a vtable errada.
//
// bcd::EngineClient e a outra ponta: o motor de audio chama por ela, no thread de audio.
// Ela tem destrutor virtual proprio, e e por isso que o destrutor desta classe e virtual
// sem a palavra `virtual` aparecer - o slot dele vive na vtable da EngineClient e nao
// desloca nenhum dos 24 slots de IASIO. IASIO NAO pode ter destrutor virtual, e o
// abicheck confere isso; ver a nota 2 no topo de asioapi.h.
class BcdAsioDriver : public IASIO, public bcd::EngineClient
{
public:
    BcdAsioDriver();
    ~BcdAsioDriver();

    // IUnknown. Escritos a mao no lugar do macro DECLARE_IUNKNOWN do combase.h do SDK,
    // que delegava os tres ao "dono" do objeto para dar conta de agregacao - maquinaria
    // que este driver nao usa. Ver a nota do QueryInterface em bcdasio.cpp: e la que mora
    // a peculiaridade do ASIO de pedir o CLSID como IID.
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv);
    virtual ULONG   STDMETHODCALLTYPE AddRef();
    virtual ULONG   STDMETHODCALLTYPE Release();

    // IASIO - os 21 metodos, na ORDEM da vtable.
    //
    // O RISCO DE VTABLE NAO MORA AQUI, e a versao anterior deste comentario mandava o
    // leitor para o lugar errado. Os 21 metodos de IASIO sao PUROS (`= 0`) em asioapi.h:
    // qualquer divergencia de ASSINATURA entre esta lista e a de la deixa um metodo sem
    // override, a classe fica abstrata e o `new BcdAsioDriver` do comserver.cpp NAO
    // COMPILA. E a ordem de DECLARACAO nesta classe derivada nao afeta vtable nenhuma:
    // os slots vem da base primaria, na ordem em que asioapi.h os declara. Reordenar
    // aqui e questao de leitura; reordenar la e questao de ABI.
    //
    // ONDE O RISCO MORA DE VERDADE: em asioapi.h. Foi la que a injecao (b) da Tarefa 11
    // trocou start() com stop() de posicao e produziu *** BUILD_OK sem um unico aviso ***
    // - o host chamaria "comecar" e o driver executaria "parar", com sintoma
    // incompreensivel. Nada em compilacao pega isso. Quem pega e o `build.bat abicheck`,
    // que compara os 24 slots em TEMPO DE EXECUCAO contra os do SDK. Mexeu em
    // asioapi.h: rode o abicheck.
    ASIOBool init(void* sysRef);
    void     getDriverName(char* name);
    long     getDriverVersion();
    void     getErrorMessage(char* string);

    ASIOError start();
    ASIOError stop();

    ASIOError getChannels(long* numInputChannels, long* numOutputChannels);
    ASIOError getLatencies(long* inputLatency, long* outputLatency);
    ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity);

    ASIOError canSampleRate(ASIOSampleRate sampleRate);
    ASIOError getSampleRate(ASIOSampleRate* sampleRate);
    ASIOError setSampleRate(ASIOSampleRate sampleRate);
    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources);
    ASIOError setClockSource(long index);

    ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp);
    ASIOError getChannelInfo(ASIOChannelInfo* info);

    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                            long bufferSize, ASIOCallbacks* callbacks);
    ASIOError disposeBuffers();

    ASIOError controlPanel();
    ASIOError future(long selector, void* opt);
    ASIOError outputReady();

    // bcd::EngineClient - chamados pelo thread de audio do motor
    virtual void onBlock(const short* in, short* out, int frames);
    virtual void onDeviceLost();

private:
    void bufferSwitchX(long index);

    // UM caminho para toda mensagem de erro que chega ao host. `what` e o que o driver
    // estava tentando fazer; `full` e a mensagem INTEGRA de quem falhou (usbdev ou
    // motor de audio). Ver o comentario da definicao em bcdasio.cpp: os tres pontos de
    // falha do driver passavam o texto JA cortado para o log, e foi assim que a causa
    // verdadeira de um incidente real desapareceu do log de campo.
    void setError(const char* what, const char* full);

    // Contagem de referencias do COM. Primeiro membro de proposito: e o unico que existe
    // por causa do COM e nao por causa do audio, e mantendo-o fora do bloco abaixo a nota
    // sobre ordem de destruicao continua falando so dos tres que importam para ela.
    // Mexido apenas por Interlocked* - um host pode chamar AddRef e Release de threads
    // diferentes.
    volatile LONG   refCount;

    // A ordem de DECLARACAO importa: os membros sao destruidos na ordem
    // inversa, entao `midi` (que no destrutor dele chama stop()) morre antes de
    // `device`. E a mesma ordem exigida em stop(): MIDI sai antes do aparelho.
    bcd::UsbDevice   device;
    bcd::AudioEngine engine;
    bcd::MidiBridge  midi;

    double          samplePosition;
    double          sampleRate;
    ASIOCallbacks*  callbacks;
    ASIOTime        asioTime;
    ASIOTimeStamp   theSystemTime;

    short*          inputBuffers[kNumInputs];    // duplo: 2 * blockFrames cada
    short*          outputBuffers[kNumOutputs];
    long            inMap[kNumInputs];
    long            outMap[kNumOutputs];

    long            blockFrames;
    long            activeInputs;
    long            activeOutputs;
    long            toggle;
    bool            active, started;
    bool            timeInfoMode, tcRead;

    // O TAMANHO DESTE MEMBRO E O CONTRATO, e nao um numero parecido com ele.
    //
    // Ele tinha 128 bytes, herdados do exemplo da Steinberg, e o getErrorMessage()
    // copiava daqui para o buffer DO HOST com `strcpy`. O contrato do metodo 4 de
    // asioapi.h e "no maximo 124 bytes com o terminador": eram quatro bytes alem do fim
    // de um buffer do processo do software de DJ, e isso disparou de verdade em
    // 2026-07-29, quatro vezes, quando uma mensagem nova chegou a 217 caracteres.
    // Com bcd::kAsioErrorMax aqui, este membro nao tem numero PROPRIO: ele e DERIVADO da
    // constante, entao nao existem dois valores para alguem manter iguais. Ver o
    // comentario da constante em usbdev.h, que tambem explica por que ela vive la.
    //
    // E o tamanho deste membro NAO decide mais quanto o getErrorMessage() escreve no
    // buffer do host - aquela copia passou a ser limitada pelo CONTRATO. AUMENTAR este
    // vetor deixou de ser perigoso, e isso esta MEDIDO: com errorMessage[256] o alvo
    // strict compila limpo e o getErrorMessage continua escrevendo no maximo os indices
    // 0..123. O que a trava de baixo protege e outra coisa, e o comentario dela diz o que.
    char            errorMessage[bcd::kAsioErrorMax];

    // A DIRECAO QUE SOBROU, e ela e uma so: ENCOLHER.
    //
    // Trocar a expressao acima por um literal MENOR que o contrato nao corrompe memoria
    // nenhuma - o getErrorMessage le uma cadeia terminada e copia no maximo 123 bytes -,
    // mas faz o software de DJ receber MENOS texto do que o contrato permite, em silencio.
    // E o defeito irmao daquele que custou uma hora em 2026-07-29: mensagem que chega
    // cortada sem ninguem saber. Por isso `>=` e nao `==`: crescer e legitimo (guardar a
    // integra no membro e uma mudanca que alguem pode querer, e agora e segura), encolher
    // nao e.
    //
    // Fica DENTRO da classe porque o membro e privado: um typedef fora dela nao pode
    // sequer nomea-lo.
    typedef char kErroGuardaOContratoInteiro[
        ((int)sizeof(errorMessage) >= bcd::kAsioErrorMax) ? 1 : -1];
};

// O ELO QUE FALTAVA: o 124 DO CONTRATO e o 124 DA NOSSA CONSTANTE sao o MESMO numero, e
// agora e o compilador que garante isso.
//
// DE ONDE VEIO A ARMADILHA. A rodada que fechou o estouro de 2026-07-29 deixou o
// getErrorMessage() limitando a copia por `sizeof(errorMessage)`, isto e, o limite do
// buffer DO HOST amarrado ao tamanho do NOSSO membro. Estava correto porque os dois
// valiam 124, e o que mantinha os dois iguais era um humano. O passo NATURAL depois de
// uma rodada cujo tema declarado e "o log guarda o texto inteiro" e alguem aumentar o
// membro para guardar a integra nele tambem - e naquele desenho a copia passaria a
// escrever ate 256 bytes no buffer de 124 do software de DJ, que e o defeito de
// 2026-07-29 de volta byte por byte. Nada pegava: o CHECK do teste mede a CONSTANTE e
// nao o membro, o alvo `tests` nao linka bcdasio.cpp, o /W4 /WX fica calado, e o
// comentario da funcao dizia ao editor futuro o CONTRARIO.
//
// SAO TRES NUMEROS, E DUAS ARESTAS. Vale escrever quais, porque a confusao entre elas e
// o que fez esta armadilha existir:
//
//   (1) o CONTRATO  = sizeof(ASIODriverInfo::errorMessage), em asioapi.h .......... 124
//   (2) a NOSSA CONSTANTE = bcd::kAsioErrorMax, em usbdev.h ...................... 124
//   (3) o NOSSO MEMBRO = sizeof(errorMessage), acima ...... derivado de (2), sem numero
//
//   aresta (2) contra (1): *** E A UNICA QUE PODE CORROMPER MEMORIA. *** Se a nossa
//     constante passar do contrato, o getErrorMessage - que agora se limita por ELA -
//     escreve alem do fim do buffer do host. E o typedef abaixo que fecha esta.
//   aresta (3) contra (2): deixou de ser aresta de memoria quando a copia passou a ser
//     limitada pelo contrato. Sobrou uma direcao, ENCOLHER, e ela e fechada dentro da
//     classe por kErroGuardaOContratoInteiro - ver o comentario de la.
//
// MEDIDO, e nao deduzido, porque a expectativa natural aqui esta errada: com
// errorMessage[256] injetado, o alvo strict compila LIMPO. Esse typedef nao menciona o
// membro, e nao deve - crescer o membro passou a ser inofensivo, e uma trava que
// proibisse isso seria trava contra uma mudanca legitima. Quem quiser ver a trava morder
// injete em bcd::kAsioErrorMax, que e a aresta que importa.
//
// POR QUE A TRAVA VIVE NESTE ARQUIVO, e nao em bcdasio.cpp junto das outras tres: este e
// o unico arquivo do projeto que inclui asioapi.h (o contrato) E usbdev.h (a nossa
// constante), e as duas travas ficam a poucas linhas do membro e da copia de que falam.
//
// E o idioma que este projeto ja usa tres vezes: kNomeDoDriverCabeNoContratoDoAsio e os
// dois kProfileNamesCobrem*, em bcdasio.cpp.
typedef char kErroCabeNoContratoDoAsio[
    ((int)sizeof(((ASIODriverInfo*)0)->errorMessage) == bcd::kAsioErrorMax) ? 1 : -1];

#endif

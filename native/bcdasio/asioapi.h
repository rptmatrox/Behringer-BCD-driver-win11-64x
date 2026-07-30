// Declaracao PROPRIA da interface ASIO 2.3 - tipos, constantes e a interface COM
// `IASIO` - escrita para este projeto, sem nenhum arquivo da Steinberg.
//
// POR QUE ESTE ARQUIVO EXISTE
// Os cabecalhos do SDK da Steinberg que o BcdAsio.dll usa hoje (asiosys.h, asio.h e
// iasiodrv.h, de native/ASIOSDK/common/) estao sob licenca DUPLA: proprietaria da
// Steinberg, que exige acordo assinado antes de publicar, OU GPL v3. Enquanto o DLL
// depender deles, este projeto nao pode escolher a propria licenca e quem clonar o
// repositorio precisa baixar o SDK para compilar. Este arquivo remove essa
// dependencia: e a NOSSA declaracao da mesma fronteira binaria.
//
// O QUE ESTE ARQUIVO NAO E
// Nao e uma copia. Nomes de tipos, nomes de campos, ordem de campos e valores de
// constantes SAO a interface - sem eles nenhum software de DJ conversa com este
// driver -, mas o texto explicativo aqui e nosso, escrito a partir do que a fronteira
// exige. Nao ha nenhuma linha de prosa da Steinberg neste arquivo.
//
// COMO SABER QUE ESTA CERTO
// Nao por revisao de codigo: por PROVA MECANICA, em `abicheck.cpp`, que inclui este
// arquivo e os do SDK ao mesmo tempo (o SDK dentro de um namespace) e compara
// `sizeof`, `alignof` e o deslocamento de CADA campo de CADA estrutura, o valor de
// CADA constante, e - em tempo de execucao, porque nenhum `static_assert` ve isso - a
// ORDEM DOS METODOS VIRTUAIS de `IASIO`, slot por slot. Rode com:
//
//     build.bat abicheck
//
// Esse alvo exige o SDK no disco e NAO faz parte do `all`: e ferramenta de
// verificacao local. Depois que o SDK sair da maquina de quem clona, a prova nao
// pode mais ser refeita - por isso este arquivo cobre a interface INTEIRA agora, e
// nao apenas os pedacos que o driver usa hoje. Declaracao que nasce depois da
// remocao do SDK nasce sem prova.
//
// POR QUE SO WINDOWS
// A largura de `ASIOSamples`, `ASIOTimeStamp` e `ASIOSampleRate` no ASIO depende da
// plataforma. Este projeto e um driver ASIO para Windows, e so a variante de Windows
// esta declarada aqui - e so ela pode ser provada nesta maquina. Declarar as outras
// as escondendo atras de `#if` daria a ilusao de portabilidade sem prova nenhuma.

#ifndef _bcd_asioapi_
#define _bcd_asioapi_

#if !defined(_WIN32) && !defined(_WIN64)
#error "asioapi.h: apenas Windows. Ver a nota POR QUE SO WINDOWS no topo do arquivo."
#endif

// IUnknown, e o `interface`/`STDMETHODCALLTYPE` do COM. Vem do Windows SDK, nao da
// Steinberg. <unknwn.h> ja traz sozinho o preambulo de COM (rpc.h, rpcndr.h e, se
// COM_NO_WINDOWS_H nao estiver definido, windows.h e ole2.h), entao este cabecalho
// funciona incluido em qualquer ordem.
#include <unknwn.h>

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// EMPACOTAMENTO: 4 bytes, e isto e load-bearing.
//
// Todas as estruturas de dados do ASIO sao empacotadas em 4 bytes. Nao e detalhe de
// estilo: em x64 sem isso o `double` e os ponteiros pedem alinhamento 8, e
// `ASIOTimeCode` passaria de 84 para 88 bytes, `ASIOTransportParameters` mudaria o
// deslocamento de `samplePosition` de 4 para 8, e todo `ASIOTime` que o host le sairia
// deslocado. A prova em abicheck.cpp confere `alignof` de cada estrutura exatamente
// para pegar este erro, que `sizeof` sozinho as vezes nao pega.
//
// A regiao empacotada cobre SO os tipos de dados. `IASIO`, no fim do arquivo, fica
// FORA dela - igual ao SDK, onde a interface vive em outro cabecalho, incluido depois
// que o empacotamento ja voltou ao normal.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#pragma pack(push, 4)

// - - - - - - - - - - - - - - - - - - - - - - - - -
// Tipos escalares
// - - - - - - - - - - - - - - - - - - - - - - - - -

// Contagem de amostras e instante de tempo: inteiros de 64 bits PARTIDOS EM DOIS de
// 32, `hi` antes de `lo`. Em Windows o ASIO nao usa o inteiro de 64 bits nativo, e a
// ordem dos dois campos e a parte mais facil de errar de todo este arquivo: trocada,
// o host le posicao de amostra multiplicada por 2^32 e a sincronia vai embora sem
// nenhum erro aparecer em lugar nenhum. Ver test_swap no abicheck.
typedef struct ASIOSamples {
    unsigned long hi;
    unsigned long lo;
} ASIOSamples;

// Instante em nanossegundos, mesmo formato partido.
typedef struct ASIOTimeStamp {
    unsigned long hi;
    unsigned long lo;
} ASIOTimeStamp;

// Taxa de amostragem em ponto flutuante de 64 bits nativo (IEEE 754).
typedef double ASIOSampleRate;

// Booleano do ASIO: `long`, nao `bool`. Passa por valor em varias assinaturas, e
// `bool` (1 byte) no lugar embaralharia a pilha.
typedef long ASIOBool;
enum {
    ASIOFalse = 0,
    ASIOTrue  = 1
};

// Formato de amostra de um canal, como devolvido em ASIOChannelInfo::type.
//
// TODOS os valores estao escritos EXPLICITAMENTE de proposito, aqui e em todas as
// enumeracoes deste arquivo. Enumeracao com valor implicito faz o valor de cada
// entrada depender de quantas entradas vem antes: uma entrada esquecida no meio
// desloca silenciosamente tudo o que vem depois, e o driver passa a responder a
// pergunta errada. Com valor explicito, um erro e um erro localizado - e a prova
// confere um por um.
typedef long ASIOSampleType;
enum {
    ASIOSTInt16MSB    = 0,
    ASIOSTInt24MSB    = 1,      // tambem usado para 20 bits
    ASIOSTInt32MSB    = 2,
    ASIOSTFloat32MSB  = 3,      // IEEE 754 de 32 bits
    ASIOSTFloat64MSB  = 4,      // IEEE 754 de 64 bits

    // Dado em recipiente de 32 bits, com o numero de bits uteis alinhado dentro dele.
    ASIOSTInt32MSB16  = 8,
    ASIOSTInt32MSB18  = 9,
    ASIOSTInt32MSB20  = 10,
    ASIOSTInt32MSB24  = 11,

    ASIOSTInt16LSB    = 16,     // <- o que este driver entrega e consome
    ASIOSTInt24LSB    = 17,
    ASIOSTInt32LSB    = 18,
    ASIOSTFloat32LSB  = 19,
    ASIOSTFloat64LSB  = 20,

    ASIOSTInt32LSB16  = 24,
    ASIOSTInt32LSB18  = 25,
    ASIOSTInt32LSB20  = 26,
    ASIOSTInt32LSB24  = 27,

    // DSD: 1 bit por amostra, 8 amostras por byte (a variante NER8 usa 1 byte por
    // amostra e nao depende de ordem de bits).
    ASIOSTDSDInt8LSB1 = 32,
    ASIOSTDSDInt8MSB1 = 33,
    ASIOSTDSDInt8NER8 = 40,

    ASIOSTLastEntry   = 41      // sentinela: um a mais que o ultimo formato
};

// - - - - - - - - - - - - - - - - - - - - - - - - -
// Codigos de erro
// - - - - - - - - - - - - - - - - - - - - - - - - -

typedef long ASIOError;
enum {
    ASE_OK               = 0,
    // Valor de sucesso PROPRIO do `future()`. La, `ASE_OK` NAO conta como sucesso -
    // o host so aceita este valor -, e o numero e arbitrario de proposito, para nao
    // colidir com nada.
    ASE_SUCCESS          = 0x3f4847a0,
    ASE_NotPresent       = -1000,   // entrada ou saida ausente/indisponivel
    ASE_HWMalfunction    = -999,    // aparelho com defeito; qualquer chamada pode devolver
    ASE_InvalidParameter = -998,
    ASE_InvalidMode      = -997,
    ASE_SPNotAdvancing   = -996,    // posicao de amostra pedida com o aparelho parado
    ASE_NoClock          = -995,    // taxa de amostragem indisponivel ou desconhecida
    ASE_NoMemory         = -994
};

// - - - - - - - - - - - - - - - - - - - - - - - - -
// Informacao de tempo
// - - - - - - - - - - - - - - - - - - - - - - - - -

typedef struct ASIOTimeCode {
    double        speed;            // relacao com a velocidade nominal; 0. ou 1. se nao suportado
    ASIOSamples   timeCodeSamples;  // instante do time code, em amostras
    unsigned long flags;            // ver ASIOTimeCodeFlags
    char          future[64];       // reservado; 64 bytes fazem parte do tamanho da estrutura
} ASIOTimeCode;

typedef enum ASIOTimeCodeFlags {
    kTcValid      = 1,
    kTcRunning    = 2,      // 1 << 1
    kTcReverse    = 4,      // 1 << 2
    kTcOnspeed    = 8,      // 1 << 3
    kTcStill      = 16,     // 1 << 4

    kTcSpeedValid = 256     // 1 << 8
} ASIOTimeCodeFlags;

// ATENCAO ao nome: `AsioTimeInfo`, com uma letra maiuscula so - nao `ASIOTimeInfo`.
// E o unico tipo do ASIO que foge do prefixo em maiusculas, e um nome diferente aqui
// nao compila do outro lado.
typedef struct AsioTimeInfo {
    double         speed;           // velocidade absoluta (1. = nominal)
    ASIOTimeStamp  systemTime;      // instante do sistema, em nanossegundos, associado a samplePosition
    ASIOSamples    samplePosition;
    ASIOSampleRate sampleRate;      // taxa corrente
    unsigned long  flags;           // ver AsioTimeInfoFlags
    char           reserved[12];
} AsioTimeInfo;

typedef enum AsioTimeInfoFlags {
    kSystemTimeValid     = 1,       // sempre tem de valer
    kSamplePositionValid = 2,       // sempre tem de valer
    kSampleRateValid     = 4,
    kSpeedValid          = 8,

    kSampleRateChanged   = 16,
    kClockSourceChanged  = 32
} AsioTimeInfoFlags;

typedef struct ASIOTime {
    long                reserved[4];    // tem de ser zero
    struct AsioTimeInfo timeInfo;       // obrigatorio
    struct ASIOTimeCode timeCode;       // opcional; o host so le se (timeCode.flags & kTcValid)
} ASIOTime;

// - - - - - - - - - - - - - - - - - - - - - - - - -
// Callbacks do host
//
// ORDEM DOS QUATRO PONTEIROS E ABI. O driver os chama por nome, mas o host preenche
// uma estrutura por deslocamento: trocar dois de lugar faz o driver chamar
// `sampleRateDidChange` quando quer avisar de um bloco de audio pronto.
// - - - - - - - - - - - - - - - - - - - - - - - - -

typedef struct ASIOCallbacks {
    // Ha um bloco de entrada cheio e um de saida a preencher, no indice dado (0 = A,
    // 1 = B). `directProcess` sugere ao host se ele pode processar na hora ou se
    // deveria adiar - vale ASIOFalse na duvida. Pode ser chamado em contexto de
    // interrupcao.
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);

    // O aparelho mudou de taxa de amostragem por conta propria (0 se ficou
    // desconhecida, por exemplo perda de relogio externo).
    void (*sampleRateDidChange)(ASIOSampleRate sRate);

    // Canal generico driver -> host. Os seletores estao abaixo. So existe a partir da
    // versao 2 do ASIO.
    long (*asioMessage)(long selector, long value, void* message, double* opt);

    // Substituto de `bufferSwitch` que carrega a informacao de tempo junto. O driver
    // so pode usa-lo depois que o host aceitar, respondendo 1 a um `asioMessage` com
    // kAsioSupportsTimeInfo.
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex,
                                      ASIOBool directProcess);
} ASIOCallbacks;

// Seletores do `asioMessage` (driver -> host).
enum {
    kAsioSelectorSupported    = 1,  // seletor em <value>; 1 se suportado, 0 se nao
    kAsioEngineVersion        = 2,  // versao do host: 2 ou mais
    kAsioResetRequest         = 3,  // pedido de reinicio do driver (fecha e reabre)
    kAsioBufferSizeChange     = 4,  // nao implementado pelos hosts; usar kAsioResetRequest
    kAsioResyncRequest        = 5,  // o driver perdeu a sincronia; o timestamp nao vale mais
    kAsioLatenciesChanged     = 6,  // as latencias mudaram; o host vai pedi-las de novo
    kAsioSupportsTimeInfo     = 7,  // se o host responder 1, passa a esperar bufferSwitchTimeInfo
    kAsioSupportsTimeCode     = 8,
    kAsioMMCCommand           = 9,  // sem uso
    kAsioSupportsInputMonitor = 10,
    kAsioSupportsInputGain    = 11, // sem uso e sem definicao
    kAsioSupportsInputMeter   = 12, // sem uso e sem definicao
    kAsioSupportsOutputGain   = 13, // sem uso e sem definicao
    kAsioSupportsOutputMeter  = 14, // sem uso e sem definicao
    kAsioOverload             = 15, // o driver detectou sobrecarga

    kAsioNumMessageSelectors  = 16  // sentinela
};

// - - - - - - - - - - - - - - - - - - - - - - - - -
// Abertura e consulta
// - - - - - - - - - - - - - - - - - - - - - - - - -

// Preenchida pelo LADO DO HOST, na chamada C `ASIOInit`. Um driver nunca ve esta
// estrutura - o `init()` dele recebe apenas o `sysRef`. Esta declarada aqui porque
// faz parte da fronteira, e porque este e o unico momento do projeto em que da para
// prova-la contra o SDK.
typedef struct ASIODriverInfo {
    long  asioVersion;          // entrada: versao do host; saida: versao do ASIO implementada
    long  driverVersion;        // saida: versao do driver, no formato que ele quiser
    char  name[32];
    char  errorMessage[124];
    void* sysRef;               // entrada: no Windows, o handle da janela principal do host
} ASIODriverInfo;

typedef struct ASIOClockSource {
    long     index;             // identificador usado em setClockSource()
    long     associatedChannel; // canal associado (S/PDIF, AES/EBU...), -1 se nenhum
    long     associatedGroup;   // grupo desse canal, -1 se nenhum
    ASIOBool isCurrentSource;   // saida: ASIOTrue se e a fonte em uso
    char     name[32];          // para o usuario escolher
} ASIOClockSource;

typedef struct ASIOChannelInfo {
    long           channel;         // entrada: indice do canal
    ASIOBool       isInput;         // entrada
    ASIOBool       isActive;        // saida
    long           channelGroup;    // saida
    ASIOSampleType type;            // saida
    char           name[32];        // saida
} ASIOChannelInfo;

// - - - - - - - - - - - - - - - - - - - - - - - - -
// Preparacao dos buffers
// - - - - - - - - - - - - - - - - - - - - - - - - -

typedef struct ASIOBufferInfo {
    ASIOBool isInput;       // entrada: ASIOTrue = entrada, senao saida
    long     channelNum;    // entrada: indice do canal
    void*    buffers[2];    // saida: as duas metades do buffer duplo, do driver
} ASIOBufferInfo;

// - - - - - - - - - - - - - - - - - - - - - - - - -
// Seletores do `future()` (host -> driver)
//
// Um driver pode implementar so os que quiser e devolver ASE_NotPresent no resto -
// e o que este faz -, mas os VALORES tem de ser todos os certos: o host manda o
// numero, nao o nome.
// - - - - - - - - - - - - - - - - - - - - - - - - -
enum {
    kAsioEnableTimeCodeRead       = 1,  // sem argumento
    kAsioDisableTimeCodeRead      = 2,  // sem argumento
    kAsioSetInputMonitor          = 3,  // ASIOInputMonitor* em params
    kAsioTransport                = 4,  // ASIOTransportParameters* em params
    kAsioSetInputGain             = 5,  // ASIOChannelControls* em params, aplica ganho
    kAsioGetInputMeter            = 6,  // ASIOChannelControls* em params, preenche medidor
    kAsioSetOutputGain            = 7,  // ASIOChannelControls* em params, aplica ganho
    kAsioGetOutputMeter           = 8,  // ASIOChannelControls* em params, preenche medidor
    kAsioCanInputMonitor          = 9,  // os kAsioCanXXX nao levam argumento
    kAsioCanTimeInfo              = 10,
    kAsioCanTimeCode              = 11,
    kAsioCanTransport             = 12,
    kAsioCanInputGain             = 13,
    kAsioCanInputMeter            = 14,
    kAsioCanOutputGain            = 15,
    kAsioCanOutputMeter           = 16,
    kAsioOptionalOne              = 17,

    // DSD: trocar o subsistema entre PCM e DSD. Os valores sao dispersos de
    // proposito, para nao colidir com a sequencia acima.
    kAsioSetIoFormat              = 0x23111961, // ASIOIoFormat* em params
    kAsioGetIoFormat              = 0x23111983, // ASIOIoFormat* em params
    kAsioCanDoIoFormat            = 0x23112004, // ASIOIoFormat* em params

    // Deteccao de falha de bloco.
    kAsioCanReportOverload        = 0x24042012, // ASE_SUCCESS se o driver sabe reportar sobrecarga

    kAsioGetInternalBufferSamples = 0x25042012  // ASIOInternalBufferInfo* em params
};

typedef struct ASIOInputMonitor {
    long     input;     // entrada monitorada, -1 = todas
    long     output;    // saida sugerida para a monitoracao
    long     gain;      // ganho sugerido, 0 a 0x7fffffff (-inf a +12 dB)
    ASIOBool state;     // ASIOTrue = ligado
    long     pan;       // 0 = tudo a esquerda, 0x7fffffff = tudo a direita
} ASIOInputMonitor;

typedef struct ASIOChannelControls {
    long     channel;       // entrada: indice do canal
    ASIOBool isInput;       // entrada
    long     gain;          // entrada: 0 a 0x7fffffff
    long     meter;         // saida:  0 a 0x7fffffff
    char     future[32];
} ASIOChannelControls;

typedef struct ASIOTransportParameters {
    long        command;            // ver a enumeracao abaixo
    ASIOSamples samplePosition;
    long        track;
    long        trackSwitches[16];  // 512 trilhas, um bit cada
    char        future[64];
} ASIOTransportParameters;

// Comandos do kAsioTransport.
enum {
    kTransStart      = 1,
    kTransStop       = 2,
    kTransLocate     = 3,   // para samplePosition
    kTransPunchIn    = 4,
    kTransPunchOut   = 5,
    kTransArmOn      = 6,   // track
    kTransArmOff     = 7,   // track
    kTransMonitorOn  = 8,   // track
    kTransMonitorOff = 9,   // track
    kTransArm        = 10,  // trackSwitches
    kTransMonitor    = 11   // trackSwitches
};

// Formato de entrada/saida (PCM ou DSD). O chamador pede um formato; um driver que
// nao o suporta troca o campo por kASIOFormatInvalid e a chamada inteira falha.
typedef long int ASIOIoFormatType;
enum ASIOIoFormatType_e {
    kASIOFormatInvalid = -1,
    kASIOPCMFormat     = 0,
    kASIODSDFormat     = 1
};

typedef struct ASIOIoFormat_s {
    ASIOIoFormatType FormatType;
    // O tamanho total da estrutura e 512 bytes, e por isso o preenchimento e
    // calculado em vez de escrito: o campo acima ja consumiu parte dele.
    char             future[512 - sizeof(ASIOIoFormatType)];
} ASIOIoFormat;

// Buffer interno do driver ALEM do buffer duplo - o caso dos drivers USB, este
// inclusive. Um host pode pedir o tamanho para explicar falhas de bloco.
typedef struct ASIOInternalBufferInfo {
    long inputSamples;      // ja incluido no que getLatencies() reporta
    long outputSamples;     // idem
} ASIOInternalBufferInfo;

#pragma pack(pop)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// A INTERFACE.
//
// Tres coisas aqui sao ABI e nao aparecem em nenhum `sizeof`:
//
//  1. A ORDEM DOS METODOS. A vtable e um vetor: o host chama pelo indice, nunca pelo
//     nome. Dois metodos trocados de lugar e um host chamando `stop()` executa
//     `start()`. Provado slot por slot, em execucao, pelo abicheck - e essa metade da
//     prova existe justamente porque nenhum `static_assert` alcanca isto.
//
//  2. NAO HA DESTRUTOR VIRTUAL, e nao pode haver. Um destrutor virtual declarado aqui
//     ocuparia o primeiro slot depois dos tres do IUnknown e deslocaria os vinte e um
//     metodos - o host chamaria o destrutor achando que chama `init()`. O abicheck
//     confere isso em tempo de compilacao (std::has_virtual_destructor).
//     Quem implementa esta interface tem o destrutor na propria classe concreta, que
//     e como o driver deste projeto faz; o host destroi o objeto por Release(), nunca
//     por `delete`.
//
//  3. NENHUMA CONVENCAO DE CHAMADA EXPLICITA nos vinte e um metodos - de proposito, e
//     e o mesmo efeito do SDK, que tambem nao poe macro nenhuma neles. Sao metodos de
//     classe comuns: em x86 isso e __thiscall, em x64 nao existe escolha. Marcar
//     STDMETHODCALLTYPE aqui MUDARIA a convencao em x86 e embaralharia a pilha contra
//     qualquer host existente. Os tres metodos do IUnknown, herdados do Windows SDK,
//     sao os unicos com convencao explicita (STDMETHODCALLTYPE), e continuam vindo de
//     la sem que este arquivo opine.
//
// `interface` do COM e apenas `struct` (Windows SDK, combaseapi.h/basetyps.h:
// `#define interface __STRUCT__`, `#define __STRUCT__ struct`). Escrevemos `struct`
// direto para nao depender de macro, e o abicheck confere que o resultado e o mesmo.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#ifndef __ASIODRIVER_FWD_DEFINED__
#define __ASIODRIVER_FWD_DEFINED__
typedef struct IASIO IASIO;
#endif

struct IASIO : public IUnknown
{
    // 1. Abrir o driver. `sysHandle` e o handle da janela principal do host.
    virtual ASIOBool  init(void* sysHandle) = 0;
    // 2. Nome do driver, no maximo 32 bytes com o terminador.
    virtual void      getDriverName(char* name) = 0;
    // 3. Versao do driver, no formato que ele quiser.
    virtual long      getDriverVersion() = 0;
    // 4. Mensagem de erro da ultima falha, no maximo 124 bytes com o terminador.
    virtual void      getErrorMessage(char* string) = 0;
    // 5. Comecar a tocar e a capturar, com a posicao de amostra zerada.
    virtual ASIOError start() = 0;
    // 6. Parar. Depois de devolver, o driver NAO pode mais chamar bufferSwitch.
    virtual ASIOError stop() = 0;
    // 7. Quantos canais existem em cada direcao.
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    // 8. Latencia de entrada e de saida, em frames, incluindo o que o aparelho atrasa.
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    // 9. Tamanhos de bloco aceitos. `granularity` -1 significa potencias de dois.
    virtual ASIOError getBufferSize(long* minSize, long* maxSize,
                                    long* preferredSize, long* granularity) = 0;
    // 10. A taxa e possivel? ASE_NoClock se nao.
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    // 11. Taxa corrente.
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    // 12. Trocar a taxa; 0 pede sincronia externa.
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    // 13. Fontes de relogio. `numSources` entra com o tamanho do vetor e sai com
    //     quantas existem (no minimo 1, o oscilador interno).
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
    // 14. Escolher a fonte de relogio, pelo `index` obtido acima.
    virtual ASIOError setClockSource(long reference) = 0;
    // 15. Par posicao-de-amostra / instante do sistema, referente ao bloco corrente.
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
    // 16. Natureza de um canal: grupo, formato, nome, e se esta ativo.
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    // 17. Alocar os buffers duplos dos canais pedidos. Os buffers sao do DRIVER.
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                                    long bufferSize, ASIOCallbacks* callbacks) = 0;
    // 18. Liberar os buffers. Implica stop().
    virtual ASIOError disposeBuffers() = 0;
    // 19. Abrir o painel de controle do aparelho, se houver.
    virtual ASIOError controlPanel() = 0;
    // 20. Extensao da interface. Sucesso aqui e ASE_SUCCESS - ASE_OK NAO basta.
    virtual ASIOError future(long selector, void* opt) = 0;
    // 21. O host acabou de preencher os buffers de saida. ASE_OK apenas se isso
    //     realmente reduzir a latencia de saida deste driver; ASE_NotPresent evita
    //     que o host continue chamando.
    virtual ASIOError outputReady() = 0;
};

#endif // _bcd_asioapi_

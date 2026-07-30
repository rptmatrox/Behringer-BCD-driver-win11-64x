// PROVA de que `asioapi.h` - a nossa declaracao da interface ASIO - e binariamente
// identica a do SDK da Steinberg. Nao entra no produto: e ferramenta de verificacao
// local, e exige o SDK no disco (native/ASIOSDK/).
//
//     build.bat abicheck && abicheck.exe
//
// POR QUE A PROVA E O PRODUTO, E NAO AS DECLARACOES
// A interface ASIO e uma fronteira BINARIA entre este DLL e o software de DJ. Uma
// estrutura com tamanho diferente, um campo em deslocamento diferente, um valor de
// enumeracao trocado ou - o pior - um metodo fora de ordem na vtable, e o host le
// lixo. E o modo de falha e o mais cruel que existe: pode funcionar por sorte nesta
// maquina e travar na de outra pessoa, ou corromper em silencio. Revisao de codigo nao
// pega isso. Duas maquinas com o mesmo compilador tambem nao provam nada uma sobre a
// outra. So a comparacao mecanica contra o SDK prova.
//
// A PROVA TEM DUAS METADES, E UMA SOZINHA NAO BASTA
//
//  (a) LAYOUT, em tempo de compilacao. Este arquivo inclui os NOSSOS cabecalhos e os
//      do SDK ao mesmo tempo - o SDK dentro do namespace `sdk`, para nao colidir - e
//      afirma com `static_assert` o `sizeof`, o `alignof` e o deslocamento de CADA
//      campo de CADA estrutura, o tamanho de cada campo, e o valor de CADA constante.
//      Divergencia = erro de COMPILACAO, com o nome do campo na mensagem.
//
//  (b) VTABLE, em tempo de execucao. `static_assert` NAO VE ordem de metodo virtual, e
//      e exatamente ali que o erro e mais provavel e mais destrutivo. Um objeto de
//      teste implementa a interface DO SDK, cada metodo grava uma marca distinta, e
//      entao o mesmo objeto e chamado PELA NOSSA declaracao, metodo por metodo: se a
//      marca que voltar for de outro metodo, os dois vetores de funcoes nao coincidem
//      e o teste diz qual slot errou. De carona, cada chamada confere os ARGUMENTOS e
//      o valor de RETORNO - o que pega troca de ordem de parametro, que nem o layout
//      nem a ordem da vtable revelam.
//
//  (c) De bonus, uma travessia de DADOS: estruturas montadas com os nossos tipos sao
//      lidas do outro lado com os tipos do SDK, e vice-versa. E o mesmo que (a) prova
//      por deslocamento, agora verificado pelo valor que chega - a rede que pega um
//      `offsetof` que eu tenha escrito no par de campos errado.
//
// AS DUAS METADES CONTAM: as verificacoes de layout tambem sao executadas em tempo de
// execucao, com o MESMO par de expressoes do `static_assert`, so para que o programa
// possa dizer QUANTAS sao. Uma lista, duas propriedades.

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Ordem de inclusao, e ela e deliberada
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// COM primeiro, em escopo global: IUnknown e o macro `interface` sao do Windows SDK e
// os DOIS lados dependem deles. O SDK da Steinberg nao os traz.
#include <windows.h>
#include <ole2.h>

#include <stdio.h>
#include <string.h>
#include <stddef.h>     // offsetof
#include <type_traits>  // is_same, has_virtual_destructor

// O SDK DA STEINBERG, isolado num namespace. O SDK vem PRIMEIRO de proposito: assim
// ele e pre-processado exatamente como na compilacao de producao, sem nada nosso
// tendo mexido antes em macro nenhuma.
namespace sdk {
#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"
}

// O guarda da declaracao adiantada de IASIO e um nome do PRE-PROCESSADOR e nao
// pertence a namespace nenhum: sem este #undef, o nosso cabecalho pularia a propria
// declaracao adiantada e a prova estaria comparando algo que a producao nao compila.
#undef __ASIODRIVER_FWD_DEFINED__

// A NOSSA declaracao, em escopo global - os mesmos nomes que o SDK usa. E de
// proposito: assim a troca na Etapa C2 e trocar um #include, sem tocar em nenhuma
// linha de bcdasio.{h,cpp}.
#include "asioapi.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Contabilidade
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int  g_layoutChecks = 0;    // (a) layout e constantes
static int  g_vtableChecks = 0;    // (b) slots, argumentos e retornos
static int  g_dataChecks   = 0;    // (c) travessia de dados
static int  g_fail         = 0;
static bool g_verbose      = false;

static void note(int* bucket, const char* label, long long ours, long long theirs)
{
    (*bucket)++;
    if (ours != theirs) {
        g_fail++;
        printf("  FALHA   %-58s nosso=%lld  sdk=%lld\n", label, ours, theirs);
    } else if (g_verbose) {
        printf("  ok      %-58s = %lld\n", label, ours);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// (a) LAYOUT
//
// Cada macro emite DUAS coisas com o mesmo par de expressoes: o `static_assert`, que e
// o veredito de verdade e falha na compilacao com o nome do campo na mensagem; e a
// chamada de contagem, que so existe para o programa poder dizer quantas verificacoes
// fez. Se as duas discordassem, discordariam por eu ter escrito expressoes
// diferentes - e escrevo uma vez.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define CHECK_EQ(label, ourExpr, sdkExpr)                                              \
    do {                                                                               \
        static_assert((long long)(ourExpr) == (long long)(sdkExpr),                     \
                      "ABI DIVERGENTE: " label);                                       \
        note(&g_layoutChecks, label, (long long)(ourExpr), (long long)(sdkExpr));       \
    } while (0)

// Tipo escalar identico (typedef de tipo embutido): a verificacao mais forte que
// existe, porque compara o TIPO e nao apenas a largura.
#define CHECK_SAME_TYPE(T)                                                             \
    CHECK_EQ("mesmo tipo: " #T, (std::is_same< ::T, sdk::T >::value ? 1 : 0), 1)

#define CHECK_SIZE(T)   CHECK_EQ("sizeof("  #T ")",  sizeof(::T),   sizeof(sdk::T))
#define CHECK_ALIGN(T)  CHECK_EQ("alignof(" #T ")",  alignof(::T),  alignof(sdk::T))

// Estrutura: tamanho e alinhamento juntos. O ALINHAMENTO importa tanto quanto o
// tamanho e e o que pega empacotamento errado - ha estruturas aqui cujo `sizeof` e o
// mesmo com e sem `#pragma pack(4)` e cujo `alignof` nao e.
#define CHECK_STRUCT(T) do { CHECK_SIZE(T); CHECK_ALIGN(T); } while (0)

// Campo: deslocamento E tamanho. O deslocamento sozinho nao pega um vetor declarado
// com o tamanho errado no ULTIMO campo de uma estrutura de tamanho fixo.
#define CHECK_FIELD(T, F)                                                              \
    do {                                                                               \
        CHECK_EQ("offsetof(" #T "::" #F ")",                                           \
                 offsetof(::T, F), offsetof(sdk::T, F));                               \
        CHECK_EQ("sizeof(" #T "::" #F ")",                                             \
                 sizeof(::T::F), sizeof(sdk::T::F));                                   \
    } while (0)

// Campo de tipo embutido: acrescenta a igualdade de TIPO ao par acima. Nao da para
// usar em campo cujo tipo e uma estrutura do proprio ASIO (`::ASIOSamples` e
// `sdk::ASIOSamples` sao tipos diferentes por construcao) nem em ponteiro para funcao
// cujos parametros sao tipos do ASIO.
#define CHECK_FIELD_T(T, F)                                                            \
    do {                                                                               \
        CHECK_FIELD(T, F);                                                             \
        CHECK_EQ("mesmo tipo: " #T "::" #F,                                            \
                 (std::is_same< decltype(::T::F), decltype(sdk::T::F) >::value ? 1 : 0), 1); \
    } while (0)

#define CHECK_CONST(C)  CHECK_EQ("constante " #C, (long long)(::C), (long long)(sdk::C))

static void checkLayout()
{
    printf("\n-- (a) layout e constantes, conferidos tambem em tempo de COMPILACAO --\n");

    // ---- tipos escalares ----
    CHECK_SAME_TYPE(ASIOBool);
    CHECK_SAME_TYPE(ASIOSampleType);
    CHECK_SAME_TYPE(ASIOError);
    CHECK_SAME_TYPE(ASIOSampleRate);   // double em Windows (IEEE754_64FLOAT)
    CHECK_SAME_TYPE(ASIOIoFormatType);

    // ---- inteiros de 64 bits partidos em dois de 32 ----
    // A ORDEM `hi` antes de `lo` e o erro mais silencioso possivel nesta fronteira.
    CHECK_STRUCT(ASIOSamples);
    CHECK_FIELD_T(ASIOSamples, hi);
    CHECK_FIELD_T(ASIOSamples, lo);

    CHECK_STRUCT(ASIOTimeStamp);
    CHECK_FIELD_T(ASIOTimeStamp, hi);
    CHECK_FIELD_T(ASIOTimeStamp, lo);

    // ---- informacao de tempo ----
    CHECK_STRUCT(ASIOTimeCode);
    CHECK_FIELD_T(ASIOTimeCode, speed);
    CHECK_FIELD  (ASIOTimeCode, timeCodeSamples);
    CHECK_FIELD_T(ASIOTimeCode, flags);
    CHECK_FIELD_T(ASIOTimeCode, future);

    CHECK_STRUCT(AsioTimeInfo);
    CHECK_FIELD_T(AsioTimeInfo, speed);
    CHECK_FIELD  (AsioTimeInfo, systemTime);
    CHECK_FIELD  (AsioTimeInfo, samplePosition);
    CHECK_FIELD_T(AsioTimeInfo, sampleRate);
    CHECK_FIELD_T(AsioTimeInfo, flags);
    CHECK_FIELD_T(AsioTimeInfo, reserved);

    CHECK_STRUCT(ASIOTime);
    CHECK_FIELD_T(ASIOTime, reserved);
    CHECK_FIELD  (ASIOTime, timeInfo);
    CHECK_FIELD  (ASIOTime, timeCode);

    CHECK_SIZE(ASIOTimeCodeFlags);
    CHECK_SIZE(AsioTimeInfoFlags);

    // ---- callbacks do host ----
    CHECK_STRUCT(ASIOCallbacks);
    CHECK_FIELD(ASIOCallbacks, bufferSwitch);
    CHECK_FIELD(ASIOCallbacks, sampleRateDidChange);
    CHECK_FIELD(ASIOCallbacks, asioMessage);
    CHECK_FIELD(ASIOCallbacks, bufferSwitchTimeInfo);

    // ---- abertura e consulta ----
    CHECK_STRUCT(ASIODriverInfo);
    CHECK_FIELD_T(ASIODriverInfo, asioVersion);
    CHECK_FIELD_T(ASIODriverInfo, driverVersion);
    CHECK_FIELD_T(ASIODriverInfo, name);
    CHECK_FIELD_T(ASIODriverInfo, errorMessage);
    CHECK_FIELD_T(ASIODriverInfo, sysRef);

    CHECK_STRUCT(ASIOClockSource);
    CHECK_FIELD_T(ASIOClockSource, index);
    CHECK_FIELD_T(ASIOClockSource, associatedChannel);
    CHECK_FIELD_T(ASIOClockSource, associatedGroup);
    CHECK_FIELD_T(ASIOClockSource, isCurrentSource);
    CHECK_FIELD_T(ASIOClockSource, name);

    CHECK_STRUCT(ASIOChannelInfo);
    CHECK_FIELD_T(ASIOChannelInfo, channel);
    CHECK_FIELD_T(ASIOChannelInfo, isInput);
    CHECK_FIELD_T(ASIOChannelInfo, isActive);
    CHECK_FIELD_T(ASIOChannelInfo, channelGroup);
    CHECK_FIELD_T(ASIOChannelInfo, type);
    CHECK_FIELD_T(ASIOChannelInfo, name);

    CHECK_STRUCT(ASIOBufferInfo);
    CHECK_FIELD_T(ASIOBufferInfo, isInput);
    CHECK_FIELD_T(ASIOBufferInfo, channelNum);
    CHECK_FIELD_T(ASIOBufferInfo, buffers);

    // ---- parametros dos seletores do future() ----
    // Este driver nao implementa nenhum destes seletores hoje. As estruturas estao
    // aqui porque esta e a UNICA janela em que da para prova-las: depois que o SDK
    // sair do disco, uma declaracao nova nasce sem prova.
    CHECK_STRUCT(ASIOInputMonitor);
    CHECK_FIELD_T(ASIOInputMonitor, input);
    CHECK_FIELD_T(ASIOInputMonitor, output);
    CHECK_FIELD_T(ASIOInputMonitor, gain);
    CHECK_FIELD_T(ASIOInputMonitor, state);
    CHECK_FIELD_T(ASIOInputMonitor, pan);

    CHECK_STRUCT(ASIOChannelControls);
    CHECK_FIELD_T(ASIOChannelControls, channel);
    CHECK_FIELD_T(ASIOChannelControls, isInput);
    CHECK_FIELD_T(ASIOChannelControls, gain);
    CHECK_FIELD_T(ASIOChannelControls, meter);
    CHECK_FIELD_T(ASIOChannelControls, future);

    CHECK_STRUCT(ASIOTransportParameters);
    CHECK_FIELD_T(ASIOTransportParameters, command);
    CHECK_FIELD  (ASIOTransportParameters, samplePosition);
    CHECK_FIELD_T(ASIOTransportParameters, track);
    CHECK_FIELD_T(ASIOTransportParameters, trackSwitches);
    CHECK_FIELD_T(ASIOTransportParameters, future);

    CHECK_STRUCT(ASIOIoFormat);
    CHECK_FIELD_T(ASIOIoFormat, FormatType);
    CHECK_FIELD_T(ASIOIoFormat, future);

    CHECK_STRUCT(ASIOInternalBufferInfo);
    CHECK_FIELD_T(ASIOInternalBufferInfo, inputSamples);
    CHECK_FIELD_T(ASIOInternalBufferInfo, outputSamples);

    // ---- a interface, no que da para ver em tempo de compilacao ----
    CHECK_STRUCT(IASIO);
    CHECK_EQ("IASIO e polimorfica",
             (std::is_polymorphic< ::IASIO >::value ? 1 : 0),
             (std::is_polymorphic< sdk::IASIO >::value ? 1 : 0));
    CHECK_EQ("IASIO e abstrata",
             (std::is_abstract< ::IASIO >::value ? 1 : 0),
             (std::is_abstract< sdk::IASIO >::value ? 1 : 0));
    // SEM destrutor virtual, nos dois lados. Um destrutor virtual ocuparia o slot
    // imediatamente depois dos tres do IUnknown e deslocaria os vinte e um metodos:
    // o host chamaria o destrutor achando que chama init().
    CHECK_EQ("IASIO sem destrutor virtual",
             (std::has_virtual_destructor< ::IASIO >::value ? 1 : 0),
             (std::has_virtual_destructor< sdk::IASIO >::value ? 1 : 0));
    CHECK_EQ("IASIO sem destrutor virtual (valor absoluto: falso)",
             (std::has_virtual_destructor< ::IASIO >::value ? 1 : 0), 0);
    // A base do IASIO e IUnknown, e e a MESMA IUnknown do Windows SDK nos dois lados.
    // Isso e o que faz os tres primeiros slots coincidirem por construcao.
    CHECK_EQ("IASIO deriva de IUnknown (nosso)",
             (std::is_base_of< ::IUnknown, ::IASIO >::value ? 1 : 0), 1);
    CHECK_EQ("IASIO deriva de IUnknown (SDK)",
             (std::is_base_of< ::IUnknown, sdk::IASIO >::value ? 1 : 0), 1);

    // ---- booleanos ----
    CHECK_CONST(ASIOFalse);
    CHECK_CONST(ASIOTrue);

    // ---- formatos de amostra ----
    CHECK_CONST(ASIOSTInt16MSB);
    CHECK_CONST(ASIOSTInt24MSB);
    CHECK_CONST(ASIOSTInt32MSB);
    CHECK_CONST(ASIOSTFloat32MSB);
    CHECK_CONST(ASIOSTFloat64MSB);
    CHECK_CONST(ASIOSTInt32MSB16);
    CHECK_CONST(ASIOSTInt32MSB18);
    CHECK_CONST(ASIOSTInt32MSB20);
    CHECK_CONST(ASIOSTInt32MSB24);
    CHECK_CONST(ASIOSTInt16LSB);
    CHECK_CONST(ASIOSTInt24LSB);
    CHECK_CONST(ASIOSTInt32LSB);
    CHECK_CONST(ASIOSTFloat32LSB);
    CHECK_CONST(ASIOSTFloat64LSB);
    CHECK_CONST(ASIOSTInt32LSB16);
    CHECK_CONST(ASIOSTInt32LSB18);
    CHECK_CONST(ASIOSTInt32LSB20);
    CHECK_CONST(ASIOSTInt32LSB24);
    CHECK_CONST(ASIOSTDSDInt8LSB1);
    CHECK_CONST(ASIOSTDSDInt8MSB1);
    CHECK_CONST(ASIOSTDSDInt8NER8);
    CHECK_CONST(ASIOSTLastEntry);

    // ---- codigos de erro ----
    CHECK_CONST(ASE_OK);
    CHECK_CONST(ASE_SUCCESS);
    CHECK_CONST(ASE_NotPresent);
    CHECK_CONST(ASE_HWMalfunction);
    CHECK_CONST(ASE_InvalidParameter);
    CHECK_CONST(ASE_InvalidMode);
    CHECK_CONST(ASE_SPNotAdvancing);
    CHECK_CONST(ASE_NoClock);
    CHECK_CONST(ASE_NoMemory);

    // ---- sinalizadores de time code ----
    CHECK_CONST(kTcValid);
    CHECK_CONST(kTcRunning);
    CHECK_CONST(kTcReverse);
    CHECK_CONST(kTcOnspeed);
    CHECK_CONST(kTcStill);
    CHECK_CONST(kTcSpeedValid);

    // ---- sinalizadores de informacao de tempo ----
    CHECK_CONST(kSystemTimeValid);
    CHECK_CONST(kSamplePositionValid);
    CHECK_CONST(kSampleRateValid);
    CHECK_CONST(kSpeedValid);
    CHECK_CONST(kSampleRateChanged);
    CHECK_CONST(kClockSourceChanged);

    // ---- seletores do asioMessage (driver -> host) ----
    CHECK_CONST(kAsioSelectorSupported);
    CHECK_CONST(kAsioEngineVersion);
    CHECK_CONST(kAsioResetRequest);
    CHECK_CONST(kAsioBufferSizeChange);
    CHECK_CONST(kAsioResyncRequest);
    CHECK_CONST(kAsioLatenciesChanged);
    CHECK_CONST(kAsioSupportsTimeInfo);
    CHECK_CONST(kAsioSupportsTimeCode);
    CHECK_CONST(kAsioMMCCommand);
    CHECK_CONST(kAsioSupportsInputMonitor);
    CHECK_CONST(kAsioSupportsInputGain);
    CHECK_CONST(kAsioSupportsInputMeter);
    CHECK_CONST(kAsioSupportsOutputGain);
    CHECK_CONST(kAsioSupportsOutputMeter);
    CHECK_CONST(kAsioOverload);
    CHECK_CONST(kAsioNumMessageSelectors);

    // ---- seletores do future (host -> driver) ----
    CHECK_CONST(kAsioEnableTimeCodeRead);
    CHECK_CONST(kAsioDisableTimeCodeRead);
    CHECK_CONST(kAsioSetInputMonitor);
    CHECK_CONST(kAsioTransport);
    CHECK_CONST(kAsioSetInputGain);
    CHECK_CONST(kAsioGetInputMeter);
    CHECK_CONST(kAsioSetOutputGain);
    CHECK_CONST(kAsioGetOutputMeter);
    CHECK_CONST(kAsioCanInputMonitor);
    CHECK_CONST(kAsioCanTimeInfo);
    CHECK_CONST(kAsioCanTimeCode);
    CHECK_CONST(kAsioCanTransport);
    CHECK_CONST(kAsioCanInputGain);
    CHECK_CONST(kAsioCanInputMeter);
    CHECK_CONST(kAsioCanOutputGain);
    CHECK_CONST(kAsioCanOutputMeter);
    CHECK_CONST(kAsioOptionalOne);
    CHECK_CONST(kAsioSetIoFormat);
    CHECK_CONST(kAsioGetIoFormat);
    CHECK_CONST(kAsioCanDoIoFormat);
    CHECK_CONST(kAsioCanReportOverload);
    CHECK_CONST(kAsioGetInternalBufferSamples);

    // ---- comandos de transporte ----
    CHECK_CONST(kTransStart);
    CHECK_CONST(kTransStop);
    CHECK_CONST(kTransLocate);
    CHECK_CONST(kTransPunchIn);
    CHECK_CONST(kTransPunchOut);
    CHECK_CONST(kTransArmOn);
    CHECK_CONST(kTransArmOff);
    CHECK_CONST(kTransMonitorOn);
    CHECK_CONST(kTransMonitorOff);
    CHECK_CONST(kTransArm);
    CHECK_CONST(kTransMonitor);

    // ---- formatos de entrada/saida ----
    CHECK_CONST(kASIOFormatInvalid);
    CHECK_CONST(kASIOPCMFormat);
    CHECK_CONST(kASIODSDFormat);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// (b) VTABLE, e (c) travessia de dados
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

typedef unsigned long long u64;

// Uma marca por slot. Os tres primeiros sao do IUnknown, herdados do Windows SDK nos
// dois lados; entram na prova de qualquer forma, porque uma prova de vtable que
// comeca no slot 3 nao prova onde o slot 3 esta.
enum Mark {
    kNone = 0,
    kQueryInterface, kAddRef, kRelease,
    kInit, kGetDriverName, kGetDriverVersion, kGetErrorMessage,
    kStart, kStop,
    kGetChannels, kGetLatencies, kGetBufferSize,
    kCanSampleRate, kGetSampleRate, kSetSampleRate,
    kGetClockSources, kSetClockSource,
    kGetSamplePosition, kGetChannelInfo,
    kCreateBuffers, kDisposeBuffers,
    kControlPanel, kFuture, kOutputReady,
    kMarkCount
};

static const char* const kMarkNames[kMarkCount] = {
    "(nenhum)",
    "QueryInterface", "AddRef", "Release",
    "init", "getDriverName", "getDriverVersion", "getErrorMessage",
    "start", "stop",
    "getChannels", "getLatencies", "getBufferSize",
    "canSampleRate", "getSampleRate", "setSampleRate",
    "getClockSources", "setClockSource",
    "getSamplePosition", "getChannelInfo",
    "createBuffers", "disposeBuffers",
    "controlPanel", "future", "outputReady"
};

static const char* markName(int m)
{
    return (m >= 0 && m < kMarkCount) ? kMarkNames[m] : "(marca invalida)";
}

// O que o ultimo metodo chamado registrou.
struct Record {
    int    mark;
    u64    a[4];    // argumentos, normalizados para 64 bits, na ordem em que chegaram
    double d;       // o argumento de ponto flutuante, quando houver
};
static Record g_rec;

static void hit(int mark, u64 a0 = 0, u64 a1 = 0, u64 a2 = 0, u64 a3 = 0, double d = 0.0)
{
    g_rec.mark = mark;
    g_rec.a[0] = a0; g_rec.a[1] = a1; g_rec.a[2] = a2; g_rec.a[3] = a3;
    g_rec.d    = d;
}

// Valores de retorno, um distinto por metodo: assim a prova cobre tambem o caminho de
// VOLTA. Nao ha nada de especial nos numeros alem de serem diferentes entre si.
enum {
    kRetInit             = 0x0201, kRetDriverVersion = 0x0203,
    kRetStart            = 0x0205, kRetStop          = 0x0206,
    kRetGetChannels      = 0x0207, kRetGetLatencies  = 0x0208,
    kRetGetBufferSize    = 0x0209, kRetCanSampleRate = 0x020a,
    kRetGetSampleRate    = 0x020b, kRetSetSampleRate = 0x020c,
    kRetGetClockSources  = 0x020d, kRetSetClockSource = 0x020e,
    kRetGetSamplePos     = 0x020f, kRetGetChannelInfo = 0x0210,
    kRetCreateBuffers    = 0x0211, kRetDisposeBuffers = 0x0212,
    kRetControlPanel     = 0x0213, kRetFuture         = 0x0214,
    kRetOutputReady      = 0x0215,
    kRetAddRef           = 0x0216, kRetRelease        = 0x0217,
    kRetQueryInterface   = 0x0218
};

// Seletor privado deste teste, para pedir ao objeto de teste que leia um ASIOTime
// NOSSO com os tipos DO SDK. Nao colide com nenhum seletor do ASIO de proposito.
enum { kSelReadOurTime = 0x7ab1e001 };

// Copias feitas PELO LADO DO SDK do que o nosso lado montou. Sao os dados da metade
// (c): se o layout divergisse, estes campos chegariam trocados.
static sdk::ASIOTime       g_sdkSawTime;
static sdk::ASIOBufferInfo g_sdkSawBufferInfo[2];
static sdk::ASIOCallbacks  g_sdkSawCallbacks;
static bool                g_sdkSawTimeValid = false;
static bool                g_sdkSawBuffersValid = false;

// - - - - - - - - - - - - - - - - - - - - - - - - -
// O objeto de teste: implementa a interface DO SDK.
//
// `override` em cada metodo nao e enfeite: e o que faz o COMPILADOR garantir que cada
// um destes corresponde a um metodo que existe em sdk::IASIO com a mesma assinatura.
// E, como a classe e concreta e instanciada abaixo, o compilador tambem garante que
// nao sobrou nenhum metodo virtual puro - ou seja, que sdk::IASIO tem EXATAMENTE
// estes. A mesma dupla de garantias e aplicada a NOSSA interface por OurProbe.
// - - - - - - - - - - - - - - - - - - - - - - - - -
struct SdkProbe : public sdk::IASIO
{
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    { hit(kQueryInterface, (u64)(const void*)&riid, (u64)ppv); return (HRESULT)kRetQueryInterface; }

    ULONG STDMETHODCALLTYPE AddRef() override  { hit(kAddRef);  return (ULONG)kRetAddRef; }
    ULONG STDMETHODCALLTYPE Release() override { hit(kRelease); return (ULONG)kRetRelease; }

    sdk::ASIOBool init(void* sysHandle) override
    { hit(kInit, (u64)sysHandle); return kRetInit; }

    void getDriverName(char* name) override
    { hit(kGetDriverName, (u64)name); }

    long getDriverVersion() override
    { hit(kGetDriverVersion); return kRetDriverVersion; }

    void getErrorMessage(char* string) override
    { hit(kGetErrorMessage, (u64)string); }

    sdk::ASIOError start() override { hit(kStart); return kRetStart; }
    sdk::ASIOError stop()  override { hit(kStop);  return kRetStop;  }

    sdk::ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override
    { hit(kGetChannels, (u64)numInputChannels, (u64)numOutputChannels); return kRetGetChannels; }

    sdk::ASIOError getLatencies(long* inputLatency, long* outputLatency) override
    { hit(kGetLatencies, (u64)inputLatency, (u64)outputLatency); return kRetGetLatencies; }

    sdk::ASIOError getBufferSize(long* minSize, long* maxSize,
                                 long* preferredSize, long* granularity) override
    {
        hit(kGetBufferSize, (u64)minSize, (u64)maxSize, (u64)preferredSize, (u64)granularity);
        return kRetGetBufferSize;
    }

    sdk::ASIOError canSampleRate(sdk::ASIOSampleRate sampleRate) override
    { hit(kCanSampleRate, 0, 0, 0, 0, sampleRate); return kRetCanSampleRate; }

    sdk::ASIOError getSampleRate(sdk::ASIOSampleRate* sampleRate) override
    { hit(kGetSampleRate, (u64)sampleRate); return kRetGetSampleRate; }

    sdk::ASIOError setSampleRate(sdk::ASIOSampleRate sampleRate) override
    { hit(kSetSampleRate, 0, 0, 0, 0, sampleRate); return kRetSetSampleRate; }

    sdk::ASIOError getClockSources(sdk::ASIOClockSource* clocks, long* numSources) override
    {
        hit(kGetClockSources, (u64)clocks, (u64)numSources);
        // (c) ESCRITA do lado do SDK, leitura do nosso: preenche a estrutura com os
        // tipos do SDK para o chamador conferir campo por campo com os nossos.
        if (clocks && numSources) {
            clocks->index             = 0x11111111;
            clocks->associatedChannel = 0x22222222;
            clocks->associatedGroup   = 0x33333333;
            clocks->isCurrentSource   = sdk::ASIOTrue;
            strcpy(clocks->name, "fonte de relogio de teste");
            *numSources = 0x44444444;
        }
        return kRetGetClockSources;
    }

    sdk::ASIOError setClockSource(long reference) override
    { hit(kSetClockSource, (u64)(long long)reference); return kRetSetClockSource; }

    sdk::ASIOError getSamplePosition(sdk::ASIOSamples* sPos,
                                     sdk::ASIOTimeStamp* tStamp) override
    {
        hit(kGetSamplePosition, (u64)sPos, (u64)tStamp);
        // (c) `hi` e `lo` escritos com valores DIFERENTES e assimetricos: trocados de
        // ordem em qualquer um dos lados, o chamador ve os dois campos invertidos.
        if (sPos)   { sPos->hi   = 0xAAAA0001u; sPos->lo   = 0x0000BBBBu; }
        if (tStamp) { tStamp->hi = 0xCCCC0002u; tStamp->lo = 0x0000DDDDu; }
        return kRetGetSamplePos;
    }

    sdk::ASIOError getChannelInfo(sdk::ASIOChannelInfo* info) override
    {
        hit(kGetChannelInfo, (u64)info);
        // (c) LEITURA do lado do SDK de uma estrutura que o nosso lado preencheu, e
        // resposta em campos diferentes dos lidos.
        if (info) {
            const long ch = info->channel;
            const long in = info->isInput;
            info->isActive     = (ch == 0x0BADC0DE && in == sdk::ASIOTrue) ? sdk::ASIOTrue
                                                                          : sdk::ASIOFalse;
            info->channelGroup = 0x55555555;
            info->type         = sdk::ASIOSTInt16LSB;
            strcpy(info->name, "canal de teste");
        }
        return kRetGetChannelInfo;
    }

    sdk::ASIOError createBuffers(sdk::ASIOBufferInfo* bufferInfos, long numChannels,
                                 long bufferSize, sdk::ASIOCallbacks* callbacks) override
    {
        hit(kCreateBuffers, (u64)bufferInfos, (u64)(long long)numChannels,
            (u64)(long long)bufferSize, (u64)callbacks);
        // (c) LEITURA, com os tipos do SDK, do vetor e da estrutura de callbacks que o
        // nosso lado montou com os NOSSOS tipos.
        if (bufferInfos && numChannels == 2 && callbacks) {
            g_sdkSawBufferInfo[0] = bufferInfos[0];
            g_sdkSawBufferInfo[1] = bufferInfos[1];
            g_sdkSawCallbacks     = *callbacks;
            g_sdkSawBuffersValid  = true;
        }
        return kRetCreateBuffers;
    }

    sdk::ASIOError disposeBuffers() override { hit(kDisposeBuffers); return kRetDisposeBuffers; }
    sdk::ASIOError controlPanel()   override { hit(kControlPanel);   return kRetControlPanel;   }

    sdk::ASIOError future(long selector, void* opt) override
    {
        hit(kFuture, (u64)(long long)selector, (u64)opt);
        // (c) A estrutura mais profunda da interface - ASIOTime, que contem
        // AsioTimeInfo, ASIOTimeCode, ASIOSamples, ASIOTimeStamp e ASIOSampleRate - e
        // a que este driver entrega ao host a cada bloco. Montada com os NOSSOS tipos
        // e lida aqui com os do SDK.
        if (selector == (long)kSelReadOurTime && opt) {
            g_sdkSawTime      = *(const sdk::ASIOTime*)opt;
            g_sdkSawTimeValid = true;
        }
        return kRetFuture;
    }

    sdk::ASIOError outputReady() override { hit(kOutputReady); return kRetOutputReady; }
};

// - - - - - - - - - - - - - - - - - - - - - - - - -
// O espelho: implementa a NOSSA interface, com `override` em tudo, e e instanciado.
// Sozinho ele nao prova ordem nenhuma; prova que a nossa interface tem EXATAMENTE
// estes vinte e quatro metodos virtuais puros, com estas assinaturas, e nenhum outro.
// Junto com SdkProbe (que prova o mesmo do lado do SDK) e com o teste de ordem, fecha:
// mesmo conjunto, mesmo tamanho, mesma ordem.
// - - - - - - - - - - - - - - - - - - - - - - - - -
struct OurProbe : public ::IASIO
{
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return 1; }
    ULONG   STDMETHODCALLTYPE Release() override { return 0; }

    ::ASIOBool  init(void*) override                       { return ASIOTrue; }
    void        getDriverName(char*) override              { }
    long        getDriverVersion() override                { return 0; }
    void        getErrorMessage(char*) override            { }
    ::ASIOError start() override                           { return ASE_OK; }
    ::ASIOError stop() override                            { return ASE_OK; }
    ::ASIOError getChannels(long*, long*) override         { return ASE_OK; }
    ::ASIOError getLatencies(long*, long*) override        { return ASE_OK; }
    ::ASIOError getBufferSize(long*, long*, long*, long*) override { return ASE_OK; }
    ::ASIOError canSampleRate(::ASIOSampleRate) override   { return ASE_OK; }
    ::ASIOError getSampleRate(::ASIOSampleRate*) override  { return ASE_OK; }
    ::ASIOError setSampleRate(::ASIOSampleRate) override   { return ASE_OK; }
    ::ASIOError getClockSources(::ASIOClockSource*, long*) override { return ASE_OK; }
    ::ASIOError setClockSource(long) override              { return ASE_OK; }
    ::ASIOError getSamplePosition(::ASIOSamples*, ::ASIOTimeStamp*) override { return ASE_OK; }
    ::ASIOError getChannelInfo(::ASIOChannelInfo*) override { return ASE_OK; }
    ::ASIOError createBuffers(::ASIOBufferInfo*, long, long, ::ASIOCallbacks*) override
                                                           { return ASE_OK; }
    ::ASIOError disposeBuffers() override                  { return ASE_OK; }
    ::ASIOError controlPanel() override                    { return ASE_OK; }
    ::ASIOError future(long, void*) override               { return ASE_NotPresent; }
    ::ASIOError outputReady() override                     { return ASE_NotPresent; }
};

// O ponteiro passa por `void* volatile` de PROPOSITO. Se o compilador soubesse o tipo
// dinamico do objeto no ponto da chamada, poderia resolver as chamadas virtuais em
// tempo de compilacao - e o teste passaria a medir o otimizador em vez da vtable.
// Com `volatile` ele e obrigado a reler o ponteiro e a despachar pela vtable, que e
// exatamente o que um software de DJ faz com o que recebe do CoCreateInstance.
static void* volatile g_probeAsVoid;

static void expectMark(int wantedMark)
{
    g_vtableChecks++;
    if (g_rec.mark != wantedMark) {
        g_fail++;
        printf("  FALHA   vtable: chamei '%s' pela NOSSA declaracao e executou '%s' "
               "-- as duas vtables NAO coincidem neste slot\n",
               markName(wantedMark), markName(g_rec.mark));
    } else if (g_verbose) {
        printf("  ok      vtable: %s\n", markName(wantedMark));
    }
}

static void expectU64(const char* label, u64 got, u64 want)
{
    g_vtableChecks++;
    if (got != want) {
        g_fail++;
        printf("  FALHA   %-58s recebido=0x%llx  esperado=0x%llx\n", label, got, want);
    } else if (g_verbose) {
        printf("  ok      %-58s = 0x%llx\n", label, got);
    }
}

static void expectDouble(const char* label, double got, double want)
{
    g_vtableChecks++;
    if (got != want) {
        g_fail++;
        printf("  FALHA   %-58s recebido=%.9g  esperado=%.9g\n", label, got, want);
    } else if (g_verbose) {
        printf("  ok      %-58s = %.9g\n", label, got);
    }
}

static void expectData(const char* label, long long got, long long want)
{
    g_dataChecks++;
    if (got != want) {
        g_fail++;
        printf("  FALHA   %-58s recebido=0x%llx  esperado=0x%llx\n", label, got, want);
    } else if (g_verbose) {
        printf("  ok      %-58s = 0x%llx\n", label, got);
    }
}

static void expectStr(const char* label, const char* got, const char* want)
{
    g_dataChecks++;
    if (strcmp(got, want) != 0) {
        g_fail++;
        printf("  FALHA   %-58s recebido='%s'  esperado='%s'\n", label, got, want);
    } else if (g_verbose) {
        printf("  ok      %-58s = '%s'\n", label, got);
    }
}

static void checkVtableAndData()
{
    printf("\n-- (b) ordem dos metodos virtuais, argumentos e retornos --\n");

    // OurProbe existe para o compilador conferir a NOSSA interface (ver o comentario
    // dela). Instanciar e a prova: uma classe com metodo virtual puro pendente nao
    // compila aqui.
    OurProbe ourProbe;
    (void)ourProbe.getDriverVersion();

    SdkProbe probe;
    // static_cast primeiro, para pegar o sub-objeto IASIO no deslocamento certo
    // (SdkProbe tem uma base so, mas o principio importa); reinterpret_cast depois,
    // para atravessar a fronteira entre as duas declaracoes. Este cast e exatamente o
    // que um host faz com o ponteiro que sai do CoCreateInstance, e e a razao de
    // existir deste teste.
    g_probeAsVoid = static_cast<sdk::IASIO*>(&probe);
    ::IASIO* iface = reinterpret_cast< ::IASIO* >(g_probeAsVoid);

    // ---- slots 0 a 2: IUnknown ----
    IID  iid = IID_IUnknown;
    void* out = 0;
    hit(kNone);
    expectU64("QueryInterface: retorno", (u64)(long long)iface->QueryInterface(iid, &out),
              (u64)kRetQueryInterface);
    expectMark(kQueryInterface);
    expectU64("QueryInterface: arg 1 (riid)", g_rec.a[0], (u64)(const void*)&iid);
    expectU64("QueryInterface: arg 2 (ppv)",  g_rec.a[1], (u64)&out);

    hit(kNone);
    expectU64("AddRef: retorno", (u64)iface->AddRef(), (u64)kRetAddRef);
    expectMark(kAddRef);

    hit(kNone);
    expectU64("Release: retorno", (u64)iface->Release(), (u64)kRetRelease);
    expectMark(kRelease);

    // ---- slot 3: init ----
    int sysRefSentinel = 0;
    hit(kNone);
    expectU64("init: retorno", (u64)(long long)iface->init(&sysRefSentinel), (u64)kRetInit);
    expectMark(kInit);
    expectU64("init: arg 1 (sysHandle)", g_rec.a[0], (u64)&sysRefSentinel);

    // ---- slot 4: getDriverName ----
    char nameBuf[64] = { 0 };
    hit(kNone);
    iface->getDriverName(nameBuf);
    expectMark(kGetDriverName);
    expectU64("getDriverName: arg 1 (name)", g_rec.a[0], (u64)nameBuf);

    // ---- slot 5: getDriverVersion ----
    hit(kNone);
    expectU64("getDriverVersion: retorno", (u64)(long long)iface->getDriverVersion(),
              (u64)kRetDriverVersion);
    expectMark(kGetDriverVersion);

    // ---- slot 6: getErrorMessage ----
    char errBuf[128] = { 0 };
    hit(kNone);
    iface->getErrorMessage(errBuf);
    expectMark(kGetErrorMessage);
    expectU64("getErrorMessage: arg 1 (string)", g_rec.a[0], (u64)errBuf);

    // ---- slots 7 e 8: start e stop ----
    // Estes dois sao vizinhos, tem a MESMA assinatura e nao levam argumento: sao os
    // que um `static_assert` jamais distinguiria, e trocados de lugar dariam um driver
    // que para quando o host manda tocar.
    hit(kNone);
    expectU64("start: retorno", (u64)(long long)iface->start(), (u64)kRetStart);
    expectMark(kStart);

    hit(kNone);
    expectU64("stop: retorno", (u64)(long long)iface->stop(), (u64)kRetStop);
    expectMark(kStop);

    // ---- slot 9: getChannels ----
    long inCh = 0, outCh = 0;
    hit(kNone);
    expectU64("getChannels: retorno", (u64)(long long)iface->getChannels(&inCh, &outCh),
              (u64)kRetGetChannels);
    expectMark(kGetChannels);
    expectU64("getChannels: arg 1 (numInputChannels)",  g_rec.a[0], (u64)&inCh);
    expectU64("getChannels: arg 2 (numOutputChannels)", g_rec.a[1], (u64)&outCh);

    // ---- slot 10: getLatencies ----
    long inLat = 0, outLat = 0;
    hit(kNone);
    expectU64("getLatencies: retorno", (u64)(long long)iface->getLatencies(&inLat, &outLat),
              (u64)kRetGetLatencies);
    expectMark(kGetLatencies);
    expectU64("getLatencies: arg 1 (inputLatency)",  g_rec.a[0], (u64)&inLat);
    expectU64("getLatencies: arg 2 (outputLatency)", g_rec.a[1], (u64)&outLat);

    // ---- slot 11: getBufferSize ----
    // Quatro ponteteiros do MESMO tipo: a assinatura em que uma troca de ordem de
    // parametro e invisivel para todo o resto desta prova. Os quatro sao conferidos
    // por identidade.
    long bsMin = 0, bsMax = 0, bsPref = 0, bsGran = 0;
    hit(kNone);
    expectU64("getBufferSize: retorno",
              (u64)(long long)iface->getBufferSize(&bsMin, &bsMax, &bsPref, &bsGran),
              (u64)kRetGetBufferSize);
    expectMark(kGetBufferSize);
    expectU64("getBufferSize: arg 1 (minSize)",       g_rec.a[0], (u64)&bsMin);
    expectU64("getBufferSize: arg 2 (maxSize)",       g_rec.a[1], (u64)&bsMax);
    expectU64("getBufferSize: arg 3 (preferredSize)", g_rec.a[2], (u64)&bsPref);
    expectU64("getBufferSize: arg 4 (granularity)",   g_rec.a[3], (u64)&bsGran);

    // ---- slots 12, 13 e 14: as tres da taxa de amostragem ----
    // canSampleRate e setSampleRate tem assinatura IDENTICA e sao separadas por
    // getSampleRate. O valor passado e diferente em cada uma, e nao redondo, para que
    // um `double` que atravesse por outro registrador apareca.
    hit(kNone);
    expectU64("canSampleRate: retorno",
              (u64)(long long)iface->canSampleRate(44100.5), (u64)kRetCanSampleRate);
    expectMark(kCanSampleRate);
    expectDouble("canSampleRate: arg 1 (sampleRate)", g_rec.d, 44100.5);

    ::ASIOSampleRate rateOut = 0.0;
    hit(kNone);
    expectU64("getSampleRate: retorno",
              (u64)(long long)iface->getSampleRate(&rateOut), (u64)kRetGetSampleRate);
    expectMark(kGetSampleRate);
    expectU64("getSampleRate: arg 1 (sampleRate)", g_rec.a[0], (u64)&rateOut);

    hit(kNone);
    expectU64("setSampleRate: retorno",
              (u64)(long long)iface->setSampleRate(96000.25), (u64)kRetSetSampleRate);
    expectMark(kSetSampleRate);
    expectDouble("setSampleRate: arg 1 (sampleRate)", g_rec.d, 96000.25);

    // ---- slots 15 e 16: relogio ----
    ::ASIOClockSource clock;
    memset(&clock, 0, sizeof(clock));
    long numSources = 0;
    hit(kNone);
    expectU64("getClockSources: retorno",
              (u64)(long long)iface->getClockSources(&clock, &numSources),
              (u64)kRetGetClockSources);
    expectMark(kGetClockSources);
    expectU64("getClockSources: arg 1 (clocks)",     g_rec.a[0], (u64)&clock);
    expectU64("getClockSources: arg 2 (numSources)", g_rec.a[1], (u64)&numSources);

    hit(kNone);
    expectU64("setClockSource: retorno",
              (u64)(long long)iface->setClockSource(0x1234abcd), (u64)kRetSetClockSource);
    expectMark(kSetClockSource);
    expectU64("setClockSource: arg 1 (reference)", g_rec.a[0], (u64)0x1234abcdull);

    // ---- slot 17: getSamplePosition ----
    ::ASIOSamples   sPos;
    ::ASIOTimeStamp tStamp;
    memset(&sPos, 0, sizeof(sPos));
    memset(&tStamp, 0, sizeof(tStamp));
    hit(kNone);
    expectU64("getSamplePosition: retorno",
              (u64)(long long)iface->getSamplePosition(&sPos, &tStamp),
              (u64)kRetGetSamplePos);
    expectMark(kGetSamplePosition);
    expectU64("getSamplePosition: arg 1 (sPos)",   g_rec.a[0], (u64)&sPos);
    expectU64("getSamplePosition: arg 2 (tStamp)", g_rec.a[1], (u64)&tStamp);

    // ---- slot 18: getChannelInfo ----
    ::ASIOChannelInfo chInfo;
    memset(&chInfo, 0, sizeof(chInfo));
    chInfo.channel = 0x0BADC0DE;
    chInfo.isInput = ASIOTrue;
    hit(kNone);
    expectU64("getChannelInfo: retorno",
              (u64)(long long)iface->getChannelInfo(&chInfo), (u64)kRetGetChannelInfo);
    expectMark(kGetChannelInfo);
    expectU64("getChannelInfo: arg 1 (info)", g_rec.a[0], (u64)&chInfo);

    // ---- slot 19: createBuffers ----
    ::ASIOBufferInfo bufInfos[2];
    memset(bufInfos, 0, sizeof(bufInfos));
    bufInfos[0].isInput    = ASIOTrue;
    bufInfos[0].channelNum = 0x0101;
    bufInfos[0].buffers[0] = (void*)0x1000;
    bufInfos[0].buffers[1] = (void*)0x2000;
    bufInfos[1].isInput    = ASIOFalse;
    bufInfos[1].channelNum = 0x0202;
    bufInfos[1].buffers[0] = (void*)0x3000;
    bufInfos[1].buffers[1] = (void*)0x4000;

    ::ASIOCallbacks cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.bufferSwitch         = (void (*)(long, ::ASIOBool))0x11000;
    cbs.sampleRateDidChange  = (void (*)(::ASIOSampleRate))0x22000;
    cbs.asioMessage          = (long (*)(long, long, void*, double*))0x33000;
    cbs.bufferSwitchTimeInfo = (::ASIOTime* (*)(::ASIOTime*, long, ::ASIOBool))0x44000;

    hit(kNone);
    expectU64("createBuffers: retorno",
              (u64)(long long)iface->createBuffers(bufInfos, 2, 512, &cbs),
              (u64)kRetCreateBuffers);
    expectMark(kCreateBuffers);
    expectU64("createBuffers: arg 1 (bufferInfos)", g_rec.a[0], (u64)bufInfos);
    expectU64("createBuffers: arg 2 (numChannels)", g_rec.a[1], 2);
    expectU64("createBuffers: arg 3 (bufferSize)",  g_rec.a[2], 512);
    expectU64("createBuffers: arg 4 (callbacks)",   g_rec.a[3], (u64)&cbs);

    // ---- slots 20 e 21: disposeBuffers e controlPanel ----
    hit(kNone);
    expectU64("disposeBuffers: retorno",
              (u64)(long long)iface->disposeBuffers(), (u64)kRetDisposeBuffers);
    expectMark(kDisposeBuffers);

    hit(kNone);
    expectU64("controlPanel: retorno",
              (u64)(long long)iface->controlPanel(), (u64)kRetControlPanel);
    expectMark(kControlPanel);

    // ---- slot 22: future, e a travessia do ASIOTime ----
    ::ASIOTime ourTime;
    memset(&ourTime, 0, sizeof(ourTime));
    ourTime.reserved[0] = 0x0A0A0001; ourTime.reserved[1] = 0x0A0A0002;
    ourTime.reserved[2] = 0x0A0A0003; ourTime.reserved[3] = 0x0A0A0004;
    ourTime.timeInfo.speed                = 1.5;
    ourTime.timeInfo.systemTime.hi        = 0x1111AAAAu;
    ourTime.timeInfo.systemTime.lo        = 0x2222BBBBu;
    ourTime.timeInfo.samplePosition.hi    = 0x3333CCCCu;
    ourTime.timeInfo.samplePosition.lo    = 0x4444DDDDu;
    ourTime.timeInfo.sampleRate           = 44100.0;
    ourTime.timeInfo.flags                = kSystemTimeValid | kSamplePositionValid |
                                            kSampleRateValid;
    memcpy(ourTime.timeInfo.reserved, "12345678901", 12);
    ourTime.timeCode.speed                = 2.25;
    ourTime.timeCode.timeCodeSamples.hi   = 0x5555EEEEu;
    ourTime.timeCode.timeCodeSamples.lo   = 0x6666FFFFu;
    ourTime.timeCode.flags                = kTcValid | kTcRunning;
    memcpy(ourTime.timeCode.future, "ASIOTime future", 16);

    hit(kNone);
    expectU64("future: retorno",
              (u64)(long long)iface->future((long)kSelReadOurTime, &ourTime),
              (u64)kRetFuture);
    expectMark(kFuture);
    expectU64("future: arg 1 (selector)", g_rec.a[0], (u64)(long)kSelReadOurTime);
    expectU64("future: arg 2 (opt)",      g_rec.a[1], (u64)&ourTime);

    // ---- slot 23: outputReady ----
    hit(kNone);
    expectU64("outputReady: retorno",
              (u64)(long long)iface->outputReady(), (u64)kRetOutputReady);
    expectMark(kOutputReady);

    // - - - - - - - - - - - - - - - - - - - - - - - - -
    // (c) travessia de dados: o que o OUTRO lado viu
    // - - - - - - - - - - - - - - - - - - - - - - - - -
    printf("\n-- (c) travessia de dados entre as duas declaracoes --\n");

    // ASIOTime montado pelos NOSSOS tipos, lido pelos do SDK.
    g_dataChecks++;
    if (!g_sdkSawTimeValid) {
        g_fail++;
        printf("  FALHA   o lado do SDK nao recebeu o ASIOTime\n");
    } else if (g_verbose) {
        printf("  ok      o lado do SDK recebeu o ASIOTime\n");
    }
    expectData("ASIOTime.reserved[0] visto pelo SDK", g_sdkSawTime.reserved[0], 0x0A0A0001);
    expectData("ASIOTime.reserved[3] visto pelo SDK", g_sdkSawTime.reserved[3], 0x0A0A0004);
    expectData("AsioTimeInfo.speed visto pelo SDK",
               (g_sdkSawTime.timeInfo.speed == 1.5) ? 1 : 0, 1);
    expectData("AsioTimeInfo.systemTime.hi visto pelo SDK",
               (long long)g_sdkSawTime.timeInfo.systemTime.hi, 0x1111AAAAll);
    expectData("AsioTimeInfo.systemTime.lo visto pelo SDK",
               (long long)g_sdkSawTime.timeInfo.systemTime.lo, 0x2222BBBBll);
    expectData("AsioTimeInfo.samplePosition.hi visto pelo SDK",
               (long long)g_sdkSawTime.timeInfo.samplePosition.hi, 0x3333CCCCll);
    expectData("AsioTimeInfo.samplePosition.lo visto pelo SDK",
               (long long)g_sdkSawTime.timeInfo.samplePosition.lo, 0x4444DDDDll);
    expectData("AsioTimeInfo.sampleRate visto pelo SDK",
               (g_sdkSawTime.timeInfo.sampleRate == 44100.0) ? 1 : 0, 1);
    expectData("AsioTimeInfo.flags visto pelo SDK",
               (long long)g_sdkSawTime.timeInfo.flags,
               (long long)(sdk::kSystemTimeValid | sdk::kSamplePositionValid |
                           sdk::kSampleRateValid));
    expectStr ("AsioTimeInfo.reserved visto pelo SDK",
               g_sdkSawTime.timeInfo.reserved, "12345678901");
    expectData("ASIOTimeCode.speed visto pelo SDK",
               (g_sdkSawTime.timeCode.speed == 2.25) ? 1 : 0, 1);
    expectData("ASIOTimeCode.timeCodeSamples.hi visto pelo SDK",
               (long long)g_sdkSawTime.timeCode.timeCodeSamples.hi, 0x5555EEEEll);
    expectData("ASIOTimeCode.timeCodeSamples.lo visto pelo SDK",
               (long long)g_sdkSawTime.timeCode.timeCodeSamples.lo, 0x6666FFFFll);
    expectData("ASIOTimeCode.flags visto pelo SDK",
               (long long)g_sdkSawTime.timeCode.flags,
               (long long)(sdk::kTcValid | sdk::kTcRunning));
    expectStr ("ASIOTimeCode.future visto pelo SDK",
               g_sdkSawTime.timeCode.future, "ASIOTime future");

    // ASIOBufferInfo e ASIOCallbacks montados pelos NOSSOS tipos, lidos pelos do SDK.
    g_dataChecks++;
    if (!g_sdkSawBuffersValid) {
        g_fail++;
        printf("  FALHA   o lado do SDK nao recebeu os ASIOBufferInfo/ASIOCallbacks\n");
    } else if (g_verbose) {
        printf("  ok      o lado do SDK recebeu os ASIOBufferInfo/ASIOCallbacks\n");
    }
    expectData("ASIOBufferInfo[0].isInput visto pelo SDK",
               (long long)g_sdkSawBufferInfo[0].isInput, (long long)sdk::ASIOTrue);
    expectData("ASIOBufferInfo[0].channelNum visto pelo SDK",
               (long long)g_sdkSawBufferInfo[0].channelNum, 0x0101);
    expectData("ASIOBufferInfo[0].buffers[0] visto pelo SDK",
               (long long)(u64)g_sdkSawBufferInfo[0].buffers[0], 0x1000);
    expectData("ASIOBufferInfo[0].buffers[1] visto pelo SDK",
               (long long)(u64)g_sdkSawBufferInfo[0].buffers[1], 0x2000);
    // O SEGUNDO elemento do vetor prova o PASSO entre elementos - ou seja, o
    // sizeof da estrutura - e nao apenas os deslocamentos internos.
    expectData("ASIOBufferInfo[1].isInput visto pelo SDK",
               (long long)g_sdkSawBufferInfo[1].isInput, (long long)sdk::ASIOFalse);
    expectData("ASIOBufferInfo[1].channelNum visto pelo SDK",
               (long long)g_sdkSawBufferInfo[1].channelNum, 0x0202);
    expectData("ASIOBufferInfo[1].buffers[0] visto pelo SDK",
               (long long)(u64)g_sdkSawBufferInfo[1].buffers[0], 0x3000);
    expectData("ASIOBufferInfo[1].buffers[1] visto pelo SDK",
               (long long)(u64)g_sdkSawBufferInfo[1].buffers[1], 0x4000);
    expectData("ASIOCallbacks.bufferSwitch visto pelo SDK",
               (long long)(u64)g_sdkSawCallbacks.bufferSwitch, 0x11000);
    expectData("ASIOCallbacks.sampleRateDidChange visto pelo SDK",
               (long long)(u64)g_sdkSawCallbacks.sampleRateDidChange, 0x22000);
    expectData("ASIOCallbacks.asioMessage visto pelo SDK",
               (long long)(u64)g_sdkSawCallbacks.asioMessage, 0x33000);
    expectData("ASIOCallbacks.bufferSwitchTimeInfo visto pelo SDK",
               (long long)(u64)g_sdkSawCallbacks.bufferSwitchTimeInfo, 0x44000);

    // Sentido inverso: escrito pelos tipos do SDK, lido pelos NOSSOS.
    expectData("ASIOClockSource.index escrito pelo SDK",
               (long long)clock.index, 0x11111111);
    expectData("ASIOClockSource.associatedChannel escrito pelo SDK",
               (long long)clock.associatedChannel, 0x22222222);
    expectData("ASIOClockSource.associatedGroup escrito pelo SDK",
               (long long)clock.associatedGroup, 0x33333333);
    expectData("ASIOClockSource.isCurrentSource escrito pelo SDK",
               (long long)clock.isCurrentSource, (long long)ASIOTrue);
    expectStr ("ASIOClockSource.name escrito pelo SDK",
               clock.name, "fonte de relogio de teste");
    expectData("numSources escrito pelo SDK", (long long)numSources, 0x44444444);

    expectData("ASIOSamples.hi escrito pelo SDK",   (long long)sPos.hi,   0xAAAA0001ll);
    expectData("ASIOSamples.lo escrito pelo SDK",   (long long)sPos.lo,   0x0000BBBBll);
    expectData("ASIOTimeStamp.hi escrito pelo SDK", (long long)tStamp.hi, 0xCCCC0002ll);
    expectData("ASIOTimeStamp.lo escrito pelo SDK", (long long)tStamp.lo, 0x0000DDDDll);

    // O lado do SDK LEU channel e isInput que escrevemos, e respondeu em isActive.
    expectData("ASIOChannelInfo: o SDK leu channel/isInput que escrevemos",
               (long long)chInfo.isActive, (long long)ASIOTrue);
    expectData("ASIOChannelInfo.channelGroup escrito pelo SDK",
               (long long)chInfo.channelGroup, 0x55555555);
    expectData("ASIOChannelInfo.type escrito pelo SDK",
               (long long)chInfo.type, (long long)ASIOSTInt16LSB);
    expectStr ("ASIOChannelInfo.name escrito pelo SDK", chInfo.name, "canal de teste");
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
            g_verbose = true;

    printf("== abicheck: asioapi.h (nosso) contra o ASIO SDK 2.3 da Steinberg ==\n");
    printf("   Ferramenta de verificacao LOCAL. Nao entra no BcdAsio.dll.\n");
    printf("   Rode com -v para listar cada verificacao.\n");

    checkLayout();
    checkVtableAndData();

    const int total = g_layoutChecks + g_vtableChecks + g_dataChecks;
    printf("\n== %d verificacoes (%d de layout/constantes, %d de vtable, "
           "%d de travessia de dados), %d falhas ==\n",
           total, g_layoutChecks, g_vtableChecks, g_dataChecks, g_fail);
    printf("== %d metodos de vtable cobertos (3 do IUnknown + 21 do IASIO) ==\n",
           kMarkCount - 1);

    if (g_fail) {
        printf("\nABICHECK_FAIL: a nossa declaracao NAO e binariamente identica a do "
               "SDK. NAO troque o driver para ela.\n");
        return 1;
    }
    printf("\nABICHECK_OK\n");
    return 0;
}

// Driver ASIO da Behringer BCD3000.
//
// Baseado no exemplo asiosample do ASIO SDK 2.3 da Steinberg (arquivos
// `driver/asiosample/asiosmpl.cpp` e `driver/asiosample/asiosmpl.h`). O miolo de audio
// foi substituido pelo motor WinUSB deste projeto.
//
// OS NOMES DOS ARQUIVOS SAO `asiosmpl`, e nao `asiosample`. O EXEMPLO e o DIRETORIO se
// chamam asiosample; os dois arquivos dentro dele nao. A versao anterior desta nota
// citava `asiosample.{h,cpp}`, que nao existem - conferido na listagem do SDK em disco,
// onde estao asiosmpl.cpp, asiosmpl.h, wintimer.cpp, mactimer.cpp, macnanosecs.cpp,
// makesamp.cpp, asiosample.def e asiosample.txt. Nao e detalhe cosmetico: depois que o
// SDK sair deste disco, a atribuicao so pode ser verificada por quem tiver os nomes
// certos, e atribuicao e a UNICA obrigacao da BSD de 3 clausulas que herdamos.
//
// A LICENCA DESTE ARQUIVO, e a de tudo o que entra no BcdAsio.dll.
//
// Este arquivo e o bcdasio.h sao DERIVADOS do exemplo asiosample, que e BSD de 3
// clausulas. A obrigacao dessa licenca e uma so - manter o aviso de direito autoral e o
// texto das condicoes -, e o texto esta em `native/bcdasio/LICENSE-asiosample.txt`. Ele
// vivia embutido em `native/bcdasio/wintimer.cpp`, o ultimo arquivo do exemplo ainda
// compilado no produto; esse arquivo foi aposentado na etapa C2 (relogio proprio em
// nanoclock.cpp) e o texto foi MOVIDO, nao apagado, porque estes dois continuam sendo
// derivados.
//
// DE ONDE VEIO O TEXTO REPRODUZIDO, medido e nao lembrado: o bloco de
// LICENSE-asiosample.txt e o cabecalho de `driver/asiosample/wintimer.cpp` do SDK, que
// e byte a byte o mesmo cabecalho que estava em `native/bcdasio/wintimer.cpp` antes do
// corte (conferido contra `git show 23892e9~1:native/bcdasio/wintimer.cpp`). Sao 36
// linhas; 24 delas batem byte a byte e as outras 12 diferem SO por um espaco no fim da
// linha, que a copia nao trouxe. Ou seja: o aviso de direito autoral, as tres condicoes
// e o disclaimer estao palavra por palavra, e nao ha diferenca de conteudo - mas
// "byte-identico" nao e verdade, e este comentario nao vai afirmar isso.
//
// E NADA MAIS DO SDK ENTRA NESTE DLL. Ate a etapa C2 entravam quatro arquivos de
// `ASIOSDK/common/` sob DUAS outras licencas - `register.cpp` e `debugmessage.cpp` sob a
// dupla da Steinberg (proprietaria, que exige acordo assinado antes de publicar, OU GPL
// v3), e `combase.{h,cpp}` mais `dllentry.cpp` sob um aviso proprio da Microsoft de
// 1992-1996 -, alem dos cabecalhos `asiosys.h` e `iasiodrv.h`. Foram todos substituidos
// por codigo deste projeto:
//
//   asiosys.h + iasiodrv.h  ->  asioapi.h   (provado identico por `build.bat abicheck`)
//   register.cpp            ->  asioreg.{h,cpp}   (provado identico por `build.bat regcheck`)
//   combase.{h,cpp}         ->  comserver.{h,cpp} + os tres metodos de IUnknown aqui
//   dllentry.cpp            ->  comserver.cpp + dllmain.cpp
//   debugmessage.cpp        ->  nada: compilava para unidade vazia
//   wintimer.cpp            ->  nanoclock.{h,cpp}
//
// A versao anterior desta nota citava tambem `asiodrvr.h` e `asiodrvr.cpp` como
// compilados aqui. NAO ERAM: conferido por busca em todos os fontes do projeto, nada
// neste repositorio jamais os incluiu. Ela apontava tambem para um LICENSE.txt do SDK
// que nao esta neste repositorio (native/ASIOSDK/ foi retirado do rastreamento do git
// porque nao podemos redistribui-lo - ver o .gitignore), ou seja, mandava o leitor a um
// arquivo inexistente para descobrir a licenca de arquivos que nao eram usados.
//
// A licenca que ESTE projeto adota e decisao do dono. O que a etapa C2 mudou e que agora
// ha decisao a tomar: nada no produto a amarra mais.

#include <stdio.h>
#include <string.h>
#include <new>       // std::nothrow
#include "bcdasio.h"
#include "asioreg.h"
#include "format.h"
#include "handoff.h"
#include "log.h"
#include "nanoclock.h"

#include "windows.h"

static const double twoRaisedTo32 = 4294967296.;
static const double twoRaisedTo32Reciprocal = 1. / twoRaisedTo32;

// CLSID proprio do driver BCD3000. NAO MUDAR: quebra o registro existente na maquina do
// dono, e `installer/check.cpp` compara o texto dele com este valor.
//
// O nome fala de IID e o tipo diz CLSID porque este GUID tem os DOIS papeis - ver o
// comentario de comserver.h e o do QueryInterface abaixo. `extern const` e para ele ter
// ligacao externa: `const` sozinho, em escopo de arquivo, seria interno em C++ e o
// comserver.cpp nao o veria.
extern const CLSID IID_ASIO_DRIVER = { 0xb0d3000a, 0x51e7, 0x4c2b,
                                       { 0x9f, 0x3a, 0x12, 0x34, 0xab, 0xcd, 0x56, 0x78 } };

// Como o driver aparece no registro do Windows. Estes tres textos SAO a especificacao do
// registro: foram lidos do registro que funciona na maquina do dono, e nao escolhidos.
// `kAsioRegName` e comparado literalmente por `installer/check.cpp`.
static const wchar_t* const kAsioRegName     = L"Behringer BCD3000";
static const wchar_t* const kAsioDescription = L"Behringer BCD3000 ASIO Driver";
static const wchar_t* const kThreadingModel  = L"Apartment";

//------------------------------------------------------------------------
// COM: identidade, contagem de referencias e criacao.
//------------------------------------------------------------------------

// *** O IID QUE OS HOSTS ASIO PEDEM E O PROPRIO CLSID DO DRIVER. ***
//
// Nao e um IID de interface e nao e IID_IUnknown: o software de DJ chama
// CoCreateInstance passando o MESMO GUID nos dois argumentos - a classe e a interface. E
// peculiaridade do ASIO, vem de como o SDK da Steinberg sempre fez, e todo host existente
// depende dela. Um QueryInterface que so aceitasse IID_IUnknown compilaria, ligaria,
// registraria e NENHUM host carregaria o driver - a falha apareceria como "driver nao
// disponivel" na lista do software de DJ, sem uma linha de log de lugar nenhum.
//
// O comportamento esta conferido no combase.cpp que saiu (a versao do SDK aceitava
// IID_ASIO_DRIVER primeiro e so depois caia no IID_IUnknown do CUnknown) e reproduzido
// aqui.
//
// O PONTEIRO DEVOLVIDO e `static_cast<IASIO*>(this)`, e nao `(void*)this`:
//   * `static_cast` deixa o COMPILADOR calcular o deslocamento do subobjeto. Hoje ele e
//     zero, porque IASIO e a primeira base - mas um `(void*)this` estaria certo por
//     coincidencia de layout, e a coincidencia mora em bcdasio.h, longe daqui;
//   * e o MESMO ponteiro nos dois casos aceitos, inclusive para IID_IUnknown. O COM exige
//     que QueryInterface(IID_IUnknown) devolva sempre o mesmo endereco para o mesmo
//     objeto - e assim que um cliente decide se dois ponteiros sao o mesmo objeto. O
//     CUnknown do SDK NAO cumpria isso: para IID_IUnknown ele devolvia o endereco do
//     subobjeto INonDelegatingUnknown, um ponteiro diferente do que devolvia para o
//     CLSID. Nunca apareceu porque nenhum host pede IID_IUnknown a um driver ASIO.
HRESULT STDMETHODCALLTYPE BcdAsioDriver::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = 0;

    if (riid == IID_ASIO_DRIVER || riid == IID_IUnknown) {
        *ppv = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE BcdAsioDriver::AddRef()
{
    return (ULONG)InterlockedIncrement(&refCount);
}

ULONG STDMETHODCALLTYPE BcdAsioDriver::Release()
{
    // O valor devolvido e o que o InterlockedDecrement JA leu, e nunca uma segunda
    // leitura de refCount: entre as duas, outro thread pode ter mexido nele, e devolver
    // 1 quando a contagem foi a zero manda o host segurar um ponteiro para memoria
    // liberada. E o mesmo cuidado que o codigo do SDK tomava com o max() dele, por um
    // caminho bem mais longo.
    const LONG left = InterlockedDecrement(&refCount);
    if (left == 0) {
        // `delete this` com o tipo estatico BcdAsioDriver: destrutor exato e ponteiro do
        // objeto COMPLETO. O host nunca faz `delete`; ele so chama Release().
        delete this;
        return 0;
    }
    return (ULONG)left;
}

HRESULT bcd::createDriverInstance(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = 0;

    BcdAsioDriver* driver = new (std::nothrow) BcdAsioDriver();
    if (!driver)
        return E_OUTOFMEMORY;

    // A CONTAGEM DE REFERENCIAS, no unico ponto onde ela e facil de errar.
    //
    // O objeto nasce com 1 - a referencia deste ponteiro local. O QueryInterface poe a
    // segunda; o Release logo abaixo devolve a primeira. Resultado:
    //   * QI com sucesso -> sobra exatamente 1, que e a do chamador. Nem vazamento nem
    //     objeto entregue com contagem zero;
    //   * QI com falha  -> o Release leva a 0 e o objeto se destroi AQUI. Nao existe um
    //     segundo caminho de limpeza para alguem esquecer de escrever, que e como esta
    //     funcao vaza na maioria dos drivers em que ela e escrita a mao.
    const HRESULT hr = driver->QueryInterface(riid, ppv);
    driver->Release();
    return hr;
}

//------------------------------------------------------------------------
// Registro e desregistro (regsvr32, e o instalador por GetProcAddress).
//------------------------------------------------------------------------

// A caixa de mensagem NAO e enfeite e nao foi acrescentada aqui: o instalador conta com
// ela e diz isso ao usuario ("the driver shows a message box of its own when this
// happens", installer/setup.cpp). O codigo de erro dentro dela e o que diz ONDE falhou.
static void sayRegistryFailure(const wchar_t* what, LONG rc)
{
    wchar_t text[192];
    _snwprintf(text, 191, L"Falha ao %s! (%ld)", what, rc);
    text[191] = 0;
    MessageBoxW(0, text, L"Driver ASIO BCD3000", MB_OK | MB_ICONERROR);
}

// ESTE CAMINHO NAO INSTANCIA O DRIVER, e isso e load-bearing: registrar nao pode roubar
// o aparelho do BCD3000Bridge.exe. Nao ha CoCreateInstance nem `new BcdAsioDriver` aqui,
// nem havia no codigo do SDK - o registro so escreve no registro do Windows. (O
// construtor do driver tambem nao abriria o aparelho: quem o abre e start(), nao init()
// nem o construtor. Mas o argumento nao precisa chegar la: nenhum objeto e criado.)
extern "C" HRESULT STDAPICALLTYPE DllRegisterServer()
{
    wchar_t modulePath[bcd::kRegPathMax];
    if (!bcd::thisModulePath(modulePath, bcd::kRegPathMax)) {
        sayRegistryFailure(L"registrar o driver", bcd::kRegErrModulePath);
        return -1;
    }

    const LONG rc = bcd::registerAsioDriver(IID_ASIO_DRIVER, modulePath, kAsioRegName,
                                            kAsioDescription, kThreadingModel);
    if (rc != bcd::kRegOk) {
        sayRegistryFailure(L"registrar o driver", rc);
        return -1;
    }
    return S_OK;
}

extern "C" HRESULT STDAPICALLTYPE DllUnregisterServer()
{
    const LONG rc = bcd::unregisterAsioDriver(IID_ASIO_DRIVER, kAsioRegName);
    if (rc != bcd::kRegOk) {
        sayRegistryFailure(L"remover o registro do driver", rc);
        return -1;
    }
    return S_OK;
}

//------------------------------------------------------------------------
BcdAsioDriver::BcdAsioDriver()
{
    refCount       = 1;     // a referencia de quem pediu a instancia
    bcd::comObjectCreated();
    blockFrames    = 512;
    samplePosition = 0;
    sampleRate     = (double)bcd::kSampleRate;
    callbacks      = 0;
    activeInputs   = 0;
    activeOutputs  = 0;
    toggle         = 0;
    active         = false;
    started        = false;
    timeInfoMode   = false;
    tcRead         = false;
    errorMessage[0] = 0;
    theSystemTime.hi = theSystemTime.lo = 0;
    memset(&asioTime, 0, sizeof(asioTime));

    for (long i = 0; i < kNumInputs; i++)  { inputBuffers[i] = 0;  inMap[i] = 0; }
    for (long i = 0; i < kNumOutputs; i++) { outputBuffers[i] = 0; outMap[i] = 0; }

    bcd::logInit("asio.log");
    bcd::logWrite("driver: instancia criada");
}

BcdAsioDriver::~BcdAsioDriver()
{
    stop();
    disposeBuffers();
    // Redundante depois de stop() (que ja parou a ponte), e mantido de
    // proposito: a ordem "MIDI antes do aparelho" fica escrita tambem aqui, e
    // continua valendo se alguem mexer nas chamadas acima. midi.stop() e
    // idempotente e inofensivo sem start().
    midi.stop();
    device.close();
    // E o bastao por ultimo, depois do aparelho fechado - a mesma ordem do
    // stop(), escrita tambem aqui. Repetir e inofensivo: releaseDevice() so
    // registra quando havia pedido de fato.
    bcd::releaseDevice();
    bcd::logWrite("driver: instancia destruida");
    bcd::logClose();
    // POR ULTIMO, depois de tudo o que pode escrever no log. Enquanto este contador nao
    // baixa, o DllCanUnloadNow responde S_FALSE e o Windows nao descarrega o DLL - e
    // decrementar antes de terminar de usar o proprio estado abriria exatamente a janela
    // que o contador existe para fechar.
    bcd::comObjectDestroyed();
}

// O nome do driver, e a trava que o mantem dentro do contrato.
//
// `name` no getDriverName e buffer DO HOST, e o contrato do metodo 2 de asioapi.h e "no
// maximo 32 bytes com o terminador". O valor cabe com folga - 8 bytes contra 32 -, mas a
// FORMA importa tanto quanto o valor: o `strcpy(name, "BCD3000")` que estava la era
// seguro pelo valor e inseguro pela forma, e era a excecao que fazia uma varredura por
// "escrita sem limite em buffer do host" achar um caso legitimo neste arquivo e concluir
// que strcpy nesta fronteira e aceitavel.
//
// O typedef abaixo e o idioma que este arquivo ja usa duas vezes (ver
// kProfileNamesCobrem*): um vetor de tamanho -1 nao compila, entao um nome mais comprido
// que o contrato quebra a BUILD em vez de estourar o buffer do host.
//
// E O 32 DEIXOU DE SER UM LITERAL SOLTO. Ele era escrito a mao aqui, e o 32 do contrato
// existia so como PROSA no metodo 2 de asioapi.h - dois numeros mantidos iguais por um
// humano, a mesma classe do problema que a trava do errorMessage acabou de fechar em
// bcdasio.h. A diferenca e que aqui parecia nao haver campo a que amarrar.
//
// HA, e ele e o MESMO mecanismo do errorMessage, medido e nao suposto:
// `ASIODriverInfo::name` E o buffer que o involucro C do host passa a este metodo -
// native/ASIOSDK/common/asio.cpp:77 faz `theAsioDriver->getDriverName(info->name)`,
// exatamente como as linhas 71 e 89 fazem
// `theAsioDriver->getErrorMessage(info->errorMessage)`. Nao e coincidencia de dois
// numeros iguais: e o mesmo campo da mesma estrutura, no mesmo caminho de chamada.
//
// POR QUE ESTA TRAVA MORA AQUI e a do erro mora no header: cada uma fica junto do que
// protege. Ali o que pode crescer errado e o MEMBRO; aqui e o NOME, e ele e este literal.
//
// E POR QUE O LIMITE DA COPIA CONTINUA SENDO `sizeof(kDriverName)` e nao o contrato, ao
// contrario do getErrorMessage: aqui a fonte e um literal DESTE arquivo, e a trava e uma
// verificacao de COMPILACAO nas mesmas duas linhas - crescer o nome quebra a build antes
// de existir binario. No getErrorMessage a fonte e uma mensagem variavel que nasce em
// outro arquivo, e a trava que existia era um CHECK de teste que nao linka aquele arquivo.
// Ali o limite tinha de ser o contrato; aqui pos o terminador no indice 7 em vez do 31,
// ou seja NAO toca 24 bytes do buffer do host que nao precisa tocar.
static const char kDriverName[] = "BCD3000";
typedef char kNomeDoDriverCabeNoContratoDoAsio[
    (sizeof(kDriverName) <= sizeof(((ASIODriverInfo*)0)->name)) ? 1 : -1];

void BcdAsioDriver::getDriverName(char* name)
{
    _snprintf(name, sizeof(kDriverName) - 1, "%s", kDriverName);
    name[sizeof(kDriverName) - 1] = 0;
}

long BcdAsioDriver::getDriverVersion()
{
    return 0x00000002L;
}

void BcdAsioDriver::getErrorMessage(char* string)
{
    // `string` e buffer DO HOST e o contrato do metodo 4 de asioapi.h e "no maximo 124
    // bytes com o terminador". O que estava aqui era `strcpy(string, errorMessage)` com
    // errorMessage[128]: ate quatro bytes alem do fim, dentro do processo do software de
    // DJ. Disparou de verdade em 2026-07-29, quatro vezes, com uma mensagem de 217
    // caracteres.
    //
    // *** O LIMITE E O CONTRATO, E NAO O TAMANHO DO NOSSO MEMBRO. ***
    //
    // A versao anterior destas duas linhas limitava por `sizeof(errorMessage)` e o
    // comentario aqui afirmava que isso "mantem a fronteira dentro do contrato se alguem
    // mexer no tamanho do membro depois". Essa frase e verdadeira na direcao de ENCOLHER
    // o membro e FALSA na de CRESCER, que e justamente a perigosa: com errorMessage[256]
    // a copia passaria a escrever ate 256 bytes no buffer de 124 do host - o defeito de
    // 2026-07-29 de volta, byte por byte -, e o comentario teria CONVIDADO a mudanca.
    // Aumentar o membro e o passo natural depois de uma rodada cujo tema e "o log guarda
    // o texto inteiro", e nada pegava: o CHECK do teste mede a CONSTANTE e nao o membro,
    // o alvo `tests` nao linka este arquivo, e o /W4 /WX fica calado.
    //
    // Com bcd::kAsioErrorMax nas duas linhas, o tamanho do membro nao decide mais nada
    // sobre memoria alheia. Ele so decide se a NOSSA copia cabe - e o typedef
    // kErroCabeNoContratoDoAsio, em bcdasio.h, trava em COMPILACAO que este 124 e o 124
    // do campo errorMessage do ASIODriverInfo, que e onde o contrato existe como numero.
    // A garantia de memoria desta funcao ficou aritmeticamente exata e independente do
    // resto do arquivo: escreve no maximo os indices 0..123, qualquer que seja o conteudo
    // ou o tamanho do membro.
    //
    // O terminador a forca cai no ULTIMO byte que o contrato permite (indice 123), e ele
    // e load-bearing so no caso extremo: com uma mensagem de exatamente 123 caracteres o
    // _snprintf enche os 123 e nao termina. Nos outros casos ele ja terminou, e esta
    // escrita e a mesma do idioma que o getClockSources() daqui usa - dentro do contrato
    // em qualquer caso, contra os 128 bytes que o strcpy anterior podia escrever.
    _snprintf(string, (size_t)(bcd::kAsioErrorMax - 1), "%s", errorMessage);
    string[bcd::kAsioErrorMax - 1] = 0;
}

// UM caminho para toda mensagem de erro que chega ao host, e tres coisas que tem de
// acontecer juntas em todo ponto de falha do driver. Antes desta correcao nenhuma das
// tres estava garantida:
//
//  1. O LOG RECEBE O TEXTO INTEGRO. As tres linhas "driver: ... falhou - %s" imprimiam
//     `errorMessage`, isto e, o texto JA cortado pelo tamanho do buffer do ASIO. Foi
//     assim que a causa VERDADEIRA de um incidente real desapareceu do log de campo em
//     2026-07-29: o aparelho tinha sumido por cabo mal encaixado, e o que sobrou na tela
//     e no log foi a primeira causa da lista, que era a errada. Agora o log recebe
//     `full` e o host recebe a copia que cabe.
//
//  2. errorMessage recebe no maximo o que o contrato aceita, com terminador garantido.
//
//  3. Se a mensagem NAO couber no contrato, sai uma linha dizendo isso, com os dois
//     numeros. ESTE e o ponto de estrangulamento certo para esse aviso: toda mensagem
//     que chega ao host passa por aqui, venha do usbdev ou do motor de audio. O alvo
//     `tests` nao linka este arquivo nem o audioengine.cpp, entao o teste unitario
//     alcanca as mensagens do usbdev (todas, por findStageMessage) e este aviso cobre o
//     resto em campo. Medido nesta rodada: das 31 mensagens que podem chegar aqui - 16
//     do usbdev e 15 do motor de audio -, UMA nao cabia (a de interface inativa, com 217
//     caracteres) e as outras 30 cabiam. Depois da correcao a mais longa tem 117
//     caracteres, 118 bytes com o terminador, contra os 124 do contrato.
void BcdAsioDriver::setError(const char* what, const char* full)
{
    if (!full)
        full = "";
    _snprintf(errorMessage, sizeof(errorMessage) - 1, "%s", full);
    errorMessage[sizeof(errorMessage) - 1] = 0;
    bcd::logWrite("driver: %s - %s", what, full);
    if (!bcd::diagnosticFitsAsio(full))
        bcd::logWrite("driver: ATENCAO - a mensagem acima tem %d bytes com o terminador "
                      "e o contrato do getErrorMessage do ASIO aceita %d. O host recebeu "
                      "uma versao CORTADA: encurte a mensagem na origem, nao aumente o "
                      "limite.", (int)strlen(full) + 1, (int)bcd::kAsioErrorMax);
}

// IMPORTANTE: init() NAO abre o aparelho. Softwares de DJ chamam init() em
// todo driver ASIO instalado so para montar a lista de opcoes; abrir aqui
// roubaria o aparelho do BCD3000Bridge.exe sem que ninguem fosse usar audio.
// So verificamos que o aparelho existe.
ASIOBool BcdAsioDriver::init(void* sysRef)
{
    (void)sysRef;
    if (active)
        return true;

    if (!device.isPresent()) {
        // A linha do log continua sendo "driver: init falhou - <mensagem>", letra por
        // letra, e o que mudou e QUAL mensagem vai nela: a integra, e nao a que caberia
        // no buffer do ASIO. Ver setError().
        setError("init falhou", device.lastError());
        return false;
    }

    active = true;
    bcd::logWrite("driver: init OK (aparelho presente, ainda nao tomado)");
    // A linha acima nao mudou de texto de proposito - ela e marcador que os testes
    // de hardware leem. A do modelo e SEPARADA e sai SO para perfil nao validado,
    // entao o fluxo de log da BCD3000 continua identico ao que foi medido.
    if (!device.profile().provenOnHardware)
        bcd::logWrite("driver: modelo %s - caminho EXPERIMENTAL, nunca executado por "
                      "ninguem deste projeto; se o audio nao subir, a linha do "
                      "usbdev ou a do motor diz qual suposicao nao se confirmou",
                      device.profile().model);
    return true;
}

ASIOError BcdAsioDriver::start()
{
    if (!callbacks)
        return ASE_NotPresent;

    samplePosition = 0;
    toggle         = 0;
    theSystemTime.hi = theSystemTime.lo = 0;

    // Pedir o aparelho ao programa de controles. requestDevice() VOLTA NA HORA -
    // a espera esta no laco de tentativas abaixo. Falhar aqui nao impede nada:
    // significa apenas que nao houve como avisar o bridge, e nesse caso ou o
    // aparelho esta livre (e o open pega na primeira tentativa) ou o open falha e
    // devolvemos erro de ASIO normalmente. Mesma regra da ponte MIDI: nunca
    // derrubar o audio por causa de uma adicao.
    if (!bcd::requestDevice())
        bcd::logWrite("driver: sem sinalizacao de bastao - se o programa de "
                      "controles estiver com o aparelho, o start vai falhar");

    // So agora tomamos o aparelho: e aqui que o audio realmente comeca.
    //
    // Em tentativas curtas, e nao com um Sleep fixo antes da primeira: o bridge
    // consulta o pedido uma vez por volta do laco de leitura dos controles, e essa
    // leitura tem prazo de 400 ms, mais o tempo de fechar os handles do aparelho.
    // Dormir um prazo fixo custaria esse prazo em TODA partida de audio -
    // inclusive quando nao ha bridge nenhum para soltar o aparelho, que e o caso
    // em que a primeira tentativa ja pega - e ainda assim nao garantiria nada se o
    // bridge demorasse mais. O numero de tentativas vai para o log de proposito: e
    // ele que diz, no teste de hardware, se a temporizacao esta confortavel ou no
    // limite.
    int  tries  = 0;
    bool opened = false;
    while (tries < bcd::kHandoffTries) {
        if (tries > 0)
            Sleep((DWORD)bcd::kHandoffRetryMs);
        tries++;
        // So a PRIMEIRA tentativa escreve as linhas de detalhe no log. As outras 14
        // repetiriam as MESMAS duas linhas ("abrindo <caminho>" e o erro 5 do
        // CreateFile): 30 linhas por start() falhado, e o software de DJ retenta a
        // cada ~60 s. O erro da ULTIMA tentativa nao se perde - fica em err_ e sai
        // na linha de resumo logo abaixo, por device.lastError().
        const bool ok = (tries == 1) ? device.open() : device.openQuiet();
        if (ok) {
            opened = true;
            break;
        }
    }
    const unsigned waitedMs = (unsigned)(tries - 1) * bcd::kHandoffRetryMs;

    if (!opened) {
        // O texto da linha de log e montado aqui porque ele leva as duas contagens; a
        // mensagem do aparelho continua vindo integra por setError(), e a linha final
        // sai com o mesmo formato de antes: "driver: start falhou ao tomar o aparelho em
        // N tentativas (M ms) - <mensagem>".
        char what[96];
        _snprintf(what, sizeof(what) - 1,
                  "start falhou ao tomar o aparelho em %d tentativas (%u ms)",
                  tries, waitedMs);
        what[sizeof(what) - 1] = 0;
        setError(what, device.lastError());
        // Desfazer o pedido: sem isto o bridge ficaria eternamente vendo "o
        // driver quer o aparelho" e o usuario perderia os controles sem entender
        // por que - com o audio tambem sem funcionar.
        bcd::releaseDevice();
        return ASE_HWMalfunction;
    }
    bcd::logWrite("driver: aparelho tomado na tentativa %d de %d (%u ms de espera)",
                  tries, bcd::kHandoffTries, waitedMs);

    // started tem de valer true ANTES de o motor subir: o thread de audio ja
    // chama onBlock durante o enchimento inicial dos buffers, e com started
    // falso esses primeiros blocos sairiam mudos.
    started = true;

    if (!engine.start(&device, blockFrames, this)) {
        started = false;
        setError("start falhou", engine.lastError());
        device.close();
        // Mesma ordem do stop(): fechar o aparelho ANTES de devolver o bastao. Se
        // o pedido fosse desligado primeiro, o bridge tentaria reabrir o aparelho
        // enquanto ainda o seguramos e sairia perdendo.
        bcd::releaseDevice();
        return ASE_HWMalfunction;
    }

    // A ponte MIDI sobe POR ULTIMO, e de proposito: assim o caminho de erro do
    // engine.start() acima nao precisa desmontar MIDI nenhum.
    //
    // E falhar aqui NAO derruba o audio. Desde que a porta virtual passou a ser
    // propriedade permanente do BCD3000Bridge.exe, o driver nao cria porta nenhuma
    // e nao depende mais da teVirtualMIDI - sobrou uma unica causa de falha aqui: o
    // aparelho sem a interface MIDI (IF3). O BCD3000Bridge.exe estar PARADO NAO e
    // falha de start(): a ponte sobe, tenta o canal a cada segundo e os controles
    // passam a funcionar sozinhos quando o programa subir.
    // O audio e o produto principal e esta validado no hardware; MIDI e adicao.
    // Nunca devolver erro de ASIO por causa de MIDI.
    if (!midi.start(&device))
        bcd::logWrite("driver: sem ponte MIDI (%s) - o audio segue normalmente",
                      midi.lastError());

    bcd::logWrite("driver: start OK (bloco=%ld frames)", blockFrames);
    return ASE_OK;
}

ASIOError BcdAsioDriver::stop()
{
    started = false;
    // MIDI PRIMEIRO, antes de engine.stop() e de device.close(). Invertido, o
    // thread da ponte escreveria LED num aparelho ja fechado: ele so para de tocar
    // no aparelho quando midi.stop() devolve.
    midi.stop();
    engine.stop();
    // Devolver o aparelho: enquanto o driver o segura, o programa dos
    // controles nao consegue abri-lo.
    device.close();
    // O bastao vai DEPOIS do device.close(), e a ordem e load-bearing: o bridge
    // reage ao pedido sumir reabrindo o aparelho. Se o pedido fosse desligado
    // antes, ele tentaria reabrir enquanto o driver ainda o segura, e sairia
    // perdendo.
    // Ordem completa: midi.stop() -> engine.stop() -> device.close() ->
    // releaseDevice().
    bcd::releaseDevice();
    bcd::logWrite("driver: stop - aparelho devolvido");
    return ASE_OK;
}

ASIOError BcdAsioDriver::getChannels(long* numInputChannels, long* numOutputChannels)
{
    *numInputChannels  = kNumInputs;
    *numOutputChannels = kNumOutputs;
    return ASE_OK;
}

ASIOError BcdAsioDriver::getLatencies(long* _inputLatency, long* _outputLatency)
{
    // Saida: bloco do host mais os blocos de 10 ms mantidos em voo. Aqui os
    // kOutXfers transfers SEGURAM dado esperando para tocar - o que se escreve
    // no buffer do transfer 2 sai 30 ms depois -, entao os tres contam.
    *_outputLatency = blockFrames + (long)(bcd::kOutXfers * bcd::kFramesPerBlock);

    // Entrada: bloco do host + nivel de regime do anel de entrada + UM bloco de
    // 10 ms do USB.
    //
    // O termo de voo NAO e simetrico com o da saida, e ja esteve errado aqui por
    // ter copiado a semantica dela. Cada transfer de ENTRADA cobre a sua propria
    // janela de 10 quadros USB e conclui no fim dela; os outros kInXfers-1 estao
    // apenas enfileirados ATRAS, sem guardar audio capturado. O atraso da captura
    // ate o anel e de 1 a 10 ms, ou seja UM bloco - nao kInXfers blocos.
    // (kInXfers segue sendo o numero de transfers em voo e e usado no motor; ele
    // so nao entra nesta conta.)
    //
    // O termo do anel nao e um limite ocioso que so importa em pico: a deriva de
    // relogio e unidirecional (o cristal do aparelho e mais rapido que o do PC),
    // entao o nivel sobe ate a marca d'agua e fica preso ali, oscilando um bloco
    // do host acima dela, porque a correcao de deriva dispara no pico da
    // oscilacao. Medido no hardware, 15 min com bloco de 512: nivel de 16.900 a
    // 19.800 bytes, contra marca d'agua de 16.384. Por isso o termo e a marca
    // d'agua MAIS MEIO BLOCO DO HOST, que e o meio da oscilacao: a 512 frames dao
    // 18.432 bytes, dentro da faixa medida e 82 bytes acima do seu centro. A
    // marca d'agua sozinha ficaria abaixo de toda a faixa medida. Isso e latencia
    // real - audio capturado esperando na fila - e o software de DJ usa este
    // numero para alinhar gravacao e monitoracao.
    //
    // ESTE NUMERO E ESTIMADO, nao medido: cada termo vem de um modelo do caminho
    // de dados, e so o do anel tem medicao direta por tras. A medicao definitiva
    // e por LOOPBACK - cabo da saida ligado na entrada, comparando a posicao do
    // sinal gravado com a do tocado -, e esta PENDENTE NO PASSO 2.4. Quem for
    // fazer o 2.4: a tarefa existe, e e esta.
    //
    // Conta fechada (frames e ms a 44100 Hz):
    //     bloco  256:   256 + 1152 + 441 =  1849 frames =  41,9 ms
    //     bloco  512:   512 + 2304 + 441 =  3257 frames =  73,9 ms
    //     bloco 1024:  1024 + 4608 + 441 =  6073 frames = 137,7 ms
    //     bloco 2048:  2048 + 9216 + 441 = 11705 frames = 265,4 ms
    //
    // A conta em si vive em bcd::inputLatencyFrames (audioengine.h), junto do resto
    // da aritmetica de anel e travada por teste de unidade. Era a UNICA aritmetica
    // de anel escrita aqui em linha - e justamente a que ja errou duas vezes, nos
    // dois sentidos. Repetir a expressao no teste faria o teste passar enquanto
    // este numero divergisse.
    *_inputLatency = (long)bcd::inputLatencyFrames((int)blockFrames);
    return ASE_OK;
}

ASIOError BcdAsioDriver::getBufferSize(long* minSize, long* maxSize,
                                       long* preferredSize, long* granularity)
{
    *minSize       = 256;
    *maxSize       = 2048;
    *preferredSize = 512;
    *granularity   = -1;        // potencias de dois
    return ASE_OK;
}

// Os tres parametros abaixo usam o prefixo _ pela MESMA razao que getLatencies:
// `sampleRate` e nome de membro desta classe, e um parametro homonimo sombreia o
// membro. Em /W4 isso sai como aviso C4458 - os tres unicos avisos que o DLL tinha
// -, e aviso e ruido que faz o proximo aviso, esse de verdade, passar em branco.
ASIOError BcdAsioDriver::canSampleRate(ASIOSampleRate _sampleRate)
{
    // O aparelho so trabalha a 44100 Hz. Aceitar outra taxa tocaria desafinado.
    if (_sampleRate == (double)bcd::kSampleRate)
        return ASE_OK;
    return ASE_NoClock;
}

ASIOError BcdAsioDriver::getSampleRate(ASIOSampleRate* _sampleRate)
{
    *_sampleRate = sampleRate;
    return ASE_OK;
}

ASIOError BcdAsioDriver::setSampleRate(ASIOSampleRate _sampleRate)
{
    if (_sampleRate != (double)bcd::kSampleRate)
        return ASE_NoClock;
    return ASE_OK;
}

ASIOError BcdAsioDriver::getClockSources(ASIOClockSource* clocks, long* numSources)
{
    clocks->index             = 0;
    clocks->associatedChannel = -1;
    clocks->associatedGroup   = -1;
    clocks->isCurrentSource   = ASIOTrue;
    // Montado do modelo do perfil, e nao gravado: com dois perfis, uma constante
    // aqui faria uma BCD2000 anunciar "BCD3000 Internal". Para a BCD3000 o
    // resultado e caractere por caractere o mesmo texto de antes desta tarefa -
    // travado por teste unitario -, e o nome do ASIOClockSource tem 32 bytes, com
    // folga para qualquer modelo da tabela.
    _snprintf(clocks->name, sizeof(clocks->name) - 1, "%s Internal",
              device.profile().model);
    clocks->name[sizeof(clocks->name) - 1] = 0;
    *numSources = 1;
    return ASE_OK;
}

ASIOError BcdAsioDriver::setClockSource(long index)
{
    if (index != 0)
        return ASE_NotPresent;
    asioTime.timeInfo.flags |= kClockSourceChanged;
    return ASE_OK;
}

ASIOError BcdAsioDriver::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp)
{
    tStamp->lo = theSystemTime.lo;
    tStamp->hi = theSystemTime.hi;
    if (samplePosition >= twoRaisedTo32) {
        sPos->hi = (unsigned long)(samplePosition * twoRaisedTo32Reciprocal);
        sPos->lo = (unsigned long)(samplePosition - (sPos->hi * twoRaisedTo32));
    } else {
        sPos->hi = 0;
        sPos->lo = (unsigned long)samplePosition;
    }
    return ASE_OK;
}

// O perfil do aparelho traz bcd::kChannels nomes por direcao, e esta casca reporta
// kNumInputs/kNumOutputs canais. Divergirem seria leitura fora dos vetores do
// perfil no getChannelInfo() abaixo - erro que compila e nao aparece em teste
// nenhum. Trava em tempo de COMPILACAO: um vetor de tamanho -1 nao compila.
typedef char kProfileNamesCobremEntradas[(kNumInputs  == bcd::kChannels) ? 1 : -1];
typedef char kProfileNamesCobremSaidas  [(kNumOutputs == bcd::kChannels) ? 1 : -1];

ASIOError BcdAsioDriver::getChannelInfo(ASIOChannelInfo* info)
{
    if (info->channel < 0 ||
        (info->isInput ? info->channel >= kNumInputs : info->channel >= kNumOutputs))
        return ASE_InvalidParameter;

    info->type         = ASIOSTInt16LSB;
    info->channelGroup = 0;
    info->isActive     = ASIOFalse;

    // OS NOMES VEM DO PERFIL DO APARELHO (tabela no topo de usbdev.cpp), e a
    // PROVA de cada um deles mora la, junto do nome. Para a BCD3000 sao exatamente
    // os quatro mais quatro nomes que estavam aqui - confirmados no hardware por
    // tres testes cruzados, e travados por teste unitario contra quem quiser
    // "arrumar" o texto.
    //
    // profile() nunca e nulo: com o aparelho ainda nao encontrado ele devolve o
    // perfil da BCD3000, que e o que este metodo respondia antes desta tarefa em
    // TODOS os casos. O host pode perguntar antes de init() ter achado nada.
    const bcd::DeviceProfile& prof = device.profile();

    // COPIA LIMITADA, e nao `strcpy`, pelo idioma que o getClockSources() daqui a
    // sessenta linhas acima ja usa. `info->name` e um char[32] DO HOST, dentro do
    // ASIOChannelInfo, e o nome vem de uma TABELA em outro arquivo que este projeto
    // convida terceiros a estender - o refatoramento que tirou os literais de dentro
    // desta funcao aumentou a exposicao em vez de reduzi-la. O teste dos 32 bytes
    // continua existindo e continua sendo a barreira certa (ele reprova o nome ANTES de
    // alguem publicar), mas passa a ser rede: um nome de 35 caracteres acrescentado por
    // quem nao roda os testes estouraria o buffer do host no strcpy que estava aqui.
    if (info->isInput) {
        for (long i = 0; i < activeInputs; i++)
            if (inMap[i] == info->channel) { info->isActive = ASIOTrue; break; }
        _snprintf(info->name, sizeof(info->name) - 1, "%s", prof.inNames[info->channel]);
    } else {
        for (long i = 0; i < activeOutputs; i++)
            if (outMap[i] == info->channel) { info->isActive = ASIOTrue; break; }
        _snprintf(info->name, sizeof(info->name) - 1, "%s", prof.outNames[info->channel]);
    }
    info->name[sizeof(info->name) - 1] = 0;
    return ASE_OK;
}

ASIOError BcdAsioDriver::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                                       long bufferSize, ASIOCallbacks* cb)
{
    if (bufferSize < 256 || bufferSize > 2048)
        return ASE_InvalidMode;

    disposeBuffers();

    activeInputs  = 0;
    activeOutputs = 0;
    blockFrames   = bufferSize;

    ASIOBufferInfo* info = bufferInfos;
    for (long i = 0; i < numChannels; i++, info++) {
        if (info->isInput) {
            if (info->channelNum < 0 || info->channelNum >= kNumInputs ||
                activeInputs >= kNumInputs) {
                disposeBuffers();
                return ASE_InvalidParameter;
            }
            inMap[activeInputs] = info->channelNum;
            inputBuffers[activeInputs] = new (std::nothrow) short[blockFrames * 2];
            if (!inputBuffers[activeInputs]) {
                disposeBuffers();
                return ASE_NoMemory;
            }
            memset(inputBuffers[activeInputs], 0, blockFrames * 2 * sizeof(short));
            info->buffers[0] = inputBuffers[activeInputs];
            info->buffers[1] = inputBuffers[activeInputs] + blockFrames;
            activeInputs++;
        } else {
            if (info->channelNum < 0 || info->channelNum >= kNumOutputs ||
                activeOutputs >= kNumOutputs) {
                disposeBuffers();
                return ASE_InvalidParameter;
            }
            outMap[activeOutputs] = info->channelNum;
            outputBuffers[activeOutputs] = new (std::nothrow) short[blockFrames * 2];
            if (!outputBuffers[activeOutputs]) {
                disposeBuffers();
                return ASE_NoMemory;
            }
            memset(outputBuffers[activeOutputs], 0, blockFrames * 2 * sizeof(short));
            info->buffers[0] = outputBuffers[activeOutputs];
            info->buffers[1] = outputBuffers[activeOutputs] + blockFrames;
            activeOutputs++;
        }
    }

    callbacks = cb;
    if (callbacks->asioMessage(kAsioSupportsTimeInfo, 0, 0, 0)) {
        timeInfoMode = true;
        asioTime.timeInfo.speed = 1.;
        asioTime.timeInfo.systemTime.hi = asioTime.timeInfo.systemTime.lo = 0;
        asioTime.timeInfo.samplePosition.hi = asioTime.timeInfo.samplePosition.lo = 0;
        asioTime.timeInfo.sampleRate = sampleRate;
        asioTime.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
        asioTime.timeCode.speed = 1.;
        asioTime.timeCode.timeCodeSamples.lo = asioTime.timeCode.timeCodeSamples.hi = 0;
        asioTime.timeCode.flags = kTcValid | kTcRunning;
    } else {
        timeInfoMode = false;
    }

    bcd::logWrite("driver: createBuffers bloco=%ld in=%ld out=%ld",
                  blockFrames, activeInputs, activeOutputs);
    return ASE_OK;
}

ASIOError BcdAsioDriver::disposeBuffers()
{
    stop();
    callbacks = 0;

    // MESMA POLITICA DO MOTOR, UM NIVEL ACIMA. O engine.stop() que o stop() acima
    // acabou de chamar pode ter desistido de esperar o thread de audio (3 s) e,
    // nesse caso, ele deliberadamente NAO liberou nada: o thread pode estar parado
    // dentro do bufferSwitch do software de DJ e, quando voltar, termina o onBlock
    // em curso - o passo 3 dele monta outCh[outMap[i]] = outputBuffers[i] + offset
    // e o interleave4 LE dali. `started`, que o stop() acabou de zerar, protege a
    // ENTRADA do onBlock; nao protege o RETORNO de um bufferSwitch que ja esta
    // dentro do host. Liberar os buffers aqui seria exatamente o defeito que o
    // motor consertou um nivel abaixo - liberacao de memoria que o thread de audio
    // ainda pode ler, dentro do processo do software de DJ -, e a casca ignorando o
    // que o motor sabe e pior que os dois nao saberem.
    if (engine.threadStuck()) {
        // Os ponteiros NAO sao zerados, e ha DOIS motivos independentes:
        //
        // 1. nulo trocaria vazamento por acesso invalido. Com outputBuffers[i]
        //    nulo, o passo 3 do onBlock leria o endereco `offset` (perto de zero) e
        //    o interleave4 tomaria falha de pagina no processo do software de DJ. E
        //    o mesmo raciocinio que deixou os ponteiros do motor intactos. O
        //    `callbacks = 0` la em cima e seguro pelo motivo OPOSTO, e a diferenca
        //    e o criterio: os dois leitores de `callbacks` o reconferem no INICIO
        //    do onBlock, e o bufferSwitchX nao o le mais depois de voltar; os
        //    ponteiros de buffer sao lidos DEPOIS do bufferSwitch retornar.
        // 2. sem os ponteiros o vazamento seria PERMANENTE; com eles de pe ele e
        //    apenas DIFERIDO. disposeBuffers() e chamado de novo pelo host e SEMPRE
        //    pelo destrutor, e o stop() de la espera outros 3 s pelo MESMO handle de
        //    thread que o motor guarda de proposito: se essa espera colher o thread,
        //    threadStuck() volta a false e o laco abaixo libera estes mesmos
        //    buffers. Nada e liberado duas vezes, porque este caminho nao libera
        //    nada. activeInputs/activeOutputs tambem ficam de pe, pelo mesmo
        //    criterio do motor - o estado fica INTEIRO para o thread que ainda pode
        //    le-lo -, e o createBuffers() seguinte os zera sozinho.
        //
        // O CUSTO e no maximo (4+4) buffers de 2048 frames duplos: 64 KB, uma vez
        // por disposeBuffers no caminho patologico. Memoria vazada num cenario raro
        // e infinitamente melhor que leitura de memoria liberada dentro do processo
        // do usuario, e a decisao esta tomada assim de proposito - nao e descuido
        // nem falta de vontade de contar bytes.
        //
        // Devolvemos ASE_OK: para o host os buffers ESTAO descartados (ele nao pode
        // mais toca-los), e um erro aqui so o faria tratar como falha um descarte
        // que funcionou.
        const unsigned long leaked =
            (unsigned long)(activeInputs + activeOutputs) *
            (unsigned long)blockFrames * 2ul * (unsigned long)sizeof(short);
        bcd::logWrite("driver: vazando de proposito os %ld buffers do host "
                      "(%lu bytes) - o thread de audio nao foi colhido e ainda pode "
                      "ler outputBuffers ao voltar do bufferSwitch. O proximo "
                      "disposeBuffers (ou o destrutor) libera se colher o thread",
                      activeInputs + activeOutputs, leaked);

        // RISCO RESIDUAL REGISTRADO, e NAO corrigido aqui de proposito: se o thread
        // continuar travado quando o objeto BcdAsioDriver for destruido, a memoria
        // do PROPRIO objeto morre com ele - e o thread ainda le membros nossos
        // (outputBuffers[], outMap[], blockFrames, toggle, asioTime). Vazar os
        // buffers nao vaza o objeto que os aponta, entao o vazamento acima nao
        // alcanca este caso. E irmao do risco ja registrado no ledger para a
        // MidiBridge (thread da ponte travado mais objeto do driver destruido), e a
        // solucao real e a mesma que o RelayWorker usa: mover para o HEAP o estado
        // que o thread le, para poder ser vazado junto. Isso e mudanca de DESENHO,
        // nao de linha, e esta fora do escopo desta correcao.
        return ASE_OK;
    }

    for (long i = 0; i < kNumInputs; i++) {
        delete[] inputBuffers[i];
        inputBuffers[i] = 0;
    }
    for (long i = 0; i < kNumOutputs; i++) {
        delete[] outputBuffers[i];
        outputBuffers[i] = 0;
    }
    activeInputs  = 0;
    activeOutputs = 0;
    return ASE_OK;
}

ASIOError BcdAsioDriver::controlPanel()
{
    return ASE_NotPresent;
}

ASIOError BcdAsioDriver::future(long selector, void* opt)
{
    (void)opt;
    switch (selector) {
        case kAsioEnableTimeCodeRead:  tcRead = true;  return ASE_SUCCESS;
        case kAsioDisableTimeCodeRead: tcRead = false; return ASE_SUCCESS;
        case kAsioCanTimeInfo:                          return ASE_SUCCESS;
        case kAsioCanTimeCode:                          return ASE_SUCCESS;
    }
    return ASE_NotPresent;
}

ASIOError BcdAsioDriver::outputReady()
{
    return ASE_NotPresent;
}

//------------------------------------------------------------------------
// Chamado pelo thread de audio do motor, uma vez por bloco.
void BcdAsioDriver::onBlock(const short* in, short* out, int frames)
{
    if (!started || !callbacks) {
        memset(out, 0, frames * bcd::kChannels * sizeof(short));
        return;
    }

    const long index  = toggle;
    const long offset = index ? blockFrames : 0;

    // 1. entrada do aparelho -> buffers de entrada do ASIO
    short* inCh[bcd::kChannels] = { 0, 0, 0, 0 };
    for (long i = 0; i < activeInputs; i++)
        inCh[inMap[i]] = inputBuffers[i] + offset;
    bcd::deinterleave4(in, inCh, frames);

    // 2. avisar o software de DJ
    bcd::getNanoSeconds(&theSystemTime);
    samplePosition += frames;
    if (timeInfoMode)
        bufferSwitchX(index);
    else
        callbacks->bufferSwitch(index, ASIOTrue);

    // 3. buffers de saida do ASIO -> aparelho
    const short* outCh[bcd::kChannels] = { 0, 0, 0, 0 };
    for (long i = 0; i < activeOutputs; i++)
        outCh[outMap[i]] = outputBuffers[i] + offset;
    bcd::interleave4(outCh, out, frames);

    toggle = index ? 0 : 1;
}

// O aparelho parou de responder (cabo arrancado). Pedir ao software de DJ que
// reinicie o audio, em vez de deixa-lo esperando por som que nao vem.
//
// AQUI NAO SE CHAMA midi.stop(), pelo mesmo motivo que nao se chama
// engine.stop(): isto roda NO thread de audio. midi.stop() espera pelo thread da
// ponte, e nada garante que o thread de audio nao seja justamente quem ficaria
// bloqueado enquanto o host espera por ele. A limpeza fica para o stop() que o
// host vai chamar em resposta ao kAsioResetRequest; ate la, o thread da ponte
// segue vivo e inofensivo, e as escritas de LED apenas falham e sao registradas
// com limite de volume.
void BcdAsioDriver::onDeviceLost()
{
    bcd::logWrite("driver: aparelho perdido - pedindo reset ao host");
    started = false;
    if (callbacks && callbacks->asioMessage)
        callbacks->asioMessage(kAsioResetRequest, 0, 0, 0);
}

void BcdAsioDriver::bufferSwitchX(long index)
{
    getSamplePosition(&asioTime.timeInfo.samplePosition, &asioTime.timeInfo.systemTime);
    if (tcRead) {
        asioTime.timeCode.timeCodeSamples.lo =
            asioTime.timeInfo.samplePosition.lo + (unsigned long)(600.0 * sampleRate);
        asioTime.timeCode.timeCodeSamples.hi = 0;
    }
    callbacks->bufferSwitchTimeInfo(&asioTime, index, ASIOTrue);
    asioTime.timeInfo.flags &= ~(kSampleRateChanged | kClockSourceChanged);
}

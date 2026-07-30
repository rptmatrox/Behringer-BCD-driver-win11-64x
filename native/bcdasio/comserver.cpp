#include "comserver.h"

#include <windows.h>
#include <new>          // std::nothrow

namespace {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// CONTAGEM DE VIDA DO SERVIDOR: TRES contadores, e nao um.
//
// Somados num contador so, a resposta do DllCanUnloadNow seria a mesma - mas um
// diagnostico nao teria como dizer QUAL dos tres impede o descarregamento, e os tres tem
// causas e prazos completamente diferentes (uma instancia do driver dura a sessao de audio;
// uma fabrica dura microssegundos; uma trava de LockServer dura o que o host quiser).
//
// Sao `volatile LONG` e mexidos SO por Interlocked*: a leitura de um LONG alinhado em x64 e
// atomica, e no MSVC para x86/x64 `volatile` tambem impede o compilador de guardar o valor
// num registrador entre as leituras. Nao ha trava nenhuma aqui de proposito - ver a nota
// sobre ordem de destruicao no fim deste arquivo.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
volatile LONG g_objects   = 0;      // instancias do BcdAsioDriver vivas
volatile LONG g_factories = 0;      // fabricas de classe entregues e nao liberadas
volatile LONG g_locks     = 0;      // travas de IClassFactory::LockServer

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// A FABRICA DE CLASSE.
//
// Uma classe so, e por isso nao existe tabela de CFactoryTemplate: a fabrica sabe qual
// objeto cria porque este DLL cria exatamente um tipo de objeto.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class DriverClassFactory : public IClassFactory
{
public:
    // Nasce com UMA referencia: a que o DllGetClassObject entrega a quem chamou. Nascer
    // com zero e depois chamar AddRef daria o mesmo resultado por dois passos, e um
    // caminho de erro entre os dois deixaria um objeto com contagem zero vivo.
    DriverClassFactory() : refCount(1)
    {
        InterlockedIncrement(&g_factories);
    }

    // IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv)
            return E_POINTER;
        *ppv = 0;
        // Aqui o IID e o normal do COM - a peculiaridade do CLSID-como-IID e do OBJETO
        // do driver, nao da fabrica dele.
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    virtual ULONG STDMETHODCALLTYPE AddRef()
    {
        return (ULONG)InterlockedIncrement(&refCount);
    }

    virtual ULONG STDMETHODCALLTYPE Release()
    {
        const LONG left = InterlockedDecrement(&refCount);
        if (left == 0) {
            delete this;
            return 0;
        }
        return (ULONG)left;
    }

    // IClassFactory
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid,
                                                     void** ppv)
    {
        if (!ppv)
            return E_POINTER;
        *ppv = 0;

        // AGREGACAO NAO E SUPORTADA, e a resposta e a que o COM manda dar. O codigo que
        // saiu aceitava `outer` nao nulo quando o riid era IID_IUnknown e repassava o
        // ponteiro externo ao objeto, que entao delegava IUnknown a ele - maquinaria de
        // agregacao que nenhum host ASIO usa e que nunca foi exercitada uma vez neste
        // projeto. Codigo de agregacao nao testado e pior que nao ter agregacao:
        // CLASS_E_NOAGGREGATION diz a verdade e o host trata.
        if (outer)
            return CLASS_E_NOAGGREGATION;

        return bcd::createDriverInstance(riid, ppv);
    }

    virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL lock)
    {
        if (lock)
            InterlockedIncrement(&g_locks);
        else
            InterlockedDecrement(&g_locks);
        return S_OK;
    }

private:
    // Privado: quem destroi esta fabrica e o Release() dela, e mais ninguem. `delete this`
    // de dentro da classe alcanca o destrutor privado; um `delete` de fora nao compila.
    ~DriverClassFactory()
    {
        InterlockedDecrement(&g_factories);
    }

    volatile LONG refCount;
};

}

void bcd::comObjectCreated()   { InterlockedIncrement(&g_objects); }
void bcd::comObjectDestroyed() { InterlockedDecrement(&g_objects); }

long bcd::comObjectsAlive()    { return g_objects; }
long bcd::comFactoriesAlive()  { return g_factories; }
long bcd::comServerLocks()     { return g_locks; }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// OS DOIS PONTOS DE ENTRADA DO COM.
//
// `extern "C"` nos dois, e nos dois de bcdasio.cpp, de proposito: sem isso os nomes saem
// decorados pelo C++ e a exportacao passa a depender de o linkador casar o nome do
// BcdAsio.def com um simbolo decorado. Ele casa - e como o DLL de hoje funciona -, mas e
// um casamento por aproximacao que ninguem pediu, num arquivo onde os quatro nomes
// exportados sao contrato com o Windows.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid,
                                                    void** ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = 0;

    if (rclsid != IID_ASIO_DRIVER)
        return CLASS_E_CLASSNOTAVAILABLE;
    if (riid != IID_IUnknown && riid != IID_IClassFactory)
        return E_NOINTERFACE;

    DriverClassFactory* factory = new (std::nothrow) DriverClassFactory();
    if (!factory)
        return E_OUTOFMEMORY;

    // Sem AddRef: a fabrica ja nasceu com a referencia que esta sendo entregue aqui.
    *ppv = static_cast<IClassFactory*>(factory);
    return S_OK;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    // S_OK aqui AUTORIZA o Windows a descarregar este DLL do processo do software de DJ.
    // Errar para o lado do S_OK derruba o processo do usuario na proxima chamada por um
    // ponteiro que ficou pendurado; errar para o lado do S_FALSE deixa um DLL carregado
    // sem uso, que custa memoria e nada mais. Os dois erros NAO tem o mesmo peso, e por
    // isso a condicao e conjuntiva e conservadora: os tres contadores em zero.
    if (bcd::comObjectsAlive() != 0 ||
        bcd::comFactoriesAlive() != 0 ||
        bcd::comServerLocks() != 0)
        return S_FALSE;
    return S_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ORDEM DE DESTRUICAO NO DESCARREGAMENTO - e por que este arquivo nao tem nenhuma.
//
// Nao ha um unico objeto global com destrutor aqui: os tres contadores sao LONG cru, e a
// fabrica e do monte. Isto e decisao, e nao acaso.
//
// O ledger deste projeto registra que a trava do log e VAZADA de proposito, porque um
// thread de audio ainda vivo usaria uma trava ja destruida. A mesma armadilha valeria em
// dobro aqui: um destrutor global roda no DLL_PROCESS_DETACH, ou seja DEPOIS de o Windows
// ja ter matado todos os threads do processo em qualquer ordem, ou (no caso de
// FreeLibrary) EM PARALELO com um thread de audio que este DLL ainda nao colheu. Um
// contador destruido nesse instante e lido por quem sobrou e um travamento no processo do
// usuario.
//
// Se algum dia alguem quiser um objeto global com estado aqui: nao destrua no detach.
// Vazar e a resposta certa, pelo mesmo motivo que ja esta escrito no motor de audio e na
// casca ASIO.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

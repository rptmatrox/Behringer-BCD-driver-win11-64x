#include "handoff.h"
#include "log.h"

#include <windows.h>

namespace bcd {

// UM slot por escopo, e os handles sao mantidos abertos pelo resto da vida do
// processo, DE PROPOSITO. Fecha-los em releaseDevice() destruiria os eventos (se
// fossemos o unico dono) e o pedido seguinte teria de recria-los - o que funciona,
// mas troca dois handles por chamadas de kernel em todo start()/stop() de audio.
//
// Manter aberto tambem e o que faz a recuperacao automatica funcionar: enquanto
// o processo vive, os eventos existem e o bridge le o estado deles; quando o
// processo morre - inclusive por queda -, o Windows fecha estes handles, os
// objetos sao destruidos porque ninguem mais os segura, e o OpenEvent do bridge
// passa a falhar, que ele le como "nenhum pedido". Nao existe caminho de limpeza
// para esquecer.
//
// Se o DLL for DESCARREGADO sem o processo morrer, os dois handles vazam e os
// eventos continuam existindo NAO sinalizados (o destrutor do driver ja chamou
// releaseDevice()). O bridge le isso como "nenhum pedido", que e o estado certo;
// o custo e dois handles presos ate o processo terminar. De proposito nao ha
// limpeza no DllMain: fechar handles durante o descarregamento, com um thread de
// audio possivelmente vivo, e trocar um vazamento inofensivo por um risco real.
//
// void* volatile em vez de HANDLE volatile para que &g_eventLocal seja exatamente
// o void* volatile* que as funcoes Interlocked pedem, sem cast.
static void* volatile g_eventLocal  = 0;
static void* volatile g_eventGlobal = 0;

// Mascara dos escopos que a ULTIMA linha de log declarou: bit 0 = local,
// bit 1 = Global; -1 = nada registrado ainda. Registrar so quando a mascara MUDA
// mantem o volume em uma linha por transicao real - e ainda assim registra se um
// escopo aparecer (ou desaparecer) depois, o que uma flag de "ja registrei"
// perderia.
static volatile long g_loggedScopes = -1;

struct HandoffEvents {
    HANDLE local;
    HANDLE global;
    DWORD  localErr;        // valido so quando o handle correspondente e 0
    DWORD  globalErr;
};

// Cria o evento de UM escopo na primeira chamada. Devolve 0 se nao houver como
// criar, com o codigo de erro em *errOut.
static HANDLE ensureOne(void* volatile* slot, const char* name, DWORD* errOut)
{
    *errOut = 0;

    // Leitura atomica e publicada do ponteiro. Um simples `*slot` bastaria em x64
    // com o /volatile:ms da build, mas a forma explicita nao depende disso.
    HANDLE h = (HANDLE)InterlockedCompareExchangePointer(slot, 0, 0);
    if (h)
        return h;

    // MANUAL RESET (segundo argumento TRUE), e isto e load-bearing: o bridge
    // consulta o pedido com WaitForSingleObject(h, 0) a cada volta do laco dele.
    // Um evento de reset automatico seria CONSUMIDO pela primeira consulta, e o
    // pedido duraria uma leitura so - o bridge soltaria o aparelho e voltaria a
    // pega-lo na volta seguinte, disputando com o driver a cada 200 ms.
    HANDLE created = CreateEventA(0, TRUE, FALSE, name);
    if (!created) {
        *errOut = GetLastError();
        return 0;
    }

    HANDLE prev = (HANDLE)InterlockedCompareExchangePointer(slot, created, 0);
    if (prev) {
        // Duas instancias do driver no MESMO processo criaram ao mesmo tempo. O
        // nome e o mesmo, logo o objeto do kernel tambem e - ficar com o da outra
        // e equivalente, e evita o vazamento de um handle por corrida.
        //
        // O que a atomicidade NAO resolve, e nao pretende: duas instancias
        // compartilham UM pedido, entao o releaseDevice() de uma zera o pedido da
        // outra. Inofensivo, porque as duas nao podem segurar o aparelho ao mesmo
        // tempo de qualquer forma - a segunda a chamar start() ja falharia no
        // device.open(). Fechar isso exigiria estado por instancia, e
        // releaseDevice() e chamado 2-3 vezes por instancia: o remedio custaria
        // mais que a doenca. Decisao consciente.
        CloseHandle(created);
        return prev;
    }

    return created;
}

// Garante os DOIS escopos e registra o resultado quando ele muda.
//
// Quando o Global nao existe, ele e tentado de novo em cada chamada. Isso e
// deliberado: e uma chamada de kernel por start() de audio - nunca no thread de
// audio -, e a alternativa (uma flag de "ja tentei") teria uma janela em que a
// flag esta posta e o handle ainda nao publicado, que e exatamente o defeito de
// double-checked locking que esta sessao ja consertou uma vez no log.
static HandoffEvents ensureEvents()
{
    HandoffEvents ev;

    // O LOCAL PRIMEIRO, e SEMPRE: ele nao depende de privilegio nenhum, e por isso
    // e o que garante o encontro com um bridge da mesma sessao, elevado ou nao.
    // O Global e um acrescimo - falhar nele nao e erro.
    ev.local  = ensureOne(&g_eventLocal,  kEventNameLocal, &ev.localErr);
    ev.global = ensureOne(&g_eventGlobal, kEventName,      &ev.globalErr);

    // O ESCOPO vai para o log de proposito, e esta linha JA provou seu valor: foi
    // ela que revelou, no hardware, que um software de DJ elevado cria no Global.
    // Se algum dia a passagem de bastao nao acontecer, ela diz onde procurar em
    // vez de deixar adivinhar.
    const long mask = (ev.local ? 1 : 0) | (ev.global ? 2 : 0);
    if (InterlockedExchange(&g_loggedScopes, mask) == mask)
        return ev;

    if (ev.local && ev.global)
        logWrite("bastao: evento criado em '%s' E em '%s' - o programa de controles "
                 "encontra o pedido em qualquer um dos dois",
                 kEventNameLocal, kEventName);
    else if (ev.local)
        logWrite("bastao: evento criado em '%s'; o escopo global ('%s') nao veio "
                 "(erro %lu), o que e NORMAL sem o privilegio "
                 "SeCreateGlobalPrivilege - o nome local basta para um programa de "
                 "controles na mesma sessao do Windows",
                 kEventNameLocal, kEventName, ev.globalErr);
    else if (ev.global)
        logWrite("bastao: evento criado APENAS em '%s' - o nome local ('%s') falhou "
                 "com erro %lu. Atencao: um programa de controles sem elevacao pode "
                 "nao conseguir abrir o objeto global",
                 kEventName, kEventNameLocal, ev.localErr);
    else
        // Nao e motivo para derrubar o audio: o resultado e o bridge nunca saber
        // que precisa soltar o aparelho, o que degrada para o comportamento
        // anterior a esta tarefa. Os DOIS nomes e os DOIS erros vao para a linha -
        // sem eles nao se sabe nem o que foi tentado.
        logWrite("bastao: nao consegui criar o evento em nenhum dos dois escopos "
                 "('%s' erro %lu; '%s' erro %lu) - o programa de controles nao vai "
                 "saber que precisa soltar o aparelho; o audio segue normalmente",
                 kEventNameLocal, ev.localErr, kEventName, ev.globalErr);

    return ev;
}

bool requestDevice()
{
    HandoffEvents ev = ensureEvents();
    if (!ev.local && !ev.global)
        return false;           // ensureEvents() ja registrou o motivo

    // Sinalizar TODOS os escopos que existirem, nunca so um: um nome existindo e
    // NAO sinalizado faria um bridge que olhasse justamente esse nome ler "nenhum
    // pedido" e continuar segurando o aparelho.
    bool anySet = false;
    if (ev.local) {
        if (SetEvent(ev.local))
            anySet = true;
        else
            logWrite("bastao: SetEvent falhou em '%s' (erro %lu)",
                     kEventNameLocal, GetLastError());
    }
    if (ev.global) {
        if (SetEvent(ev.global))
            anySet = true;
        else
            logWrite("bastao: SetEvent falhou em '%s' (erro %lu)",
                     kEventName, GetLastError());
    }

    if (!anySet) {
        logWrite("bastao: nenhum escopo pode ser sinalizado - o pedido nao foi "
                 "feito; o audio segue normalmente");
        return false;
    }

    logWrite("bastao: pedido do aparelho sinalizado");

    // NAO se dorme aqui. A espera pelo bridge fica no laco de tentativas de quem
    // chamou (ver bcdasio.cpp), que se auto-temporiza: sem bridge nenhum para
    // soltar o aparelho, dormir aqui atrasaria TODO start() de audio de graca.
    return true;
}

void releaseDevice()
{
    // So LE os slots - releaseDevice() nunca cria evento nenhum. Sem handle
    // nenhum, nunca pedimos nada e nao ha nada a devolver.
    HANDLE local  = (HANDLE)InterlockedCompareExchangePointer(&g_eventLocal, 0, 0);
    HANDLE global = (HANDLE)InterlockedCompareExchangePointer(&g_eventGlobal, 0, 0);
    if (!local && !global)
        return;

    // Registrar apenas quando havia pedido de fato. releaseDevice() e chamado no
    // stop(), no destrutor e no caminho de erro do start(), e disposeBuffers()
    // chama stop() de novo - sem esta guarda, uma parada normal deixaria tres
    // linhas iguais no log e a linha deixaria de ser marcador confiavel da
    // devolucao no teste de hardware.
    //
    // "Havia pedido" e um OU sobre os escopos - o MESMO OU que o lado Python faz:
    // basta um sinalizado para haver um bridge esperando por nos.
    bool haviaPedido = false;
    if (local && WaitForSingleObject(local, 0) == WAIT_OBJECT_0)
        haviaPedido = true;
    if (global && WaitForSingleObject(global, 0) == WAIT_OBJECT_0)
        haviaPedido = true;

    // Zerar TODOS. Um escopo que sobrasse sinalizado seguraria para sempre um
    // bridge que olhasse justamente aquele nome. ResetEvent e idempotente:
    // repetir e inofensivo.
    if (local)
        ResetEvent(local);
    if (global)
        ResetEvent(global);

    if (haviaPedido)
        logWrite("bastao: aparelho devolvido");
}

}

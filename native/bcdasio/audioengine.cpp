#include "audioengine.h"
#include "format.h"
#include "log.h"

#include <new>       // std::nothrow
#include <avrt.h>
#include <stdio.h>
#include <string.h>

namespace bcd {

AudioEngine::AudioEngine()
    : dev_(0), client_(0), blockFrames_(0),
      thread_(0), threadId_(0), threadStuck_(false),
      running_(false), stopRequested_(false), deviceLost_(false),
      outBuf_(0), outIsoch_(0),
      inBuf_(0), inIsoch_(0), inMaxPacket_(0), inXferBytes_(0), inOffsetLogged_(false),
      clientIn_(0), clientOut_(0),
      inBytesTotal_(0), pumpCount_(0), lastReportTick_(0), driftDrops_(0),
      framesPlayed_(0), underruns_(0), overruns_(0), inStarves_(0)
{
    err_[0] = 0;
    memset(outXfer_, 0, sizeof(outXfer_));
    memset(inXfer_, 0, sizeof(inXfer_));
    memset(inDesc_, 0, sizeof(inDesc_));
}

AudioEngine::~AudioEngine()
{
    stop();
}

void AudioEngine::fail(const char* what)
{
    _snprintf(err_, sizeof(err_) - 1, "%s (erro %lu)", what, GetLastError());
    err_[sizeof(err_) - 1] = 0;
    logWrite("motor: %s", err_);
}

bool AudioEngine::start(UsbDevice* dev, int blockFrames, EngineClient* client)
{
    if (running_)
        return true;

    // 1. REAVALIACAO SEM BLOQUEIO do thread que o stop() anterior desistiu de
    //    esperar. Vem antes da recusa de proposito: o thread quase sempre sai
    //    poucos milissegundos depois do timeout, porque device.close() faz as
    //    transferencias em voo falharem e o laco sai sozinho.
    //    E para poder fazer esta pergunta que o stop() NAO fecha o handle do thread
    //    no caminho do timeout - fechado, threadStuck_ seria irreversivel e um
    //    transiente de 3 s custaria o audio pelo resto da vida desta instancia do
    //    driver. A ponte MIDI ja aprendeu isso da forma dificil (ver midibridge.cpp).
    if (threadStuck_ && thread_ && WaitForSingleObject(thread_, 0) == WAIT_OBJECT_0) {
        CloseHandle(thread_);
        thread_      = 0;
        threadId_    = 0;
        threadStuck_ = false;
        logWrite("motor: o thread de audio travado saiu; o motor pode subir de novo "
                 "(os recursos vazados dele ficam vazados, como anunciado)");
    }

    // 2. RECUSA enquanto o thread anterior puder estar vivo. Ele ainda pode estar
    //    dentro do bufferSwitch do software de DJ e, ao voltar, escreve nos buffers
    //    isocronos, le os aneis e submete transferencias. Um segundo thread de audio
    //    sobre os MESMOS pipes e os MESMOS buffers entregaria audio embaralhado no
    //    melhor caso, e no pior os dois escreveriam a mesma regiao do buffer
    //    registrado enquanto o driver USB a le. E o mesmo desenho que a ponte MIDI
    //    recusa por escrito; recusar nao custa memoria nem corrompe nada, e o
    //    software de DJ ve um erro de ASIO honesto.
    if (threadStuck_) {
        strcpy(err_, "o thread de audio anterior nunca saiu - nao subo um segundo "
                     "sobre os mesmos pipes e os mesmos buffers");
        logWrite("motor: %s", err_);
        return false;
    }

    // 3. LIMPEZA DE CADAVER: o thread pode ter saido sozinho (cabo arrancado) e
    //    deixado para tras o handle dele e os streams montados. running_ ja e false,
    //    mas thread_ nao. Sem isto, o setupStreams() abaixo vazaria os eventos, o
    //    registro isocrono e os buffers da sessao anterior em TODO start() seguinte.
    //
    //    A ORDEM contra a reavaliacao acima e carregada: este stop() so e alcancado
    //    com threadStuck_ FALSO, porque a recusa logo acima devolve antes. Ou seja,
    //    ou o thread nunca travou, ou a reavaliacao acabou de provar que ele saiu.
    //    NUNCA se chama stop() com um thread travado ainda vivo - isso custaria
    //    outros 3 s de espera dentro do start() do ASIO, com o host bloqueado.
    if (thread_)
        stop();

    if (!dev || !dev->isOpen() || !client || blockFrames <= 0) {
        strcpy(err_, "start com parametros invalidos");
        logWrite("motor: %s", err_);
        return false;
    }

    dev_         = dev;
    client_      = client;
    blockFrames_ = blockFrames;
    err_[0]      = 0;
    deviceLost_  = false;
    framesPlayed_ = 0;
    underruns_   = 0;
    overruns_    = 0;
    inStarves_   = 0;
    inBytesTotal_   = 0;
    pumpCount_      = 0;
    lastReportTick_ = GetTickCount();
    driftDrops_ = 0;
    inOffsetLogged_ = false;

    if (!setupStreams()) {
        // O thread nem nasceu, entao o desmonte pode soltar tudo.
        teardownStreams(true);
        return false;
    }

    // --- pre-carga do anel de ENTRADA com silencio ---
    // O anel de entrada nasce vazio, e o priming de threadMain() enche apenas o
    // de SAIDA. Sem esta pre-carga, o nivel do anel de entrada so sobe pela
    // deriva de relogio acumulada (90 a 107 B/s medidos neste hardware), o que
    // leva dezenas de segundos; e enquanto o nivel medio for menor que um bloco
    // do host, a oscilacao natural do anel - o pump consome
    // blockFrames_*kBytesPerFrame de uma vez - faz pumpBlock encontrar dado
    // insuficiente com frequencia. Enchemos uma vez so, pelo mesmo principio do
    // priming da saida. Nao e latencia desperdicada: e o que compra as tres
    // melhoras abaixo. Para que a pre-carga sobrevivesse ate o primeiro bloco de
    // audio, o laco de aquecimento do anel de saida em threadMain() precisou
    // parar de consumi-la - ele chama pumpBlock(false) e entrega silencio ao
    // cliente sem ler este anel. Sobra um residuo, no LIMITE CONHECIDO no fim
    // deste comentario.
    //
    //  1. a entrada para de entregar blocos parcialmente mudos no comeco da
    //     sessao. Hoje passa em branco porque o VirtualDJ abre 0 canais de
    //     entrada, mas no primeiro uso real (gravacao, vinil, microfone) seria
    //     defeito de campo.
    //  2. inStarves_ deixa de contar transiente de partida. MEDIDO NO HARDWARE,
    //     num build SEM esta pre-carga (bloco de 512, ~65 s de motor):
    //     starve=4, e PAROU DE SUBIR. A fome nao e recorrente porque o nivel do
    //     anel sobe monotonicamente pela deriva; ela acaba sozinha quando o
    //     nivel medio passa de um bloco do host. O que esta escrito aqui antes
    //     era uma estimativa de "milhares de vezes e dezenas de linhas em toda
    //     sessao", e o log a refutou por tres ordens de grandeza - fica
    //     registrado para nao ser reintroduzida. O ganho real e menor do que
    //     aquela estimativa prometia: sao os 4 blocos de entrada incompletos da
    //     partida (o starve=4 medido) que vao a zero. Esse zero esta CONFIRMADO NO
    //     HARDWARE a 512 frames, em dois portoes independentes: no da Tarefa 8
    //     (inStarves=96, e os 96 cairam TODOS dentro de 4 ms no instante da
    //     desconexao do cabo - starve=0 em toda a operacao normal) e no da Tarefa
    //     10 (inStarves=0 nas duas execucoes limpas; as duas interrompidas por
    //     cabo arrancado deram 453 e 467, a mesma assinatura de desconexao). A
    //     1024 e 2048 o zero segue SIMULADO - o software de DJ usou 512, que e o
    //     preferredSize, em todas as execucoes medidas.
    //  3. getLatencies reporta o nivel de regime deste anel como latencia de
    //     entrada, e comecar cheio aproxima o anel desse nivel desde o primeiro
    //     bloco em vez de deixa-lo subir do zero. Esta e a justificativa que o
    //     MESMO log confirma: com o anel em 6.800-10.672 bytes e drift=0 aos
    //     65 s, o ponto de operacao (marca d'agua, 16.384) so seria alcancado
    //     depois de ~2 min, e ate la o numero informado ao host era falso.
    //     A correcao de deriva tambem passa a agir mais cedo; os tempos estao na
    //     tabela do LIMITE CONHECIDO, no fim deste comentario.
    //
    // AQUI, e nao dentro de setupStreams(), porque este e o unico ponto em que
    // as duas condicoes sao verificaveis lendo poucas linhas: o anel acabou de
    // ser criado e zerado por setupStreams(), e o thread de audio - o unico
    // outro que toca inRing_, por pumpBlock e handleInDone - so nasce no
    // CreateThread logo abaixo. A pre-carga tambem e estado inicial de sessao,
    // como os contadores zerados acima, e nao alocacao de recurso.
    //
    // Reinicio nao acumula: setupStreams() chama inRing_.init(), que zera
    // head_/tail_ antes de qualquer outra coisa, e teardownStreams() ainda faz
    // inRing_.reset(). Dois mecanismos independentes.
    //
    // LIMITE CONHECIDO QUE SOBRA, medido por simulacao evento a evento do
    // transiente (nao no hardware), com deriva de 100 B/s:
    //
    // O laco de aquecimento de threadMain() nao consome mais este anel - ele
    // chama pumpBlock(false) -, mas a PRIMEIRA RECOMPOSICAO do anel de saida
    // ainda consome. Ela roda ao concluir a primeira transferencia de saida, uns
    // 10 ms depois da partida, e vem antes da primeira conclusao de ENTRADA
    // porque WaitForMultipleObjects devolve o menor indice sinalizado e os
    // eventos de saida ocupam os primeiros. Nesse instante nenhum byte capturado
    // entrou ainda e pumpBlock acopla as duas direcoes 1:1, entao o enchimento do
    // pipeline de saida sai da pre-carga - uma vez por sessao:
    //     bloco  256: 5 pumps = 10240 bytes (29,0 ms), MAIS que a pre-carga
    //     bloco  512: 3 pumps = 12288 bytes (34,8 ms), sobram  4096
    //     bloco 1024: 1 pump  =  8192 bytes (23,2 ms), sobram 24576
    //     bloco 2048: 1 pump  = 16384 bytes (46,4 ms), sobram 49152
    //
    // As duas consequencias que ficam, ambas ACEITAS de proposito:
    //  a) o anel comeca a sessao abaixo da marca d'agua e sobe ate ela pela
    //     propria deriva de relogio. Quem dispara a correcao e o PICO da
    //     oscilacao, nao o nivel medio - a guarda de handleInDone e
    //     used() > marca -, e a amplitude da oscilacao e um bloco do host. Logo
    //     o que conta e a distancia do pico a marca, e o pico na partida e o
    //     residuo mais um bloco do host:
    //       bloco  256: residuo 0     -> pico ~2048  -> faltam ~6144 B (~62 s)
    //       bloco  512: residuo 4096  -> pico ~8192  -> faltam ~8192 B (~89 s)
    //       blocos 1024 e 2048: o residuo fica exatamente UM BLOCO DO HOST
    //         abaixo da marca (24576 de 32768; 49152 de 65536). Como a amplitude
    //         da oscilacao e justamente um bloco do host, o pico cruza a marca
    //         quase de imediato: a correcao de deriva engata em SEGUNDOS, nao em
    //         dezenas de segundos.
    //     Os tempos entre parenteses sao da simulacao (100 B/s); a conta
    //     deficit/deriva com a faixa medida de 90 a 107 B/s da 57-68 s e
    //     77-91 s, que os contem. A versao anterior deste comentario dizia
    //     "~48/49 s" para 1024 E para 2048, e "~213 s" para os dois casos sem
    //     pre-carga: impossivel nos dois pares, porque com a mesma deriva o
    //     deficit de 2048 e o dobro do de 1024 e os tempos teriam de diferir 2:1.
    //     O unico numero desta secao CONFIRMADO NO HARDWARE e o de 512 frames
    //     SEM pre-carga, ~124 s: o log ao vivo de um build sem pre-carga marcava
    //     inRing=6800..10672 com drift=0 aos 65 s de motor. O pico previsto para
    //     t=0 e um bloco do host (4096) e o medido aos 65 s bate com 4096 mais
    //     65 s a ~100 B/s; faltavam 5712 B, que a ~100 B/s levam 57 s, ou seja
    //     ~122 s no total. E a unica validacao independente do modelo acima.
    //     Nessa janela getLatencies SOBRENOTIFICA a latencia de entrada: informa
    //     a marca d'agua mais meio bloco do host e o anel ainda nao chegou la.
    //     A 512 frames, contra o nivel do anel logo depois da primeira
    //     recomposicao (4096 B), o excesso e 18432-4096 = 14336 B = 1792 frames
    //     = 40,6 ms. Esse e o MAXIMO, no instante inicial; o nivel sobe
    //     linearmente ate a marca, onde sobra so o residuo permanente de meio
    //     bloco (2048 B, 5,8 ms), entao a MEDIA do transiente e ~8192 B
    //     (23,2 ms). Os dois numeros importam por motivos diferentes: 23 ms soa
    //     aceitavel e 40,6 ms e quase um bloco inteiro do host.
    //     Nao existe "lado seguro" nisso, e o ASIO nao define nenhum: o host
    //     desloca o material gravado pelo inputLatency informado, entao
    //     sobrenotificar ADIANTA o material pelo excesso exatamente como
    //     subnotificar o atrasa. O erro e em modulo, nos dois sentidos, e
    //     desalinhamento e desalinhamento. E em regime o numero nao e
    //     "verdadeiro": e ESTIMADO, e ainda oscila +-meio bloco do host em torno
    //     da estimativa. A medicao definitiva e por LOOPBACK (cabo da saida
    //     ligado na entrada), pendente no passo 2.4.
    //  b) so no bloco de 256 frames a pre-carga (8192 bytes) e menor que essa
    //     primeira recomposicao (10240), e sobram 3 blocos de entrada com cauda
    //     muda - 2712 bytes, 7,7 ms de silencio no total. O PRIMEIRO deles esta
    //     INTEIRAMENTE mudo, e nao parcialmente: faltaram 2048 de 2048 bytes,
    //     porque a pre-carga acaba exatamente no quarto dos cinco pumps. Os tres
    //     tambem nao estao todos na partida imediata: caem em t~10 ms, t~20 ms e
    //     t~130 ms. A simulacao independente do revisor reproduziu o total em
    //     2720 bytes - um frame de diferenca. Nos blocos de 512, 1024 e 2048 a
    //     fome desaparece por completo (starve=0 em 600 s simulados).
    //
    // NAO fechar isso somando o enchimento do pipeline a pre-carga: a 256 frames
    // a pre-carga iria a ~96% da capacidade do anel na partida, trocando fome por
    // risco de transbordo, e quebraria a invariante - travada por teste de
    // unidade - de que a marca d'agua fica em 25% da capacidade em todo tamanho
    // de bloco. O preco de a) e b) e um transiente de dezenas de segundos uma vez
    // por sessao; o preco da "correcao" seria estrutural.
    const int primeTarget = inHighWaterBytes(blockFrames_);
    const int primed      = primeRingWithSilence(inRing_, primeTarget);
    // UMA linha, nos dois desfechos. Duas linhas para o mesmo evento fazem quem
    // le o log procurar dois eventos; o caso incompleto so acrescenta o aviso.
    logWrite("motor: entrada pre-carregada com %d bytes de silencio "
             "(marca d'agua %d, anel de %d bytes)%s",
             primed, primeTarget, inRing_.capacity(),
             primed < primeTarget
                 ? " - INCOMPLETA, o anel nao comeca no ponto de operacao" : "");

    stopRequested_ = false;
    running_       = true;
    thread_ = CreateThread(0, 0, &AudioEngine::threadEntry, this, 0, 0);
    if (!thread_) {
        fail("CreateThread falhou");
        running_ = false;
        teardownStreams(true);   // idem: nao ha thread para respeitar
        return false;
    }

    logWrite("motor: iniciado (bloco do host = %d frames)", blockFrames_);
    return true;
}

void AudioEngine::stop()
{
    if (!running_ && !thread_)
        return;

    // Protecao contra o thread de audio pedir a propria parada: esperar pelo
    // proprio handle nunca sinaliza, e o teardown rodaria por cima de uma pilha
    // ainda ativa. Nesse caso so pedimos a saida e voltamos; quem realmente
    // desmonta e o dono, do thread dele.
    if (threadId_ != 0 && GetCurrentThreadId() == threadId_) {
        stopRequested_ = true;
        logWrite("motor: stop() chamado DE DENTRO do thread de audio - "
                 "apenas sinalizado, sem desmontar (veja onDeviceLost)");
        return;
    }

    stopRequested_ = true;

    bool threadGone = true;
    if (thread_) {
        if (WaitForSingleObject(thread_, 3000) == WAIT_OBJECT_0) {
            CloseHandle(thread_);
            thread_ = 0;
        } else {
            threadGone = false;
            // NEM o handle do thread e fechado aqui, de proposito: ele e a UNICA
            // forma de descobrir depois que o thread saiu, e sem ele a recusa do
            // start() seguinte seria permanente. Fechar troca um transiente de 3 s
            // pela perda do audio pelo resto da vida desta instancia.
            logWrite("motor: thread de audio nao saiu em 3 s - o handle do thread "
                     "fica de proposito e o start() seguinte reavalia");
        }
    }

    // threadId_ so volta a zero com o thread comprovadamente morto. Com ele vivo,
    // manter o valor e LOAD-BEARING: e por threadId_ que a guarda de auto-parada la
    // em cima reconhece o proprio thread de audio pedindo a parada. Zerado, uma
    // chamada vinda de dentro dele passaria pela guarda e o desmonte rodaria por
    // cima de uma pilha ainda ativa.
    if (threadGone)
        threadId_ = 0;
    running_ = false;

    // threadStuck_ e FUNCAO da espera, e nao um bit que so se liga: um stop()
    // ANTERIOR pode ter desistido de esperar e a espera DESTA chamada acabou de
    // colher o thread. Sem zerar aqui, o estado travado sobreviveria a coleta - e o
    // stop() duplo e ROTINEIRO nesta casca (disposeBuffers() chama stop(), e o
    // destrutor chama os dois). Foi exatamente por aqui que a irreversibilidade
    // voltou uma vez na ponte MIDI, por via rotineira em vez de patologica.
    threadStuck_ = !threadGone;

    teardownStreams(threadGone);
    logWrite("motor: parado (underruns=%u overruns=%u inStarves=%u drift=%lld "
             "frames=%llu)",
             underruns_, overruns_, inStarves_, driftDrops_,
             (unsigned long long)framesPlayed_);
}

// Abortar NAO e sincrono nem bloqueante: manda o Windows resolver o que estiver em
// voo nos dois endpoints e volta. Quem espera e o chamador, se quiser esperar.
void AudioEngine::abortPipes()
{
    if (!dev_ || !dev_->isOpen())
        return;   // aparelho ja fechado: o Windows ja resolveu a I/O pendente
    const DeviceProfile& p = dev_->profile();
    WinUsb_AbortPipe(dev_->playbackIf(), p.epPlayback);
    WinUsb_AbortPipe(dev_->captureIf(), p.epCapture);
}

// Cancela as transferencias ainda em voo e espera cada uma terminar.
// Obrigatorio ANTES de fechar eventos, desregistrar buffers isocronos ou
// liberar memoria: o thread sai do laco com todas as transferencias
// recem-submetidas, e mexer em qualquer um desses recursos enquanto o driver
// USB ainda pode usa-los e corrupcao de heap.
// Informa POR DIRECAO se tudo drenou. O que nao drenou nao pode ser tocado.
void AudioEngine::drainPending(bool* outDrained, bool* inDrained)
{
    *outDrained = true;
    *inDrained  = true;

    if (!dev_ || !dev_->isOpen())
        return;   // aparelho ja fechado: o Windows ja resolveu a I/O pendente

    abortPipes();

    // Esperar TODAS de uma vez, com um unico timeout de 1 s para o conjunto —
    // e nao 1 s por transferencia. Com 6 em voo, esperar uma por uma daria ate
    // 6 s no pior caso, e o pior caso e exatamente o cabo ser arrancado
    // tocando: o software de DJ congelaria por 6 s na cara do usuario, o que
    // viola o critério de aceite "desconectar nao trava".
    HANDLE pend[kOutXfers + kInXfers];
    int n = 0;
    for (int i = 0; i < kOutXfers; i++)
        if (outXfer_[i].pending && outXfer_[i].ev)
            pend[n++] = outXfer_[i].ev;
    for (int i = 0; i < kInXfers; i++)
        if (inXfer_[i].pending && inXfer_[i].ev)
            pend[n++] = inXfer_[i].ev;

    if (n > 0)
        WaitForMultipleObjects(n, pend, TRUE, 1000);

    // Dar baixa so em quem realmente sinalizou. Um timeout coletivo pode ter
    // deixado parte concluida e parte nao.
    for (int i = 0; i < kOutXfers; i++) {
        if (!outXfer_[i].pending || !outXfer_[i].ev)
            continue;
        if (WaitForSingleObject(outXfer_[i].ev, 0) == WAIT_OBJECT_0) {
            DWORD got = 0;
            WinUsb_GetOverlappedResult(dev_->playbackIf(), &outXfer_[i].ovl, &got, FALSE);
            outXfer_[i].pending = false;
        } else {
            logWrite("motor: transferencia de saida %d nao terminou apos abort", i);
            *outDrained = false;
        }
    }
    for (int i = 0; i < kInXfers; i++) {
        if (!inXfer_[i].pending || !inXfer_[i].ev)
            continue;
        if (WaitForSingleObject(inXfer_[i].ev, 0) == WAIT_OBJECT_0) {
            DWORD got = 0;
            WinUsb_GetOverlappedResult(dev_->captureIf(), &inXfer_[i].ovl, &got, FALSE);
            inXfer_[i].pending = false;
        } else {
            logWrite("motor: transferencia de entrada %d nao terminou apos abort", i);
            *inDrained = false;
        }
    }
}

bool AudioEngine::setupStreams()
{
    // Endpoints, alternate settings e indices de interface vem do PERFIL do
    // aparelho (ver a tabela no topo de usbdev.cpp). Para a BCD3000 sao os mesmos
    // valores das constantes que existiam em usbdev.h - 0x02, 0x83, alt 1 e alt 0 -,
    // e por isso este caminho nao muda em nada para ela. Para um perfil
    // experimental, os dois `if` de falha abaixo sao onde a suposicao sobre o
    // alternate setting aparece como erro legivel em vez de silencio.
    const DeviceProfile& prof = dev_->profile();

    // --- descobrir o tamanho de pacote de cada endpoint ---
    PipeDesc pipes[8];
    int outMaxPacket = 0;
    int n = dev_->queryPipes(dev_->playbackIf(), prof.altStreaming, pipes, 8);
    for (int i = 0; i < n; i++)
        if (pipes[i].id == prof.epPlayback)
            outMaxPacket = pipes[i].maxPacketSize;

    inMaxPacket_ = 0;
    n = dev_->queryPipes(dev_->captureIf(), prof.altStreaming, pipes, 8);
    for (int i = 0; i < n; i++)
        if (pipes[i].id == prof.epCapture)
            inMaxPacket_ = pipes[i].maxPacketSize;

    if (outMaxPacket <= 0 || inMaxPacket_ <= 0) {
        _snprintf(err_, sizeof(err_) - 1,
                  "endpoints de audio nao encontrados no alternate setting %u",
                  prof.altStreaming);
        err_[sizeof(err_) - 1] = 0;
        logWrite("motor: %s", err_);
        return false;
    }
    if (blockBytesFor(outMaxPacket) != kBlockBytes) {
        _snprintf(err_, sizeof(err_) - 1,
                  "wMaxPacketSize=%d incompativel com o bloco de 10 ms", outMaxPacket);
        err_[sizeof(err_) - 1] = 0;
        logWrite("motor: %s", err_);
        return false;
    }
    inXferBytes_ = kUsbFramesPerBlock * inMaxPacket_;
    logWrite("motor: pacotes out=%d in=%d, bloco out=%d bytes, bloco in=%d bytes",
             outMaxPacket, inMaxPacket_, kBlockBytes, inXferBytes_);

    // --- ligar o streaming ---
    if (!dev_->setAlternate(dev_->playbackIf(), prof.altStreaming)) {
        strncpy(err_, dev_->lastError(), sizeof(err_) - 1);
        err_[sizeof(err_) - 1] = 0;
        logWrite("motor: nao consegui ligar o streaming de saida - %s", err_);
        return false;
    }
    if (!dev_->setAlternate(dev_->captureIf(), prof.altStreaming)) {
        strncpy(err_, dev_->lastError(), sizeof(err_) - 1);
        err_[sizeof(err_) - 1] = 0;
        logWrite("motor: nao consegui ligar o streaming de entrada - %s", err_);
        return false;
    }

    // --- buffers ---
    // Folga de 8 blocos ASIO em cada anel: absorve a diferenca entre o bloco
    // do host e o bloco de 10 ms do USB com margem larga. A conta vive em
    // ringBytesFor() (audioengine.h) para o teste unitario medir a mesma coisa
    // que o motor usa.
    const int ringBytes = ringBytesFor(blockFrames_);
    if (!outRing_.init(ringBytes) || !inRing_.init(ringBytes)) {
        strcpy(err_, "sem memoria para os buffers circulares");
        logWrite("motor: %s", err_);
        return false;
    }

    clientIn_  = new (std::nothrow) short[blockFrames_ * kChannels];
    clientOut_ = new (std::nothrow) short[blockFrames_ * kChannels];
    if (!clientIn_ || !clientOut_) {
        strcpy(err_, "sem memoria para os blocos do cliente");
        logWrite("motor: %s", err_);
        return false;
    }
    memset(clientIn_, 0, blockFrames_ * kChannels * sizeof(short));
    memset(clientOut_, 0, blockFrames_ * kChannels * sizeof(short));

    outBuf_ = new (std::nothrow) unsigned char[kOutXfers * kBlockBytes];
    inBuf_  = new (std::nothrow) unsigned char[kInXfers * inXferBytes_];
    if (!outBuf_ || !inBuf_) {
        strcpy(err_, "sem memoria para os buffers isocronos");
        logWrite("motor: %s", err_);
        return false;
    }
    memset(outBuf_, 0, kOutXfers * kBlockBytes);
    memset(inBuf_, 0, kInXfers * inXferBytes_);

    if (!WinUsb_RegisterIsochBuffer(dev_->playbackIf(), prof.epPlayback,
                                    outBuf_, kOutXfers * kBlockBytes, &outIsoch_)) {
        fail("RegisterIsochBuffer da saida falhou");
        return false;
    }
    if (!WinUsb_RegisterIsochBuffer(dev_->captureIf(), prof.epCapture,
                                    inBuf_, kInXfers * inXferBytes_, &inIsoch_)) {
        fail("RegisterIsochBuffer da entrada falhou");
        return false;
    }

    // Sem checar CreateEvent, um handle nulo no array de espera apareceria
    // depois como "aparelho sumiu" em vez de erro de inicializacao.
    for (int i = 0; i < kOutXfers; i++) {
        outXfer_[i].ev = CreateEventA(0, TRUE, FALSE, 0);
        if (!outXfer_[i].ev) {
            fail("CreateEvent da saida falhou");
            return false;
        }
        outXfer_[i].offset = (unsigned)(i * kBlockBytes);
        outXfer_[i].pending = false;
    }
    for (int i = 0; i < kInXfers; i++) {
        inXfer_[i].ev = CreateEventA(0, TRUE, FALSE, 0);
        if (!inXfer_[i].ev) {
            fail("CreateEvent da entrada falhou");
            return false;
        }
        inXfer_[i].offset = (unsigned)(i * inXferBytes_);
        inXfer_[i].pending = false;
    }
    return true;
}

void AudioEngine::teardownStreams(bool threadGone)
{
    // 0. O PRIMEIRO USUARIO DOS RECURSOS A CONSIDERAR E O NOSSO PROPRIO THREAD, e
    //    nao o driver USB. Com threadGone falso, o thread de audio nao foi colhido:
    //    ele pode estar parado dentro do bufferSwitch do software de DJ e, quando
    //    voltar, faz memset(clientOut_), le os aneis, escreve em outBuf_ + offset e
    //    submete transferencias. Liberar qualquer um desses recursos aqui e
    //    corrupcao de memoria DENTRO DO PROCESSO DO SOFTWARE DE DJ.
    //
    //    O criterio e o mesmo que este arquivo ja aplica por direcao mais abaixo, e
    //    que a ponte MIDI aplica no estado do rele: VAZAR em vez de liberar recurso
    //    em uso. Alguns kilobytes e um punhado de handles ate o fim do processo sao
    //    estritamente melhores que uma escrita em memoria reciclada.
    if (!threadGone) {
        // Abortar ajuda o thread a sair (as transferencias em voo se resolvem) e
        // NAO espera por ele - esperar aqui seria pagar o timeout duas vezes.
        abortPipes();
        logWrite("motor: vazando de proposito TODO o estado dos streams (%d+%d "
                 "transferencias, %d eventos, os dois registros isocronos, os dois "
                 "buffers isocronos e os dois blocos do cliente) - o thread de audio "
                 "nao foi colhido e pode voltar a escrever em qualquer um deles",
                 kOutXfers, kInXfers, kOutXfers + kInXfers);
        // Os ponteiros tambem NAO sao zerados, e isso importa tanto quanto nao
        // liberar: submitOut escreve em outBuf_ + offset e pumpBlock faz
        // memset(clientOut_), entao um ponteiro NULO aqui viraria escrita no
        // endereco do offset - trocaria vazamento por corrupcao. O estado fica
        // INTEIRO, e o setupStreams() de um start() futuro escreve por cima; esse
        // start() so acontece com o thread comprovadamente morto, porque start()
        // recusa enquanto threadStuck_ valer.
        //
        // Nem o alternate setting nem os aneis sao tocados, pelo mesmo motivo: o
        // thread vivo usa os dois, e reset() nos aneis embaixo de uma leitura em
        // curso desalinharia os canais dele. O stopRequested_ que o stop() acabou
        // de ligar e o que tira o thread do laco na primeira volta apos destravar.
        return;
    }

    // 1. Cancelar e drenar o que esta em voo, ANTES de tocar em qualquer
    //    recurso que o driver USB ainda possa estar usando.
    bool outDrained = true;
    bool inDrained  = true;
    drainPending(&outDrained, &inDrained);

    // 2. Soltar cada direcao SO se ela drenou. Evento, registro do buffer
    //    isocrono e memoria formam um conjunto indivisivel: se sobrou uma
    //    transferencia viva, nenhum dos tres pode ser tocado - desregistrar o
    //    buffer ou fechar o evento que um OVERLAPPED vivo referencia e tao
    //    ruim quanto liberar a memoria. Vazar alguns kilobytes e tres handles
    //    ate o fim do processo e estritamente melhor que corromper o heap.
    if (outDrained) {
        for (int i = 0; i < kOutXfers; i++)
            if (outXfer_[i].ev)
                CloseHandle(outXfer_[i].ev);
        if (outIsoch_)
            WinUsb_UnregisterIsochBuffer(outIsoch_);
        delete[] outBuf_;
    } else {
        logWrite("motor: vazando de proposito os recursos de SAIDA - havia "
                 "transferencia sem terminar; solta-los seria corromper memoria");
    }
    for (int i = 0; i < kOutXfers; i++) { outXfer_[i].ev = 0; outXfer_[i].pending = false; }
    outIsoch_ = 0;
    outBuf_   = 0;

    if (inDrained) {
        for (int i = 0; i < kInXfers; i++)
            if (inXfer_[i].ev)
                CloseHandle(inXfer_[i].ev);
        if (inIsoch_)
            WinUsb_UnregisterIsochBuffer(inIsoch_);
        delete[] inBuf_;
    } else {
        logWrite("motor: vazando de proposito os recursos de ENTRADA - havia "
                 "transferencia sem terminar; solta-los seria corromper memoria");
    }
    for (int i = 0; i < kInXfers; i++) { inXfer_[i].ev = 0; inXfer_[i].pending = false; }
    inIsoch_ = 0;
    inBuf_   = 0;

    // 3. Desligar o streaming no aparelho.
    if (dev_ && dev_->isOpen()) {
        const DeviceProfile& prof = dev_->profile();
        dev_->setAlternate(dev_->playbackIf(), prof.altIdle);
        dev_->setAlternate(dev_->captureIf(), prof.altIdle);
    }

    // 4. Estes dois nunca sao tocados pelo driver USB - so pelo NOSSO thread. Por
    //    isso nao entram na conta por direcao do passo 2: nenhum drainPending
    //    protege contra o nosso proprio thread. Quem protege e a guarda do passo 0,
    //    que devolve antes de chegar aqui quando o thread nao foi colhido. Dizer
    //    apenas "o driver USB nao toca neles" era verdadeiro e IRRELEVANTE, e foi
    //    essa meia-verdade que justificou por um tempo liberar os dois com o thread
    //    possivelmente vivo.
    delete[] clientIn_;  clientIn_  = 0;
    delete[] clientOut_; clientOut_ = 0;

    // Simetria de estado: os aneis voltam a vazios como todo o resto.
    outRing_.reset();
    inRing_.reset();
}

// Pede um bloco ao cliente e o deposita no anel de saida.
//
// consumeInput=false serve a UM unico chamador: o laco de aquecimento do anel
// de saida em threadMain(). Ali o cliente e chamado varias vezes antes de a
// primeira transferencia de ENTRADA ter sido sequer submetida, e ler o anel de
// entrada nesse momento gastaria a pre-carga de silencio que start() acabou de
// depositar - a latencia de entrada inteira, torrada para encher a saida.
//
// A troca e invisivel para o caminho de audio, e a razao e simples: naquele
// instante o anel de entrada contem EXATAMENTE a pre-carga, que e silencio, e
// nada mais (nenhuma transferencia de entrada chegou ainda). Ler dele ou
// receber um bloco zerado entrega ao cliente os mesmos bytes. O que muda e so
// o que fica no anel depois. A equivalencia esta travada por teste de unidade
// em test_pre_carga_da_entrada.
//
// Dois efeitos colaterais que essa passagem NAO pode ter, e nao tem:
//  - fome: sem leitura nao ha pedido de dado, logo inStarves_ nao e tocado.
//    Contar fome aqui poluiria o contador que existe para acusar defeito;
//  - desalinhamento de frame: este caminho nao le nem descarta nada do anel,
//    entao os ponteiros nem se movem. O alinhamento de 8 bytes do anel de
//    entrada e invariante do motor - quebra-lo rotaciona os canais de forma
//    permanente -, e nao consumir e estritamente mais seguro que consumir.
void AudioEngine::pumpBlock(bool consumeInput)
{
    pumpCount_++;
    const int inBytes  = blockFrames_ * kBytesPerFrame;
    const int outBytes = inBytes;

    // 1. entrada -> bloco do cliente (silencio se faltar dado)
    // A cauda muda e o espelho do underrun na saida, e precisa do mesmo
    // contador: sem ele, blocos de entrada parcialmente mudos nao deixariam
    // rastro nenhum no log. Desde a pre-carga de silencio feita em start(), o
    // anel de entrada ja comeca perto do ponto de operacao, entao fome aqui
    // deixou de ser ruido de partida e voltou a ser sinal de defeito - e por
    // isso o limite de log continua baixo de proposito.
    if (consumeInput) {
        int got = inRing_.read(clientIn_, inBytes);
        if (got < inBytes) {
            memset((unsigned char*)clientIn_ + got, 0, inBytes - got);
            inStarves_++;
            if (inStarves_ <= 20 || (inStarves_ % 100) == 0)
                logWrite("motor: fome na entrada #%u (faltaram %d bytes)",
                         inStarves_, inBytes - got);
        }
    } else {
        // Zerar o bloco INTEIRO, e nao so uma cauda: clientIn_ e reusado entre
        // chamadas, e sobra da chamada anterior sairia como audio fantasma.
        // inBytes e o tamanho exato da alocacao feita em setupStreams()
        // (blockFrames_ * kChannels shorts = blockFrames_ * kBytesPerFrame).
        memset(clientIn_, 0, inBytes);
    }

    // 2. o cliente consome a entrada e preenche a saida
    memset(clientOut_, 0, outBytes);
    client_->onBlock(clientIn_, clientOut_, blockFrames_);

    // 3. saida -> anel
    outRing_.write(clientOut_, outBytes);
}

bool AudioEngine::submitOut(int i, bool continueStream)
{
    Xfer& x = outXfer_[i];

    // Encher a regiao deste transfer a partir do anel.
    unsigned char* dst = outBuf_ + x.offset;
    int got = outRing_.read(dst, kBlockBytes);
    if (got < kBlockBytes) {
        memset(dst + got, 0, kBlockBytes - got);
        underruns_++;
        if (underruns_ <= 20 || (underruns_ % 100) == 0)
            logWrite("motor: underrun #%u (faltaram %d bytes)", underruns_, kBlockBytes - got);
    }

    memset(&x.ovl, 0, sizeof(x.ovl));
    x.ovl.hEvent = x.ev;
    ResetEvent(x.ev);

    BOOL ok = WinUsb_WriteIsochPipeAsap(outIsoch_, x.offset, kBlockBytes,
                                        continueStream ? TRUE : FALSE, &x.ovl);
    DWORD e = GetLastError();
    // Quirk conhecido: em iso ASAP o retorno vem falso com ERROR_IO_PENDING.
    if (!ok && e != ERROR_IO_PENDING) {
        fail("WriteIsochPipeAsap falhou");
        return false;
    }
    x.pending = true;
    return true;
}

bool AudioEngine::submitIn(int i, bool continueStream)
{
    Xfer& x = inXfer_[i];

    memset(&x.ovl, 0, sizeof(x.ovl));
    x.ovl.hEvent = x.ev;
    ResetEvent(x.ev);
    memset(inDesc_[i], 0, sizeof(inDesc_[i]));

    BOOL ok = WinUsb_ReadIsochPipeAsap(inIsoch_, x.offset, inXferBytes_,
                                       continueStream ? TRUE : FALSE,
                                       kUsbFramesPerBlock, inDesc_[i], &x.ovl);
    DWORD e = GetLastError();
    if (!ok && e != ERROR_IO_PENDING) {
        fail("ReadIsochPipeAsap falhou");
        return false;
    }
    x.pending = true;
    return true;
}

void AudioEngine::handleOutDone(int i)
{
    Xfer& x = outXfer_[i];
    DWORD got = 0;
    // O numero de bytes vem zerado em iso ASAP; o que vale e o tamanho submetido.
    WinUsb_GetOverlappedResult(dev_->playbackIf(), &x.ovl, &got, FALSE);
    x.pending = false;
    framesPlayed_ += kFramesPerBlock;
}

void AudioEngine::handleInDone(int i)
{
    Xfer& x = inXfer_[i];
    DWORD got = 0;
    WinUsb_GetOverlappedResult(dev_->captureIf(), &x.ovl, &got, FALSE);
    x.pending = false;

    // Os descritores dizem onde e quanto chegou em cada pacote. CONFIRMADO no
    // hardware em 2026-07-28: o campo Offset e relativo a TRANSFERENCIA, nao
    // ao buffer registrado. Evidencia no log: "transfer de entrada em offset
    // 3600 -> desc[0].Offset=0". Por isso o ajuste abaixo e obrigatorio; sem
    // ele a captura leria da posicao errada do buffer.
    if (!inOffsetLogged_ && x.offset != 0) {
        logWrite("motor: transfer de entrada em offset %u -> desc[0].Offset=%lu "
                 "desc[0].Length=%lu",
                 x.offset,
                 (unsigned long)inDesc_[i][0].Offset,
                 (unsigned long)inDesc_[i][0].Length);
        inOffsetLogged_ = true;
    }

    // --- correcao de deriva de relogio ---
    // O cristal do aparelho e o do PC nao marcam o mesmo tempo. Medido nesta
    // maquina: o aparelho entrega de 11 a 13 frames/s a mais do que consumimos
    // (~0,03%), o que enchia o anel de entrada e o fazia transbordar depois de
    // ~12 minutos de uso continuo. Confirmado por dois caminhos independentes:
    // 726,7 s para encher 65536 bytes = 90,2 B/s; e a instrumentacao mediu ate
    // 107 B/s. Os 90 a 107 B/s sao exatamente os 11 a 13 frames/s de 8 bytes.
    //
    // Descartamos UM frame (uma amostra) por vez, sempre que o anel passa da
    // marca d'agua - ou seja, de 11 a 13 descartes por segundo, cada um de 23
    // microssegundos de audio. Isso e muito menos audivel que descartar 44
    // amostras de uma vez, que produziria um clique periodico na gravacao; mas
    // um descarte de amostra e uma descontinuidade de forma de onda, e a
    // duracao curta nao prova inaudibilidade. VALIDACAO AUDITIVA PENDENTE no
    // passo 2.4: o teste de 900 s rodou com o host em in=0, sem ninguem escutar
    // nem gravar o fluxo de entrada.
    const int highWater = inHighWaterBytes(blockFrames_);
    if (inRing_.used() > highWater) {
        inRing_.discard(kBytesPerFrame);
        driftDrops_++;
        if (driftDrops_ == 1 || (driftDrops_ % 5000) == 0)
            logWrite("motor: correcao de deriva #%llu (anel em %d de %d bytes)",
                     (unsigned long long)driftDrops_, inRing_.used(),
                     inRing_.capacity());
    }

    for (int p = 0; p < kUsbFramesPerBlock; p++) {
        ULONG len = inDesc_[i][p].Length;
        if (len == 0)
            continue;

        // Descartar qualquer sobra que nao complete um frame de audio.
        len -= (len % (ULONG)kBytesPerFrame);
        if (len == 0)
            continue;

        ULONG off = inDesc_[i][p].Offset;
        // Aceitar tanto offset relativo ao buffer quanto ao transfer.
        if (off < x.offset)
            off += x.offset;

        // Os descritores vem do driver: tratamos como entrada nao confiavel.
        // A checagem e feita SEM somar off + len, porque essa soma pode dar a
        // volta em 32 bits: um off corrompido perto de 0xFFFFFFFF somado a len
        // viraria um numero pequeno, passaria na checagem ingenua, e o codigo
        // leria memoria a gigabytes de distancia — despejando lixo no fluxo de
        // audio como se fosse som capturado.
        const ULONG limit = (ULONG)(kInXfers * inXferBytes_);
        if (off >= limit || len > limit - off)
            continue;   // fora do buffer: descarta em vez de ler memoria alheia

        if (inRing_.space() < (int)len) {
            // Diagnostico: registrar o estado do anel no momento exato do
            // estouro. Se used() estiver perto da capacidade, o anel encheu
            // de verdade; se nao estiver, o problema e outro.
            const int usedAntes  = inRing_.used();
            const int spaceAntes = inRing_.space();
            inRing_.discard((int)len);   // abre espaco jogando fora o mais antigo
            overruns_++;
            if (overruns_ <= 20 || (overruns_ % 100) == 0)
                logWrite("motor: overrun de entrada #%u (anel used=%d space=%d cap=%d len=%lu)",
                         overruns_, usedAntes, spaceAntes, inRing_.capacity(),
                         (unsigned long)len);
        }
        // Contar o que REALMENTE entrou no anel, e nao o que foi oferecido:
        // o valor bate hoje porque o tratamento de overrun acima garante o
        // espaco, mas depender de uma invariante que vive em outro `if` faria
        // o numero mentir no dia em que aquele `if` mudasse.
        inBytesTotal_ += inRing_.write(inBuf_ + off, (int)len);
    }
}

DWORD WINAPI AudioEngine::threadEntry(void* self)
{
    ((AudioEngine*)self)->threadMain();
    return 0;
}

void AudioEngine::threadMain()
{
    threadId_ = GetCurrentThreadId();

    // Prioridade de audio profissional; se o servico MMCSS nao atender,
    // cair para prioridade de thread alta.
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    if (!mmcss)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Encher o anel de saida antes de comecar, para nao iniciar em silencio.
    //
    // pumpBlock(false): este laco NAO consome o anel de entrada. Nenhuma
    // transferencia de entrada foi submetida ainda - isso acontece logo abaixo -,
    // entao o anel de entrada contem apenas a pre-carga de silencio de start().
    // Consumi-lo aqui jogaria a latencia de entrada inteira dentro do anel de
    // saida antes do primeiro bloco de audio. O cliente recebe os mesmos bytes
    // de um jeito ou do outro (zeros); a diferenca e so a pre-carga sobreviver.
    while (outRing_.space() >= blockFrames_ * kBytesPerFrame &&
           outRing_.used() < kOutXfers * kBlockBytes)
        pumpBlock(false);

    bool ok = true;
    for (int i = 0; i < kOutXfers && ok; i++)
        ok = submitOut(i, i != 0);
    for (int i = 0; i < kInXfers && ok; i++)
        ok = submitIn(i, i != 0);

    HANDLE events[kOutXfers + kInXfers];
    for (int i = 0; i < kOutXfers; i++)
        events[i] = outXfer_[i].ev;
    for (int i = 0; i < kInXfers; i++)
        events[kOutXfers + i] = inXfer_[i].ev;

    while (ok && !stopRequested_) {
        DWORD r = WaitForMultipleObjects(kOutXfers + kInXfers, events, FALSE, 1000);

        if (r == WAIT_TIMEOUT) {
            logWrite("motor: 1 s sem nenhuma transferencia concluida - aparelho sumiu?");
            deviceLost_ = true;
            break;
        }
        if (r < WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + kOutXfers + kInXfers) {
            fail("WaitForMultipleObjects falhou");
            deviceLost_ = true;
            break;
        }

        // Relatorio periodico de diagnostico. Cinco segundos e raro o
        // bastante para nao pesar no caminho de tempo real e frequente o
        // bastante para mostrar a tendencia dos aneis.
        const DWORD agora = GetTickCount();
        if (agora - lastReportTick_ >= 5000) {
            const DWORD dt = agora - lastReportTick_;
            lastReportTick_ = agora;
            logWrite("motor: estado dt=%lums inRing=%d/%d outRing=%d/%d "
                     "pumps=%lld inBytes=%lld frames=%llu under=%u over=%u "
                     "starve=%u drift=%lld",
                     (unsigned long)dt,
                     inRing_.used(), inRing_.capacity(),
                     outRing_.used(), outRing_.capacity(),
                     pumpCount_, inBytesTotal_,
                     (unsigned long long)framesPlayed_,
                     underruns_, overruns_, inStarves_, driftDrops_);
        }

        int idx = (int)(r - WAIT_OBJECT_0);
        if (idx < kOutXfers) {
            handleOutDone(idx);
            // Manter o anel abastecido antes de resubmeter. Aqui o audio ja
            // esta correndo nas duas direcoes, entao pumpBlock CONSOME o anel de
            // entrada: e este o caminho normal, o que entrega ao cliente o som
            // realmente capturado pelo aparelho.
            while (outRing_.used() < kOutXfers * kBlockBytes &&
                   outRing_.space() >= blockFrames_ * kBytesPerFrame)
                pumpBlock(true);
            ok = submitOut(idx, true);
            if (!ok) {
                // Falha isolada: recompoe o stream em vez de desistir.
                logWrite("motor: recompondo o stream de saida");
                ok = submitOut(idx, false);
            }
        } else {
            int i = idx - kOutXfers;
            handleInDone(i);
            ok = submitIn(i, true);
            if (!ok) {
                logWrite("motor: recompondo o stream de entrada");
                ok = submitIn(i, false);
            }
        }
    }

    if (!ok && !stopRequested_)
        deviceLost_ = true;

    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);

    running_ = false;

    // Avisar o dono que o aparelho sumiu, para ele nao ficar esperando som
    // que nunca vem. Num stop pedido pelo usuario isso nao acontece.
    if (deviceLost_ && !stopRequested_ && client_)
        client_->onDeviceLost();
}

}

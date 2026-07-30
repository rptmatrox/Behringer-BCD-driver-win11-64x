#include "midibridge.h"
#include "log.h"

#include <new>       // std::nothrow
#include <stdio.h>
#include <string.h>

namespace bcd {

//----------------------------------------------------------------------
// RelayLink - o canal local, lado cliente.
//----------------------------------------------------------------------
RelayLink::RelayLink()
    : pipe_(INVALID_HANDLE_VALUE), rxEvent_(0), txEvent_(0),
      rxPending_(false), txStuck_(false)
{
    memset(&ovRx_, 0, sizeof(ovRx_));
    memset(&ovTx_, 0, sizeof(ovTx_));
    memset(rxBuf_, 0, sizeof(rxBuf_));
}

RelayLink::~RelayLink()
{
    // Se close() nao conseguir resolver a I/O pendente, nada e liberado - nem os
    // eventos. Vazar handles e infinitamente melhor que deixar o kernel escrever
    // num OVERLAPPED que acabou de ser apagado. E o chamador nao deveria ter
    // chegado aqui: o contrato da classe e nao destruir sem close() == true.
    if (!close())
        return;
    if (rxEvent_) {
        CloseHandle(rxEvent_);
        rxEvent_ = 0;
    }
    if (txEvent_) {
        CloseHandle(txEvent_);
        txEvent_ = 0;
    }
}

bool RelayLink::ensureEvents(DWORD* errOut)
{
    // MANUAL RESET nos dois: quem colhe e o WaitForSingleObject(evento, 0) do laco
    // da ponte, que pode acontecer VOLTAS DEPOIS de a operacao terminar. Um evento
    // de reset automatico seria consumido pelo WaitForMultipleObjects e a colheita
    // seguinte veria "nada pronto" com o dado ja na mao.
    if (!rxEvent_) {
        rxEvent_ = CreateEventA(0, TRUE, FALSE, 0);
        if (!rxEvent_) {
            *errOut = GetLastError();
            return false;
        }
    }
    if (!txEvent_) {
        txEvent_ = CreateEventA(0, TRUE, FALSE, 0);
        if (!txEvent_) {
            *errOut = GetLastError();
            return false;
        }
    }
    return true;
}

bool RelayLink::connect(DWORD* errOut)
{
    *errOut = 0;
    if (pipe_ != INVALID_HANDLE_VALUE)
        return true;
    if (!ensureEvents(errOut))
        return false;

    // SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION e endurecimento de verdade,
    // nao enfeite: sem isso, um programa qualquer da MESMA conta que criasse este
    // nome ANTES do bridge poderia IMPERSONAR este processo - que roda dentro do
    // software de DJ e, nesta maquina, roda ELEVADO. Com SECURITY_IDENTIFICATION o
    // servidor consegue apenas identificar o cliente, nunca agir como ele. Custa
    // uma flag. Os dados em si sao 4 bytes de estado de botao, mas o token nao.
    HANDLE h = CreateFileW(kRelayPipeName,
                           GENERIC_READ | GENERIC_WRITE,
                           0, 0, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT |
                           SECURITY_IDENTIFICATION,
                           0);
    if (h == INVALID_HANDLE_VALUE) {
        *errOut = GetLastError();
        return false;
    }

    // O modo de leitura do CLIENTE nasce em bytes, mesmo quando o servidor criou o
    // canal em modo mensagem - e isto e load-bearing. Em modo de bytes uma leitura
    // pode devolver 1, 2 ou 3 bytes de um pacote de 4, e voltaria a existir a
    // necessidade de acumular e reenquadrar, que e exatamente o que o tamanho fixo
    // de 4 bytes existe para eliminar.
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(h, &mode, 0, 0)) {
        *errOut = GetLastError();
        CloseHandle(h);
        return false;
    }

    pipe_ = h;
    return true;
}

bool RelayLink::close()
{
    bool clean = true;

    if (rxPending_ && pipe_ != INVALID_HANDLE_VALUE) {
        // Cancelar NAO e sincrono: CancelIoEx devolve antes de o kernel soltar a
        // operacao. Esperar pelo EVENTO (e nao por GetOverlappedResult) e o que
        // funciona tambem quando o handle ja esta a caminho de morrer.
        CancelIoEx(pipe_, &ovRx_);
        if (WaitForSingleObject(rxEvent_, kRelayDrainMs) == WAIT_OBJECT_0)
            rxPending_ = false;
        else
            clean = false;
    }

    if (txStuck_) {
        // Uma escrita cancelada de uma chamada anterior que nao assentou. Dar-lhe
        // mais uma chance aqui, porque o prazo pode ter estourado por disputa e
        // nao por defeito.
        if (WaitForSingleObject(txEvent_, kRelayDrainMs) == WAIT_OBJECT_0)
            txStuck_ = false;
        else
            clean = false;
    }

    if (!clean)
        return false;      // handle NAO fechado de proposito: ha I/O em voo

    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    return true;
}

void RelayLink::abandon()
{
    // Sem condicao nenhuma, de proposito: este e o caminho em que close() JA
    // devolveu false. O raciocinio inteiro esta no header, e o resumo e que quem
    // protege os OVERLAPPED e a memoria vazada, nao o handle - e que um handle
    // deixado aberto trava o servidor do programa de controles (nMaxInstances = 1,
    // escrita bloqueante) em vez de custar apenas 464 bytes.
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool RelayLink::send(const unsigned char* data, DWORD bytes, HANDLE stopEvent,
                     DWORD* errOut)
{
    *errOut = 0;
    if (pipe_ == INVALID_HANDLE_VALUE) {
        *errOut = ERROR_INVALID_HANDLE;
        return false;
    }
    if (txStuck_) {
        *errOut = ERROR_IO_PENDING;
        return false;
    }
    if (!data || bytes == 0 || (bytes % (DWORD)kRelayPacketBytes) != 0) {
        *errOut = ERROR_INVALID_PARAMETER;
        return false;
    }

    ResetEvent(txEvent_);
    memset(&ovTx_, 0, sizeof(ovTx_));
    ovTx_.hEvent = txEvent_;

    DWORD written = 0;
    if (WriteFile(pipe_, data, bytes, &written, &ovTx_)) {
        if (written == bytes)
            return true;
        // Escrita curta no caminho SINCRONO. Inalcancavel em modo mensagem - o
        // servidor recebe a mensagem inteira ou nao a recebe -, mas devolver false
        // com *errOut em 0 faria o log da ponte dizer "erro 0", que e o pior
        // diagnostico possivel: parece defeito no log em vez de no canal.
        *errOut = ERROR_WRITE_FAULT;
        return false;
    }

    const DWORD e = GetLastError();
    if (e != ERROR_IO_PENDING) {
        *errOut = e;
        return false;
    }

    // Esperar COM PRAZO, e tambem pelo evento de parada. E aqui que o modo de
    // falha "escrita no canal bloqueando" e fechado: o thread que le o USB nao
    // pode ficar preso escrevendo se o outro lado estiver lento ou morto.
    HANDLE hs[2];
    DWORD  n = 0;
    hs[n++] = txEvent_;
    if (stopEvent)
        hs[n++] = stopEvent;

    const DWORD w = WaitForMultipleObjects(n, hs, FALSE, kRelayWriteMs);
    if (w == WAIT_OBJECT_0) {
        if (GetOverlappedResult(pipe_, &ovTx_, &written, FALSE) && written == bytes)
            return true;
        *errOut = GetLastError();
        return false;
    }

    // Prazo estourado, parada pedida, ou falha da espera. A escrita TEM de ser
    // resolvida antes de devolver: ovTx_ e membro e sera reusado na chamada
    // seguinte, e o kernel pode escrever nele a qualquer momento.
    CancelIoEx(pipe_, &ovTx_);
    if (WaitForSingleObject(txEvent_, kRelayDrainMs) != WAIT_OBJECT_0)
        txStuck_ = true;

    *errOut = (w == WAIT_TIMEOUT) ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
    return false;
}

bool RelayLink::armRead(DWORD* errOut)
{
    *errOut = 0;
    if (pipe_ == INVALID_HANDLE_VALUE) {
        *errOut = ERROR_INVALID_HANDLE;
        return false;
    }
    if (rxPending_)
        return true;

    ResetEvent(rxEvent_);
    memset(&ovRx_, 0, sizeof(ovRx_));
    ovRx_.hEvent = rxEvent_;

    DWORD got = 0;
    if (ReadFile(pipe_, rxBuf_, (DWORD)kRelayReadBufBytes, &got, &ovRx_)) {
        // Completou na hora. O evento e sinalizado A MAO em vez de se confiar em
        // que o Windows o sinalize no caminho sincrono: assim o invariante "se
        // armRead devolveu true, ou a leitura esta pendente ou o evento esta
        // sinalizado" vale por construcao, e quem colhe e SEMPRE o finishRead.
        // Evento de reset manual: sinalizar duas vezes e inofensivo.
        SetEvent(rxEvent_);
        rxPending_ = true;
        return true;
    }

    const DWORD e = GetLastError();
    if (e == ERROR_IO_PENDING) {
        rxPending_ = true;
        return true;
    }
    *errOut = e;
    return false;
}

int RelayLink::finishRead(DWORD* errOut)
{
    *errOut = 0;
    if (!rxPending_)
        return 0;

    DWORD got = 0;
    // bWait = TRUE nao espera nada aqui: so se chega neste ponto com o evento
    // sinalizado, ou seja com a operacao concluida. O TRUE existe para o caso de
    // um chamador futuro colher antes da hora - melhor esperar que ler lixo.
    const BOOL ok = GetOverlappedResult(pipe_, &ovRx_, &got, TRUE);
    rxPending_ = false;
    if (!ok) {
        *errOut = GetLastError();
        return -1;
    }
    // Truncar na fronteira do pacote. Em modo mensagem isto NAO dessincroniza
    // nada: o resto de uma mensagem mal formada morre com ela, e a mensagem
    // seguinte chega inteira.
    return (int)(got - (got % (DWORD)kRelayPacketBytes));
}

//----------------------------------------------------------------------
// Estado de trabalho do thread da ponte.
//
// Vive no HEAP, e nao na pilha do thread, por um motivo unico e decisivo: se uma
// operacao cancelada nao assentar no prazo, este bloco tem de poder ser VAZADO.
// Na pilha isso e impossivel - o quadro morre quando o thread devolve, e o driver
// USB passaria a escrever em memoria reciclada. E o mesmo criterio que o motor de
// audio ja aplica em drainPending: vazar em vez de liberar recurso em uso.
struct RelayWorker {
    RelayLink     link;
    OVERLAPPED    ovUsb;
    HANDLE        usbEvent;
    bool          usbPending;
    unsigned char usbBuf[64];

    RelayWorker() : usbEvent(0), usbPending(false)
    {
        memset(&ovUsb, 0, sizeof(ovUsb));
        memset(usbBuf, 0, sizeof(usbBuf));
    }
};

//----------------------------------------------------------------------
// A regra e UMA linha, e o valor dela e ser a UNICA: qualquer lugar que precise
// decidir se um modelo tem controles suportados chama isto. Ver o header.
bool bridgeSupportsProfile(const DeviceProfile& profile)
{
    return profile.controlProtocol == kCtrlUsbMidi10;
}

//----------------------------------------------------------------------
MidiBridge::MidiBridge()
    : dev_(0), thread_(0), stopEvent_(0),
      running_(false), stopRequested_(false), threadStuck_(false),
      packets_(0), leds_(0), ledErrors_(0), dropped_(0), connects_(0)
{
    err_[0] = 0;
}

MidiBridge::~MidiBridge()
{
    stop();
}

bool MidiBridge::start(UsbDevice* dev)
{
    if (running_)
        return true;

    // Reavaliacao SEM BLOQUEIO do thread que o stop() anterior desistiu de
    // esperar. Vem antes da recusa de proposito: o thread quase sempre sai poucos
    // milissegundos depois do timeout, porque device.close() zera o midi_ e a
    // leitura pendente falha, tirando o laco do ar.
    // E para poder fazer esta pergunta que o stop() NAO fecha o handle do thread
    // no caminho do timeout - fechado, threadStuck_ seria irreversivel e um
    // transiente de 2 s custaria os botoes do controlador pelo resto da vida desta
    // instancia do driver.
    if (threadStuck_ && thread_ && WaitForSingleObject(thread_, 0) == WAIT_OBJECT_0) {
        CloseHandle(thread_);
        thread_ = 0;
        if (stopEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = 0;
        }
        threadStuck_ = false;
        // Com o thread morto, dev_ nao serve mais a nada - nem a repeticao do
        // AbortPipe, que era o motivo de mante-lo - e volta a zero, deixando o
        // objeto no mesmo estado do caminho normal do stop().
        dev_ = 0;
        logWrite("midi: o thread travado saiu; a ponte pode subir de novo");
    }

    // Um thread que nao saiu no prazo do stop() anterior pode ainda estar dentro
    // da leitura do endpoint dos controles. Subir outro criaria DOIS consumidores
    // independentes disputando o mesmo pipe: cada pacote de 4 bytes iria para um
    // dos dois de forma imprevisivel, e as mensagens do controlador chegariam ao
    // software de DJ fora de ordem ou pela metade.
    // NAO e proibicao do WinUSB - ele aceita varias transferencias no mesmo pipe,
    // e o proprio motor de audio mantem tres; este mesmo EP 0x81 ja e tocado por
    // dois threads (a leitura aqui, o AbortPipe no thread do host), o que e
    // correto e intencional. O que nao serve e o DESENHO de dois leitores
    // concorrentes. Recusar e a saida segura, e ela nao custa o audio.
    if (threadStuck_) {
        strcpy(err_, "o thread da ponte MIDI anterior nunca saiu - "
                     "nao subo um segundo leitor no mesmo endpoint");
        logWrite("midi: %s", err_);
        return false;
    }

    // O thread pode ter saido sozinho (aparelho arrancado) deixando para tras o
    // handle dele e o evento de parada: running_ ja e false, mas os dois membros
    // nao. Sem limpar o cadaver, vazariamos os dois em todo start() seguinte.
    //
    // A ORDEM contra a reavaliacao acima e carregada: este stop() so e alcancado
    // com threadStuck_ FALSO, porque a recusa logo acima devolve false antes. Ou
    // seja, ou o thread nunca travou, ou a reavaliacao acabou de provar que ele
    // saiu. Nunca se chama stop() com um thread travado ainda vivo - se isso
    // acontecesse, o stop() esperaria os 2 s de novo dentro do start() do ASIO,
    // com o host esperando.
    if (thread_ || stopEvent_)
        stop();

    if (!dev || !dev->isOpen()) {
        strcpy(err_, "start da ponte MIDI sem aparelho aberto");
        logWrite("midi: %s", err_);
        return false;
    }

    // GUARDA POR MODELO, e ela vem ANTES da checagem da IF3 de proposito: o
    // enquadramento dos controles e propriedade do MODELO e nao da interface, entao
    // ter a IF3 na mao nao muda nada quando o protocolo dela nao e o que esta ponte
    // repassa.
    //
    // Sem esta guarda, uma BCD2000 subiria o thread e ele leria o EP 0x81 tratando
    // bytes de enquadramento proprietario como pacotes USB-MIDI de 4 bytes - o que
    // atravessaria o canal e chegaria a porta MIDI virtual do usuario como
    // mensagens ALEATORIAS, e voltaria como escrita de LED igualmente aleatoria. Um
    // defeito silencioso e confuso no lugar de uma linha de log que diz o que falta.
    //
    // O AUDIO SEGUE. Falha de MIDI nunca derruba o audio - regra estabelecida deste
    // projeto, registrada no ledger, e quem chama (BcdAsioDriver::start) apenas
    // registra lastError() e continua.
    const DeviceProfile& prof = dev->profile();
    if (!bridgeSupportsProfile(prof)) {
        _snprintf(err_, sizeof(err_) - 1,
                  "modelo %s: o protocolo dos controles dele e %s, e esta ponte so "
                  "sabe repassar %s - o audio funciona, os controles nao",
                  prof.model, controlProtocolName(prof.controlProtocol),
                  controlProtocolName(kCtrlUsbMidi10));
        err_[sizeof(err_) - 1] = 0;
        logWrite("midi: %s", err_);
        return false;
    }

    // A falta da IF3 nao impede o audio, entao open() segue adiante e e AQUI que
    // ela e tratada - com mensagem clara e sem tocar no aparelho.
    if (!dev->midiIf()) {
        strcpy(err_, "aparelho aberto sem a interface MIDI (IF3) - "
                     "o audio funciona, os controles nao");
        logWrite("midi: %s", err_);
        return false;
    }

    dev_       = dev;
    err_[0]    = 0;
    packets_   = 0;
    leds_      = 0;
    ledErrors_ = 0;
    dropped_   = 0;
    connects_  = 0;

    // MANUAL RESET: o thread testa o evento em varios pontos do laco (na espera
    // principal e dentro da escrita no canal), e um evento de reset automatico
    // seria consumido pelo primeiro deles.
    stopEvent_ = CreateEventA(0, TRUE, FALSE, 0);
    if (!stopEvent_) {
        _snprintf(err_, sizeof(err_) - 1,
                  "CreateEvent de parada da ponte MIDI falhou (erro %lu)",
                  GetLastError());
        err_[sizeof(err_) - 1] = 0;
        logWrite("midi: %s", err_);
        return false;
    }

    stopRequested_ = false;
    running_       = true;
    thread_ = CreateThread(0, 0, &MidiBridge::threadEntry, this, 0, 0);
    if (!thread_) {
        _snprintf(err_, sizeof(err_) - 1, "CreateThread da ponte MIDI falhou (erro %lu)",
                  GetLastError());
        err_[sizeof(err_) - 1] = 0;
        logWrite("midi: %s", err_);
        running_ = false;
        CloseHandle(stopEvent_);
        stopEvent_ = 0;
        return false;
    }

    // NAO se espera pela conexao com o bridge aqui. Isto roda dentro do start() do
    // ASIO, com o host bloqueado, e a conexao pode simplesmente nao existir (o
    // bridge parado). O thread tenta na primeira volta e a cada kRelayRetryMs
    // depois, e o log diz o que aconteceu.
    logWrite("midi: ponte iniciada (canal '%ls' com o programa de controles)",
             kRelayPipeName);
    return true;
}

void MidiBridge::stop()
{
    if (!running_ && !thread_ && !stopEvent_)
        return;

    stopRequested_ = true;
    if (stopEvent_)
        SetEvent(stopEvent_);

    // Abortar a leitura pendente dos controles. O prazo de 300 ms do pipe ja a
    // limitaria, mas abortar encurta a saida do thread - e o evento de parada
    // sozinho nao a encurta, porque o thread ainda tem de resolver a leitura em
    // voo antes de devolver.
    if (dev_ && dev_->isOpen() && dev_->midiIf())
        WinUsb_AbortPipe(dev_->midiIf(), dev_->profile().epControls);

    bool threadGone = true;
    if (thread_) {
        if (WaitForSingleObject(thread_, kStopWaitMs) == WAIT_OBJECT_0) {
            CloseHandle(thread_);
            thread_ = 0;
        } else {
            threadGone = false;
            // Nem o handle do thread NEM o evento de parada sao fechados aqui, de
            // proposito: o handle e a UNICA forma de descobrir depois que o thread
            // saiu, e o evento e aquilo em que ele pode estar esperando. Fechar
            // qualquer um dos dois trocaria um transiente de 2 s pela perda dos
            // botoes do controlador pelo resto da vida desta instancia.
            logWrite("midi: thread nao saiu em %u ms - dev_, o evento de parada e o "
                     "handle do thread ficam de proposito; o start() seguinte "
                     "reavalia", kStopWaitMs);
        }
    }
    running_ = false;

    // dev_ so volta a zero com o thread comprovadamente morto. Com o thread vivo,
    // manter apontado e LOAD-BEARING para o stop() SEGUINTE: o AbortPipe la em
    // cima esta sob `if (dev_ && ...)`, e repetir esse abort e o que pode tirar o
    // thread de dentro da leitura. O thread em si nao depende mais deste membro:
    // ele tira uma copia local na entrada de threadMain.
    if (threadGone) {
        dev_ = 0;
        if (stopEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = 0;
        }
        // threadStuck_ e FUNCAO de threadGone, e nao um bit que so se liga: um
        // stop() ANTERIOR pode ter desistido de esperar, e a espera desta chamada
        // acabou de colher o thread. Sem zerar aqui, o estado travado sobreviveria
        // a coleta com thread_ == 0 - e a reavaliacao do start() exige o handle
        // para provar a morte, entao a ponte nunca mais subiria. O caminho e real:
        // a casca ASIO chama stop() no stop() do ASIO E no destrutor.
        threadStuck_ = false;
    } else {
        threadStuck_ = true;
    }

    // ledErrors e semCanal saem junto de leds de proposito: sem eles, uma sessao em
    // que NENHUM LED funcionou (aparelho sem IF3, EP 0x01 recusando, bridge
    // parado) e indistinguivel de uma em que o software de DJ nao mandou LED
    // nenhum - as tres dao leds=0. E e justamente essa distincao que o portao de
    // hardware precisa ler.
    logWrite("midi: ponte parada (pacotes=%u leds=%u ledErrors=%u semCanal=%u "
             "conexoes=%u)", packets_, leds_, ledErrors_, dropped_, connects_);
}

DWORD WINAPI MidiBridge::threadEntry(void* self)
{
    ((MidiBridge*)self)->threadMain();
    return 0;
}

void MidiBridge::threadMain()
{
    // UMA copia de cada coisa partilhada, para a vida toda do thread. dev_ nao
    // pode ser nulo: start() recusa aparelho nulo ou fechado e escreve dev_ ANTES
    // do CreateThread. E o thread nunca troca de aparelho - enquanto ele roda,
    // start() devolve na primeira linha.
    UsbDevice* const dev  = dev_;
    HANDLE     const stop = stopEvent_;
    // Os dois endpoints vem do perfil do aparelho, e a referencia aponta para dado
    // estatico de leitura (a tabela em usbdev.cpp) - nao ha ciclo de vida a
    // administrar aqui. Para a BCD3000 sao 0x81 e 0x01, os mesmos valores das
    // constantes que existiam em usbdev.h. E start() ja recusou todo modelo cujo
    // protocolo de controles nao seja o USB-MIDI 1.0, entao este thread nunca roda
    // sobre um enquadramento que ele nao saiba ler.
    const DeviceProfile& prof = dev->profile();

    RelayWorker* const w = new (std::nothrow) RelayWorker();
    if (!w) {
        logWrite("midi: sem memoria para o estado da ponte - ponte encerrada");
        running_ = false;
        return;
    }

    w->usbEvent = CreateEventA(0, TRUE, FALSE, 0);
    if (!w->usbEvent) {
        logWrite("midi: CreateEvent da leitura dos controles falhou (erro %lu) - "
                 "ponte encerrada", GetLastError());
        delete w;
        running_ = false;
        return;
    }

    // Prazo da LEITURA dos controles: sem ele a leitura pendente pode nunca
    // completar com o aparelho quieto, e a saida passaria a depender so do
    // AbortPipe.
    ULONG timeout = kCtrlReadTimeoutMs;
    if (!WinUsb_SetPipePolicy(dev->midiIf(), prof.epControls, PIPE_TRANSFER_TIMEOUT,
                              sizeof(timeout), &timeout))
        logWrite("midi: SetPipePolicy(controles) falhou (erro %lu) - a leitura pode "
                 "bloquear sem prazo e a saida dependera do AbortPipe",
                 GetLastError());

    // Prazo da ESCRITA de LED. Novo, e nao e enfeite: a escrita agora roda no
    // MESMO thread que le os controles, entao uma escrita sem prazo nao atrasaria
    // apenas o LED - pararia os controles.
    timeout = kLedWriteTimeoutMs;
    if (!WinUsb_SetPipePolicy(dev->midiIf(), prof.epLeds, PIPE_TRANSFER_TIMEOUT,
                              sizeof(timeout), &timeout))
        logWrite("midi: SetPipePolicy(LEDs) falhou (erro %lu) - uma escrita de LED "
                 "sem prazo pode atrasar a leitura dos controles",
                 GetLastError());

    DWORD    nextConnect = GetTickCount();
    unsigned tentativas  = 0;      // tentativas de conexao desde o start()
    bool     semCanalRegistrado = false;

    while (!stopRequested_) {
        // Passo do recuo graduado, recalculado a cada volta porque ele tambem e o
        // prazo da espera abaixo: um passo de 100 ms com espera de 1000 ms nao
        // adiantaria nada - a volta seguinte so chegaria depois do segundo inteiro.
        const DWORD retryMs = (tentativas < kRelayFirstTries) ? kRelayFirstRetryMs
                                                              : kRelayRetryMs;

        // ---- 1. conectar ao programa de controles, se preciso ----
        //
        // Nunca se desiste. O bridge pode nao estar rodando quando o audio liga, e
        // pode subir depois: sem repeticao, o MIDI ficaria morto pelo resto da
        // sessao e o usuario nao teria como recuperar sem parar o audio.
        if (!w->link.isConnected() &&
            (long)(GetTickCount() - nextConnect) >= 0) {
            DWORD e = 0;
            tentativas++;
            if (w->link.connect(&e)) {
                connects_++;
                semCanalRegistrado = false;
                logWrite("midi: canal '%ls' conectado (conexao #%u, tentativa %u)",
                         kRelayPipeName, connects_, tentativas);
            } else if (!semCanalRegistrado) {
                // UMA linha por interrupcao de canal, e nao uma por tentativa: a
                // tentativa se repete pelo resto da sessao.
                semCanalRegistrado = true;
                logWrite("midi: sem canal para o programa de controles (erro %lu) - "
                         "o audio segue normalmente e eu retento a cada %u ms (as "
                         "primeiras %u tentativas) e a cada %u ms depois. Se o "
                         "BCD3000Bridge.exe estiver parado, os controles ficam mudos "
                         "ate ele subir", e, kRelayFirstRetryMs, kRelayFirstTries,
                         kRelayRetryMs);
            }
            // O passo do PROXIMO agendamento sai do contador JA incrementado, entao
            // as kRelayFirstTries primeiras tentativas ficam de fato espacadas pelo
            // passo curto.
            nextConnect = GetTickCount() +
                          ((tentativas < kRelayFirstTries) ? kRelayFirstRetryMs
                                                           : kRelayRetryMs);
        }

        // ---- 2. deixar uma leitura do canal armada (LEDs vindos do host) ----
        if (w->link.isConnected() && !w->link.readPending()) {
            DWORD e = 0;
            if (!w->link.armRead(&e)) {
                logWrite("midi: canal quebrou ao armar a leitura (erro %lu) - "
                         "reconectando", e);
                if (!w->link.close())
                    break;      // I/O nao resolvida: sair vazando, ver a saida
            }
        }

        // ---- 3. deixar uma leitura do USB armada (controles) ----
        if (!w->usbPending) {
            ResetEvent(w->usbEvent);
            memset(&w->ovUsb, 0, sizeof(w->ovUsb));
            w->ovUsb.hEvent = w->usbEvent;
            DWORD got = 0;
            if (WinUsb_ReadPipe(dev->midiIf(), prof.epControls, w->usbBuf,
                                (ULONG)sizeof(w->usbBuf), &got, &w->ovUsb)) {
                // Completou na hora. Mesmo truque do armRead: sinalizar a mao e
                // deixar a colheita para o passo 5, que e o unico lugar que trata
                // dado de controle. Sem isto, uma rajada de controles nunca cairia
                // na espera e o sentido dos LEDs passaria fome.
                SetEvent(w->usbEvent);
                w->usbPending = true;
            } else {
                const DWORD e = GetLastError();
                if (e == ERROR_IO_PENDING) {
                    w->usbPending = true;
                } else if (e != ERROR_SEM_TIMEOUT && e != ERROR_OPERATION_ABORTED) {
                    logWrite("midi: leitura dos controles falhou (erro %lu)", e);
                    break;
                }
                // Falha sincrona por prazo ou aborto: NAO se repete na hora. Cair
                // na espera abaixo e o que impede um laco quente.
            }
        }

        // ---- 4. esperar por algo ----
        HANDLE hs[3];
        DWORD  n = 0;
        hs[n++] = stop;
        if (w->usbPending)
            hs[n++] = w->usbEvent;
        if (w->link.readPending())
            hs[n++] = w->link.readEvent();

        // O prazo da espera acompanha o recuo graduado enquanto nao ha canal. Com o
        // canal de pe ele volta a ser o de regime: nao ha nada a reagendar, e a volta
        // acontece por evento e nao por prazo.
        const DWORD waited = WaitForMultipleObjects(
            n, hs, FALSE, w->link.isConnected() ? kRelayRetryMs : retryMs);
        if (waited == WAIT_OBJECT_0)
            break;                       // parada pedida
        if (waited == WAIT_FAILED) {
            logWrite("midi: WaitForMultipleObjects falhou (erro %lu)", GetLastError());
            break;
        }

        // ---- 5. colher o que ficou pronto ----
        //
        // Os DOIS sentidos sao consultados em toda volta, com espera de tempo zero,
        // em vez de se despachar pelo indice que o Wait devolveu. O Wait devolve o
        // MENOR indice sinalizado, entao despachar por ele daria prioridade
        // permanente aos controles e os LEDs passariam fome sob rajada. Os eventos
        // sao de reset manual justamente para isto: quem nao foi colhido nesta
        // volta continua sinalizado.
        if (w->usbPending &&
            WaitForSingleObject(w->usbEvent, 0) == WAIT_OBJECT_0) {
            w->usbPending = false;
            DWORD got = 0;
            if (WinUsb_GetOverlappedResult(dev->midiIf(), &w->ovUsb, &got, FALSE)) {
                // O endpoint entrega multiplos de 4 bytes; truncar e defesa contra
                // um transfer curto, nao contra o caso normal.
                const DWORD useful = got - (got % (DWORD)kRelayPacketBytes);
                if (useful > 0) {
                    const unsigned pk = (unsigned)(useful / kRelayPacketBytes);
                    if (w->link.isConnected()) {
                        DWORD e = 0;
                        // UMA transferencia USB = UMA mensagem do canal. Os pacotes
                        // vao CRUS, enchimento incluido: o filtro (tabela de CIN
                        // mais o teste do bit de status) mora do outro lado, num
                        // caminho unico compartilhado com o leitor do aparelho do
                        // bridge. Duas copias divergem com o tempo.
                        if (w->link.send(w->usbBuf, useful, stop, &e)) {
                            packets_ += pk;
                        } else {
                            dropped_ += pk;
                            logWrite("midi: escrita no canal falhou (erro %lu) - "
                                     "reconectando", e);
                            if (!w->link.close())
                                break;
                        }
                    } else {
                        // Sem canal, os controles sao DESCARTADOS, nao acumulados.
                        // Guardar posicao de fader velha nao serve para nada, e
                        // parar de ler o endpoint faria o aparelho entregar uma
                        // rajada de eventos vencidos quando o canal voltasse.
                        dropped_ += pk;
                    }
                }
            } else {
                const DWORD e = GetLastError();
                if (e != ERROR_SEM_TIMEOUT && e != ERROR_OPERATION_ABORTED) {
                    logWrite("midi: leitura dos controles falhou (erro %lu)", e);
                    break;
                }
            }
        }

        if (w->link.readPending() &&
            WaitForSingleObject(w->link.readEvent(), 0) == WAIT_OBJECT_0) {
            DWORD e = 0;
            const int got = w->link.finishRead(&e);
            if (got < 0) {
                logWrite("midi: leitura do canal falhou (erro %lu) - reconectando", e);
                if (!w->link.close())
                    break;
            } else {
                unsigned char* const buf = w->link.readBuffer();
                for (int i = 0; i + kRelayPacketBytes <= got; i += kRelayPacketBytes) {
                    ULONG written = 0;
                    if (!WinUsb_WritePipe(dev->midiIf(), prof.epLeds, buf + i,
                                          (ULONG)kRelayPacketBytes, &written, 0)) {
                        const DWORD we = GetLastError();
                        // Aparelho arrancado no meio: nao vale poluir o log a cada
                        // tentativa. O contador e PROPRIO das falhas e e
                        // incrementado ANTES do teste - o mesmo idioma de
                        // inStarves_ em audioengine.cpp. Com um contador que so
                        // conta sucesso, o valor CONGELA durante a sequencia de
                        // falhas e a guarda vira uma constante: ou loga toda falha
                        // para sempre, ou nenhuma.
                        //
                        // CANDIDATO DE MELHORIA, deliberadamente NAO feito aqui:
                        // uma falha de escrita pode ter deixado o pipe em estado de
                        // parada (halt), e nada neste caminho chama
                        // WinUsb_ResetPipe. O dano e cosmetico (LED errado ou
                        // apagado) e fica VISIVEL no log por ledErrors_, entao nao
                        // justifica arriscar um reset no meio do caminho que tambem
                        // le os controles. Reavaliar se aparecer sequencia longa de
                        // ledErrors_ com o aparelho presente.
                        const unsigned k = ++ledErrors_;
                        if (k <= 5 || (k % 500) == 0)
                            logWrite("midi: escrita de LED falhou #%u (erro %lu)",
                                     k, we);
                        continue;
                    }
                    leds_++;
                }
            }
        }
    }

    // ---- saida: nada pode ficar pendente sobre memoria que vai ser liberada ----
    //
    // O OVERLAPPED da leitura do USB e o do canal vivem DENTRO de *w. Devolver com
    // I/O em voo deixaria o kernel escrevendo em memoria reciclada - a mesma classe
    // de defeito que o motor de audio ja consertou uma vez (transferencias em voo
    // na hora de desmontar).
    bool clean = true;

    if (w->usbPending) {
        // O handle da interface pode JA ser 0: o host chama stop() e, no caminho de
        // aparelho perdido, device.close() vem logo depois. Fechar o handle do
        // arquivo faz o kernel completar a I/O pendente, entao esperar pelo EVENTO
        // funciona nos dois casos - ao contrario de WinUsb_GetOverlappedResult, que
        // falharia na hora com handle 0 e nao resolveria nada.
        WINUSB_INTERFACE_HANDLE h = dev->midiIf();
        if (h)
            WinUsb_AbortPipe(h, prof.epControls);
        if (WaitForSingleObject(w->usbEvent, kRelayDrainMs) == WAIT_OBJECT_0)
            w->usbPending = false;
        else
            clean = false;
    }

    if (!w->link.close())
        clean = false;

    if (clean) {
        CloseHandle(w->usbEvent);
        delete w;
    } else {
        // O HANDLE DO CANAL SAI, A MEMORIA FICA. Nao e meio-vazamento por descuido:
        // e a divisao certa. O que protege os OVERLAPPED e a memoria nunca ser
        // reciclada; o handle aberto nao protege nada e, do outro lado, TRAVA o
        // servidor do programa de controles para sempre (nMaxInstances = 1, e a
        // escrita de LED dele enche 4096 bytes e bloqueia, matando junto o caminho
        // dos controles, porque e o mesmo thread). Ver abandon() no header.
        w->link.abandon();

        // Mesma decisao, mesma palavra e mesmo marcador de log do motor de audio,
        // para que uma busca por "vazando de proposito" encontre os dois casos.
        //
        // O tamanho sai de sizeof em tempo de compilacao, e nao de um numero escrito
        // a mao: a versao anterior desta linha dizia "uns 300 bytes e 1 a 3 handles"
        // quando o real e 464 bytes e 3 handles, e numero errado em log e defeito -
        // e quem le o log em campo nao tem como conferir. Assim ele nao envelhece.
        // Os 3 handles sao os dois eventos do canal (rxEvent_/txEvent_, que ficam
        // presos dentro do RelayLink vazado) e o evento da leitura do USB.
        logWrite("midi: I/O da ponte nao resolveu em %u ms - vazando de proposito o "
                 "estado do rele (%u bytes e 3 handles: os dois eventos do canal e o "
                 "da leitura dos controles) em vez de entregar memoria morta ao "
                 "driver USB. O handle do canal foi FECHADO - deixa-lo aberto "
                 "travaria o servidor do programa de controles",
                 kRelayDrainMs, (unsigned)sizeof(RelayWorker));
    }

    running_ = false;
}

}

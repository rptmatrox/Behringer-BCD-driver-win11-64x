#pragma once

#include "usbdev.h"
#include "ringbuf.h"
#include "format.h"

namespace bcd {

// Transferencias mantidas em voo por direcao. Tres blocos de 10 ms dao
// 30 ms de folga contra atrasos de agendamento do Windows.
const int kOutXfers = 3;
const int kInXfers  = 3;

// Marca d'agua do anel de entrada, em blocos do host. Acima dela, o motor
// descarta uma amostra por transferencia para compensar a deriva de relogio
// entre o aparelho e o PC. Quatro blocos (~46 ms a 512 frames) fica bem acima
// da oscilacao normal do anel e bem abaixo da capacidade.
const int kInHighWaterBlocks = 4;

// Aritmetica dos aneis em UM lugar so. O motor e os testes unitarios usam
// estas funcoes; repetir a expressao nos dois lados faria o teste passar
// enquanto o codigo real divergisse.
//
// Nivel do anel de entrada acima do qual a correcao de deriva age.
inline int inHighWaterBytes(int blockFrames) { return kInHighWaterBlocks * blockFrames * kBytesPerFrame; }
// Tamanho de cada anel: 8 blocos do host de folga mais 4 blocos de 10 ms do USB.
inline int ringBytesFor(int blockFrames)     { return blockFrames * kBytesPerFrame * 8 + kBlockBytes * 4; }
// Nivel de REGIME do anel de entrada, em bytes: a marca d'agua mais MEIO bloco do
// host. A deriva de relogio e unidirecional, entao o nivel sobe ate a marca e fica
// preso ali, oscilando um bloco do host acima dela - a correcao de deriva dispara
// no PICO da oscilacao, nao no nivel medio. Medido no hardware, 15 min com bloco de
// 512: nivel de 16.900 a 19.800 bytes contra marca d'agua de 16.384. O meio da
// oscilacao (18.432 a 512 frames) fica dentro da faixa medida e 82 bytes acima do
// centro dela; a marca d'agua sozinha ficaria abaixo de toda a faixa.
inline int inRingSteadyBytes(int blockFrames) { return inHighWaterBytes(blockFrames) + blockFrames * kBytesPerFrame / 2; }
// Latencia de ENTRADA em frames, do jeito que a casca ASIO informa ao software de
// DJ: bloco do host + nivel de regime do anel + UM bloco de 10 ms do USB.
//
// Vive AQUI, ao lado do resto da aritmetica de anel, e nao no ponto de chamada, e a
// razao e a mesma das duas de cima: o teste de unidade tem de medir a MESMA conta
// que o driver informa. Este numero em particular ja esteve errado DUAS VEZES e nos
// DOIS sentidos - por copiar a semantica do lado da saida, e depois por medir o
// anel na marca d'agua em vez do meio da oscilacao -, e era a unica conta do motor
// que ficava fora desta secao e sem teste. O porque de cada termo esta em
// bcdasio.cpp, em getLatencies(), com a tabela dos quatro tamanhos de bloco.
inline int inputLatencyFrames(int blockFrames)
{
    return blockFrames + inRingSteadyBytes(blockFrames) / kBytesPerFrame + kFramesPerBlock;
}

// Pedaco usado na pre-carga de silencio do anel de entrada. Dimensionado em
// FRAMES de proposito, e nao em bytes: o alinhamento de frame do anel e uma
// invariante do motor, e um pedaco que nao fosse multiplo de kBytesPerFrame
// deixaria o ponteiro do anel fora da fronteira de frame - o que rotaciona os
// canais de forma permanente e silenciosa (o canal 1 passa a sair no canal 2).
const int kPrimeChunkFrames = 64;
const int kPrimeChunkBytes  = kPrimeChunkFrames * kBytesPerFrame;   // 512

// Escreve `bytes` de silencio em `ring`, em pedacos de kPrimeChunkBytes e sem
// alocar nada no heap. Devolve quantos bytes realmente entraram; menos que o
// pedido significa que o anel nao tinha espaco, e o chamador deve registrar
// isso em vez de seguir em silencio.
//
// O pedido e truncado para baixo na fronteira de frame, entao o alinhamento
// vale para qualquer `bytes` - inclusive nao multiplo de kBytesPerFrame, zero
// ou negativo.
//
// A condicao de validade completa envolve tambem o ESTADO DO ANEL, e nao apenas
// o argumento: se ring.space() nao fosse multiplo de kBytesPerFrame, a escrita
// curta que leva ao `break` gravaria exatamente space() bytes e deixaria head_
// fora da fronteira de frame. No motor essa condicao e inalcancavel - a
// pre-carga roda com o anel vazio, logo space() == cap_, e ByteRing::init
// arredonda a capacidade para a proxima potencia de dois (aqui sempre >= 32768),
// que e multiplo de 8. Nao ha guarda no codigo de proposito: ela seria linha
// morta que nenhum teste alcanca. Quem reusar esta funcao fora do motor tem de
// olhar a condicao acima em vez de supor alinhamento incondicional.
inline int primeRingWithSilence(ByteRing& ring, int bytes)
{
    const unsigned char silence[kPrimeChunkBytes] = { 0 };

    bytes -= bytes % kBytesPerFrame;

    int done = 0;
    while (done < bytes) {
        int chunk = bytes - done;
        if (chunk > kPrimeChunkBytes)
            chunk = kPrimeChunkBytes;
        const int wrote = ring.write(silence, chunk);
        done += wrote;
        if (wrote < chunk)
            break;              // anel sem espaco: insistir nao mudaria nada
    }
    return done;
}

// Quem consome/produz audio. Implementado pela casca ASIO e pelo testaudio.
class EngineClient {
public:
    virtual ~EngineClient() {}

    // Chamado uma vez por bloco, sempre pelo thread de audio.
    //   in    - `frames` frames intercalados vindos do aparelho (nunca nulo)
    //   out   - `frames` frames intercalados a preencher (nunca nulo)
    // Deve retornar rapido: qualquer demora aqui vira estalo no audio.
    virtual void onBlock(const short* in, short* out, int frames) = 0;

    // Chamado uma unica vez, pelo thread de audio, quando o aparelho para de
    // responder (cabo arrancado, por exemplo). Nao e chamado num stop normal.
    // O padrao nao faz nada: so a casca ASIO precisa reagir.
    //
    // ATENCAO: isto roda NO thread de audio. Nao chame AudioEngine::stop()
    // aqui de forma sincrona — seria o thread pedindo para esperar por si
    // mesmo. Avise o software de DJ (kAsioResetRequest) e deixe que ele chame
    // stop() do proprio thread dele. O stop() tem protecao contra esse erro,
    // mas depender dela e pedir problema.
    virtual void onDeviceLost() {}
};

// Motor de audio: um thread dirige as duas direcoes.
// Nao sabe nada sobre COM nem sobre ASIO.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // blockFrames e o tamanho de bloco do software de DJ (ex.: 512).
    // O aparelho continua sendo alimentado em blocos de kFramesPerBlock.
    bool start(UsbDevice* dev, int blockFrames, EngineClient* client);
    void stop();

    bool        isRunning() const  { return running_; }
    bool        deviceLost() const { return deviceLost_; }
    // O ultimo stop() desistiu de esperar o thread de audio e NAO liberou nada.
    // Existe para a CASCA poder aplicar a mesma politica um nivel acima: os
    // buffers que ela entrega ao onBlock tambem nao podem ser liberados enquanto
    // isto valer. Sem esta pergunta, o motor vazaria de proposito e a casca
    // liberaria por baixo dele - meio-conserto e pior que nenhum. Ver
    // BcdAsioDriver::disposeBuffers() e o porque de threadStuck_ mais abaixo.
    bool        threadStuck() const { return threadStuck_; }
    const char* lastError() const  { return err_; }

    unsigned long long framesPlayed() const { return framesPlayed_; }
    unsigned    underruns() const { return underruns_; }
    unsigned    overruns() const  { return overruns_; }

private:
    struct Xfer {
        OVERLAPPED ovl;
        HANDLE     ev;
        unsigned   offset;     // dentro do buffer isocrono registrado
        bool       pending;
    };

    static DWORD WINAPI threadEntry(void* self);
    void   threadMain();
    bool   setupStreams();
    // threadGone=false quer dizer que o thread de audio NAO foi comprovadamente
    // colhido, e nesse caso NADA e liberado - nem os buffers do cliente, nem os
    // ponteiros sao zerados. Parametro explicito e sem valor padrao, pelo mesmo
    // motivo de pumpBlock: quem le o ponto de chamada ve qual dos dois
    // comportamentos esta em uso. O porque esta na definicao, em audioengine.cpp.
    void   teardownStreams(bool threadGone);
    // Manda o Windows abortar o que estiver em voo nos dois endpoints de audio.
    // Nao espera nada. Usado pelo drainPending e tambem pelo caminho de vazamento,
    // que aborta para ajudar o thread travado a sair e nao pode esperar por ele.
    void   abortPipes();
    // Cancela e espera o que estiver em voo. Informa POR DIRECAO se tudo
    // drenou: evento, registro do buffer isocrono e memoria formam um
    // conjunto, e nenhum dos tres pode ser tocado se sobrou transferencia viva.
    void   drainPending(bool* outDrained, bool* inDrained);
    // Chama o cliente uma vez. consumeInput=false entrega silencio na entrada
    // SEM ler o anel de entrada; serve so ao laco de aquecimento do anel de
    // saida, e o porque esta na definicao, em audioengine.cpp. Parametro
    // explicito de proposito, e sem valor padrao: quem le o ponto de chamada
    // ve qual dos dois comportamentos esta em uso.
    void   pumpBlock(bool consumeInput);
    bool   submitOut(int i, bool continueStream);
    bool   submitIn(int i, bool continueStream);
    void   handleOutDone(int i);
    void   handleInDone(int i);
    void   fail(const char* what);

    UsbDevice*   dev_;
    EngineClient* client_;
    int          blockFrames_;

    HANDLE       thread_;
    volatile DWORD threadId_;       // para detectar stop() chamado de dentro
    // O thread de audio nao saiu no prazo do stop() e pode estar vivo em algum
    // lugar - dentro do bufferSwitch do software de DJ, por exemplo. Nesse estado
    // NADA e liberado: os buffers isocronos, os eventos, o registro isocrono, os
    // blocos do cliente e o proprio handle do thread ficam de pe, vazados de
    // proposito. O handle e a UNICA forma de descobrir depois que ele saiu, e sem
    // ele este estado seria irreversivel.
    // Um start() seguinte e RECUSADO enquanto isto valer: subir um segundo thread
    // de audio sobre os mesmos pipes e os mesmos buffers e o desenho que a ponte
    // MIDI recusa por escrito, e aqui seria pior - o thread velho escreve nos
    // buffers isocronos que o novo tambem usa.
    // O estado e REVERSIVEL: start() reavalia sem bloquear e, se o thread ja saiu
    // (o caso comum - device.close() faz as transferencias falharem), o motor sobe
    // de novo. NAO e volatile de proposito: quem escreve e quem le e sempre o
    // thread do DONO (start(), stop() e o destrutor); o thread de audio nunca o
    // toca.
    bool         threadStuck_;
    volatile bool running_;
    volatile bool stopRequested_;
    volatile bool deviceLost_;
    char         err_[256];

    // Saida
    unsigned char*             outBuf_;      // kOutXfers * kBlockBytes
    WINUSB_ISOCH_BUFFER_HANDLE outIsoch_;
    Xfer                       outXfer_[kOutXfers];
    ByteRing                   outRing_;

    // Entrada
    unsigned char*             inBuf_;       // kInXfers * inXferBytes_
    WINUSB_ISOCH_BUFFER_HANDLE inIsoch_;
    Xfer                       inXfer_[kInXfers];
    ByteRing                   inRing_;
    int                        inMaxPacket_;
    int                        inXferBytes_;
    USBD_ISO_PACKET_DESCRIPTOR inDesc_[kInXfers][16];
    bool                       inOffsetLogged_;

    // Blocos de trabalho entregues ao cliente
    short*       clientIn_;
    short*       clientOut_;

    // Instrumentacao de diagnostico. Zerados por start() e, no caso de
    // driftDrops_, lidos por stop() - os dois rodam no thread do DONO, nao no
    // de audio. Fora desses dois momentos, so o thread de audio escreve aqui.
    // Os tres primeiros nunca saem do thread de audio depois de start(), e por
    // isso nao precisam de volatile; driftDrops_ precisa, porque stop() o le e
    // o caminho do timeout de 3 s segue adiante com o thread possivelmente vivo.
    long long inBytesTotal_;      // bytes que entraram no anel de entrada
    long long pumpCount_;         // quantas vezes o cliente foi chamado
    DWORD     lastReportTick_;    // ultimo relatorio periodico
    volatile long long driftDrops_;   // amostras descartadas por deriva de relogio

    volatile unsigned long long framesPlayed_;
    volatile unsigned           underruns_;
    volatile unsigned           overruns_;
    volatile unsigned           inStarves_;   // blocos de entrada entregues incompletos
};

}

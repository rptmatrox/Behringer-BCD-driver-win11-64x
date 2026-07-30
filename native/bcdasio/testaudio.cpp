// Exercita o motor de audio sem o VirtualDJ. Nao entra no produto final.
//   testaudio tone [seg]     - toca um tom de 440 Hz pelo Master e pelo fone
//   testaudio capture [seg]  - mede e mostra o que chega das entradas
#include "audioengine.h"
#include "format.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <new>       // std::nothrow

using namespace bcd;

//----------------------------------------------------------------------
// Gera um tom continuo nos 4 canais de saida e ignora a entrada.
class ToneClient : public EngineClient {
public:
    ToneClient(double hz) : phase_(0.0), step_(2.0 * 3.14159265358979 * hz / kSampleRate) {}

    virtual void onBlock(const short* in, short* out, int frames)
    {
        (void)in;
        for (int i = 0; i < frames; i++) {
            short v = (short)(0.25 * 32767.0 * sin(phase_));
            phase_ += step_;
            if (phase_ > 6.283185307179586)
                phase_ -= 6.283185307179586;
            out[i * kChannels + 0] = v;   // Master L
            out[i * kChannels + 1] = v;   // Master R
            out[i * kChannels + 2] = v;   // Phones L
            out[i * kChannels + 3] = v;   // Phones R
        }
    }

private:
    double phase_;
    double step_;
};

//----------------------------------------------------------------------
static int cmdTone(UsbDevice& dev, int seconds)
{
    ToneClient client(440.0);
    AudioEngine engine;

    printf("tocando 440 Hz por %d s no Master e no fone...\n", seconds);
    if (!engine.start(&dev, 512, &client)) {
        printf("ERRO ao iniciar o motor: %s\n", engine.lastError());
        return 1;
    }

    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < (DWORD)seconds * 1000) {
        Sleep(500);
        if (engine.deviceLost()) {
            printf("\nERRO: o aparelho parou de responder.\n");
            break;
        }
        printf("\r  %.1f s | frames=%llu | underruns=%u   ",
               (GetTickCount() - t0) / 1000.0,
               (unsigned long long)engine.framesPlayed(),
               engine.underruns());
        fflush(stdout);
    }
    printf("\n");

    unsigned long long frames = engine.framesPlayed();
    unsigned under = engine.underruns();
    DWORD elapsed = GetTickCount() - t0;
    engine.stop();

    double rate = (elapsed > 0) ? (double)frames * 1000.0 / (double)elapsed : 0.0;

    printf("\n=== VEREDITO ===\n");
    printf("%llu frames em %lu ms -> %.1f amostras/s\n",
           (unsigned long long)frames, (unsigned long)elapsed, rate);
    printf("underruns: %u\n", under);
    if (under == 0 && rate > 44050.0 && rate < 44150.0) {
        printf("CONFORME: taxa correta e nenhum underrun.\n");
    } else {
        char path[512];
        logPath(path, sizeof(path));
        printf("FORA DO ESPERADO. Verificar porta USB 2.0 e o log em %s\n", path);
    }
    return (under == 0) ? 0 : 1;
}

//----------------------------------------------------------------------
// Toca um arquivo PCM cru estereo (16 bits, 44100 Hz) nas quatro saidas:
// o par L/R vai ao mesmo tempo para o Master (canais 1 e 2) e para o fone
// (canais 3 e 4), para dar para conferir as duas saidas de uma vez.
class FileClient : public EngineClient {
public:
    FileClient(const short* pcm, long long stereoFrames)
        : pcm_(pcm), total_(stereoFrames), pos_(0) {}

    virtual void onBlock(const short* in, short* out, int frames)
    {
        (void)in;
        for (int i = 0; i < frames; i++) {
            short l = 0, r = 0;
            if (pos_ < total_) {
                l = pcm_[pos_ * 2];
                r = pcm_[pos_ * 2 + 1];
                pos_++;
            }
            out[i * kChannels + 0] = l;   // Master L
            out[i * kChannels + 1] = r;   // Master R
            out[i * kChannels + 2] = l;   // Phones L
            out[i * kChannels + 3] = r;   // Phones R
        }
    }

    bool finished() const { return pos_ >= total_; }

private:
    const short* pcm_;
    long long    total_;
    long long    pos_;
};

//----------------------------------------------------------------------
// Le o arquivo inteiro para a memoria. Devolve o buffer (do chamador, que
// deve dar delete[]) e o numero de frames estereo, ou 0 em caso de erro.
static short* loadRaw(const char* path, long long* stereoFrames)
{
    *stereoFrames = 0;

    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("ERRO: nao consegui abrir %s\n", path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        printf("ERRO: fseek falhou em %s\n", path);
        fclose(f);
        return 0;
    }
    long size = ftell(f);
    rewind(f);

    if (size <= 0 || (size % 4) != 0) {
        printf("ERRO: %s tem %ld bytes; esperado multiplo de 4 "
               "(PCM cru 16 bits estereo)\n", path, size);
        fclose(f);
        return 0;
    }

    short* pcm = new (std::nothrow) short[size / 2];
    if (!pcm) {
        printf("ERRO: sem memoria para %ld bytes\n", size);
        fclose(f);
        return 0;
    }

    size_t got = fread(pcm, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        printf("ERRO: leitura incompleta de %s\n", path);
        delete[] pcm;
        return 0;
    }

    *stereoFrames = size / 4;
    return pcm;
}

//----------------------------------------------------------------------
static int cmdFile(UsbDevice& dev, const char* path, int seconds)
{
    long long total = 0;
    short* pcm = loadRaw(path, &total);
    if (!pcm)
        return 1;

    printf("arquivo: %s\n", path);
    printf("%lld frames estereo (%.1f s de audio)\n",
           total, (double)total / (double)kSampleRate);

    FileClient  client(pcm, total);
    AudioEngine engine;

    printf("tocando por ate %d s no Master e no fone...\n", seconds);
    if (!engine.start(&dev, 512, &client)) {
        printf("ERRO ao iniciar o motor: %s\n", engine.lastError());
        delete[] pcm;
        return 1;
    }

    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < (DWORD)seconds * 1000 && !client.finished()) {
        Sleep(500);
        if (engine.deviceLost()) {
            printf("\nERRO: o aparelho parou de responder.\n");
            break;
        }
        printf("\r  %.1f s | frames=%llu | underruns=%u   ",
               (GetTickCount() - t0) / 1000.0,
               (unsigned long long)engine.framesPlayed(),
               engine.underruns());
        fflush(stdout);
    }
    printf("\n");

    unsigned long long frames = engine.framesPlayed();
    unsigned under = engine.underruns();
    DWORD elapsed = GetTickCount() - t0;
    engine.stop();
    delete[] pcm;

    double rate = (elapsed > 0) ? (double)frames * 1000.0 / (double)elapsed : 0.0;

    printf("\n=== VEREDITO ===\n");
    printf("%llu frames em %lu ms -> %.1f amostras/s\n",
           frames, (unsigned long)elapsed, rate);
    printf("underruns: %u\n", under);
    if (under == 0 && rate > 44050.0 && rate < 44150.0) {
        printf("CONFORME: taxa correta e nenhum underrun.\n");
    } else {
        char logp[512];
        logPath(logp, sizeof(logp));
        printf("FORA DO ESPERADO. Verificar porta USB 2.0 e o log em %s\n", logp);
    }
    return (under == 0) ? 0 : 1;
}

//----------------------------------------------------------------------
// Teste de separacao de canais. Seis etapas de 3 s de som + 1 s de silencio,
// 24 s no total. Serve a dois propositos de uma vez: confirmar que o estereo
// funciona (esquerda / direita / ambos soam diferentes) e confirmar qual par
// de canais e o fone e qual e o Master (quem tem so fone ligado nao deve ouvir
// nada nas etapas do Master).
class PanClient : public EngineClient {
public:
    enum { kSteps = 6 };

    PanClient()
        : phase_(0.0),
          inc_(2.0 * 3.14159265358979 * 440.0 / (double)kSampleRate),
          pos_(0) {}

    virtual void onBlock(const short* in, short* out, int frames)
    {
        (void)in;

        const long long secFrames  = (long long)kSampleRate;
        const long long stepFrames = 4 * secFrames;   // 3 s de som + 1 s de silencio
        const long long toneFrames = 3 * secFrames;

        for (int i = 0; i < frames; i++) {
            const long long t      = pos_ + i;
            const int       step   = (int)(t / stepFrames);
            const long long within = t % stepFrames;
            const bool sounding    = (step < kSteps) && (within < toneFrames);

            short v = 0;
            if (sounding)
                v = (short)(0.25 * 32767.0 * sin(phase_));

            phase_ += inc_;
            if (phase_ > 6.283185307179586)
                phase_ -= 6.283185307179586;

            short ch[4] = { 0, 0, 0, 0 };
            if (sounding) {
                switch (step) {
                    case 0: ch[2] = v;            break;   // fone   so esquerda
                    case 1: ch[3] = v;            break;   // fone   so direita
                    case 2: ch[2] = ch[3] = v;    break;   // fone   ambos
                    case 3: ch[0] = v;            break;   // master so esquerda
                    case 4: ch[1] = v;            break;   // master so direita
                    case 5: ch[0] = ch[1] = v;    break;   // master ambos
                }
            }

            out[i * kChannels + 0] = ch[0];
            out[i * kChannels + 1] = ch[1];
            out[i * kChannels + 2] = ch[2];
            out[i * kChannels + 3] = ch[3];
        }

        pos_ += frames;
    }

    int currentStep() const
    {
        const long long stepFrames = 4 * (long long)kSampleRate;
        return (int)(pos_ / stepFrames);
    }

    bool finished() const { return currentStep() >= kSteps; }

private:
    double    phase_;
    double    inc_;
    long long pos_;
};

//----------------------------------------------------------------------
static const char* kPanStepNames[PanClient::kSteps] = {
    "FONE   - so ESQUERDA",
    "FONE   - so DIREITA",
    "FONE   - AMBOS",
    "MASTER - so ESQUERDA",
    "MASTER - so DIREITA",
    "MASTER - AMBOS"
};

//----------------------------------------------------------------------
static int cmdPan(UsbDevice& dev)
{
    PanClient   client;
    AudioEngine engine;

    printf("Seis etapas de 3 s de som + 1 s de silencio (24 s no total).\n");
    printf("Quem tem so o fone ligado nao deve ouvir as tres ultimas.\n\n");

    if (!engine.start(&dev, 512, &client)) {
        printf("ERRO ao iniciar o motor: %s\n", engine.lastError());
        return 1;
    }

    DWORD t0 = GetTickCount();
    int shown = -1;
    while (!client.finished()) {
        Sleep(200);
        if (engine.deviceLost()) {
            printf("\nERRO: o aparelho parou de responder.\n");
            break;
        }
        const int s = client.currentStep();
        if (s != shown && s < PanClient::kSteps) {
            printf("  etapa %d de %d: %s\n", s + 1, (int)PanClient::kSteps, kPanStepNames[s]);
            fflush(stdout);
            shown = s;
        }
        if (GetTickCount() - t0 > 40000)     // rede de seguranca
            break;
    }

    unsigned long long frames  = engine.framesPlayed();
    unsigned           under   = engine.underruns();
    DWORD              elapsed = GetTickCount() - t0;
    engine.stop();

    const double rate = (elapsed > 0) ? (double)frames * 1000.0 / (double)elapsed : 0.0;

    printf("\n=== VEREDITO ===\n");
    printf("%llu frames em %lu ms -> %.1f amostras/s\n",
           frames, (unsigned long)elapsed, rate);
    printf("underruns: %u\n", under);
    printf("O julgamento do que foi ouvido em cada etapa e do usuario.\n");
    return (under == 0) ? 0 : 1;
}

//----------------------------------------------------------------------
// Mede o que chega das entradas. Nao produz som.
//
// A janela de medicao e fechada DENTRO do thread de audio, e nao pelo thread
// que imprime na tela. Se o thread principal zerasse os acumuladores, os dois
// threads estariam escrevendo nos mesmos campos: daria para o zeramento
// atropelar uma atualizacao no meio e a tela mostrar um pico alto ao lado de
// uma contagem zerada. Como este e justamente o teste que define qual botao
// do aparelho corresponde a qual canal, um numero sem sentido aqui atrapalha
// a conclusao. Aqui: acumuladores sao exclusivos do thread de audio, e o
// thread principal so LE os valores publicados.
class CaptureClient : public EngineClient {
public:
    CaptureClient()
    {
        for (int c = 0; c < kChannels; c++) {
            acc_[c]     = 0;
            accPeak_[c] = 0;
            pubPeak_[c] = 0;
            pubMean_[c] = 0;
        }
        accFrames_ = 0;
    }

    virtual void onBlock(const short* in, short* out, int frames)
    {
        memset(out, 0, frames * kChannels * sizeof(short));   // silencio na saida

        for (int i = 0; i < frames; i++) {
            for (int c = 0; c < kChannels; c++) {
                short v = in[i * kChannels + c];
                int   a = (v < 0) ? -(int)v : (int)v;
                if (a > accPeak_[c])
                    accPeak_[c] = a;
                acc_[c] += a;
            }
        }
        accFrames_ += frames;

        // Fecha a janela a cada ~0,25 s e publica. Rapido o bastante para
        // acompanhar um botao girando, lento o bastante para ser legivel.
        if (accFrames_ >= kSampleRate / 4) {
            for (int c = 0; c < kChannels; c++) {
                pubPeak_[c] = accPeak_[c];
                pubMean_[c] = (int)(acc_[c] / accFrames_);
                accPeak_[c] = 0;
                acc_[c]     = 0;
            }
            accFrames_ = 0;
        }
    }

    // Lidos pelo thread principal. Inteiros alinhados: leitura atomica na
    // pratica, e no pior caso mostram a janela anterior.
    int peak(int c) const { return pubPeak_[c]; }
    int mean(int c) const { return pubMean_[c]; }

private:
    // Exclusivos do thread de audio.
    long long acc_[kChannels];
    int       accPeak_[kChannels];
    long long accFrames_;
    // Escritos pelo thread de audio, lidos pelo principal.
    volatile int pubPeak_[kChannels];
    volatile int pubMean_[kChannels];
};

//----------------------------------------------------------------------
static int cmdCapture(UsbDevice& dev, int seconds)
{
    CaptureClient client;
    AudioEngine   engine;

    printf("medindo as entradas por %d s...\n", seconds);
    printf("DICAS: gire os botoes de ganho do aparelho e veja o nivel mudar;\n");
    printf("       encoste o dedo no pino do meio de um RCA de entrada para\n");
    printf("       gerar zumbido.\n\n");

    if (!engine.start(&dev, 512, &client)) {
        printf("ERRO ao iniciar o motor: %s\n", engine.lastError());
        return 1;
    }

    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < (DWORD)seconds * 1000) {
        Sleep(250);
        if (engine.deviceLost()) {
            printf("\nERRO: o aparelho parou de responder.\n");
            break;
        }
        printf("\r  %4.1f s | pico  ch1=%5d ch2=%5d ch3=%5d ch4=%5d | medio ch1=%5d ch2=%5d ch3=%5d ch4=%5d  ",
               (GetTickCount() - t0) / 1000.0,
               client.peak(0), client.peak(1), client.peak(2), client.peak(3),
               client.mean(0), client.mean(1), client.mean(2), client.mean(3));
        fflush(stdout);
        // A janela e fechada pelo proprio thread de audio; aqui so lemos.
    }
    printf("\n");

    DWORD elapsed = GetTickCount() - t0;
    unsigned over = engine.overruns();
    unsigned under = engine.underruns();
    engine.stop();

    // A taxa de entrada e medida pela contagem total de frames entregues ao
    // cliente, que so avancam com dado real vindo do aparelho.
    printf("\n=== VEREDITO ===\n");
    printf("tempo=%lu ms | overruns=%u | underruns=%u\n",
           (unsigned long)elapsed, over, under);
    printf("Confirme com o usuario: o nivel mexeu ao girar o ganho?\n");
    // Codigo de saida reflete o criterio de aceite: zero perdas nos dois
    // sentidos. Sem isso, um script que so olha o exit code passaria batido
    // por uma regressao de overrun.
    return (over == 0 && under == 0) ? 0 : 1;
}

//----------------------------------------------------------------------
int main(int argc, char** argv)
{
    logInit("asio.log");

    const char* cmd = (argc > 1) ? argv[1] : "tone";
    int seconds = (argc > 2) ? atoi(argv[2]) : 30;
    if (seconds < 1)
        seconds = 30;

    UsbDevice dev;
    if (!dev.open()) {
        printf("ERRO ao abrir o aparelho:\n  %s\n", dev.lastError());
        logClose();
        return 1;
    }

    int rc;
    if (strcmp(cmd, "file") == 0) {
        if (argc < 3) {
            printf("uso: testaudio file <arquivo.raw> [segundos]\n");
            rc = 1;
        } else {
            int secs = (argc > 3) ? atoi(argv[3]) : 30;
            if (secs < 1)
                secs = 30;
            rc = cmdFile(dev, argv[2], secs);
        }
    } else if (strcmp(cmd, "pan") == 0) {
        rc = cmdPan(dev);
    } else if (strcmp(cmd, "capture") == 0) {
        rc = cmdCapture(dev, seconds);
    } else if (strcmp(cmd, "tone") == 0) {
        rc = cmdTone(dev, seconds);
    } else {
        printf("modo desconhecido: %s (use tone, file, pan ou capture)\n", cmd);
        rc = 1;
    }

    dev.close();
    logClose();
    return rc;
}

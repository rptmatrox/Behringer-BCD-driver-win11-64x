// Diagnostico da BCD3000. Nao entra no produto final.
//   probe info          - lista interfaces, alternate settings e pipes
//   probe rate [seg]    - mede a taxa real de amostras da captura (padrao 60 s)
#include "usbdev.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

using namespace bcd;

static const char* pipeTypeName(int t)
{
    switch (t) {
        case 0:  return "control";
        case 1:  return "isochronous";
        case 2:  return "bulk";
        case 3:  return "interrupt";
        default: return "?";
    }
}

static void dumpIface(UsbDevice& dev, const char* label, WINUSB_INTERFACE_HANDLE h)
{
    printf("\n%s\n", label);
    for (unsigned char alt = 0; alt < 4; alt++) {
        PipeDesc pipes[8];
        int n = dev.queryPipes(h, alt, pipes, 8);
        if (n == 0 && alt > 0)
            continue;
        printf("  alternate setting %u: %d pipe(s)\n", alt, n);
        for (int i = 0; i < n; i++) {
            printf("    pipe 0x%02X  %-11s  maxPacketSize=%d  interval=%d\n",
                   pipes[i].id, pipeTypeName(pipes[i].type),
                   pipes[i].maxPacketSize, pipes[i].interval);
        }
    }
}

static int cmdInfo(UsbDevice& dev)
{
    // Endpoints e alternate settings vem do PERFIL do aparelho (tabela no topo de
    // usbdev.cpp). O modelo entra no relatorio: um diagnostico que nao diz de qual
    // aparelho esta falando nao serve para nada num projeto com dois perfis.
    const DeviceProfile& prof = dev.profile();

    printf("caminho: %s\n", dev.devicePath());
    printf("modelo:  %s%s\n", prof.model,
           prof.provenOnHardware ? "" : "  (perfil EXPERIMENTAL, nunca rodado)");

    char label[80];
    _snprintf(label, sizeof(label) - 1, "IF1 - playback (EP 0x%02X ISO OUT)",
              prof.epPlayback);
    label[sizeof(label) - 1] = 0;
    dumpIface(dev, label, dev.playbackIf());
    _snprintf(label, sizeof(label) - 1, "IF2 - captura  (EP 0x%02X ISO IN)",
              prof.epCapture);
    label[sizeof(label) - 1] = 0;
    dumpIface(dev, label, dev.captureIf());

    PipeDesc pipes[8];
    int n = dev.queryPipes(dev.playbackIf(), prof.altStreaming, pipes, 8);
    int mps = 0;
    for (int i = 0; i < n; i++)
        if (pipes[i].id == prof.epPlayback)
            mps = pipes[i].maxPacketSize;

    printf("\n=== VEREDITO ===\n");
    if (mps == 0) {
        printf("NAO ENCONTRADO o EP 0x%02X no alternate setting %u. "
               "Nada a fazer sem isso.\n", prof.epPlayback, prof.altStreaming);
        return 1;
    }
    printf("wMaxPacketSize do EP 0x%02X = %d bytes (%d frames de audio)\n",
           prof.epPlayback, mps, mps / 8);
    if (mps == 360)
        printf("CONFORME o esperado: o bloco de 3528 bytes / 10 ms do plano vale.\n");
    else
        printf("DIFERENTE de 360: recalcular a aritmetica do bloco antes de seguir.\n");
    return 0;
}

static int cmdRate(UsbDevice& dev, int seconds)
{
    const DeviceProfile& prof = dev.profile();

    if (!dev.setAlternate(dev.captureIf(), prof.altStreaming)) {
        printf("ERRO: %s\n", dev.lastError());
        return 1;
    }

    PipeDesc pipes[8];
    int n = dev.queryPipes(dev.captureIf(), prof.altStreaming, pipes, 8);
    int mps = 0;
    for (int i = 0; i < n; i++)
        if (pipes[i].id == prof.epCapture)
            mps = pipes[i].maxPacketSize;
    if (mps == 0) {
        printf("ERRO: EP 0x%02X nao encontrado no alternate setting %u.\n",
               prof.epCapture, prof.altStreaming);
        dev.setAlternate(dev.captureIf(), prof.altIdle);
        return 1;
    }
    printf("EP 0x%02X maxPacketSize=%d, medindo por %d s...\n",
           prof.epCapture, mps, seconds);

    const int kPackets = 10;
    const int kBufLen  = kPackets * mps;

    unsigned char* buf = (unsigned char*)malloc(kBufLen);
    WINUSB_ISOCH_BUFFER_HANDLE isoch = 0;
    if (!WinUsb_RegisterIsochBuffer(dev.captureIf(), prof.epCapture, buf, kBufLen, &isoch)) {
        printf("ERRO: RegisterIsochBuffer falhou (%lu)\n", GetLastError());
        free(buf);
        dev.setAlternate(dev.captureIf(), prof.altIdle);
        return 1;
    }

    HANDLE ev = CreateEventA(0, TRUE, FALSE, 0);
    ULONGLONG totalBytes = 0;
    DWORD t0 = GetTickCount();
    DWORD limit = (DWORD)seconds * 1000;
    bool first = true;

    while (GetTickCount() - t0 < limit) {
        USBD_ISO_PACKET_DESCRIPTOR desc[kPackets];
        memset(desc, 0, sizeof(desc));
        OVERLAPPED ovl;
        memset(&ovl, 0, sizeof(ovl));
        ovl.hEvent = ev;
        ResetEvent(ev);

        WinUsb_ReadIsochPipeAsap(isoch, 0, kBufLen, first ? FALSE : TRUE,
                                 kPackets, desc, &ovl);
        DWORD got = 0;
        // Quirk: em iso ASAP este retorno traz 0 bytes; os descritores sao a verdade.
        WinUsb_GetOverlappedResult(dev.captureIf(), &ovl, &got, TRUE);

        for (int i = 0; i < kPackets; i++)
            totalBytes += desc[i].Length;
        first = false;
    }

    DWORD elapsed = GetTickCount() - t0;
    CloseHandle(ev);
    WinUsb_UnregisterIsochBuffer(isoch);
    free(buf);
    dev.setAlternate(dev.captureIf(), prof.altIdle);

    double frames = (double)totalBytes / 8.0;
    double rate   = frames * 1000.0 / (double)elapsed;

    printf("\n=== VEREDITO ===\n");
    printf("%llu bytes em %lu ms  ->  %.1f amostras/s\n",
           (unsigned long long)totalBytes, (unsigned long)elapsed, rate);
    if (rate > 44050.0 && rate < 44150.0)
        printf("CONFORME: o aparelho entrega 44100 Hz.\n");
    else
        printf("FORA DO ESPERADO (44100 +- 50). Investigar antes de seguir.\n");
    return 0;
}

int main(int argc, char** argv)
{
    logInit("asio.log");

    const char* cmd = (argc > 1) ? argv[1] : "info";

    UsbDevice dev;
    if (!dev.open()) {
        printf("ERRO ao abrir o aparelho:\n  %s\n", dev.lastError());
        logClose();
        return 1;
    }

    int rc;
    if (strcmp(cmd, "rate") == 0) {
        int seconds = (argc > 2) ? atoi(argv[2]) : 60;
        if (seconds < 1) seconds = 60;
        rc = cmdRate(dev, seconds);
    } else {
        rc = cmdInfo(dev);
    }

    dev.close();
    logClose();
    return rc;
}

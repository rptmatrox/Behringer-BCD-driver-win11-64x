#include "nanoclock.h"

#include <windows.h>

namespace {

const unsigned long long kNsPerSecond = 1000000000ull;

// O maior `freq` para o qual a conta de ticksToNanoSeconds e exata sem estourar:
// (freq - 1) * 1e9 tem de caber em 64 bits, ou seja freq <= 2^64/1e9.
const unsigned long long kMaxSafeFrequency = 18446744073ull;

}

unsigned long long bcd::ticksToNanoSeconds(unsigned long long ticks,
                                          unsigned long long freq)
{
    if (freq == 0)
        return 0;

    // A guarda existe para o caso impossivel ficar EXPLICITO em vez de virar lixo
    // silencioso. Se algum dia um contador passar de 18,4 GHz, esta linha e o lugar
    // onde a conta tem de ser refeita - e o teste de unidade cobre justamente a
    // fronteira, para que a decisao nao dependa de alguem lembrar deste comentario.
    if (freq > kMaxSafeFrequency)
        return 0;

    const unsigned long long whole = ticks / freq;
    const unsigned long long rest  = ticks % freq;
    return whole * kNsPerSecond + (rest * kNsPerSecond) / freq;
}

unsigned long long bcd::nanoSecondsNow()
{
    // A frequencia do contador e FIXA durante a vida do sistema (documentado pela
    // Microsoft desde o Windows XP), por isso e lida uma vez e guardada.
    //
    // O `static` tem inicializador CONSTANTE de proposito: assim ele e apenas uma
    // variavel zerada na imagem, e NAO um "magic static" - que o compilador protegeria
    // com uma trava de inicializacao dupla. Isto roda no thread de audio, uma vez por
    // bloco, e trava nenhuma tem lugar ali.
    //
    // A corrida entre threads e benigna e nao acidental: dois threads que cheguem aqui
    // ao mesmo tempo leem a MESMA frequencia do sistema e gravam o MESMO valor. Nao ha
    // estado intermediario invalido para ninguem observar - e uma escrita de 8 bytes
    // alinhada, que em x64 nao rasga.
    static unsigned long long freq = 0;
    if (freq == 0) {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0)
            return 0;
        freq = (unsigned long long)f.QuadPart;
    }

    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return 0;

    return ticksToNanoSeconds((unsigned long long)counter.QuadPart, freq);
}

void bcd::getNanoSeconds(ASIOTimeStamp* ts)
{
    if (!ts)
        return;

    const unsigned long long ns = nanoSecondsNow();

    // A ordem `hi` antes de `lo` e ABI, e trocada aqui o host leria o instante
    // multiplicado por 2^32 sem nenhum erro aparecer em lugar nenhum. Ver a nota do
    // ASIOTimeStamp em asioapi.h.
    ts->hi = (unsigned long)(ns >> 32);
    ts->lo = (unsigned long)(ns & 0xffffffffull);
}

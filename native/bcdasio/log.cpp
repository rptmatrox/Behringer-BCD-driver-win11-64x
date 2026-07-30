#include "log.h"

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace bcd {

// A trava e construida quando o modulo carrega, antes de qualquer thread
// poder chamar aqui. Inicializa-la sob demanda ("if (!pronto) inicializa")
// seria uma corrida: dois threads poderiam inicializar a mesma trava ao
// mesmo tempo, o que o Windows nao permite.
// De proposito NAO ha destrutor: a trava vaza, e isso e o certo aqui. Se ela
// fosse destruida no descarregamento do modulo, um thread de audio ainda vivo
// nesse instante entraria numa secao critica ja apagada. Vazar um objeto
// desses ate o fim do processo nao custa nada — o sistema o recupera.
namespace {
struct LogLock {
    CRITICAL_SECTION cs;
    LogLock() { InitializeCriticalSection(&cs); }
};
LogLock g_lock;
}

static FILE* g_file = 0;
static char  g_path[512] = {0};

bool logInit(const char* filename)
{
    logClose();

    char base[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathA(0, CSIDL_LOCAL_APPDATA, 0, 0, base)))
        return false;

    char dir[MAX_PATH];
    _snprintf(dir, sizeof(dir) - 1, "%s\\BCD3000Bridge", base);
    dir[sizeof(dir) - 1] = 0;
    CreateDirectoryA(dir, 0);   // se ja existir, tudo bem

    char full[512];
    _snprintf(full, sizeof(full) - 1, "%s\\%s", dir, filename);
    full[sizeof(full) - 1] = 0;

    FILE* f = fopen(full, "a");
    if (!f)
        return false;

    EnterCriticalSection(&g_lock.cs);
    g_file = f;
    strcpy(g_path, full);
    LeaveCriticalSection(&g_lock.cs);

    logWrite("---- log aberto ----");
    return true;
}

void logWrite(const char* fmt, ...)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    // Sem atalho antes da trava: ler g_file fora dela seria uma corrida com
    // logInit/logClose. Entrar numa secao critica sem disputa custa poucos
    // nanossegundos.
    EnterCriticalSection(&g_lock.cs);
    if (g_file) {
        fprintf(g_file, "%02d:%02d:%02d.%03d ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_file, fmt, ap);
        va_end(ap);
        fputc('\n', g_file);
        fflush(g_file);
    }
    LeaveCriticalSection(&g_lock.cs);
}

void logClose()
{
    EnterCriticalSection(&g_lock.cs);
    if (g_file) {
        fclose(g_file);
        g_file = 0;
    }
    g_path[0] = 0;
    LeaveCriticalSection(&g_lock.cs);
}

void logPath(char* out, int outSize)
{
    if (!out || outSize <= 0)
        return;
    EnterCriticalSection(&g_lock.cs);
    strncpy(out, g_path, outSize - 1);
    out[outSize - 1] = 0;
    LeaveCriticalSection(&g_lock.cs);
}

}

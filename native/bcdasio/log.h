#pragma once

namespace bcd {

// Abre %LOCALAPPDATA%\BCD3000Bridge\<filename> em modo append.
// Cria a pasta se nao existir. Retorna false se nao conseguir.
bool logInit(const char* filename);

// Grava uma linha com carimbo de tempo. Sem logInit, nao faz nada.
// Seguro chamar de qualquer thread, inclusive o de audio.
void logWrite(const char* fmt, ...);

// Fecha o arquivo. Depois disso logPath() devolve string vazia.
void logClose();

// Copia o caminho completo do arquivo aberto para `out` (string vazia se
// nao houver arquivo aberto). Copia sob trava: devolver um ponteiro para o
// buffer interno deixaria o chamador lendo uma string que outro thread pode
// estar reescrevendo.
void logPath(char* out, int outSize);

}

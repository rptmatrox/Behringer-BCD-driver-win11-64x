#pragma once

#include "asioapi.h"

// Relogio de nanossegundos do driver, escrito para este projeto.
//
// SUBSTITUI `native/bcdasio/wintimer.cpp`, que veio do exemplo asiosample da Steinberg
// e era o ultimo arquivo de terceiro compilado no produto. O aviso BSD que vivia la
// esta preservado em `native/bcdasio/LICENSE-asiosample.txt` - `bcdasio.{h,cpp}`
// continuam sendo derivados do mesmo exemplo e a obrigacao do aviso continua valendo.
//
// O QUE MUDOU, E O QUE NAO MUDOU
// A EPOCA e a mesma: nanossegundos desde a partida da maquina. O `timeGetTime()` que o
// wintimer.cpp usava conta milissegundos desde a partida, e o QueryPerformanceCounter
// conta ticks desde a partida - os dois zeram no mesmo instante, entao um host que
// compare este carimbo com o proprio relogio dele ve a mesma grandeza de antes. Nao e
// detalhe: trocar a epoca faria o host achar que o audio esta atrasado em horas.
//
// Tres coisas melhoraram, e nenhuma delas e cosmetica:
//
//  1. RESOLUCAO. `timeGetTime()` devolve milissegundos, e pior: a resolucao real dele
//     depende do `timeBeginPeriod` que ALGUM OUTRO processo da maquina tenha pedido -
//     sem ninguem pedir, o passo e de ~15,6 ms. Um bloco de 512 frames a 44100 Hz dura
//     11,6 ms, ou seja, o carimbo de tempo de blocos CONSECUTIVOS podia sair igual, e o
//     host que derivasse velocidade dele dividiria por zero ou veria a taxa pular. O
//     QueryPerformanceCounter tem passo de 100 ns nesta maquina.
//
//  2. A VOLTA AO ZERO. `timeGetTime()` devolve `DWORD` e volta a zero a cada 49,7 dias
//     de maquina ligada. No instante da volta, o carimbo entregue ao host CAI de
//     4,29e15 ns para perto de zero, e a posicao de amostra continua subindo - um host
//     que calcule deriva entre os dois ve um salto absurdo. O contador de desempenho e
//     de 64 bits e, ja convertido para nanossegundos, cabe em 584 anos.
//
//  3. A CONTA. O wintimer.cpp fazia a partida em `hi`/`lo` com aritmetica de ponto
//     flutuante (`nanoSeconds / 2^32`). Em 49 dias o valor em nanossegundos chega a
//     4,29e15, e um `double` tem 53 bits de mantissa - ou seja, ~9,0e15 e o ultimo
//     inteiro representavel exatamente. A conta ficava a menos de um fator 2 do limite
//     de precisao e sem contador nenhum avisando. Aqui a partida e inteira e exata.
//
// POR QUE A CONVERSAO E UMA FUNCAO SEPARADA E PUBLICA
// `ticksToNanoSeconds` nao le relogio nenhum: e so a aritmetica. Separada, ela tem
// teste de unidade (test_nanoclock em tests.cpp) que confere a conta com valores
// escolhidos - inclusive os que estouram em 64 bits se a ordem das operacoes estiver
// errada. Uma funcao que le o relogio E converte nao tem como ser testada assim,
// porque o resultado depende de que instante e agora; e esta e mais uma aritmetica
// deste projeto que, sem teste, erraria em silencio.

namespace bcd {

// Converte um valor do contador de desempenho para nanossegundos.
//
// A conta e feita em DUAS partes - segundos inteiros e resto - de proposito:
//   ns = (ticks / freq) * 1e9  +  (ticks % freq) * 1e9 / freq
// A forma ingenua `ticks * 1e9 / freq` estoura em 64 bits depois de 18,4 segundos de
// maquina ligada com freq = 10 MHz, e o resultado nao seria "impreciso": seria lixo.
// Nesta forma, o unico produto que pode estourar e `(ticks % freq) * 1e9`, cujo fator
// da esquerda e menor que `freq` - portanto ela e exata para qualquer freq de ate
// 18.446.744.073 Hz (18,4 GHz). Nenhum hardware existente chega perto.
//
// Devolve 0 se `freq` for 0, que e o unico valor de entrada sem resposta possivel.
unsigned long long ticksToNanoSeconds(unsigned long long ticks, unsigned long long freq);

// Instante atual em nanossegundos desde a partida da maquina, ou 0 se o contador de
// desempenho nao estiver disponivel (o que no Windows XP em diante nao acontece).
unsigned long long nanoSecondsNow();

// O mesmo instante, no formato PARTIDO EM DOIS de 32 bits que o ASIO exige, com `hi`
// antes de `lo`. E esta a funcao que o driver chama, uma vez por bloco de audio.
void getNanoSeconds(ASIOTimeStamp* ts);

}

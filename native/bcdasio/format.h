#pragma once

namespace bcd {

// Formato fixo da BCD3000.
const int kChannels      = 4;
const int kBytesPerSample = 2;
const int kBytesPerFrame = kChannels * kBytesPerSample;   // 8
const int kSampleRate    = 44100;

// Bloco de transporte: 10 ms exatos.
//
// O endpoint aceita no maximo 360 bytes por quadro USB de 1 ms, o que daria
// 45 amostras/ms = 45000 Hz. Enviar sempre o pacote cheio desafinaria o audio
// em 2%. A correcao e submeter blocos de 441 frames (3528 bytes): o WinUSB
// fatia em 9 pacotes de 360 e um de 288, cuja media da 44,1 frames/ms exatos.
const int kFramesPerBlock    = 441;                                   // 44100 / 100
const int kBlockBytes        = kFramesPerBlock * kBytesPerFrame;      // 3528
const int kUsbFramesPerBlock = 10;                                    // quadros de 1 ms

// Verifica se um dado wMaxPacketSize permite o bloco de 10 ms.
// Retorna kBlockBytes se permitir, ou 0 se nao permitir.
//
// Tres condicoes: nenhum pacote pode partir um frame de audio (todo tamanho
// multiplo de kBytesPerFrame), o bloco nao pode ocupar mais de
// kUsbFramesPerBlock quadros, e maxPacketSize precisa ser positivo.
int blockBytesFor(int maxPacketSize);

// Intercala 4 buffers por canal num bloco de frames de 8 bytes.
// Um ponteiro nulo em ch[i] produz silencio naquele canal.
void interleave4(const short* const* ch, short* dst, int frames);

// Desintercala um bloco em 4 buffers por canal.
// Um ponteiro nulo em ch[i] e simplesmente ignorado.
void deinterleave4(const short* src, short* const* ch, int frames);

}

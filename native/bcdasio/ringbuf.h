#pragma once

namespace bcd {

// Buffer circular de bytes para uso de UM UNICO thread.
//
// No driver, o thread de audio e o unico que escreve e le: ele recolhe a
// saida do software de DJ e a entrega ao USB, e faz o inverso na entrada.
// Como nao ha concorrencia, nao existem atomicos nem travas aqui. Usar esta
// classe a partir de dois threads e um erro de programacao.
class ByteRing {
public:
    ByteRing();
    ~ByteRing();

    // Aloca o buffer. capacityBytes e arredondado para cima ate a proxima
    // potencia de dois, e precisa estar entre 1 e 16 MiB (1 << 24).
    // Retorna false se nao houver memoria ou se o valor pedido estiver
    // fora dessa faixa.
    bool init(int capacityBytes);

    void reset();

    int capacity() const { return cap_; }
    int used() const     { return (int)(head_ - tail_); }
    int space() const    { return cap_ - used(); }

    // Escreve ate `bytes`; retorna quantos coube.
    int write(const void* src, int bytes);

    // Le ate `bytes`; retorna quantos havia.
    int read(void* dst, int bytes);

    // Descarta ate `bytes` dos mais antigos; retorna quantos descartou.
    int discard(int bytes);

private:
    unsigned char* buf_;
    int            cap_;
    unsigned int   mask_;
    unsigned int   head_;   // total escrito
    unsigned int   tail_;   // total lido
};

}

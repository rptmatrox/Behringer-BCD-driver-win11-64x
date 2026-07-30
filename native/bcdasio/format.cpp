#include "format.h"

namespace bcd {

int blockBytesFor(int maxPacketSize)
{
    if (maxPacketSize <= 0)
        return 0;
    if (maxPacketSize % kBytesPerFrame != 0)
        return 0;                       // partiria um frame entre pacotes

    const int remainder = kBlockBytes % maxPacketSize;
    if (remainder % kBytesPerFrame != 0)
        return 0;                       // o ultimo pacote partiria um frame

    const int packets = kBlockBytes / maxPacketSize + (remainder ? 1 : 0);
    if (packets > kUsbFramesPerBlock)
        return 0;                       // levaria mais de 10 ms

    return kBlockBytes;
}

void interleave4(const short* const* ch, short* dst, int frames)
{
    for (int c = 0; c < kChannels; c++) {
        const short* src = ch[c];
        short*       out = dst + c;
        if (src) {
            for (int i = 0; i < frames; i++, out += kChannels)
                *out = src[i];
        } else {
            for (int i = 0; i < frames; i++, out += kChannels)
                *out = 0;
        }
    }
}

void deinterleave4(const short* src, short* const* ch, int frames)
{
    for (int c = 0; c < kChannels; c++) {
        short* out = ch[c];
        if (!out)
            continue;
        const short* in = src + c;
        for (int i = 0; i < frames; i++, in += kChannels)
            out[i] = *in;
    }
}

}

#include "ringbuf.h"

#include <new>       // std::nothrow
#include <string.h>

namespace bcd {

ByteRing::ByteRing()
    : buf_(0), cap_(0), mask_(0), head_(0), tail_(0)
{
}

ByteRing::~ByteRing()
{
    delete[] buf_;
    buf_ = 0;
}

bool ByteRing::init(int capacityBytes)
{
    delete[] buf_;
    buf_ = 0;
    cap_ = 0;
    mask_ = 0;
    head_ = tail_ = 0;

    if (capacityBytes <= 0 || capacityBytes > (1 << 24))
        return false;

    int cap = 1;
    while (cap < capacityBytes)
        cap <<= 1;

    buf_ = new (std::nothrow) unsigned char[cap];
    if (!buf_)
        return false;

    cap_  = cap;
    mask_ = (unsigned int)(cap - 1);
    return true;
}

void ByteRing::reset()
{
    head_ = tail_ = 0;
}

int ByteRing::write(const void* src, int bytes)
{
    if (!buf_ || bytes <= 0)
        return 0;

    int n = space();
    if (bytes < n)
        n = bytes;
    if (n <= 0)
        return 0;

    const unsigned char* s = (const unsigned char*)src;
    unsigned int start = head_ & mask_;
    int first = cap_ - (int)start;
    if (first > n)
        first = n;

    memcpy(buf_ + start, s, first);
    if (n > first)
        memcpy(buf_, s + first, n - first);

    head_ += (unsigned int)n;
    return n;
}

int ByteRing::read(void* dst, int bytes)
{
    if (!buf_ || bytes <= 0)
        return 0;

    int n = used();
    if (bytes < n)
        n = bytes;
    if (n <= 0)
        return 0;

    unsigned char* d = (unsigned char*)dst;
    unsigned int start = tail_ & mask_;
    int first = cap_ - (int)start;
    if (first > n)
        first = n;

    memcpy(d, buf_ + start, first);
    if (n > first)
        memcpy(d + first, buf_, n - first);

    tail_ += (unsigned int)n;
    return n;
}

int ByteRing::discard(int bytes)
{
    if (!buf_ || bytes <= 0)
        return 0;

    int n = used();
    if (bytes < n)
        n = bytes;
    if (n <= 0)
        return 0;

    tail_ += (unsigned int)n;
    return n;
}

}

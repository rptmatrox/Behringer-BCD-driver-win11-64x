// BcdMidi.dll - the product's virtual MIDI port, on Windows MIDI Services.
//
// This is the only file in the product that talks to Microsoft's MIDI API, and
// it never mentions any third-party MIDI library. See the design spec, D2.
//
// SHAPE OF THE PORT, and why. Measured on this machine on 2026-08-01, not
// guessed:
//   * CreateOnlyUmpEndpoints stays at its default, false. That default is what
//     makes the service also publish the legacy MIDI 1.0 view, which is the
//     only thing a WinMM application such as VirtualDJ can see. Turning it on
//     still creates the endpoint and still reports success - and the port
//     simply never appears in any DJ software. selftest.cpp checks WinMM
//     visibility, not creation, for exactly that reason.
//   * TWO function blocks, one input and one output, BOTH ON GROUP 0. One
//     block per direction on groups 0 and 1 also works, but the service then
//     names the output side "<name> Gr 2", and VirtualDJ's factory mapping
//     matches on the name. Both on group 0 gives a clean name in both
//     directions.
//   * The function blocks are deliberately NOT named after the endpoint. If a
//     block name ever started leaking into the WinMM port name, a block called
//     "BCD3000" would hide the leak by matching anyway. See the note at
//     kOutputBlockName.
//
// THREADING. Every WinRT object below lives on one dedicated MTA thread per
// port, created here and owned here, and is released on that same thread. Two
// reasons, both practical:
//   * the caller is Python through ctypes, which gives no promise about which
//     thread calls in, whether it initialised COM, or in which apartment;
//   * the create and the close calls can block inside midisrv forever - see
//     the time-limit note below - and a worker thread is what lets this
//     library return a numeric error instead of hanging the caller.
//
// TIME LIMITS, and the defect they exist for. microsoft/MIDI issue #1047, open
// as of 2026-08-01: after the first virtual port on a machine is closed, the
// MIDI service can stop answering, and later calls into it block with no
// timeout of their own (observed over 10 minutes) rather than failing. A
// caller that hangs tells nobody anything, so both calls give up and report:
//   * create waits kCreateTimeoutMs and then returns NULL with
//     kBcdMidiCreateFailed and HRESULT_FROM_WIN32(ERROR_TIMEOUT);
//   * close waits kCloseTimeoutMs and then returns anyway.
//
// THE TRANSLATION, and where it lives. The bridge speaks MIDI 1.0 bytes;
// Windows MIDI Services speaks Universal MIDI Packets. This file is the only
// place in the product that knows what a UMP is, in either direction, and the
// rules it follows are not invented here - they are the rules the bridge's
// midi_to_usb already follows, because the bridge's 100-check self-test pins
// them and two translators that disagree would show up as "some controls work
// in one mode and not the other".
//   * 0x80..0xEF, channel voice -> UMP message type 2 on group 0. Two data
//     bytes, except Program Change and Channel Pressure which carry one.
//   * 0xF8..0xFF, system real time -> UMP message type 1, one byte, and it
//     never disturbs a channel message being assembled around it.
//   * 0xF0..0xF7, System Exclusive and system common -> REFUSED, in both
//     directions. midi_to_usb returns b"" for exactly this range: there is no
//     single USB code index for it, the device generates none of it, and half a
//     SysEx is worse than no SysEx.
//   * a data byte with no status byte before it -> REFUSED, which is
//     midi_to_usb(b"\x40\x40\x40") == b"". RUNNING STATUS is what makes that
//     rule survivable: once a status byte has been seen on this port, later
//     data bytes reuse it, so the "no status" case is only ever the true one -
//     a stream that started in the middle. midi_to_usb cannot do this because
//     it is handed one already-parsed message at a time; that parsing used to
//     be the third-party library's job and is now this file's.
//
// BOTH OF THOSE PATHS ABANDON THE WORKER THREAD AND ITS PORT OBJECT ON
// PURPOSE, AND BOTH SIGNAL stopEvent BEFORE THEY DO. The signal is not
// housekeeping and the leak is not "a few hundred bytes":
//   * what leaks is memory - one thread and one object - and that is accepted,
//     because the worker may be blocked inside the service and still writes
//     through the object, so freeing it is not on the table;
//   * what must NOT leak is the port. stopEvent is manual-reset and starts
//     unsignalled, so setting it before walking away means a worker that is
//     merely slow - not dead - finishes creating the port, comes straight out
//     of its wait and tears it down. Without that signal, a service that
//     answers a minute late would leave a LIVE virtual port carrying the
//     product's name in every DJ application, with nobody holding the pointer
//     and therefore no way to ever close it, and a bridge that retried would
//     stack another one beside it.

#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Midi2.h>
#include <winrt/Windows.Devices.Midi2.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.Transports.Virtual.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "bcdmidi.h"

namespace midi2 = winrt::Windows::Devices::Midi2;
namespace midi2enum = winrt::Windows::Devices::Midi2::Enumeration;
namespace midi2virt = winrt::Windows::Devices::Midi2::Transports::Virtual;

// ---------------------------------------------------------------- constants

// Both function blocks sit on group 0. These are two named constants and not
// one because the difference between them is the whole defect: giving the two
// blocks different groups renames the output port to "<name> Gr 2".
static const uint8_t kOutputBlockGroup = 0;
static const uint8_t kInputBlockGroup = 0;

// Names that share no prefix with any endpoint name we use. If the service
// ever started building the WinMM port name out of the function block name,
// selftest.cpp's exact-name check would go red instead of passing by accident.
static const wchar_t* const kOutputBlockName = L"Control surface out";
static const wchar_t* const kInputBlockName = L"Control surface in";

// See the TIME LIMITS note at the top of this file.
static const DWORD kCreateTimeoutMs = 45000;
static const DWORD kCloseTimeoutMs = 30000;

// How long BcdMidiSend waits for the port's own thread to hand a message to
// the service. Short on purpose: this is the LED path, it runs while somebody
// is playing, and the only way to exceed it is the service defect above. A
// send that runs out of time reports failure rather than blocking the caller.
static const DWORD kSendTimeoutMs = 2000;

// How many UMP words one hand-off to the port thread carries. A MIDI message
// is one word here, so this is "messages per trip"; a longer stream is simply
// sent in several trips. It bounds a fixed-size buffer inside the port object
// so that no caller-controlled length ever decides an allocation.
static const unsigned int kSendBatchWords = 64;

// UMP message types this file uses. The whole 4-bit field is in the top nibble
// of word 0.
static const uint32_t kUmpSystemRealTime32 = 0x1u;   // MidiMessageType::SystemCommon32
static const uint32_t kUmpMidi1ChannelVoice32 = 0x2u; // MidiMessageType::Midi1ChannelVoice32
static const uint32_t kUmpMidi2ChannelVoice64 = 0x4u; // MidiMessageType::Midi2ChannelVoice64

// The longest MIDI 1.0 message this DLL ever hands to the callback.
static const unsigned int kMaxMidi1MessageBytes = 3;

// ------------------------------------------------------------- error coding

// A failure has TWO outputs and they are not packed together: a category, and
// the whole HRESULT. See the enum comment in bcdmidi.h for why the first draft
// of this - category in the low bits, half an HRESULT in the high ones - was
// wrong and had to go.
static long win32AsHresult(unsigned long win32)
{
    return static_cast<long>(HRESULT_FROM_WIN32(win32));
}

// -------------------------------------------------------------- port object

// The value a live port carries, and the reason it is not zero or one: it has
// to be something a freed or a wild pointer is unlikely to be holding. Cleared
// atomically before the object is freed, so a second BcdMidiClosePort on the
// same handle finds it already gone. 'B','C','M','P'.
static const unsigned int kPortMagic = 0x42434D50u;

struct BcdMidiPort
{
    std::atomic<unsigned int> magic{ kPortMagic };

    BcdMidiRecvCb cb = nullptr;
    void* user = nullptr;
    std::wstring name;

    HANDLE readyEvent = nullptr;  // worker -> caller: creation finished, err is set
    HANDLE stopEvent = nullptr;   // caller -> worker: tear the port down
    HANDLE doneEvent = nullptr;   // worker -> caller: teardown finished
    std::thread worker;

    std::atomic<unsigned int> err{ kBcdMidiOk };
    std::atomic<long> hr{ 0 };

    // ---------------------------------------------------------- send path
    //
    // A send does NOT touch WinRT on the caller's thread, and that is the same
    // rule the rest of this file follows: the caller is Python through ctypes
    // and promises nothing about its apartment, or about having initialised COM
    // at all. So BcdMidiSend does the translation - which is arithmetic and
    // needs nothing - stages the resulting words here, wakes the port thread,
    // and waits for it to report what the service said. The wait is what lets
    // the return value mean "it left" instead of "it was queued".
    std::mutex sendLock;              // one sender at a time owns the staging area
    HANDLE sendRequestEvent = nullptr; // caller -> worker: words are staged
    HANDLE sendDoneEvent = nullptr;    // worker -> caller: the service answered
    uint32_t sendWords[kSendBatchWords];
    unsigned int sendWordCount = 0;
    std::atomic<int> sendOk{ 0 };
    // Set when a hand-off ran out of time. The staging area still belongs to a
    // worker that may yet wake up and read it, so the next sender has to see
    // that hand-off finish before it may overwrite anything.
    bool sendStalled = false;

    // Running status, carried ACROSS calls because the caller is allowed to
    // split a stream anywhere. Guarded by sendLock like everything else here.
    unsigned char runStatus = 0;      // 0 = no status seen yet on this port
    unsigned char runData[2] = { 0, 0 };
    unsigned int runDataCount = 0;

    // ------------------------------------------------------- receive path
    //
    // The event handler runs on a Windows MIDI Services thread, which is
    // nobody's thread here, so teardown cannot simply assume it has stopped.
    // recvOpen is cleared BEFORE the in-flight count is read, and the handler
    // increments the count BEFORE it reads recvOpen; between those two orders,
    // a handler that starts after teardown began cannot get as far as the
    // callback, and one that started before it is waited for.
    std::atomic<bool> recvOpen{ true };
    std::atomic<int> recvInFlight{ 0 };
};

static void closeHandles(BcdMidiPort* p)
{
    if (p->readyEvent)       ::CloseHandle(p->readyEvent);
    if (p->stopEvent)        ::CloseHandle(p->stopEvent);
    if (p->doneEvent)        ::CloseHandle(p->doneEvent);
    if (p->sendRequestEvent) ::CloseHandle(p->sendRequestEvent);
    if (p->sendDoneEvent)    ::CloseHandle(p->sendDoneEvent);
    p->readyEvent = p->stopEvent = p->doneEvent = nullptr;
    p->sendRequestEvent = p->sendDoneEvent = nullptr;
}

// --------------------------------------------------- MIDI 1.0 <-> UMP
//
// Everything in this section is arithmetic. It touches no WinRT, no handle and
// no lock of its own, so it runs wherever it is called from: the caller's
// thread on the way out, a Windows MIDI Services thread on the way in.

// How many DATA bytes follow this channel-voice status byte. Program Change
// and Channel Pressure carry one; everything else in 0x80..0xEF carries two.
// This is the same split the bridge's CIN_LEN table encodes (CIN 0xC and 0xD
// are the two-byte entries, and a two-byte USB packet is a one-data-byte
// message).
static unsigned int midi1DataByteCount(unsigned char status)
{
    const unsigned char hi = static_cast<unsigned char>(status & 0xF0u);
    return (hi == 0xC0u || hi == 0xD0u) ? 1u : 2u;
}

// One 32-bit UMP: message type, group, status byte, and up to two data bytes.
static uint32_t umpWord32(uint32_t messageType, uint8_t group,
                          unsigned char status, unsigned char d1, unsigned char d2)
{
    return ((messageType & 0x0Fu) << 28)
         | ((static_cast<uint32_t>(group) & 0x0Fu) << 24)
         | (static_cast<uint32_t>(status) << 16)
         | ((static_cast<uint32_t>(d1) & 0x7Fu) << 8)
         | (static_cast<uint32_t>(d2) & 0x7Fu);
}

// Feeds ONE byte of the outgoing stream. Returns true, and fills `word`, on the
// byte that completed a message. See the translation note at the top of the
// file for the rules; this function is those rules.
static bool feedMidi1Byte(BcdMidiPort* p, unsigned char b, uint32_t& word)
{
    if (b >= 0xF8u)
    {
        // System real time. One byte, and it is legal for it to arrive in the
        // MIDDLE of another message - which is why this returns before touching
        // runStatus or runDataCount.
        word = umpWord32(kUmpSystemRealTime32, kOutputBlockGroup, b, 0, 0);
        return true;
    }

    if (b >= 0xF0u)
    {
        // System Exclusive and system common: refused, exactly as midi_to_usb
        // refuses 0xF0..0xF7. Both cancel running status per the MIDI 1.0
        // specification, and clearing it here is what stops the data bytes of a
        // dropped SysEx from being read as the tail of the channel message that
        // happened to come before it.
        p->runStatus = 0;
        p->runDataCount = 0;
        return false;
    }

    if ((b & 0x80u) != 0)
    {
        // A new status byte abandons whatever was half-assembled. That is the
        // honest reading of a truncated message: the sender moved on.
        p->runStatus = b;
        p->runDataCount = 0;
        return false;
    }

    if (p->runStatus == 0)
        return false;   // midi_to_usb(b"\x40\x40\x40") == b""

    p->runData[p->runDataCount] = static_cast<unsigned char>(b & 0x7Fu);
    ++p->runDataCount;
    if (p->runDataCount < midi1DataByteCount(p->runStatus))
        return false;

    word = umpWord32(kUmpMidi1ChannelVoice32, kOutputBlockGroup, p->runStatus,
                     p->runData[0],
                     p->runDataCount > 1 ? p->runData[1] : static_cast<unsigned char>(0));
    p->runDataCount = 0;   // running status: the next data byte starts a new one
    return true;
}

// Turns an incoming UMP into MIDI 1.0 bytes. Returns how many bytes were
// written to `out` (which must hold kMaxMidi1MessageBytes), or 0 for a packet
// this product has nothing to do with.
static unsigned int midi1FromUmp(uint32_t w0, uint32_t w1, unsigned char* out)
{
    const uint32_t messageType = (w0 >> 28) & 0x0Fu;

    if (messageType == kUmpMidi1ChannelVoice32)
    {
        const unsigned char status = static_cast<unsigned char>((w0 >> 16) & 0xFFu);
        if (status < 0x80u || status >= 0xF0u)
            return 0;   // not a channel voice status; the packet is malformed
        out[0] = status;
        out[1] = static_cast<unsigned char>((w0 >> 8) & 0x7Fu);
        if (midi1DataByteCount(status) == 1u)
            return 2;
        out[2] = static_cast<unsigned char>(w0 & 0x7Fu);
        return 3;
    }

    if (messageType == kUmpSystemRealTime32)
    {
        const unsigned char status = static_cast<unsigned char>((w0 >> 16) & 0xFFu);
        if (status >= 0xF8u)
        {
            out[0] = status;
            return 1;
        }
        return 0;   // system common: refused on the way in too
    }

    if (messageType == kUmpMidi2ChannelVoice64)
    {
        // MIDI 2.0 channel voice, DOWNSCALED. This branch exists because of a
        // thing that is not measured on this machine and cannot be, cheaply:
        // the endpoint declares support for both protocols, and whether the
        // service hands a legacy application's Note On to us as message type 2
        // or upscales it to message type 4 depends on what the endpoint
        // negotiates. Dropping type 4 would mean every control silently doing
        // nothing, with no error anywhere. Forty lines is a cheap insurance
        // premium against a defect whose only symptom is silence.
        //
        // A PLAIN SHIFT IS THE EXACT INVERSE OF THE SPECIFIED UPSCALING, which
        // is why there is no table here. Min-centre-max upscaling of a 7-bit
        // value to 16 bits produces (v << 9) plus a remainder strictly below
        // 512, so (x >> 9) returns v unchanged. The same holds for 7 -> 32
        // (>> 25) and for 14 -> 32 (>> 18).
        const unsigned char opcode = static_cast<unsigned char>((w0 >> 20) & 0x0Fu);
        const unsigned char channel = static_cast<unsigned char>((w0 >> 16) & 0x0Fu);
        const unsigned char index = static_cast<unsigned char>((w0 >> 8) & 0x7Fu);

        switch (opcode)
        {
        case 0x8:   // Note Off
        case 0x9:   // Note On - velocity is the HIGH half of word 1
        {
            unsigned char vel = static_cast<unsigned char>((w1 >> 25) & 0x7Fu);
            // A MIDI 2.0 Note On with a non-zero velocity must not become a
            // MIDI 1.0 Note Off, and velocity 0 is exactly that.
            if (opcode == 0x9 && vel == 0 && ((w1 >> 16) & 0xFFFFu) != 0)
                vel = 1;
            out[0] = static_cast<unsigned char>((opcode << 4) | channel);
            out[1] = index;
            out[2] = vel;
            return 3;
        }
        case 0xA:   // Poly Pressure
        case 0xB:   // Control Change
            out[0] = static_cast<unsigned char>((opcode << 4) | channel);
            out[1] = index;
            out[2] = static_cast<unsigned char>((w1 >> 25) & 0x7Fu);
            return 3;
        case 0xC:   // Program Change - the bank select flags are dropped
            out[0] = static_cast<unsigned char>((opcode << 4) | channel);
            out[1] = static_cast<unsigned char>((w1 >> 24) & 0x7Fu);
            return 2;
        case 0xD:   // Channel Pressure
            out[0] = static_cast<unsigned char>((opcode << 4) | channel);
            out[1] = static_cast<unsigned char>((w1 >> 25) & 0x7Fu);
            return 2;
        case 0xE:   // Pitch Bend - 14 bits, low seven first
        {
            const uint32_t v14 = (w1 >> 18) & 0x3FFFu;
            out[0] = static_cast<unsigned char>((opcode << 4) | channel);
            out[1] = static_cast<unsigned char>(v14 & 0x7Fu);
            out[2] = static_cast<unsigned char>((v14 >> 7) & 0x7Fu);
            return 3;
        }
        default:
            // Per-note controllers, registered and assignable controllers, and
            // the rest of MIDI 2.0 have no MIDI 1.0 spelling worth guessing at.
            return 0;
        }
    }

    return 0;
}

// ------------------------------------------------------------ the WinRT part

// Runs on a Windows MIDI Services thread, once per incoming packet. NOTHING
// may escape it: it is called from a C++/WinRT event source that has no idea
// what a BcdMidiPort is, and the callback it ends in belongs to Python.
static void onMessageReceived(BcdMidiPort* p,
                              midi2::MidiMessageReceivedEventArgs const& args) noexcept
{
    // Announce this handler BEFORE reading recvOpen. Teardown clears recvOpen
    // before it reads this counter, so the two orders together mean no handler
    // can reach the callback after teardown has decided it is done waiting.
    p->recvInFlight.fetch_add(1);

    try
    {
        if (p->recvOpen.load() && p->cb != nullptr)
        {
            uint32_t w0 = 0, w1 = 0, w2 = 0, w3 = 0;
            args.FillWords(w0, w1, w2, w3);

            unsigned char out[kMaxMidi1MessageBytes] = { 0, 0, 0 };
            const unsigned int n = midi1FromUmp(w0, w1, out);
            if (n != 0)
                p->cb(p->user, out, n);
        }
    }
    catch (...)
    {
        // There is no channel to report on here and no caller to report to.
        // Swallowing is the only option that keeps the service's thread alive.
    }

    p->recvInFlight.fetch_sub(1);
}

// Runs on the port's own MTA thread. Returns 0 and three live objects, or a
// numeric code and nothing. Throws only winrt::hresult_error, which the caller
// below turns into a number; nothing escapes this file.
static unsigned int createDevice(BcdMidiPort* p,
                                 midi2virt::MidiVirtualDevice& device,
                                 midi2::MidiSession& session,
                                 midi2::MidiEndpointConnection& connection,
                                 winrt::event_token& recvToken)
{
    std::wstring const& name = p->name;

    if (!midi2::MidiApi::EnsureServiceAvailable())
        return kBcdMidiServiceMissing;

    if (!midi2virt::MidiVirtualDeviceManager::IsTransportAvailable())
        return kBcdMidiTransportMissing;

    // The service derives the device instance id from this, so it has to be
    // unique: reusing one collides with any registration left behind by an
    // earlier port. Process id plus a counter covers both several ports in one
    // process and several processes at once.
    static std::atomic<unsigned long> instanceSeq{ 0 };
    wchar_t instanceId[64];
    ::swprintf_s(instanceId, L"BCD3000WMS%lu_%lu",
                 static_cast<unsigned long>(::GetCurrentProcessId()),
                 instanceSeq.fetch_add(1));

    midi2enum::MidiDeclaredEndpointInfo info{};
    info.Name(name);
    info.ProductInstanceId(instanceId);
    info.SupportsMidi10Protocol(true);
    info.SupportsMidi20Protocol(true);
    info.SupportsReceivingJitterReductionTimestamps(false);
    info.SupportsSendingJitterReductionTimestamps(false);
    info.HasStaticFunctionBlocks(true);
    info.DeclaredFunctionBlockCount(uint8_t{ 2 });
    info.SpecificationVersionMajor(uint8_t{ 1 });
    info.SpecificationVersionMinor(uint8_t{ 1 });

    // (name, description, manufacturer, declaredEndpointInfo) - the four
    // argument overload, read off the generated projection, not assumed.
    midi2virt::MidiVirtualDeviceCreationConfig config(
        name,
        L"BCD3000 control surface",
        L"BCD3000 driver",
        info);

    // false is already the default; writing it anyway pins the one setting the
    // whole migration depends on, in the place a reader looks for it, and
    // survives a future SDK changing that default. Read the note at the top of
    // this file before touching this line: true creates the endpoint, reports
    // success, and produces a port no DJ software can see.
    config.CreateOnlyUmpEndpoints(false);

    midi2enum::MidiFunctionBlock fbOut{};
    fbOut.Number(uint8_t{ 0 });
    fbOut.Name(kOutputBlockName);
    fbOut.IsActive(true);
    fbOut.Direction(midi2enum::MidiFunctionBlockDirection::BlockOutput);
    fbOut.UIHint(midi2enum::MidiFunctionBlockUIHint::Sender);
    fbOut.RepresentsMidi10Connection(midi2enum::MidiFunctionBlockRepresentsMidi10Connection::Not10);
    fbOut.FirstGroup(midi2::MidiGroup(kOutputBlockGroup));
    fbOut.GroupCount(uint8_t{ 1 });
    config.FunctionBlocks().Append(fbOut);

    midi2enum::MidiFunctionBlock fbIn{};
    fbIn.Number(uint8_t{ 1 });
    fbIn.Name(kInputBlockName);
    fbIn.IsActive(true);
    fbIn.Direction(midi2enum::MidiFunctionBlockDirection::BlockInput);
    fbIn.UIHint(midi2enum::MidiFunctionBlockUIHint::Receiver);
    fbIn.RepresentsMidi10Connection(midi2enum::MidiFunctionBlockRepresentsMidi10Connection::Not10);
    fbIn.FirstGroup(midi2::MidiGroup(kInputBlockGroup));
    fbIn.GroupCount(uint8_t{ 1 });
    config.FunctionBlocks().Append(fbIn);

    device = midi2virt::MidiVirtualDeviceManager::CreateVirtualDevice(config);
    if (device == nullptr)
        return kBcdMidiCreateFailed;

    // The device side of the endpoint has to be connected and open, or the
    // endpoint exists on paper and carries nothing.
    session = midi2::MidiSession::Create(L"BCD3000 driver");
    if (session == nullptr)
        return kBcdMidiOpenFailed;

    connection = session.CreateEndpointConnection(device.DeviceEndpointDeviceId());
    if (connection == nullptr)
        return kBcdMidiOpenFailed;

    connection.AddMessageProcessingPlugin(device);

    // BEFORE Open, not after. Open is what starts the flow of messages, and a
    // handler registered afterwards would silently miss anything that arrived
    // in between - which on this port is whatever the DJ software sends the
    // instant the name appears.
    recvToken = connection.MessageReceived(
        [p](midi2::IMidiMessageReceivedEventSource const&,
            midi2::MidiMessageReceivedEventArgs const& args)
        {
            onMessageReceived(p, args);
        });

    if (!connection.Open())
        return kBcdMidiOpenFailed;

    return kBcdMidiOk;
}

// Owns the port for its whole life: creates it, parks until close, tears it
// down. The three WinRT objects are locals so that they are released on this
// thread and nowhere else.
static void portThread(BcdMidiPort* p)
{
    midi2virt::MidiVirtualDevice device{ nullptr };
    midi2::MidiSession session{ nullptr };
    midi2::MidiEndpointConnection connection{ nullptr };
    winrt::event_token recvToken{};

    // "Send this now" as the service spells it. Read once, on this thread,
    // because reading it is a WinRT activation-factory call and BcdMidiSend is
    // not allowed to make one.
    uint64_t sendNow = 0;

    unsigned int err = kBcdMidiOk;
    long hr = 0;
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        err = createDevice(p, device, session, connection, recvToken);
        if (err == kBcdMidiOk)
            sendNow = midi2::MidiClock::TimestampConstantSendImmediately();
    }
    catch (winrt::hresult_error const& e)
    {
        err = kBcdMidiException;
        hr = static_cast<long>(static_cast<int32_t>(e.code()));
    }
    catch (std::bad_alloc const&)
    {
        err = kBcdMidiException;
        hr = static_cast<long>(E_OUTOFMEMORY);
    }
    catch (...)
    {
        // No HRESULT to report, and saying 0 is the honest version of that.
        err = kBcdMidiException;
        hr = 0;
    }

    // hr before err before the event: the caller reads them in the other
    // order, after the wait returns.
    p->hr.store(hr);
    p->err.store(err);
    ::SetEvent(p->readyEvent);

    // The park is no longer a plain wait: it also serves the sends. Every WinRT
    // call this port ever makes happens on this thread, which is the same rule
    // the create and the teardown already follow, and for the same reason - the
    // caller's apartment is unknown and unknowable.
    if (err == kBcdMidiOk)
    {
        HANDLE waits[2] = { p->stopEvent, p->sendRequestEvent };
        for (;;)
        {
            const DWORD w = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0 + 1)
                break;   // the stop, or a wait that failed: either way, tear down

            int ok = 1;
            try
            {
                for (unsigned int i = 0; i < p->sendWordCount; ++i)
                {
                    const midi2::MidiSendMessageResults r =
                        connection.SendSingleMessageWords(sendNow, p->sendWords[i]);
                    if ((r & midi2::MidiSendMessageResults::Succeeded)
                            != midi2::MidiSendMessageResults::Succeeded)
                        ok = 0;
                }
            }
            catch (...)
            {
                ok = 0;
            }
            p->sendOk.store(ok);
            ::SetEvent(p->sendDoneEvent);
        }
    }

    // A sender that is waiting right now must not sit out its whole timeout for
    // an answer that is never coming. Answering "it did not leave" is both true
    // and immediate.
    p->sendOk.store(0);
    ::SetEvent(p->sendDoneEvent);

    // Stop the receive side before releasing anything it reads. Clearing the
    // flag first and reading the counter second is the half of the handshake
    // that lives here; the other half is in onMessageReceived. The bounded
    // spin is bounded on purpose: a handler that has been inside the caller's
    // callback for a second is a caller that is not coming back, and hanging
    // the close on it would only spread the problem.
    p->recvOpen.store(false);
    try
    {
        if (connection != nullptr && recvToken)
            connection.MessageReceived(recvToken);
    }
    catch (...)
    {
    }
    for (int i = 0; i < 1000 && p->recvInFlight.load() != 0; ++i)
        ::Sleep(1);

    try
    {
        if (connection != nullptr && session != nullptr)
            session.DisconnectEndpointConnection(connection.ConnectionId());
        connection = nullptr;
        device = nullptr;
        if (session != nullptr)
            session.Close();
        session = nullptr;
    }
    catch (winrt::hresult_error const&)
    {
        // Nothing useful is left to do with it: the caller has already been
        // told the port is closing and there is no channel to report on.
    }
    catch (...)
    {
    }

    ::SetEvent(p->doneEvent);
}

// ------------------------------------------------------------- the C surface

extern "C" {

void* BcdMidiCreatePort(const wchar_t* name, BcdMidiRecvCb cb, void* user,
                        unsigned int* errOut, long* hrOut)
{
    // A caller that wants only half of the answer gets it; everything below
    // writes through these two either way, so no path can forget one.
    unsigned int discardedErr = 0;
    long discardedHr = 0;
    unsigned int* out = errOut ? errOut : &discardedErr;
    long* hrp = hrOut ? hrOut : &discardedHr;
    *out = kBcdMidiOk;
    *hrp = 0;

    if (name == nullptr || name[0] == L'\0')
    {
        *out = kBcdMidiBadArgument;
        *hrp = static_cast<long>(E_INVALIDARG);
        return nullptr;
    }

    BcdMidiPort* p = nullptr;
    try
    {
        p = new BcdMidiPort();
        // These three belong INSIDE the try. std::wstring::operator= allocates
        // and can throw std::bad_alloc, and an exception that leaves this
        // function crosses the DLL boundary into a C caller - into Python,
        // in the shape we actually ship. Vanishingly unlikely is not the same
        // as impossible, and the requirement is that nothing escapes.
        p->cb = cb;
        p->user = user;
        p->name = name;
    }
    catch (...)
    {
        delete p;
        *out = kBcdMidiCreateFailed;
        *hrp = static_cast<long>(E_OUTOFMEMORY);
        return nullptr;
    }

    p->readyEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p->doneEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // The request is AUTO-reset: the port thread's wait consumes it, so one
    // signal is one batch and a batch cannot be served twice. The answer is
    // MANUAL-reset and is cleared by the sender just before it asks, which is
    // what stops a hand-off that timed out from leaving a stale "done" behind
    // for the next one to mistake for its own answer.
    p->sendRequestEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    p->sendDoneEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!p->readyEvent || !p->stopEvent || !p->doneEvent ||
        !p->sendRequestEvent || !p->sendDoneEvent)
    {
        const unsigned long win32 = ::GetLastError();
        closeHandles(p);
        delete p;
        *out = kBcdMidiCreateFailed;
        *hrp = win32AsHresult(win32);
        return nullptr;
    }

    try
    {
        p->worker = std::thread(portThread, p);
    }
    catch (...)
    {
        closeHandles(p);
        delete p;
        *out = kBcdMidiCreateFailed;
        *hrp = win32AsHresult(ERROR_MAX_THRDS_REACHED);
        return nullptr;
    }

    if (::WaitForSingleObject(p->readyEvent, kCreateTimeoutMs) != WAIT_OBJECT_0)
    {
        // The service never answered inside the time limit. THE ORDER OF THE
        // NEXT TWO LINES IS THE WHOLE POINT.
        //
        // Signal the stop FIRST. stopEvent is manual-reset and was created
        // unsignalled, so setting it now means that if midisrv eventually does
        // answer, the worker finishes creating the port, walks straight out of
        // its WaitForSingleObject and tears the port down. Without this line
        // the late-answering worker would park on that wait forever holding a
        // LIVE VIRTUAL MIDI PORT - one carrying the product's name, appearing
        // in every DJ application minutes after the caller was told the create
        // failed, with nobody left holding p and therefore no way left to close
        // it. A bridge that retried would stack another one on top.
        //
        // The pointer and the thread are still abandoned deliberately: the
        // worker may be blocked inside the service and still writes through p,
        // so freeing p is not an option. What is abandoned is memory. What must
        // not be abandoned is the port.
        ::SetEvent(p->stopEvent);
        p->worker.detach();
        *out = kBcdMidiCreateFailed;
        *hrp = win32AsHresult(ERROR_TIMEOUT);
        return nullptr;
    }

    const unsigned int err = p->err.load();
    const long hr = p->hr.load();
    if (err != kBcdMidiOk)
    {
        // The worker has already run its teardown and is on its way out.
        if (p->worker.joinable())
            p->worker.join();
        p->magic.store(0);
        closeHandles(p);
        delete p;
        *out = err;
        *hrp = hr;
        return nullptr;
    }

    *out = kBcdMidiOk;
    *hrp = 0;
    return p;
}

void BcdMidiClosePort(void* port)
{
    if (port == nullptr)
        return;

    BcdMidiPort* p = static_cast<BcdMidiPort*>(port);

    // Claim the port, once, atomically. exchange() both reads the marker and
    // clears it in one step, so two threads closing the same handle at the same
    // moment cannot both get past this line, and the ordinary Python sequence -
    // an explicit close() followed by a __del__ that still holds the same
    // integer - stops here on the second call instead of calling SetEvent on
    // freed memory. bcdmidi.h states plainly what this does and does not buy.
    if (p->magic.exchange(0) != kPortMagic)
        return;

    ::SetEvent(p->stopEvent);

    if (::WaitForSingleObject(p->doneEvent, kCloseTimeoutMs) != WAIT_OBJECT_0)
    {
        // Same abandonment as in create, and for the same defect. Returning
        // matters more than tidiness: the caller has other things to shut down
        // and a hang here would take all of them with it. The stop is already
        // signalled above, so the worker tears the port down whenever the
        // service finally answers - what is abandoned here is memory, not a
        // live port.
        p->worker.detach();
        return;
    }

    if (p->worker.joinable())
        p->worker.join();
    closeHandles(p);
    delete p;
}

// Stages one batch of UMP words and waits for the port thread to tell us what
// the service said. Called with sendLock held.
static bool handOffToPortThread(BcdMidiPort* p, const uint32_t* words, unsigned int n)
{
    if (p->sendStalled)
    {
        // The previous hand-off ran out of time, so the staging area still
        // belongs to a worker that may yet wake up and read it. Overwriting it
        // now would corrupt a message that is still in flight. Ask, without
        // waiting, whether that hand-off has finished since.
        if (::WaitForSingleObject(p->sendDoneEvent, 0) != WAIT_OBJECT_0)
            return false;
        p->sendStalled = false;
    }

    std::memcpy(p->sendWords, words, static_cast<size_t>(n) * sizeof(uint32_t));
    p->sendWordCount = n;
    p->sendOk.store(0);

    ::ResetEvent(p->sendDoneEvent);
    ::SetEvent(p->sendRequestEvent);

    if (::WaitForSingleObject(p->sendDoneEvent, kSendTimeoutMs) != WAIT_OBJECT_0)
    {
        p->sendStalled = true;
        return false;
    }
    return p->sendOk.load() != 0;
}

int BcdMidiSend(void* port, const unsigned char* bytes, unsigned int count)
{
    if (port == nullptr || bytes == nullptr || count == 0)
        return 0;

    BcdMidiPort* p = static_cast<BcdMidiPort*>(port);
    // The same seatbelt BcdMidiClosePort wears, and with the same honesty about
    // its limits: it catches a send on a handle that was already closed, which
    // is the mistake ctypes makes easy. It cannot make a send that RACES a
    // close defined - see the note in bcdmidi.h.
    if (p->magic.load() != kPortMagic)
        return 0;

    std::lock_guard<std::mutex> guard(p->sendLock);

    uint32_t words[kSendBatchWords];
    unsigned int n = 0;
    bool anySent = false;
    bool anyFailed = false;

    for (unsigned int i = 0; i < count; ++i)
    {
        uint32_t word = 0;
        if (!feedMidi1Byte(p, bytes[i], word))
            continue;   // this byte did not finish a message

        words[n] = word;
        ++n;
        if (n == kSendBatchWords)
        {
            if (handOffToPortThread(p, words, n)) anySent = true; else anyFailed = true;
            n = 0;
        }
    }
    if (n != 0)
    {
        if (handOffToPortThread(p, words, n)) anySent = true; else anyFailed = true;
    }

    // Nothing complete in the buffer is NOT success. midi_to_usb answers b"" to
    // the same inputs and injetar_pacote answers False to that, so a caller
    // counting successful sends counts the same thing on both sides of this
    // migration.
    return (anySent && !anyFailed) ? 1 : 0;
}

const char* BcdMidiErrorText(unsigned int err)
{
    // Per-thread so that two threads logging at once cannot overwrite each
    // other's sentence.
    static thread_local char buf[224];

    const char* words;
    switch (err)
    {
    case kBcdMidiOk:               words = "no error"; break;
    case kBcdMidiServiceMissing:   words = "the Windows MIDI service is not available"; break;
    case kBcdMidiTransportMissing: words = "the Windows MIDI virtual device transport is not available"; break;
    case kBcdMidiCreateFailed:     words = "the virtual MIDI device was not created"; break;
    case kBcdMidiOpenFailed:       words = "the connection to the virtual MIDI device would not open"; break;
    case kBcdMidiBadArgument:      words = "bad argument"; break;
    case kBcdMidiException:        words = "the Windows MIDI API raised an exception"; break;
    default:                       words = "unknown error"; break;
    }

    // The digits are never optional. A sentence without them is what cost this
    // project an hour on 2026-08-01; see the note in bcdmidi.h.
    //
    // This says "category" and points at hrOut rather than quoting a number it
    // does not have. The previous version printed "detail 0x05B4 = the last
    // four digits of the HRESULT", which was the best that could be said while
    // half the HRESULT was being thrown away by the encoding. It is a whole
    // HRESULT now, it lives in *hrOut, and the caller is told to print it.
    ::snprintf(buf, sizeof(buf),
               "%s (category %u; the HRESULT, if any, was written to hrOut - log both)",
               words, err);
    return buf;
}

} // extern "C"

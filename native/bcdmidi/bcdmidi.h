/* BcdMidi.dll - the product's virtual MIDI port, built on Windows MIDI
 * Services. This header is the whole contract: the Python bridge and any
 * future C++ caller read nothing else.
 *
 * There is deliberately no reference to the third-party MIDI library this
 * replaces anywhere in here, not even to diagnose. See the design spec,
 * decision D2. Its name is absent on purpose so that a grep for it over the
 * product sources answers honestly.
 *
 * EVERY FAILING ENTRY POINT YIELDS A NUMBER THE CALLER CAN LOG.
 * This is a requirement, not a style. On 2026-08-01 a message that translated
 * error 3 into "port in use by another program" sent the owner hunting for a
 * DJ program that did not exist and cost an hour. Commit 93eea3e fixed that on
 * the bridge side; the rule holds here from the first line. Anything that
 * prints one of these errors must print the digits too.
 */
#ifndef BCDMIDI_H
#define BCDMIDI_H

#ifdef __cplusplus
extern "C" {
#endif

/* The CATEGORY of a failure, written to *errOut and to nothing else. This
 * value carries a category and NEVER has anything packed into its high bits.
 * The HRESULT travels separately, in *hrOut, whole.
 *
 * The first draft of this contract packed the HRESULT into the top 16 bits of
 * this same word, and that was wrong: an unsigned int has 32 bits, so shifting
 * a 32-bit HRESULT up by 16 threw away its severity and its facility, and
 * 0x80070005 and 0x80040005 both arrived as 0x0005. Two out parameters cost
 * one argument and lose nothing.
 */
enum {
    kBcdMidiOk               = 0,
    kBcdMidiServiceMissing   = 1,   /* the Windows MIDI service is not available */
    kBcdMidiTransportMissing = 2,   /* the virtual device transport is not available */
    kBcdMidiCreateFailed     = 3,   /* CreateVirtualDevice did not return a device */
    kBcdMidiOpenFailed       = 4,   /* the connection would not open */
    kBcdMidiBadArgument      = 5,
    kBcdMidiException        = 6    /* a WinRT exception; the whole HRESULT is in *hrOut */
};

/* Delivers MIDI 1.0 bytes that arrived at the port. Registered at create time.
 *
 * ONE COMPLETE MESSAGE PER CALL, NEVER HALF OF ONE AND NEVER TWO. `bytes`
 * always begins with a status byte; running status is already resolved, so a
 * caller never has to remember anything between calls. `count` is 1, 2 or 3.
 * The buffer belongs to the DLL and is only valid for the duration of the call
 * - copy it if you need to keep it.
 *
 * WHAT NEVER ARRIVES, and why the caller does not have to filter it: system
 * common and System Exclusive (0xF0..0xF7) are dropped inside the DLL, because
 * the product has no use for them and half a SysEx is worse than none. System
 * real time (0xF8..0xFF) does arrive, as a single byte.
 *
 * IT RUNS ON A WINDOWS MIDI SERVICES THREAD, not on the caller's. It must not
 * block: whatever it does is in the path of every control the DJ software
 * sends. It must not raise a C++ exception across the DLL boundary either, and
 * it must not call BcdMidiClosePort on its own port. */
typedef void (*BcdMidiRecvCb)(void* user, const unsigned char* bytes, unsigned int count);

/* Creates the virtual port and returns an opaque handle, or NULL on failure.
 *
 * TWO OUT PARAMETERS, AND BOTH ARE ALWAYS WRITTEN:
 *   *errOut  one of the categories above.
 *   *hrOut   the WHOLE HRESULT, unmodified, when the failure has one, and 0
 *            when it does not. Failures that come from Win32 rather than from
 *            WinRT arrive here already wrapped by HRESULT_FROM_WIN32, so a
 *            create that timed out reads 0x800705B4 and not a bare 1460.
 * On success both are set to 0. On failure both are set. Neither is ever left
 * holding a stale value from an earlier call and neither is left untouched.
 * Either pointer may be NULL if the caller does not want that half.
 *
 * THE CALL HAS A TIME LIMIT. If the MIDI service stops answering - a known
 * Windows defect, see bcdmidi.cpp - this returns NULL with
 * kBcdMidiCreateFailed and HRESULT_FROM_WIN32(ERROR_TIMEOUT) instead of
 * blocking the caller forever.
 *
 * NAME BUDGET: 31 usable characters. A WinMM port name lives in MAXPNAMELEN
 * bytes, which is 32 INCLUDING the terminator, so anything longer is silently
 * truncated by Windows - no error, no warning, just a different name than the
 * one asked for. Nothing here rejects a longer name, because truncation is
 * legal and occasionally wanted; THE CALLER OWNS THE CHOICE, and the caller is
 * the bridge. The product's name is `BCD3000`, which is 7 characters. Whoever
 * changes it should know that VirtualDJ's factory mapping matches on exactly
 * that string. */
__declspec(dllexport) void*        BcdMidiCreatePort(const wchar_t* name, BcdMidiRecvCb cb, void* user, unsigned int* errOut, long* hrOut);

/* Destroys the port.
 *
 * Safe with NULL, and safe to call TWICE on the same handle: the second call
 * does nothing. That is not a courtesy, it is for the caller we actually have
 * - Python through ctypes, where an explicit close() plus a __del__ is the
 * ordinary shape and ctypes does not blank the pointer for you. The guard is a
 * marker inside the object, cleared atomically before the object is freed.
 * BE HONEST ABOUT WHAT THAT CAN AND CANNOT DO: it reliably catches the double
 * close that immediately follows a normal close, which is the case that
 * happens. It cannot make reading freed memory defined, and if the allocator
 * has already handed that block to somebody else the marker may read as
 * anything. Treat it as the seatbelt for the one pattern ctypes makes easy,
 * not as licence to close at random.
 *
 * Always returns, even if the service stops answering: see the note on the
 * shutdown time limit in bcdmidi.cpp. */
__declspec(dllexport) void         BcdMidiClosePort(void* port);

/* Pushes MIDI 1.0 bytes out of the port. Non-zero on success.
 *
 * NON-ZERO MEANS THE BYTES REALLY LEFT, not that they were queued. The call
 * hands the translated message to the port's own thread and waits for the
 * service to accept it, so a caller that counts the non-zero returns is
 * counting deliveries. That is deliberate: the bridge already learned, in
 * injetar_pacote, that a send which reports success on a mere attempt turns a
 * port that stopped accepting into something indistinguishable from a perfect
 * session.
 *
 * ZERO MEANS NOTHING LEFT, and that includes the case where there was nothing
 * to send: an empty buffer, a buffer holding only bytes this DLL refuses
 * (System Exclusive and system common, 0xF0..0xF7), or a data byte with no
 * status byte before it to belong to. Those match what the bridge's own
 * midi_to_usb does with the same input, which is to return nothing at all.
 *
 * `bytes` is a STREAM, not necessarily one message. Several messages in one
 * call are fine, a message split across two calls is fine, and running status
 * is carried from one call to the next. Every message that comes out complete
 * is sent; the return value is non-zero only if at least one message was sent
 * AND none of the ones that were tried failed.
 *
 * Safe to call from any thread and from several at once - the calls serialise.
 * It is NOT safe to call while another thread is inside BcdMidiClosePort on
 * the same handle: close frees the port, and the caller owns that ordering. */
__declspec(dllexport) int          BcdMidiSend(void* port, const unsigned char* bytes, unsigned int count);

/* A sentence for a category from the enum above. The returned text ALWAYS
 * carries the number as well as the words, so a log line cannot lose it.
 * It describes the CATEGORY only, and says so: the HRESULT is *hrOut's job.
 * Print both - the category names what failed, the HRESULT names why.
 * The buffer is per-thread and is overwritten by the next call on the same
 * thread; copy it if you need to keep it. Never returns NULL. */
__declspec(dllexport) const char*  BcdMidiErrorText(unsigned int err);

#ifdef __cplusplus
}
#endif

#endif /* BCDMIDI_H */

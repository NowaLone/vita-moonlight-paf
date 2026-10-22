# Main startup crash — 2026-10-22

## Evidence and limits

The maintainer reports a crash while loading Main after the dynamic-host-list change.
Input: `psp2core-1792669346-0x00001332b7-eboot.bin.psp2dmp`, gzip SHA-256
`914ea2106f85c837ed2d990b140609778583c28e3e6ab3ea84cc25f507b78756`.
The extracted ELF core is 2,805,876 bytes. Neither the dump nor extracted process memory
is committed: these can contain private runtime data.

Offline comparison uses the existing maintainer-built system ELF, SHA-256
`6e625ad74a8ba88dd5980f3177614d3f645531b31bc53f5158ab26eaa8e814fd`, and
its VELF, SHA-256 `81b49c31ae2ebaef3b14c7eb65673d4a26c256e32c3982b646da5a5a4038e091`.
No configure, compilation, packaging or deployment was run by the agent.

The core's application RX is `0x81075000`, size `0x294028`, and RW is `0x81340000`,
size `0x73114`. These sizes match the system VELF's RX/RW (including converter metadata).
The debug ELF RX starts at `0x81000000`; relocation delta is therefore `0x75000`.
The normal ELF has different segment sizes/instructions. The faulting code page is absent
from the core, so this is a strongly corroborated mapping, not a byte-for-byte installed
code match or proof of the installed Git revision.

## Fault and root cause

The stopped main thread reports data abort `0x30004`, PC `0x810898d2`, SP `0x813e0d20`.
With the above mapping, ELF PC `0x810148d2` is the inlined `std::fill_n` in
`Main::RefreshHosts()`'s `paf::vector<HostRowState> rows(hosts.size())` construction.
Instruction `stmdb r6, {r0,r1,r2,r3}` has `r6=0x10`, so its destination is address zero.
The source value at `r5=SP+0x30` is the zero-initialized 16-byte HostRowState.

The stack independently fits the preceding allocation/growth sequence:

- requested row count at SP+8 is **1**;
- growth input at SP+0x28 is `0x8149ec44`, an unrelated pointer-like stack value;
- computed capacity at SP+0x0c is `0xc1eee266`, exactly that input multiplied by 1.5;
- capacity × 16 modulo 32 bits is `0x1eee2660` bytes, about 495 MiB;
- the subsequent fill targets a null allocation, consistent with allocation failure.

The installed PAF vector header (SHA-256
`d171ce550818fba41e284a58fd8ab165de119b607f3da29ea17a540d8078424a`) matches the
relevant [ScePaf-RE b76d6ae constructor](https://github.com/GrapheneCt/ScePaf-RE/blob/b76d6ae00516b107d37e7e8a1a16211e3b729a07/include/paf/std/vector).
Its sized constructor initializes `_capacity(calculateGrowth(count))`, while
calculateGrowth reads capacity() (the same `_capacity`) before that initializer completes.
The default member initializer `_capacity = 0` does not run when overridden by this explicit
initializer. The sized constructor therefore reads an indeterminate value. It then allocates
and fills without checking the allocation. The disassembly/stack agree with that mechanism.
This is a regression introduced by the sized row-vector construction in `02541bc`, not
confirmed evidence of a discovery, network, empty-host-list or PAF layout-timing defect.

## Minimal correction and validation

Main now default-constructs rows and then calls resize(hosts.size()). The installed default
constructor explicitly initializes size/capacity to zero; the first resize uses growth from
that initialized capacity and value-initializes the rows. Apps and host snapshots already
follow this empty-then-resize pattern. The project does not patch installed SDK headers,
restore an eight-PC limit, weaken allocation errors or change InitialRefreshTask/CXML/storage.
General low-memory recovery and unrelated SDK vector defects are outside this correction.

The earlier host-list source interpreter replaced the sized constructor with idealized safe
allocation. That abstraction hid the defect; its passing checks did not validate SDK behavior.
The interpreter now handles the actual empty-then-resize path and no longer normalizes sized
construction into safe allocation. A regression guard rejects reintroducing the unsafe row
constructor. All **127 non-build Python checks pass**. These still model allocation/widgets;
they do not compile C++, execute PAF malloc or prove startup on Vita.

At the source-correction stage, maintainer rebuild/install and startup acceptance were pending.
The following dated result supersedes that absence of runtime evidence, within its stated scope.

## Maintainer runtime follow-up — 2026-10-22

The supplied psp2shell log identifies system profile, system_app=ON and build `6af2e3639c69`.
After Settings closes it reports Main `hosts=1`, cells before=1, after_delete=0, after_insert=1,
selection=0. Two mDNS initialization/callback cycles each discover two PCs and then clear the
callback. Search resolves the selected entry to the one saved UUID record, metadata closes
successfully, ConnectHost reports ready, and Apps inserts all four returned applications.

This is device-log evidence that the corrected row allocation/one-host Main refresh and the
ordinary Search → paired Connect → Apps path run without the previous crash in this attempt.
It is stronger than the earlier source-only checks, but does not measure allocator behavior or
prove every startup. The launch marker is repeated verbatim; do not count it as two independent
successful launches. Main's captured refresh follows Settings closure, so initial cold-start
timing/visibility is not separately established. Callback clearing alone does not distinguish
natural scan timeout from explicit Stop. No >8-PC display, Delete/selection, stream start/stop,
forced storage/start errors, firmware-wide or repeated cold-start acceptance is supplied here.
No runtime code changes, agent build or agent hardware run accompany this evidence record.

## Subsequent ordinary stream start/stop log — 2026-10-22

The maintainer supplies a continuation after the 6af2e3639c69 Main/Search/Apps run. This
fragment has no independent launch marker or exact Sunshine release; retain that attribution
limit rather than assigning a server release from an older run.

App 1093255277 resumes successfully with an encrypted RTSP session URL. All common-c setup
stages complete, input starts, StreamPage is created and infobar hide succeeds. Mode is
960×544 at 60 fps/10000 kbps, with the nongame decoder and stereo 48 kHz audio port 6.
First video/audio packet delays are 500/5400 ms and the final decoded count is 411. The
OOS/speculative-RFI transition is followed by continued decoding. Asked specifically about this
attempt, the maintainer confirms that image and audible sound worked. This adds user-reported
playback acceptance on the tested Vita, not measured frame rate, latency or error-free quality.

A deliberate double PS stops the owner. Input/touch stop, audio drain/release return zero,
all common-c stages clean up, owner stop finishes with result 0 and infobar restore succeeds.
All three video-buffer unmaps return zero, followed by deferred-release completion. The
ENet interrupted/control-failed:4 messages occur after the stop request; local common-c
Misc.c sets EINTR when ConnectionInterrupted breaks the ENet wait, matching this teardown
sequence. There is no setup failure or unexpected stop shown in the fragment.

USER_RW free is 33836 KiB before setup and 33068 KiB after deferred release; newlib used is
95704 then 99000 bytes. These are one-cycle observations, not proof of a leak or full memory
return. Motion type 1/2 state messages establish reporting activity, not correctness of gyro
input. >8-PC lists, repeated cold starts, natural scan timeout, host disconnect/reconnect and
other error/shutdown cases remain outside this record. No runtime code change or agent build.

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

Maintainer rebuild/install and cold-start with the existing saved PC are required next.
Verify Main appears, then ordinary Search/selection/Apps return. Mark device startup fixed
only after that result; no hardware success is claimed here.

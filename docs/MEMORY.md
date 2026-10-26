# Memory budget

Measured on 26 Oct 2026 by building with JieLi's Linux toolchain (20260730.1) and the AC79 SDK v1.2.1.

## Builds

| Firmware | Flash (app slot 581 KB) | RAM .data+.bss | Pool |
| --- | --- | --- | --- |
| fm1-base `examples/wifi` | 417 KB | 122 of 128 KB | 306 of 311 KB |
| fm1-radio 1.3 | 582 KB (full) | 130 of 128 KB | 309 of 311 KB |
| COSMOS PROBE 0.1 (WiFi, HTTPS, Terminus) | 450 KB | 122 of 128 KB | 306 of 311 KB |

The `.fwsc` packages are all ~610 KB: they are padded to a fixed size and do not tell an app's size.

## Pieces (from the symbol tables)

| Piece | Flash |
| --- | --- |
| A two-drone engine with filter, reverb and delay (measured on an existing firmware) | ~49 KB |
| BearSSL in fm1-radio | ~16–20 KB |
| MP3/AAC decoders in fm1-radio (not needed) | ~47 KB |
| Barlow fonts in `examples/wifi` | ~60 KB |
| Terminus fonts as fm1-base stores them (4 bits a pixel) | ~62 KB: no saving; a 1-bit store would cut it to ~16 KB |

## COSMOS estimate

Flash free after `examples/wifi`: ~165 KB; the probe, with HTTPS, leaves ~130 KB. Fonts at 1 bit a pixel would give
~45 KB more. Needed beyond the probe:

| Part | Flash |
| --- | --- |
| Sound engine with ring mod and cross-mod | ~55 KB |
| Sky: star catalog and astronomy | ~15 KB |
| UI, WiFi screens included | ~30 KB |
| **Total** | **~100 KB: fits** |

## RAM: the tight part

With WiFi, static RAM (9 KB left) and the pool (5 KB left) are nearly full. fm1-base puts new buffers on the WiFi
heap (`malloc`, 272 KB in all, shared with the WiFi library's buffers and task stacks). COSMOS needs about
80–100 KB of it:

| Buffer | Size |
| --- | --- |
| Delay, stereo, 0.5 s, int16, 22.05 kHz | ~44 KB |
| Reverb, int16 | ~37 KB |
| Noise | ~0 |

How much heap is free with WiFi connected can only be measured on the FM-1: COSMOS PROBE (`probe/`) shows it.

## Measured on the FM-1 (PROBE 0.1, 26 Oct 2026)

Connected at -85 dBm (a weak signal), both requests answered over HTTPS, the longest handshake 386 ms.

| | KB |
| --- | --- |
| WiFi heap, in all | 272 |
| In use at rest, connected | 92 |
| In use at the fetch's peak (two HTTPS requests) | 121 |
| Biggest free block at rest | 176 |
| Delay + reverb (44 + 37) allocated beside WiFi | fit |

So a fetch takes about 29 KB more than rest, and COSMOS can keep about 151 KB of buffers for good (272 − 121)
and still fetch. With ~25 KB kept spare, **~125 KB for the sound**: the delay can grow from 0.5 s to about 1 s stereo
(88 KB) beside the reverb (37 KB), or stay at 0.75 s (66 KB) and leave room for a bigger reverb. The buffers are
allocated once at start, before the first fetch, so the fetch takes what is left.

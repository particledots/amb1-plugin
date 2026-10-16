# Third-party material and credits

## AMB-1 (the engine)

`engine/amb.c`, `engine/amb.h`, `engine/fastmath.h` are from **AMB-1** by Charles Vestal,
https://github.com/charlesvestal/fm1-amb, GPL-3.0-only (copied unmodified; see `engine/UPSTREAM.md`).
AMB-1's own credits, which apply here too:

- The idea comes from Brian Eno's generative music: the tape loops of unequal length of *Music for Airports*
  and the echo and tape-delay chain of *Discreet Music*.
- The reverb is Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997), by way of Charles Vestal's FoMni-1.
- The Euclidean rhythms are Godfried Toussaint's reading of Bjorklund's algorithm.
- AMB-1's hardware platform layer is Felucca by Leo Kuroshita (Hügelton Instruments). It is **not** part of
  this repository: only the sound engine is used.

## JUCE

The plugin wrapper uses **JUCE 8** (https://juce.com), fetched by CMake at build time and not stored in this
repository. JUCE is available under the AGPL-3.0 or a commercial licence; this project uses it under the
AGPL-3.0 terms. Check JUCE's current licensing terms before distributing builds.

## Trademarks

"M-VAVE" and "FM-1" are trademarks of their owners. "Felucca" and "Hügelton Instruments" are names of
Hügelton Instruments. This project is independent and is not affiliated with, endorsed by or supported by any of them.

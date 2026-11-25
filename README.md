# amb1-plugin

An **unofficial** Audio Unit / VST3 port of the **AMB-1** generative ambient engine, for Logic Pro on macOS.

AMB-1 is Charles Vestal's open-source firmware that turns the M-VAVE FM-1 into a generative ambient machine,
inspired by Brian Eno's *Music for Airports*: breathing pad voices, FM bells on tape loops of unequal length
(13, 17, 23 and 29 beats), a gliding bass, quiet Euclidean percussion, then a tape echo, a long tape loop and
a plate reverb. It plays itself; you tend it.

- Upstream: https://github.com/charlesvestal/fm1-amb (GPL-3.0-only)
- This is not affiliated with or endorsed by Charles Vestal, M-VAVE, Hügelton Instruments or Brian Eno.

## Status: v0.14.0

Two plugins, both running the same engine in Logic Pro on Apple silicon and Intel Macs (tried in Logic by the author on both):

- **AMB-1**: AU instrument (and VST3), universal (arm64 + x86_64), minimum macOS 11.
- **AMB-1 MIDI**: a MIDI effect that sends the garden's notes to an instrument of your choice (pad ch 1, bells ch 2, bass ch 3, melodic pulse ch 4, drums ch 10).

What is in it (see `CHANGELOG.md` for the history):

- All engine parameters plus Play, Freeze, Next chord, Master and Tempo sync as host parameters; the eight pieces as programs.
- Any host sample rate: the engine runs at 44.1 kHz and is resampled.
- A grouped, resizable editor with a live picture of the garden, coloured knobs and tooltips.
- Scale control: Mode and Root, a Scale menu (20 more scales), or click the ring to edit your own.
- A level for each drum (hats up to 400 %), a **Palette** knob (struck-object bells and a slow whole-sound morph), and a **Pulse sound** switch (Drums or Melodic plucks) with a **Pulse chance** knob.
- Random start: each start begins a different garden.

Known limits:

- **One AMB-1 at a time.** The engine keeps its state in file-level statics, so a second instance is silent (the editor says so).
- **Free-running.** The garden keeps playing whether or not the host is running. With Tempo sync on it takes the host's tempo only (the engine's own tempo range is 30 to 120 bpm).
- The AMB-1 instrument takes no MIDI notes in; in AMB-1 MIDI the level knobs do nothing (everything goes to the instrument after it).
- Not code-signed or notarized (ad-hoc signed only).

## Building

Built by GitHub Actions (`.github/workflows/build-macos.yml`): push, then download the `amb1-macos` artifact
from the run. To build the tests on any machine without JUCE:

```
cmake -S . -B build -DAMB1_ENGINE_ONLY=ON && cmake --build build
./build/render_test && ./build/rate_test
```

The full plugin needs JUCE 8 (fetched by CMake) and builds on macOS.

## Layout

| Path | What |
| --- | --- |
| `engine/` | AMB-1's engine, from upstream; `amb.h` and `fastmath.h` unmodified, `amb.c` has four small marked local changes (`engine/UPSTREAM.md` has the commit, the changes and hashes) |
| `Source/AmbCore.*`, `Source/Resampler.h` | engine glue and the 44.1 kHz resampler (no JUCE) |
| `Source/Plugin*.{h,cpp}` | the JUCE plugin |
| `tests/` | `render_test` (engine), `rate_test` (resampler and sample rates), `plugin_test` (the plugin object) |

## Licence and credits

GPL-3.0-only; see `LICENSE` and `THIRD_PARTY.md`. If you share a build, you must share its source.

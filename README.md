# amb1-plugin

An **unofficial** Audio Unit / VST3 port of the **AMB-1** generative ambient engine, for Logic Pro on macOS.

AMB-1 is Charles Vestal's open-source firmware that turns the M-VAVE FM-1 into a generative ambient machine,
inspired by Brian Eno's *Music for Airports*: breathing pad voices, FM bells on tape loops of unequal length
(13, 17, 23 and 29 beats), a gliding bass, quiet Euclidean percussion, then a tape echo, a long tape loop and
a plate reverb. It plays itself; you tend it.

- Upstream: https://github.com/charlesvestal/fm1-amb (GPL-3.0-only)
- This is not affiliated with or endorsed by Charles Vestal, M-VAVE, Hügelton Instruments or Brian Eno.

## Status: v0.1, first scaffold

What works (headless-tested, and tried in Logic Pro on an Intel Mac; not yet tried on Apple silicon):

- AU instrument and VST3, universal (arm64 + x86_64), minimum macOS 11.
- All 27 engine parameters, plus Play, Freeze, Next chord, Master and Tempo sync, as host parameters.
- The eight pieces as programs (factory presets): Garden, Departures, Voices, Afternoon, Night Bus,
  Shoreline, Glass, Tides.
- Any host sample rate: the engine runs at 44.1 kHz and is resampled.
- A grouped, resizable editor: a live picture of the garden across the top (the scale and chord, the four bell loops,
  the level of each layer, the three pulse patterns, the way to the next chord), then knobs, a piece menu,
  Play / Freeze / Next chord, Master, and tooltips.
- Optional tempo sync (off by default): the garden follows the host's tempo instead of its own Tempo parameter.

Known limits of this version:

- **One AMB-1 at a time.** The engine keeps its state in file-level statics, so a second instance is silent
  (the editor says so).
- **Free-running.** The garden keeps playing whether or not the host is running, and does not follow its transport
  or bar position. With Tempo sync on it takes the host's tempo only (the engine's own tempo range is 30 to 120 bpm,
  which the echo times and swing use even if the host is faster or slower).
- **Not repeatable between runs.** The engine's random generator is not reset when the plugin starts.
- No MIDI in or out yet; the scale is set by Mode and Root.
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
| `engine/` | AMB-1's engine, copied unmodified from upstream (`engine/UPSTREAM.md` has the commit and hashes) |
| `Source/AmbCore.*`, `Source/Resampler.h` | engine glue and the 44.1 kHz resampler (no JUCE) |
| `Source/Plugin*.{h,cpp}` | the JUCE plugin |
| `tests/` | `render_test` (engine), `rate_test` (resampler and sample rates), `plugin_test` (the plugin object) |

## Licence and credits

GPL-3.0-only; see `LICENSE` and `THIRD_PARTY.md`. If you share a build, you must share its source.

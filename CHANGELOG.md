# Changelog

Newest first. Versions are the CMake VERSION. "Tested" means headless tests in CI, plus Bryn's own listening in Logic Pro where noted.

## 0.14.0 (2026-11-25): Pulse sound and Pulse chance
- New **Pulse sound** menu (Drums / Melodic), in the Rhythm group. Drums is the default and is byte-for-byte the previous sound. Melodic turns each pulse hit into a short plucked note taken from the sounding chord and scale, on the same rhythm patterns. The plucks follow the Palette's slow morph.
- New **Pulse chance** knob (0..100 %, default 100): leaves out a share of the Melodic hits.
- In Melodic, the Thump / Closed hat / Open hat knobs set the low / middle / high register levels (up to 150 %).
- AMB-1 MIDI sends the plucks on MIDI channel 4 (drums stay on channel 10).
- Parameters 38 and 39 added at the end of the list; older projects open as Drums / 100 %.
- Engine: local change 4 in `engine/UPSTREAM.md`.

## 0.13.0 (2026-11-24): Palette 2
- The Palette knob makes the bells struck objects (material, size, stiffness, damping) and runs one slow "scene" (brightness, character, room) that moves the bells, Rhodes pad, Voices pad and bass together. At Palette 0 everything is unchanged.

## 0.12.0 (2026-10-18)
- Closed hat and Open hat levels go up to 400 % (the thump stays at 150 %).

## 0.11.0 (2026-10-17)
- Knob colours: the layer knobs match the display's level bars; other groups have their own colour.

## 0.10.0 (2026-10-17)
- A level for each drum (Thump, Closed hat, Open hat). Engine: local change 2.

## 0.9.0 (2026-10-17)
- New second plugin, **AMB-1 MIDI** (a MIDI effect): the garden's notes go out as MIDI to an instrument of your choice.

## 0.8.0 (2026-10-17)
- Random start: every time the engine starts it begins a different garden.

## 0.7.0 (2026-10-17)
- Scale menu with 20 more scales.

## 0.6.x (2026-10-16)
- Scale editing by clicking the ring, then a bigger ring (0.6.1) and five equal rings in a row (0.6.2).

## 0.5.0 (2026-10-16)
- Longer echo tape, so every Echo time plays as asked (Voices and Shoreline sound different). Engine: local change 1.

## 0.4.x (2026-10-16)
- The garden display across the top of the editor; a fix so a re-prepared plugin starts from its controls (0.4.1).

## 0.3.0 (2026-10-16)
- Tempo sync (host tempo only), tooltips.

## 0.2.x (2026-10-16)
- The grouped, resizable editor; wider Master slider and menus (0.2.1).

## 0.1 (2026-10)
- First scaffold: AU instrument and VST3, universal (arm64 + x86_64), eight pieces as programs, any host sample rate.

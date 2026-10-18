# Vendored engine

`amb.h` and `fastmath.h` are copied **unmodified** from AMB-1 by Charles Vestal; `amb.c` is copied with **two
local changes** (see "Local changes" below):

- Source: https://github.com/charlesvestal/fm1-amb, path `firmware/src/dsp/`
- Commit: `d9ef0a35823bbad8edc7cc0feb2eae59539e4081` (2026-10-15)
- Licence: GPL-3.0-only (see `../LICENSE`)

SHA-256 of the files, to check they have not been edited. `amb.c` is the plugin's edited copy (v0.10.0); the
upstream `amb.c` at the commit above is `67e311ba8562a08199257778f641669d1f6477c28282dfd89f7e347b8908ab7b`:

```
deb8043384cd2e507767306ff07b8d37365a8e939947799f4aa91339981c7c3a  amb.c
26e4de7fe8abe145a653a3ecf615b8a3d3feab2954bfe2a6b77ce18b2f06d30d  amb.h
71d0fb4c242e6c03e435cebacc1e978f928ce2672d6b47a48b68372e72453349  fastmath.h
```

## Local changes to `amb.c` (two)

### 1. Line 1029 (v0.5.0), the echo tape's length:

```
-#define TAPE_N 62000                             /* 5.6 s at 11.025 kHz */
+#define TAPE_N 360000                            /* AMB1-PLUGIN: was 62000 (5.6 s, the FM-1's RAM); now 32.6 s so every Echo time fits */
```

Why: 62000 samples (5.6 s) is the hardware's RAM size. `echo_time()` steps an Echo time that does not fit the tape
down to a shorter choice, so "2 bars" and "4 bars" were cut back at most tempos (163 of 546 choice/tempo cases;
`tests/echo_test.c`). 360000 samples (32.65 s, 720 KB) holds the longest choice, 4 bars at 30 bpm (32 s, plus the
0.3% wow), so all 546 cases play as asked. Nothing else in `amb.c` refers to a fixed tape length; no other line
was touched. Echo times that already fitted are bit-identical (checked by rendering both engines).

### 2. A level for each drum (v0.10.0, marked `AMB1-PLUGIN`)

Added after `DRUM_KMAX`: `static float drum_gain[EN_NDRUM] = {1.0f, 1.0f, 1.0f};` and `void en_drum_gain(int d, float g)`
(sets one of them). In `render_drums()` the thump's output is multiplied by `drum_gain[0]`, and the closed and open hats'
gains by `drum_gain[1]` and `drum_gain[2]`. At 1.0 every product is unchanged, so the output is bit-identical to before
(the eight `render_test` hashes are the same as in v0.9.0). `en_drum_gain` is declared in the plugin (`Source/AmbCore.h`),
not in `amb.h`, so `amb.h` stays unmodified. The plugin calls it from the thread that calls `en_render()`.

When taking an upstream update: copy the three files, then re-apply both local changes (check that the tape is still
sized by `TAPE_N` alone and that `render_drums()` is still the place the three drums are mixed), and update the hash above.

Everything plugin-specific lives outside this folder (`../Source`). To take an upstream update, copy the
three files again, re-apply the local change, check the diff, and update the commit and hashes above.

Build requirements of the engine (from upstream's own build): compile with `-DEN_HOST`, `-std=gnu99`
and `-ffp-contract=off`. The engine is float-only, uses file-level static state (so one instance per
process) and assumes a 44.1 kHz sample rate (`EN_SR`).

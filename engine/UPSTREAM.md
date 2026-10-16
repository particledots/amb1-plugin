# Vendored engine

`amb.c`, `amb.h` and `fastmath.h` are copied **unmodified** from AMB-1 by Charles Vestal:

- Source: https://github.com/charlesvestal/fm1-amb, path `firmware/src/dsp/`
- Commit: `d9ef0a35823bbad8edc7cc0feb2eae59539e4081` (2026-10-15)
- Licence: GPL-3.0-only (see `../LICENSE`)

SHA-256 of the copied files, to check they have not been edited:

```
67e311ba8562a08199257778f641669d1f6477c28282dfd89f7e347b8908ab7b  amb.c
26e4de7fe8abe145a653a3ecf615b8a3d3feab2954bfe2a6b77ce18b2f06d30d  amb.h
71d0fb4c242e6c03e435cebacc1e978f928ce2672d6b47a48b68372e72453349  fastmath.h
```

Everything plugin-specific lives outside this folder (`../Source`). To take an upstream update, copy the
three files again, check the diff, and update the commit and hashes above.

Build requirements of the engine (from upstream's own build): compile with `-DEN_HOST`, `-std=gnu99`
and `-ffp-contract=off`. The engine is float-only, uses file-level static state (so one instance per
process) and assumes a 44.1 kHz sample rate (`EN_SR`).

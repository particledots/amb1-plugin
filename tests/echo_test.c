/* SPDX-License-Identifier: GPL-3.0-only */
/* Echo-time test (no JUCE). Measures what the six Echo time choices really do at every tempo from 30 to 120.
 *
 * It #includes the engine's amb.c so that it can read the engine's own file-static state; it is therefore
 * built on its own (not linked with amb_engine). Two parts:
 *
 *  1. For all 6 choices x 91 tempos (546 cases): what the engine asks for (choice x beats x 60 / tempo) and what it
 *     really uses (echo_len_t / TAPE_SR, read after en_set -> en_render, the real path).
 *  2. For a subset of tempos: the same in the AUDIO. All four layers and Reverb are set to 0 and Motion to 0, so the
 *     only thing in the output is the tape heads. One impulse is put on the tape; the output then shows the
 *     impulse coming back: first from the second head (3/4 of the loop), then from the first head (the whole loop).
 *     The time of the second arrival is the echo time the listener gets.
 *
 * Goal (what the test asks for): every Echo time plays as requested at every tempo. It exits non-zero when not.
 *   ./echo_test [quick]      ("quick": part 2 only for a few tempos)
 */
#define EN_HOST
#include "../engine/amb.c"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
}

/* a fresh engine, Garden's values, but silent but for the echo; the given tempo and Echo time */
static void setup(int tempo, int choice)
{
    int16_t p[P_NPARAMS];
    uint16_t pool;
    int i;
    en_init();
    en_piece(0, p, &pool);
    p[P_PAD] = p[P_BELLS] = p[P_BASS] = p[P_PULSE] = 0;
    p[P_ECHO] = 100;
    p[P_REVERB] = 0;
    p[P_MOTION] = 0;
    p[P_TEMPO] = (int16_t)tempo;
    p[P_ETIME] = (int16_t)choice;
    for (i = 0; i < P_NPARAMS; i++)
        en_set(i, i == P_MIDI ? 0 : p[i]);
    en_pool(pool);
}

static double requested_s(int tempo, int choice) { return (double)ETIME_BEATS[choice] * 60.0 / (double)tempo; }

/* "as asked", or the name of the shorter choice it was cut back to, or "capped" */
static const char *played_as(int tempo, int choice, double played, char *buf)
{
    int j;
    if (fabs(played - requested_s(tempo, choice)) < 1e-3)
        return "as asked";
    for (j = choice - 1; j >= 0; j--)
        if (fabs(played - requested_s(tempo, j)) < 1e-3) {
            sprintf(buf, "cut to %s", ETIME_NAME[j]);
            return buf;
        }
    return "capped";
}

static int32_t blk[2 * 128];
static void render_silent(double secs)
{
    long n = (long)(secs * 44100.0);
    while (n > 0) {
        uint32_t k = n > 128 ? 128 : (uint32_t)n;
        en_render(blk, k, 2400);
        n -= k;
    }
}

/* the engine's own value, after the real path (en_set -> en_render) */
static double engine_played_s(int tempo, int choice)
{
    setup(tempo, choice);
    render_silent(0.1);
    return (double)echo_len_t / (double)TAPE_SR;
}

/* the audio: one impulse on the tape, then where the output shows it. Returns the second arrival (the first
 * head's, the whole loop) in seconds, the first arrival in *t1, the peak level before the impulse in *pre. */
static double measure_audio(int tempo, int choice, double *t1, double *pre, double *a1, double *a2)
{
    const double want = requested_s(tempo, choice);
    const double win = want * 1.1 + 1.0 > 40.0 ? 40.0 : want * 1.1 + 1.0;
    const long total = (long)(win * 44100.0);
    float *e = (float *)malloc(sizeof(float) * (size_t)total);
    long i, w = 441, got = 0;
    double sum = 0, mx = 0, secs[3] = {-1, -1, -1};
    float *env = (float *)malloc(sizeof(float) * (size_t)total);
    setup(tempo, choice);
    render_silent(1.5);                            /* settles; nothing should be sounding */
    *pre = 0;
    for (i = 0; i < 100; i++) {                    /* the level just before the impulse: should be 0 */
        int j;
        en_render(blk, 128, 2400);
        for (j = 0; j < 256; j++)
            if (fabs((double)blk[j]) / 8388608.0 > *pre)
                *pre = fabs((double)blk[j]) / 8388608.0;
    }
    tape[tape_w] = 12000;                          /* the impulse, as if the send had put it there now */
    {
        long done = 0;
        while (done < total) {
            uint32_t k = total - done > 128 ? 128 : (uint32_t)(total - done);
            uint32_t j;
            en_render(blk, k, 2400);
            for (j = 0; j < k; j++) {
                double l = fabs((double)blk[2 * j]) / 8388608.0, r = fabs((double)blk[2 * j + 1]) / 8388608.0;
                e[done + j] = (float)(l > r ? l : r);
            }
            done += k;
        }
    }
    for (i = 0; i < total; i++) {                  /* 10 ms moving average of the level */
        sum += e[i];
        if (i >= w)
            sum -= e[i - w];
        env[i] = (float)(sum / (double)w);
        if (env[i] > mx)
            mx = env[i];
    }
    for (i = 1; i < total - 1 && got < 3; i++) {   /* the first peaks above 20% of the highest, 150 ms apart */
        long k, lo = i > 6615 ? i - 6615 : 0, hi = i + 6615 < total ? i + 6615 : total - 1;
        int ismax = env[i] > 0.2f * (float)mx;
        for (k = lo; k <= hi && ismax; k++)
            if (env[k] > env[i])
                ismax = 0;
        if (ismax) {
            secs[got++] = (double)i / 44100.0;
            if (got == 1)
                *a1 = env[i];
            if (got == 2)
                *a2 = env[i];
            i += 6615;
        }
    }
    *t1 = secs[0];
    free(e);
    free(env);
    return secs[1];
}

int main(int argc, char **argv)
{
    static const int tempos_audio[] = {30, 40, 48, 56, 64, 72, 84, 96, 120};
    static const int tempos_quick[] = {30, 64, 72};
    const int quick = argc > 1 && !strcmp(argv[1], "quick");
    const int *ta = quick ? tempos_quick : tempos_audio;
    const int nta = quick ? 3 : 9;
    int tempo, c, bad_engine = 0, total = 0, bad_audio = 0, n_audio = 0, fits_total = 0;
    double max_pre = 0;
    char buf[32];

    printf("TAPE_N = %d samples at %.0f Hz = %.2f s\n\n", (int)TAPE_N, (double)TAPE_SR, (double)TAPE_N / (double)TAPE_SR);

    if (!quick) {
        printf("Part 1: engine's own value (echo_len_t / TAPE_SR), all 6 choices x tempos 30..120\n");
        for (c = 0; c < 6; c++) {
            int cut = 0, lo = 0, hi = 0;
            for (tempo = 30; tempo <= 120; tempo++) {
                const double got = engine_played_s(tempo, c);
                const int asked = fabs(got - requested_s(tempo, c)) < 1e-3;
                total++;
                if (!asked) {
                    cut++;
                    bad_engine++;
                    if (!lo)
                        lo = tempo;
                    hi = tempo;
                }
            }
            if (cut) {
                printf("  %-7s cut back at %2d of 91 tempos (", ETIME_NAME[c], cut);
                for (tempo = 30; tempo <= 120; tempo++)
                    if (fabs(engine_played_s(tempo, c) - requested_s(tempo, c)) >= 1e-3) {
                        printf("%d..", tempo);
                        break;
                    }
                printf("%d bpm)\n", hi);
            } else {
                printf("  %-7s plays as asked at all 91 tempos\n", ETIME_NAME[c]);
            }
            (void)lo;
        }
        printf("  total: %d of %d cases cut back\n\n", bad_engine, total);
        (void)fits_total;
    }

    printf("Part 2: audio (impulse on the tape, time of its echoes in the output)\n");
    printf("  bpm  choice   asked s   engine s   1st echo   2nd echo   (2nd = echo time heard)   verdict\n");
    for (c = 3; c < 6; c++) {
        int ti;
        for (ti = 0; ti < nta; ti++) {
            double t1, pre, a1 = 0, a2 = 0, t2, asked, eng;
            tempo = ta[ti];
            asked = requested_s(tempo, c);
            eng = engine_played_s(tempo, c);
            t2 = measure_audio(tempo, c, &t1, &pre, &a1, &a2);
            n_audio++;
            if (pre > max_pre)
                max_pre = pre;
            {
                const int ok = t2 > 0 && fabs(t2 - asked) < 0.05;
                if (!ok)
                    bad_audio++;
                printf("  %3d  %-7s %7.3f   %7.3f    %7.3f    %7.3f   (%s)   %s\n", tempo, ETIME_NAME[c], asked, eng, t1, t2,
                       played_as(tempo, c, eng, buf), ok ? "as asked" : "NOT as asked");
            }
        }
    }
    printf("  audio: %d of %d cases not as asked; highest output level before any impulse: %.6f\n\n", bad_audio, n_audio,
           max_pre);
    printf((bad_engine == 0 && bad_audio == 0) ? "PASS\n" : "FAIL (some Echo times are cut back)\n");
    return (bad_engine == 0 && bad_audio == 0) ? 0 : 1;
}

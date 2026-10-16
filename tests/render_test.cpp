// SPDX-License-Identifier: GPL-3.0-only
// Engine-only render test (no JUCE). Renders every piece for a while at the engine's own 44.1 kHz and checks
// the output is finite, not silent, and not clipping. It prints a hash of each piece's audio.
//   ./render_test [seconds] [outdir]     (outdir: also write piece<N>.wav there)
// Repeatability: the engine's random generator is file-static and en_init() does not reset it, so a SECOND
// run in the same process differs from the first. A fresh process always starts from the same state, so two
// separate runs print identical hashes (CI compares two runs). Hence one render per piece per process here.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "amb.h"
}

static void wavWrite(const std::string& path, const std::vector<int16_t>& pcm)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return;
    const uint32_t bytes = (uint32_t)(pcm.size() * 2), v16 = 16, sr = 44100, br = 44100 * 4;
    const uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
    const uint32_t riff = 36 + bytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&v16, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
}

struct Result { double peak = 0, rms = 0; long nan = 0; uint64_t hash = 1469598103934665603ull; };

static Result renderPiece(int piece, double secs, std::vector<int16_t>* keep)
{
    int16_t par[P_NPARAMS];
    uint16_t pool;
    en_init();
    en_piece(piece, par, &pool);
    for (int i = 0; i < P_NPARAMS; ++i)
        en_set(i, i == P_MIDI ? 0 : par[i]);
    en_pool(pool);
    en_seed(12345);
    en_regrow();
    en_play(1);

    Result r;
    double sumsq = 0;
    const uint32_t total = (uint32_t)(secs * 44100.0);
    uint32_t done = 0;
    int32_t blk[2 * 128];
    while (done < total)
    {
        const uint32_t n = std::min<uint32_t>(128, total - done);
        en_render(blk, n, 2400);
        for (uint32_t i = 0; i < 2 * n; ++i)
        {
            double x = blk[i] / 8388608.0;
            if (x != x) { ++r.nan; x = 0; }
            r.peak = std::max(r.peak, std::fabs(x));
            sumsq += x * x;
            r.hash = (r.hash ^ (uint32_t)blk[i]) * 1099511628211ull;
            if (keep)
                keep->push_back((int16_t)(std::max(-1.0, std::min(1.0, x)) * 32767.0));
        }
        done += n;
    }
    r.rms = std::sqrt(sumsq / (2.0 * total));
    return r;
}

int main(int argc, char** argv)
{
    const double secs = argc > 1 ? atof(argv[1]) : 20.0;
    const std::string outdir = argc > 2 ? argv[2] : "";
    bool ok = true;
    for (int p = 0; p < EN_NPIECES; ++p)
    {
        std::vector<int16_t> pcm;
        const Result a = renderPiece(p, secs, outdir.empty() ? nullptr : &pcm);
        const bool finite = a.nan == 0, quiet = a.rms < 0.005, loud = a.peak > 0.95;
        printf("%-11s peak %.3f  rms %.4f  hash %016llx  %s%s%s\n", en_piece_name(p), a.peak, a.rms,
               (unsigned long long)a.hash, finite ? "" : "NaN! ", quiet ? "SILENT! " : "", loud ? "CLIPS! " : "");
        ok = ok && finite && !quiet && !loud;
        if (!outdir.empty())
            wavWrite(outdir + "/piece" + std::to_string(p) + ".wav", pcm);
    }
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}

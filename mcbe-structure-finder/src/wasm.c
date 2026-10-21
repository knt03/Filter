// ブラウザ (WebAssembly) 向けの入口。結果は共有バッファに [x, z, バイオーム, 付加情報] の順で入る。
#include "bedrock.h"
#include "util.h"

#define BUF_INTS (1 << 21)
static int buf[BUF_INTS];

int *wasm_buffer(void) { return buf; }

int wasm_setup(uint32_t seed_lo, uint32_t seed_hi, int mc)
{
    return be_setup(((uint64_t)seed_hi << 32) | seed_lo, mc);
}

int wasm_available(int kind) { return be_kind_available(kind); }

int wasm_scan(int kind, int x0, int z0, int x1, int z1, int part, int nparts)
{
    return be_scan(kind, x0, z0, x1, z1, part, nparts, (BEHit *)buf, BUF_INTS / 4);
}

int wasm_biome_grid(int dim, int x0, int z0, int w, int h, int scale)
{
    if ((long long)w * h > BUF_INTS)
        return -1;
    return be_biome_grid(dim, x0, z0, w, h, scale, buf);
}

// 地図の背景: バイオームの色 (cubiomes の配色) を RGBA で buf に書く
int wasm_biome_rgba(int dim, int x0, int z0, int w, int h, int scale)
{
    static unsigned char colors[256][3];
    static int ready;
    if ((long long)w * h > BUF_INTS)
        return -1;
    int err = be_biome_grid(dim, x0, z0, w, h, scale, buf);
    if (err)
        return err;
    if (!ready) {
        initBiomeColors(colors);
        ready = 1;
    }
    unsigned char *px = (unsigned char *)buf;  // buf[i] を読んでから同じ 4 バイトに書く
    for (int i = 0; i < w * h; i++) {
        int id = buf[i] & 255;
        px[4 * i] = colors[id][0];
        px[4 * i + 1] = colors[id][1];
        px[4 * i + 2] = colors[id][2];
        px[4 * i + 3] = 255;
    }
    return 0;
}

// 統合版のエンド (end.h)。統合版の専用サーバーを機械語から移植した acacia-rs/sapling と
// Java 版 1.12 の EndCityPieces をもとにしている。島の形は float の計算の丸めまで
// 合わせないと一致しないので、FMA を使わずにコンパイルする (-ffp-contract=off)。
#include "end.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mt.h"

static float unitf(uint32_t r) { return (float)((double)r * 2.3283064365386963e-10); }

// --- ノイズ ---
typedef struct { float xo, yo, zo; uint8_t p[256]; } Improved;
typedef struct { uint8_t p[256]; } Simplex;

static uint32_t g_seed;
static Improved g_min[16], g_max[16], g_main[8];
static Simplex g_island;

static const float GRAD[16][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1},
};

static void shuffle(uint8_t p[256], MT *mt)
{
    for (int i = 0; i < 256; i++)
        p[i] = (uint8_t)i;
    for (int i = 0; i < 256; i++) {
        int j = i + mt_int_full(mt, 256 - i);
        uint8_t t = p[i];
        p[i] = p[j];
        p[j] = t;
    }
}

static void improved_init(Improved *n, MT *mt)
{
    n->xo = unitf(mt_next_full(mt)) * 256.0f;
    n->yo = unitf(mt_next_full(mt)) * 256.0f;
    n->zo = unitf(mt_next_full(mt)) * 256.0f;
    shuffle(n->p, mt);
}

static int sfloor(float v) { return (int)v - (v <= 0.0f); }

static float simplex_corner(int g, float x, float y)
{
    float t = 0.5f - x * x - y * y;
    if (0.0f > t)
        return 0.0f;
    const float *gr = GRAD[g % 12];
    return (y * gr[1] + x * gr[0]) * (t * t * t * t);
}

static float simplex2(const Simplex *s, float x, float y)
{
    union { uint32_t u; float f; } f2 = { 0x3ebb67aeu };
    const float G2 = 0.21132487f;
    float sk = (x + y) * f2.f;
    int i = sfloor(x + sk), j = sfloor(sk + y);
    float t = (float)(i + j) * G2;
    float x0 = x - ((float)i - t), y0 = y - ((float)j - t);
    int i1 = x0 > y0, j1 = !i1;
    float x1 = x0 - (float)i1 + G2, y1 = y0 - (float)j1 + G2;
    float x2 = x0 + -1.0f + (G2 + G2), y2 = y0 + -1.0f + (G2 + G2);
    int ii = i & 255, jj = j & 255;
#define SP(k) ((int)s->p[(k) & 255])
    float n0 = simplex_corner(SP(ii + SP(jj)), x0, y0);
    float n1 = simplex_corner(SP(ii + i1 + SP(jj + j1)), x1, y1);
    float n2 = simplex_corner(SP(ii + 1 + SP(jj + 1)), x2, y2);
#undef SP
    return (n0 + n1 + n2) * 70.0f;
}

// --- 島の高さ ---
// 島の中心になるチャンクかどうかの表 (計算済みの印と答えのビット)。±1920 チャンク
// (範囲の上限 ±30000 ブロックに島の影響が届く 12 チャンクを足した広さ) まで覚える。
#define CENTRE_R 1920
#define CENTRE_BITS ((size_t)(2 * CENTRE_R) * (2 * CENTRE_R))
static uint32_t g_known[CENTRE_BITS / 32], g_centre[CENTRE_BITS / 32];

static int is_centre(int ix, int iz)
{
    if ((int64_t)iz * iz + (int64_t)ix * ix <= 4096)
        return 0;
    if (ix < -CENTRE_R || ix >= CENTRE_R || iz < -CENTRE_R || iz >= CENTRE_R)
        return simplex2(&g_island, (float)ix, (float)iz) < -0.9f;
    size_t i = (size_t)(ix + CENTRE_R) * (2 * CENTRE_R) + (size_t)(iz + CENTRE_R);
    uint32_t bit = 1u << (i & 31);
    if (!(g_known[i >> 5] & bit)) {
        g_known[i >> 5] |= bit;
        if (simplex2(&g_island, (float)ix, (float)iz) < -0.9f)
            g_centre[i >> 5] |= bit;
    }
    return (g_centre[i >> 5] & bit) != 0;
}

static float hclamp(float h) { return h > 80.0f ? 80.0f : (h <= -100.0f ? -100.0f : h); }

// (cellx, cellz) はチャンクの中の 8 ブロックごとの格子 (0〜2)
static float island_height(int cx, int cz, int cellx, int cellz)
{
    float x = (float)(cellx + cx * 2), z = (float)(cellz + cz * 2);
    float h = hclamp(sqrtf(z * z + x * x) * -8.0f + 100.0f);
    for (int dx = -12; dx <= 12; dx++) {
        int ix = cx + dx;
        float a = (float)(cellx + dx * -2);
        for (int dz = -12; dz <= 12; dz++) {
            int iz = cz + dz;
            if (!is_centre(ix, iz))
                continue;
            uint32_t aiz = (uint32_t)(iz < 0 ? -(int64_t)iz : iz), aix = (uint32_t)(ix < 0 ? -(int64_t)ix : ix);
            float factor = (float)((aiz * 147u + aix * 3439u) % 13u + 9u);
            float b = (float)(cellz - dz * 2);
            float o = hclamp(100.0f - sqrtf(b * b + a * a) * factor);
            if (o > h)
                h = o;
        }
    }
    return h;
}

float end_island(int cx, int cz) { return island_height(cx, cz, 1, 1); }

// チャンクの 3×3 の格子の島の高さをまとめて (island_height を 9 回呼ぶのと同じ値)
static void island_heights9(int cx, int cz, float h[9])
{
    for (int c = 0; c < 9; c++) {
        float x = (float)(c / 3 + cx * 2), z = (float)(c % 3 + cz * 2);
        h[c] = hclamp(sqrtf(z * z + x * x) * -8.0f + 100.0f);
    }
    for (int dx = -12; dx <= 12; dx++) {
        int ix = cx + dx;
        for (int dz = -12; dz <= 12; dz++) {
            int iz = cz + dz;
            if (!is_centre(ix, iz))
                continue;
            uint32_t aiz = (uint32_t)(iz < 0 ? -(int64_t)iz : iz), aix = (uint32_t)(ix < 0 ? -(int64_t)ix : ix);
            float factor = (float)((aiz * 147u + aix * 3439u) % 13u + 9u);
            for (int c = 0; c < 9; c++) {
                float a = (float)(c / 3 + dx * -2), b = (float)(c % 3 - dz * 2);
                float o = hclamp(100.0f - sqrtf(b * b + a * a) * factor);
                if (o > h[c])
                    h[c] = o;
            }
        }
    }
}

// --- 地形 ---
// 3×3 本の柱 × 33 段の密度を、統合版と同じ順番 (柱ごとに上へ) で計算する。
// 統合版は前の柱の勾配を使い回す癖があり、それも再現する。
#define ROWS 33

static float smooth(float t) { return ((t * 6.0f - 15.0f) * t + 10.0f) * (t * t * t); }

static float gdot(int h, float x, float y, float z)
{
    const float *g = GRAD[h & 15];
    return g[0] * x + g[1] * y + g[2] * z;
}

// 地形は y = 76 より上には決してできない (密度を最大に見積もっても 19 段目より上は負)。
// なので 0〜19 段目だけ計算し、20 段目から上は負の値にしておく。
#define LIVE 20

// need[c][gy] が 0 の格子は計算しない。ただし勾配の使い回しの状態は同じように進め、
// 使い回される勾配は必要になったときに計算する。
static void fill_octave(const Improved *n, float ox, float oz, float xzs, float ys, float factor,
                        float rows[ROWS][9], const uint8_t need[ROWS][9])
{
    float inv = 1.0f / factor;
    float fudge_y = ys * 0.0f + n->yo;
    int fudge_index = (int)fudge_y;
    float fudge_frac = fudge_y - (float)fudge_index;
    int ix[9], iz[9];
    float fx[9], fz[9], sx[9], sz[9];
    for (int c = 0; c < 9; c++) {
        float vx = ((float)(c / 3) + ox) * xzs + n->xo;
        float vz = ((float)(c % 3) + oz) * xzs + n->zo;
        float flx = floorf(vx), flz = floorf(vz);
        ix[c] = (int)flx & 255;
        fx[c] = vx - flx;
        iz[c] = (int)flz & 255;
        fz[c] = vz - flz;
        sx[c] = smooth(fx[c]);
        sz[c] = smooth(fz[c]);
    }
    const uint8_t *p = n->p;
#define P(k) ((int)p[(k) & 255])
    int last_iy = -1;
    int pending = 0, pc = 0, piy = 0;  // まだ計算していない勾配 (どの柱の、どの高さか)
    float pgy = 0;
    float cr[4] = { 0, 0, 0, 0 };
    for (int c = 0; c < 9; c++)
        for (int gy = 0; gy < ROWS; gy++) {
            float y = (float)gy * ys + n->yo;
            float fl = floorf(y);
            int iy = (int)fl & 255;
            float fy = y - fl;
            if ((int)y == fudge_index) {
                pending = 1;
                pgy = fudge_frac;
            } else if (iy != last_iy) {
                pending = 1;
                pgy = fy - floorf(fy / ys) * ys;
            }
            if (pending) {
                pc = c;
                piy = iy;
                last_iy = iy;
            }
            if (!need[gy][c])
                continue;
            if (pending) {
                int a = P(ix[pc]) + piy, b = P(ix[pc] + 1) + piy;
                int aa = P(a), ab = P(a + 1), ba = P(b), bb = P(b + 1);
                int z0 = iz[pc];
                float X = fx[pc], Z = fz[pc], S = sx[pc], g = pgy, d0, d1;
                d0 = gdot(P(aa + z0), X, g, Z);
                d1 = gdot(P(ba + z0), X - 1.0f, g, Z);
                cr[0] = (d1 - d0) * S + d0;
                d0 = gdot(P(ab + z0), X, g - 1.0f, Z);
                d1 = gdot(P(bb + z0), X - 1.0f, g - 1.0f, Z);
                cr[1] = (d1 - d0) * S + d0;
                d0 = gdot(P(aa + z0 + 1), X, g, Z - 1.0f);
                d1 = gdot(P(ba + z0 + 1), X - 1.0f, g, Z - 1.0f);
                cr[2] = (d1 - d0) * S + d0;
                d0 = gdot(P(ab + z0 + 1), X, g - 1.0f, Z - 1.0f);
                d1 = gdot(P(bb + z0 + 1), X - 1.0f, g - 1.0f, Z - 1.0f);
                cr[3] = (d1 - d0) * S + d0;
                pending = 0;
            }
            float sy = smooth(fy);
            float nearv = (cr[1] - cr[0]) * sy + cr[0];
            float farv = (cr[3] - cr[2]) * sy + cr[2];
            rows[gy][c] += ((farv - nearv) * sz[c] + nearv) * inv;
        }
#undef P
}

static void fill_grid(const Improved *oct, int count, float ox, float oz, float xzs, float ys,
                      float rows[ROWS][9], const uint8_t need[ROWS][9])
{
    memset(rows, 0, sizeof(float) * ROWS * 9);
    float factor = 1.0f;
    for (int k = 0; k < count; k++) {
        fill_octave(&oct[k], ox, oz, xzs * factor, ys * factor, factor, rows, need);
        factor *= 0.5f;
    }
}

static float slide(int row, float blended, float h)
{
    if (row == ROWS - 1)
        return (h + (blended + -8.0f)) * 0.71875f + -843.75f;
    float v = blended + -8.0f + h;
    if (row > 14) {
        float t = (float)(row - 14) * 0.015625f;
        return t * -3000.0f + (1.0f - t) * v;
    }
    if (row < 8) {
        float t = (float)(8 - row) / 7.0f;
        return t * -30.0f + (1.0f - t) * v;
    }
    return v;
}

// エンドストーンのあるブロック。col[x][z] の 128 ビットが y = 0〜127。
// 高い所しか使わないことが多いので、まず y >= ylo だけ作る (それより下は未計算)。
typedef struct { int cx, cz, used, ylo; uint32_t col[16][16][4]; } Chunk;

// lo 段目 (y = 4 * lo) から上を作る
static void make_chunk(int cx, int cz, int lo, Chunk *o)
{
    static float mainv[ROWS][9], minv[ROWS][9], maxv[ROWS][9], cells[9][ROWS], t[ROWS][9], ih[9];
    static uint8_t live[ROWS][9], need_min[ROWS][9], need_max[ROWS][9];
    float ox = (float)(cx * 2), oz = (float)(cz * 2);
    // 混ぜる割合 t が 0 未満なら下限のノイズだけ、1 を超えれば上限のノイズだけ使う
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < 9; c++)
            live[r][c] = r >= lo && r < LIVE;
    fill_grid(g_main, 8, ox, oz, 17.1103f, 4.277575f, mainv, live);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < 9; c++) {
            t[r][c] = (mainv[r][c] / 10.0f + 1.0f) * 0.5f;
            need_min[r][c] = live[r][c] && t[r][c] <= 1.0f;
            need_max[r][c] = live[r][c] && t[r][c] >= 0.0f;
        }
    fill_grid(g_min, 16, ox, oz, 1368.824f, 684.412f, minv, need_min);
    fill_grid(g_max, 16, ox, oz, 1368.824f, 684.412f, maxv, need_max);
    island_heights9(cx, cz, ih);
    for (int c = 0; c < 9; c++) {
        float h = ih[c];
        for (int r = 0; r < ROWS; r++) {
            float lo = minv[r][c] * 0.001953125f, hi = maxv[r][c] * 0.001953125f, tt = t[r][c];
            float b = tt < 0.0f ? lo : (tt <= 1.0f ? (hi - lo) * tt + lo : hi);
            cells[c][r] = r < LIVE ? slide(r, b, h) : -1.0f;
        }
    }
    memset(o->col, 0, sizeof o->col);
    o->cx = cx;
    o->cz = cz;
    o->used = 1;
    o->ylo = lo * 4;
    static const float fr[4] = { 0.0f, 0.25f, 0.5f, 0.75f };
#define AT(x, z, y) cells[(x) * 3 + (z)][y]
    for (int ax = 0; ax < 2; ax++)
        for (int az = 0; az < 2; az++)
            for (int ay = lo; ay < LIVE - 1; ay++) {
                float c000 = AT(ax, az, ay), c001 = AT(ax, az, ay + 1);
                float c100 = AT(ax + 1, az, ay), c101 = AT(ax + 1, az, ay + 1);
                float c010 = AT(ax, az + 1, ay), c011 = AT(ax, az + 1, ay + 1);
                float c110 = AT(ax + 1, az + 1, ay), c111 = AT(ax + 1, az + 1, ay + 1);
                for (int lz = 0; lz < 8; lz++) {
                    float tz = (float)lz * 0.125f;
                    float low = (c010 - c000) * tz + c000;
                    float high = (c011 - c001) * tz + c001;
                    for (int lx = 0; lx < 8; lx++) {
                        float tx = (float)lx * 0.125f;
                        float bottom = (((c110 - c100) * tz + c100) - low) * tx + low;
                        float step = (tx * ((tz * (c111 - c101) + c101) - high) + high) - bottom;
                        for (int ly = 0; ly < 4; ly++)
                            if (step * fr[ly] + bottom > 0.0f) {
                                int y = ay * 4 + ly;
                                o->col[ax * 8 + lx][az * 8 + lz][y >> 5] |= 1u << (y & 31);
                            }
                    }
                }
            }
#undef AT
}

// 作った地形を覚えておく (同じチャンクを何度も使うので)。幅 64 チャンク × 8 行ぶん。
static Chunk g_chunks[512];

// 高さ y まで作ってあるチャンク
static const Chunk *get_chunk(int cx, int cz, int y)
{
    Chunk *c = &g_chunks[(cx & 63) | (cz & 7) << 6];
    if (!c->used || c->cx != cx || c->cz != cz)
        make_chunk(cx, cz, 10, c);
    if (y < c->ylo)
        make_chunk(cx, cz, 0, c);
    return c;
}

// ブロック (x, y, z) がエンドストーンか
static int solid(int x, int y, int z)
{
    if (y < 0 || y >= 128)
        return 0;
    const Chunk *c = get_chunk(x >> 4, z >> 4, y);
    return (c->col[x & 15][z & 15][y >> 5] >> (y & 31)) & 1;
}

static int col_top(const Chunk *c, int x, int z)
{
    for (int w = 3; w >= 0; w--)
        if (c->col[x][z][w])
            return w * 32 + (31 - __builtin_clz(c->col[x][z][w])) + 1;
    return 0;
}

// 列 (x, z) の一番上のエンドストーンの 1 つ上 (何もなければ 0)
static int top(int x, int z)
{
    const Chunk *c = get_chunk(x >> 4, z >> 4, 127);
    int h = col_top(c, x & 15, z & 15);
    if (h > c->ylo || c->ylo == 0)
        return h;
    return col_top(get_chunk(x >> 4, z >> 4, 0), x & 15, z & 15);
}

// --- エンドシティ ---
enum {
    BASE_FLOOR, BASE_ROOF, BRIDGE_END, BRIDGE_GENTLE, BRIDGE_PIECE, BRIDGE_STEEP, FAT_BASE, FAT_MIDDLE,
    FAT_TOP, SECOND_FLOOR, SECOND_FLOOR_2, SECOND_ROOF, SHIP, THIRD_FLOOR, THIRD_FLOOR_C, THIRD_ROOF,
    TOWER_BASE, TOWER_PIECE, TOWER_TOP,
};
// Java 版 1.12 の部品 (.nbt) の大きさ
static const int SIZE[][3] = {
    {10, 4, 10}, {12, 2, 12}, {5, 6, 2}, {5, 7, 8}, {5, 6, 4}, {5, 7, 4}, {13, 4, 13}, {13, 8, 13},
    {17, 6, 17}, {12, 8, 12}, {12, 8, 12}, {14, 2, 14}, {13, 24, 29}, {14, 8, 14}, {14, 8, 14}, {16, 2, 16},
    {7, 7, 7}, {7, 4, 7}, {9, 5, 9},
};

typedef struct { int x0, y0, z0, x1, y1, z1; } Box;
typedef struct { int type, x, y, z, rot; int64_t kind; Box bb; } Piece;
typedef struct { Piece *v; int n, cap; } List;

static void rotate(int rot, int x, int z, int *ox, int *oz)
{
    switch (rot & 3) {
    case 0: *ox = x; *oz = z; break;
    case 1: *ox = -z; *oz = x; break;
    case 2: *ox = -x; *oz = -z; break;
    default: *ox = z; *oz = -x; break;
    }
}

static int add(List *l, const Piece *parent, int ox, int oy, int oz, int type, int rot)
{
    if (l->n >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = realloc(l->v, sizeof(Piece) * l->cap);
    }
    int dx, dz;
    rotate(parent->rot, ox, oz, &dx, &dz);
    Piece p = { type, parent->x + dx, parent->y + oy, parent->z + dz, rot & 3, 0, { 0 } };
    int sx = SIZE[type][0], sy = SIZE[type][1], sz = SIZE[type][2];
    if (p.rot & 1) {
        int t = sx;
        sx = sz;
        sz = t;
    }
    // 箱は x, z 方向に部品の大きさ + 1 (Java 版 1.12 と同じ)
    int x0 = p.rot == 1 || p.rot == 2 ? -sx : 0;
    int z0 = p.rot >= 2 ? -sz : 0;
    p.bb = (Box){ p.x + x0, p.y, p.z + z0, p.x + x0 + sx, p.y + sy - 1, p.z + z0 + sz };
    l->v[l->n] = p;
    return l->n++;
}

static int meet(const Box *a, const Box *b)
{
    return a->x1 >= b->x0 && a->x0 <= b->x1 && a->z1 >= b->z0 && a->z0 <= b->z1 && a->y1 >= b->y0 && a->y0 <= b->y1;
}

enum { G_TOWER, G_FAT, G_BRIDGE, G_HOUSE };
typedef struct { MT *r; int ship; } Ctx;
static int gen(Ctx *c, int g, int depth, const Piece *parent, int px, int py, int pz, List *list);

// 子の部品をまとめて作り、既にある部品とぶつかったらまとめて捨てる
static int recurse(Ctx *c, int g, int depth, const Piece *parent, int px, int py, int pz, List *list)
{
    if (depth > 8)
        return 0;
    List local = { 0 };
    Piece par = *parent;
    if (!gen(c, g, depth, &par, px, py, pz, &local)) {
        free(local.v);
        return 0;
    }
    int64_t kind = (int64_t)(mt_next_full(c->r) >> 1);
    for (int i = 0; i < local.n; i++)
        local.v[i].kind = kind;
    for (int i = 0; i < local.n; i++)
        for (int k = 0; k < list->n; k++)
            if (meet(&list->v[k].bb, &local.v[i].bb)) {
                if (list->v[k].kind != par.kind) {
                    free(local.v);
                    return 0;
                }
                break;
            }
    for (int i = 0; i < local.n; i++) {
        if (list->n >= list->cap) {
            list->cap = list->cap ? list->cap * 2 : 64;
            list->v = realloc(list->v, sizeof(Piece) * list->cap);
        }
        list->v[list->n++] = local.v[i];
    }
    free(local.v);
    return 1;
}

static const int TOWER_BRIDGES[4][4] = { {0, 1, -1, 0}, {1, 6, -1, 1}, {3, 0, -1, 5}, {2, 5, -1, 6} };
static const int FAT_BRIDGES[4][4] = { {0, 4, -1, 0}, {1, 12, -1, 4}, {3, 0, -1, 8}, {2, 8, -1, 12} };

static int gen(Ctx *c, int g, int depth, const Piece *parent, int px, int py, int pz, List *list)
{
    int r = parent->rot;
    MT *R = c->r;
    Piece tmp;
    if (g == G_TOWER) {
        int a = mt_int_full(R, 2);
        int b = mt_int_full(R, 2);
        int t = add(list, parent, 3 + a, -3, 3 + b, TOWER_BASE, r);
        tmp = list->v[t];
        t = add(list, &tmp, 0, 7, 0, TOWER_PIECE, r);
        int floor = mt_int_full(R, 3) == 0 ? t : -1;
        int n = mt_int_full(R, 3);
        for (int j = 0; j <= n; j++) {
            tmp = list->v[t];
            t = add(list, &tmp, 0, 4, 0, TOWER_PIECE, r);
            if (j < n && mt_bool_full(R))
                floor = t;
        }
        if (floor >= 0) {
            Piece fl = list->v[floor];
            for (int k = 0; k < 4; k++)
                if (mt_bool_full(R)) {
                    const int *B = TOWER_BRIDGES[k];
                    int e = add(list, &fl, B[1], B[2], B[3], BRIDGE_END, r + B[0]);
                    Piece end = list->v[e];  // add で list->v が動くので先に添字を取る
                    recurse(c, G_BRIDGE, depth + 1, &end, 0, 0, 0, list);
                }
        } else if (depth != 7) {
            tmp = list->v[t];
            return recurse(c, G_FAT, depth + 1, &tmp, 0, 0, 0, list);
        }
        tmp = list->v[t];
        add(list, &tmp, -1, 4, -1, TOWER_TOP, r);
        return 1;
    }
    if (g == G_FAT) {
        int t = add(list, parent, -3, 4, -3, FAT_BASE, r);
        tmp = list->v[t];
        t = add(list, &tmp, 0, 4, 0, FAT_MIDDLE, r);
        for (int i = 0; i < 2 && mt_int_full(R, 3) != 0; i++) {
            tmp = list->v[t];
            t = add(list, &tmp, 0, 8, 0, FAT_MIDDLE, r);
            Piece mid = list->v[t];
            for (int k = 0; k < 4; k++)
                if (mt_bool_full(R)) {
                    const int *B = FAT_BRIDGES[k];
                    int e = add(list, &mid, B[1], B[2], B[3], BRIDGE_END, r + B[0]);
                    Piece end = list->v[e];
                    recurse(c, G_BRIDGE, depth + 1, &end, 0, 0, 0, list);
                }
        }
        tmp = list->v[t];
        add(list, &tmp, -2, 8, -2, FAT_TOP, r);
        return 1;
    }
    if (g == G_BRIDGE) {
        int n = mt_int_full(R, 4);
        int t = add(list, parent, 0, 0, -4, BRIDGE_PIECE, r);
        list->v[t].kind = -1;
        int j = 0;
        for (int k = 0; k <= n; k++) {
            Piece last = list->v[t];
            if (mt_bool_full(R)) {
                t = add(list, &last, 0, j, -4, BRIDGE_PIECE, r);
                j = 0;
            } else {
                if (mt_bool_full(R))
                    t = add(list, &last, 0, j, -4, BRIDGE_STEEP, r);
                else
                    t = add(list, &last, 0, j, -8, BRIDGE_GENTLE, r);
                j = 4;
            }
        }
        Piece last = list->v[t];
        if (!c->ship && mt_int_full(R, 10 - depth) == 0) {
            int a = mt_int_full(R, 8);
            int b = mt_int_full(R, 10);
            add(list, &last, a - 8, j, b - 70, SHIP, r);
            c->ship = 1;
        } else if (!recurse(c, G_HOUSE, depth + 1, &last, -3, j + 1, -11, list)) {
            return 0;
        }
        t = add(list, &last, 4, j, 0, BRIDGE_END, r + 2);
        list->v[t].kind = -1;
        return 1;
    }
    // 家
    if (depth > 8)
        return 0;
    int t = add(list, parent, px, py, pz, BASE_FLOOR, r);
    int k = mt_int_full(R, 3);
    tmp = list->v[t];
    if (k == 0) {
        add(list, &tmp, -1, 4, -1, BASE_ROOF, r);
        return 1;
    }
    t = add(list, &tmp, -1, 0, -1, SECOND_FLOOR_2, r);
    tmp = list->v[t];
    if (k == 1) {
        t = add(list, &tmp, -1, 8, -1, SECOND_ROOF, r);
    } else {
        t = add(list, &tmp, -1, 4, -1, THIRD_FLOOR_C, r);
        tmp = list->v[t];
        t = add(list, &tmp, -1, 8, -1, THIRD_ROOF, r);
    }
    tmp = list->v[t];
    recurse(c, G_TOWER, depth + 1, &tmp, 0, 0, 0, list);
    return 1;
}

// 部品を全部作って、最後まで残ったエンドシップを探す (途中で捨てられた枝の船は無い)
static void city_ship(MT *R, int x, int y, int z, int rot, EndCity *city)
{
    List list = { 0 };
    Piece origin = { 0, x, y, z, 0, 0, { 0 } };
    add(&list, &origin, 0, 0, 0, BASE_FLOOR, rot);
    Piece par = list.v[0];
    int t = add(&list, &par, -1, 0, -1, SECOND_FLOOR, rot);
    par = list.v[t];
    t = add(&list, &par, -1, 4, -1, THIRD_FLOOR, rot);
    par = list.v[t];
    t = add(&list, &par, -1, 8, -1, THIRD_ROOF, rot);
    par = list.v[t];
    Ctx c = { R, 0 };
    recurse(&c, G_TOWER, 1, &par, 0, 0, 0, &list);
    city->ship = 0;
    for (int i = 0; i < list.n; i++)
        if (list.v[i].type == SHIP) {
            // エリトラの額縁は船の部品の (6, 5, 7)
            int dx, dz;
            rotate(list.v[i].rot, 6, 7, &dx, &dz);
            city->ship = 1;
            city->ex = list.v[i].x + dx;
            city->ey = list.v[i].y + 5;
            city->ez = list.v[i].z + dz;
            break;
        }
    free(list.v);
}

// 一度計算した領域を覚えておく (エンドシティとエンドシップで 2 回使う)
typedef struct { int rx, rz, state; EndCity c; } CityMemo;
static CityMemo g_memo[1 << 13];

static int city_at(int rx, int rz, EndCity *out)
{
    MT R;
    mt_seed_full(&R, (uint32_t)((uint64_t)(int64_t)rx * 341873128712ull + (uint64_t)(int64_t)rz * 132897987541ull
                                + g_seed + 10387313u));
    int a = mt_int_full(&R, 9), b = mt_int_full(&R, 9);
    int cx = rx * 20 + (a + b) / 2;
    a = mt_int_full(&R, 9);
    b = mt_int_full(&R, 9);
    int cz = rz * 20 + (a + b) / 2;
    // 原点から 64 チャンク以上離れていて、島がある (Java 版の「40 より高い」より緩い)
    if ((int64_t)cx * cx + (int64_t)cz * cz <= 4096)
        return 0;
    if ((int16_t)(int)floorf(end_island(cx, cz)) < 0)
        return 0;
    // 向きで決まる 4 つの柱の地面の高さの一番低いところが 60 以上
    MT m;
    mt_seed(&m, (uint32_t)cx + (uint32_t)cz * 10387313u, 1);
    int rot = mt_next(&m) & 3;
    static const int DX[4] = { 12, 2, 2, 12 }, DZ[4] = { 12, 12, 2, 2 };
    int bx = cx * 16, bz = cz * 16, h;
    int y = top(bx + 7, bz + 7);
    if ((h = top(bx + 7, bz + DZ[rot])) < y) y = h;
    if ((h = top(bx + DX[rot], bz + 7)) < y) y = h;
    if ((h = top(bx + DX[rot], bz + DZ[rot])) < y) y = h;
    if (y < 60)
        return 0;
    out->x = cx * 16 + 8;
    out->y = y;
    out->z = cz * 16 + 8;
    city_ship(&R, out->x, y, out->z, rot, out);
    return 1;
}

int end_city(int rx, int rz, EndCity *c)
{
    CityMemo *m = &g_memo[((unsigned)rx * 31u + (unsigned)rz * 1031u) & ((1 << 13) - 1)];
    if (m->state == 0 || m->rx != rx || m->rz != rz) {
        m->rx = rx;
        m->rz = rz;
        m->state = city_at(rx, rz, &m->c) ? 2 : 1;
    }
    *c = m->c;
    return m->state == 2;
}

// --- 外側の島のエンドゲートウェイ ---
// チャンクの装飾の乱数で、コーラスフラワーを育てたあとの次の乱数が 700 で割り切れると置かれる。
// コーラスの育ち方で乱数の数が変わるので、地形を使ってコーラスを育ててみる。
enum { AIR, STONE, PLANT };

// 装飾で置いたブロック (チャンク 1 つぶんなので小さい表)
#define OVERLAY 4096
static struct { int x, y, z, b; } g_over[OVERLAY];
static int g_over_used[OVERLAY], g_over_n;

static unsigned over_slot(int x, int y, int z)
{
    return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u) & (OVERLAY - 1);
}

static void over_clear(void)
{
    for (int i = 0; i < g_over_n; i++)
        g_over[g_over_used[i]].b = 0;
    g_over_n = 0;
}

static void over_set(int x, int y, int z, int b)
{
    unsigned h = over_slot(x, y, z);
    while (g_over[h].b && !(g_over[h].x == x && g_over[h].y == y && g_over[h].z == z))
        h = (h + 1) & (OVERLAY - 1);
    if (!g_over[h].b) {
        if (g_over_n >= OVERLAY / 2)
            return;  // 起こらない (1 チャンクのコーラスはずっと少ない)
        g_over_used[g_over_n++] = (int)h;
        g_over[h].x = x;
        g_over[h].y = y;
        g_over[h].z = z;
    }
    g_over[h].b = b;
}

static int block(int x, int y, int z)
{
    if (y < 0 || y > 255)
        return AIR;
    for (unsigned h = over_slot(x, y, z); g_over[h].b; h = (h + 1) & (OVERLAY - 1))
        if (g_over[h].x == x && g_over[h].y == y && g_over[h].z == z)
            return g_over[h].b;
    return solid(x, y, z) ? STONE : AIR;
}

static const int SIDES[4][2] = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };

static int sides_empty(int x, int y, int z, int except)
{
    for (int i = 0; i < 4; i++)
        if (i != except && block(x + SIDES[i][0], y, z + SIDES[i][1]) != AIR)
            return 0;
    return 1;
}

// チャンクの装飾の乱数を先に必要なだけ作っておく。コーラス 1 本が使う乱数は、
// 枝分かれの数の上限 (根元 4 本、その先 3 本ずつ、深さ 4 まで) から多くても 376 個。
#define GROW_MAX 374
typedef struct { uint32_t v[2 + 4 * (2 + GROW_MAX) + 3]; int i; } Draws;

static int dint(Draws *d, int n) { return (int)(d->v[d->i++] % (uint32_t)n); }

// Java 版 1.12 の BlockChorusFlower.growTreeRecursive と同じ順番で乱数を引く
static void grow(Draws *r, int x, int y, int z, int rx, int rz, int depth)
{
    int n = dint(r, 4) + 1 + (depth == 0);
    for (int j = 1; j <= n; j++) {
        if (!sides_empty(x, y + j, z, -1))
            return;
        over_set(x, y + j, z, PLANT);
    }
    int ty = y + n, branched = 0;
    if (depth < 4) {
        int m = dint(r, 4) + (depth == 0);
        for (int q = 0; q < m; q++) {
            int side = dint(r, 4);
            int bx = x + SIDES[side][0], bz = z + SIDES[side][1];
            if (abs(bx - rx) < 8 && abs(bz - rz) < 8 && block(bx, ty, bz) == AIR && block(bx, ty - 1, bz) == AIR
                && sides_empty(bx, ty, bz, (side + 2) % 4)) {
                branched = 1;
                over_set(bx, ty, bz, PLANT);
                grow(r, bx, ty, bz, rx, rz, depth + 1);
            }
        }
    }
    if (!branched)
        over_set(x, ty, z, PLANT);  // 花
}

static uint32_t g_xf, g_zf;

int end_gateway(int cx, int cz, int *gx, int *gy, int *gz)
{
    if ((int64_t)cx * cx + (int64_t)cz * cz <= 4096 || end_island(cx, cz) <= 40.0f)
        return 0;
    MT mt;
    mt_seed_full(&mt, ((uint32_t)cz * g_zf + (uint32_t)cx * g_xf) ^ g_seed);
    // 0 番目は 1.18 からある地上用のマグマの規則が引く (エンドでは何も置かない)。1 番目がコーラスの数。
    static Draws d;
    d.v[0] = mt_next_full(&mt);
    d.v[1] = mt_next_full(&mt);
    uint32_t count = d.v[1] % 5, roll;
    // 最後のコーラスのあとの乱数が 700 で割り切れればゲートウェイ。そうなりうる乱数が
    // 1 つもなければ、地形を作ってコーラスを育てるまでもない
    int last = 2 + (int)count * (2 + GROW_MAX), maybe = 0;
    for (int k = 2; k <= last + 3; k++) {
        d.v[k] = mt_next_full(&mt);
        maybe |= k >= 2 + 2 * (int)count && k <= last && d.v[k] % 700 == 0;
    }
    if (!maybe)
        return 0;
    d.i = 2;
    int ox = cx * 16 | 8, oz = cz * 16 | 8;
    over_clear();
    for (uint32_t k = 0;; k++) {
        roll = d.v[d.i++];
        if (k >= count)
            break;
        uint32_t dz = d.v[d.i++];
        int x = (int)(roll & 15) + ox, z = (int)(dz & 15) + oz;
        int h = top(x, z);
        if (h > 0 && block(x, h, z) == AIR && block(x, h - 1, z) == STONE) {
            over_set(x, h, z, PLANT);
            grow(&d, x, h, z, x, z, 0);
        }
    }
    if (roll % 700 != 0)
        return 0;
    int x = (int)(d.v[d.i++] & 15) + ox;
    int z = (int)(d.v[d.i++] & 15) + oz;
    int h = top(x, z);
    if (h <= 0)
        return 0;
    *gx = x;
    *gy = (int)(uint16_t)(h + 3) + (int)(d.v[d.i++] % 7);
    *gz = z;
    return 1;
}

// --- 中央の島のエンドゲートウェイ ---
// 半径 96 の円上の 20 か所 (0 に向けて切り捨て)。ドラゴンを倒すたびに、
// シード値で並べ替えた表の後ろから 1 つずつできる。
static const int RING[20][2] = {
    {96, 0}, {91, 29}, {77, 56}, {56, 77}, {29, 91}, {0, 96}, {-29, 91}, {-56, 77}, {-77, 56}, {-91, 29},
    {-96, 0}, {-91, -29}, {-77, -56}, {-56, -77}, {-29, -91}, {0, -96}, {29, -91}, {56, -77}, {77, -56}, {91, -29},
};
static int g_ring[20];

void end_ring_gateway(int kill, int *x, int *z)
{
    int i = g_ring[20 - kill];
    *x = RING[i][0];
    *z = RING[i][1];
}

void end_setup(uint32_t seed)
{
    static MT mt;
    g_seed = seed;
    mt_seed_full(&mt, seed);
    for (int i = 0; i < 16; i++)
        improved_init(&g_min[i], &mt);
    for (int i = 0; i < 16; i++)
        improved_init(&g_max[i], &mt);
    for (int i = 0; i < 8; i++)
        improved_init(&g_main[i], &mt);
    for (int i = 0; i < 3; i++)
        mt_next_full(&mt);
    shuffle(g_island.p, &mt);

    mt_seed(&mt, seed, 2);
    g_xf = (mt_next(&mt) >> 1) | 1;
    g_zf = (mt_next(&mt) >> 1) | 1;

    mt_seed(&mt, seed, 19);
    for (int i = 0; i < 20; i++)
        g_ring[i] = i;
    for (int i = 1; i < 20; i++) {
        int j = mt_int(&mt, i + 1), t = g_ring[i];
        g_ring[i] = g_ring[j];
        g_ring[j] = t;
    }

    memset(g_known, 0, sizeof g_known);
    memset(g_centre, 0, sizeof g_centre);
    memset(g_memo, 0, sizeof g_memo);
    for (int i = 0; i < 512; i++)
        g_chunks[i].used = 0;
}

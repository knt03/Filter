// 統合版の構造物配置。定数と規則は、実機で確認された座標と照合されている
// オープンソースの統合版ファインダー (SeedFinder: github.com/zebedelu/SeedFinder,
// MCBE-seedcracker: github.com/Alist2930/MCBE-seedcracker,
// mc-mapper: github.com/BrendonKing32/mc-mapper) に揃えている。
#include "bedrock.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "finders.h"
#include "generator.h"

static Generator g;
static int g_mc;
static uint64_t g_seed;
static uint32_t g_seed32;  // MT を使う配置は seed の下位 32 ビットだけで決まる
static uint32_t g_chunk_a, g_chunk_b;  // チャンク単位の乱数に使う係数

// --- メルセンヌ・ツイスタ (MT19937) ---
// 先頭の数個の出力だけを作る。i 番目の出力には状態 i, i+1, i+397 が要る。
typedef struct { uint32_t s[624]; int i; } MT;

static void mt_seed(MT *mt, uint32_t seed, int n)
{
    mt->s[0] = seed;
    for (int k = 1; k < n + 397; k++)
        mt->s[k] = 1812433253u * (mt->s[k - 1] ^ (mt->s[k - 1] >> 30)) + (uint32_t)k;
    mt->i = 0;
}

static uint32_t mt_next(MT *mt)
{
    int k = mt->i++;
    uint32_t y = (mt->s[k] & 0x80000000u) | (mt->s[k + 1] & 0x7fffffffu);
    y = mt->s[k + 397] ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0);
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    return y ^ (y >> 18);
}

static int mt_int(MT *mt, int n) { return (int)(mt_next(mt) % (uint32_t)n); }
static float mt_float(MT *mt) { return (float)(mt_next(mt) * 2.3283064365386963e-10); }

// --- 構造物ごとの設定 ---
enum { SPREAD_UNIFORM, SPREAD_TRIANGULAR, SPREAD_JAVA, SPREAD_CHUNK, SPREAD_SPECIAL };

typedef struct {
    int salt;
    int spacing;   // 領域の一辺 (チャンク)
    int range;     // 領域内で位置が動く幅 = spacing - separation (チャンク)
    int spread;
    int cubiomes;  // cubiomes の StructureType (バイオーム判定・Java 配置に使う)
    int min_mc;
} Config;

static const Config CONFIGS[BE_KIND_NUM] = {
    [BE_VILLAGE]        = { 10387312, 34, 26, SPREAD_TRIANGULAR, Village,        MC_1_18 },
    [BE_DESERT_PYRAMID] = { 14357617, 32, 24, SPREAD_UNIFORM,    Desert_Pyramid, MC_1_18 },
    [BE_JUNGLE_TEMPLE]  = { 14357617, 32, 24, SPREAD_UNIFORM,    Jungle_Pyramid, MC_1_18 },
    [BE_SWAMP_HUT]      = { 14357617, 32, 24, SPREAD_UNIFORM,    Swamp_Hut,      MC_1_18 },
    [BE_IGLOO]          = { 14357617, 32, 24, SPREAD_UNIFORM,    Igloo,          MC_1_18 },
    [BE_MONUMENT]       = { 10387313, 32, 27, SPREAD_TRIANGULAR, Monument,       MC_1_18 },
    [BE_MANSION]        = { 10387319, 80, 60, SPREAD_TRIANGULAR, Mansion,        MC_1_18 },
    [BE_OUTPOST]        = { 165745296, 80, 56, SPREAD_TRIANGULAR, Outpost,       MC_1_18 },
    [BE_SHIPWRECK]      = { 165745295, 24, 20, SPREAD_UNIFORM,   Shipwreck,      MC_1_18 },
    [BE_OCEAN_RUIN]     = { 14357621, 20, 12, SPREAD_UNIFORM,    Ocean_Ruin,     MC_1_18 },
    [BE_TREASURE]       = { 16842397,  4,  2, SPREAD_TRIANGULAR, Treasure,       MC_1_18 },
    [BE_RUINED_PORTAL]  = { 40552231, 40, 25, SPREAD_UNIFORM,    Ruined_Portal,  MC_1_18 },
    [BE_ANCIENT_CITY]   = { 20083232, 24, 16, SPREAD_TRIANGULAR, Ancient_City,   MC_1_19_2 },
    // 新しい構造物は統合版でも Java 版と同じ配置 (64 ビットのシード全体を使う)
    [BE_TRAIL_RUINS]    = { 0, 0, 0, SPREAD_JAVA, Trail_Ruins,    MC_1_20 },
    [BE_TRIAL_CHAMBERS] = { 0, 0, 0, SPREAD_JAVA, Trial_Chambers, MC_1_21_1 },
    [BE_ABANDONED_CAMP] = { 0, 0, 0, SPREAD_JAVA, Abandoned_Camp, MC_26_3 },
    [BE_MINESHAFT]      = { 0, 1, 1, SPREAD_CHUNK, Mineshaft,     MC_1_18 },
    [BE_STRONGHOLD]     = { 0, 0, 0, SPREAD_SPECIAL, Stronghold,  MC_1_18 },
    [BE_DESERT_WELL]    = { 0, 1, 1, SPREAD_CHUNK, Desert_Well,   MC_1_18 },
};

int be_setup(uint64_t seed, int mc)
{
    if (mc < MC_1_18 || mc > MC_NEWEST)
        return -1;
    g_mc = mc;
    g_seed = seed;
    g_seed32 = (uint32_t)seed;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);
    MT mt;
    mt_seed(&mt, g_seed32, 2);
    g_chunk_a = mt_next(&mt) >> 1;
    g_chunk_b = mt_next(&mt) >> 1;
    return 0;
}

int be_kind_available(int kind)
{
    return kind >= 0 && kind < BE_KIND_NUM && g_mc >= CONFIGS[kind].min_mc;
}

// --- 位置の計算 ---

// 領域 (rx, rz) の生成候補のチャンク座標。
static void region_chunk(const Config *c, int rx, int rz, int *cx, int *cz)
{
    uint32_t rs = (uint32_t)((uint64_t)(int64_t)rx * 341873128712ull
                           + (uint64_t)(int64_t)rz * 132897987541ull
                           + g_seed + (uint64_t)(int64_t)c->salt);
    MT mt;
    if (c->spread == SPREAD_TRIANGULAR) {
        // 大きな構造物は 2 回引いた平均で、領域の中央に寄る
        mt_seed(&mt, rs, 4);
        int x1 = mt_int(&mt, c->range), x2 = mt_int(&mt, c->range);
        int z1 = mt_int(&mt, c->range), z2 = mt_int(&mt, c->range);
        *cx = rx * c->spacing + (x1 + x2) / 2;
        *cz = rz * c->spacing + (z1 + z2) / 2;
    } else {
        mt_seed(&mt, rs, 2);
        *cx = rx * c->spacing + mt_int(&mt, c->range);
        *cz = rz * c->spacing + mt_int(&mt, c->range);
    }
}

// --- バイオーム判定 ---

// 統合版は構造物のチャンク中央のバイオームを見る (地表、y=319 で取ると洞窟バイオームを避けられる)。
static int surface_biome(int cx, int cz)
{
    return getBiomeAt(&g, 4, cx * 4 + 2, 319 >> 2, cz * 4 + 2);
}

// 統合版の村は Java 版の村のバイオームに加えてヒマワリ平原と雪のタイガにもできる。
static int is_village_biome(int id)
{
    return id == plains || id == sunflower_plains || id == meadow || id == desert
        || id == savanna || id == taiga || id == snowy_plains || id == snowy_taiga;
}

// 村はチャンク中央のバイオームだけで決まる。cubiomes の Java 版判定は雪のタイガを
// 弾いてしまう (Chunkbase がタイガの村を出す位置を落とす) ので使わない。
// シード 6666 の実機の村 8 か所と、川に落ちて村ができない 3 か所の両方と一致する。
static int village_ok(int cx, int cz, int *biome)
{
    *biome = surface_biome(cx, cz);
    return is_village_biome(*biome);
}

// 前哨基地も統合版では雪のタイガ・ヒマワリ平原にできる。
static int is_outpost_biome(int id)
{
    return isViableFeatureBiome(g_mc, Outpost, id) || id == snowy_taiga
        || id == sunflower_plains;
}

static int viable(int kind, int cx, int cz, int *biome)
{
    const Config *c = &CONFIGS[kind];
    int x = cx * 16 + 8, z = cz * 16 + 8;
    int id;
    switch (kind) {
    case BE_VILLAGE:
        return village_ok(cx, cz, biome);
    case BE_MONUMENT:
        // 深海の中央 + 周囲 29 ブロックが海 (cubiomes の判定) かつ地表も深海
        id = *biome = surface_biome(cx, cz);
        return isDeepOcean(id) && isViableStructurePos(Monument, &g, x, z, 0);
    case BE_MANSION:
        id = *biome = surface_biome(cx, cz);
        return isViableFeatureBiome(g_mc, Mansion, id)
            && isViableStructurePos(Mansion, &g, x, z, 0);
    case BE_OUTPOST:
        // Java 版の「5 回に 1 回」「近くに村がない」は Java の乱数に依存するので課さない
        return is_outpost_biome(*biome = surface_biome(cx, cz));
    case BE_SHIPWRECK:
        id = *biome = surface_biome(cx, cz);
        return isOceanic(id) || id == beach || id == snowy_beach;
    case BE_TREASURE:
        // 統合版では「砂浜」タグのバイオーム (石だらけの海岸を含む)
        id = *biome = surface_biome(cx, cz);
        return id == beach || id == snowy_beach || id == stony_shore;
    case BE_RUINED_PORTAL:
        *biome = surface_biome(cx, cz);
        return 1;
    case BE_ANCIENT_CITY:
        // ディープダークは地下にあるので古代都市の基準の高さ (y=-27) で見る。
        // 表示するのは探すときに目印になる地表のバイオーム。
        *biome = surface_biome(cx, cz);
        return getBiomeAt(&g, 4, cx * 4 + 2, -27 >> 2, cz * 4 + 2) == deep_dark;
    default:
        id = *biome = surface_biome(cx, cz);
        return isViableFeatureBiome(g_mc, c->cubiomes, id);
    }
}

// --- 走査 ---

static int floordiv_i(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

typedef struct { BEHit *out; int max, n; int x0, z0, x1, z1; } Sink;

static int sink_add(Sink *s, int x, int z, int biome)
{
    if (x < s->x0 || x > s->x1 || z < s->z0 || z > s->z1)
        return 1;
    if (s->n >= s->max)
        return 0;
    s->out[s->n].x = x;
    s->out[s->n].z = z;
    s->out[s->n].biome = biome;
    s->n++;
    return 1;
}

// 行範囲 [lo, hi] を nparts 個に分けたうちの part 番目
static void band(int lo, int hi, int part, int nparts, int *a, int *b)
{
    long long n = (long long)hi - lo + 1;
    *a = lo + (int)(n * part / nparts);
    *b = lo + (int)(n * (part + 1) / nparts) - 1;
}

static int scan_regions(int kind, Sink *s, int part, int nparts)
{
    const Config *c = &CONFIGS[kind];
    int blocks = c->spacing * 16;
    int rx0 = floordiv_i(s->x0, blocks) - 1, rx1 = floordiv_i(s->x1, blocks) + 1;
    int rz0 = floordiv_i(s->z0, blocks) - 1, rz1 = floordiv_i(s->z1, blocks) + 1;
    int za, zb;
    band(rz0, rz1, part, nparts, &za, &zb);
    for (int rz = za; rz <= zb; rz++)
        for (int rx = rx0; rx <= rx1; rx++) {
            int cx, cz, biome;
            region_chunk(c, rx, rz, &cx, &cz);
            int x = cx * 16 + 8, z = cz * 16 + 8;
            if (x < s->x0 || x > s->x1 || z < s->z0 || z > s->z1)
                continue;
            if (!viable(kind, cx, cz, &biome))
                continue;
            if (!sink_add(s, x, z, biome))
                return 0;
        }
    return 1;
}

// Java 版と同じ配置の構造物 (旅路の遺跡・試練の間・放棄されたキャンプ)
static int scan_java(int kind, Sink *s, int part, int nparts)
{
    int type = CONFIGS[kind].cubiomes;
    StructureConfig sc;
    if (!getStructureConfig(type, g_mc, &sc))
        return 1;
    int blocks = sc.regionSize * 16;
    int rx0 = floordiv_i(s->x0, blocks) - 1, rx1 = floordiv_i(s->x1, blocks) + 1;
    int rz0 = floordiv_i(s->z0, blocks) - 1, rz1 = floordiv_i(s->z1, blocks) + 1;
    int za, zb;
    band(rz0, rz1, part, nparts, &za, &zb);
    for (int rz = za; rz <= zb; rz++)
        for (int rx = rx0; rx <= rx1; rx++) {
            Pos p;  // チャンクの角のブロック座標
            if (!getStructurePos(type, g_mc, g_seed, rx, rz, &p))
                continue;
            if (p.x + 8 < s->x0 || p.x + 8 > s->x1 || p.z + 8 < s->z0 || p.z + 8 > s->z1)
                continue;
            if (!isViableStructurePos(type, &g, p.x, p.z, 0))
                continue;
            // 開始ピースの中心 (Chunkbase の表示と一致する)。分からなければチャンク中央。
            int x = p.x + 8, z = p.z + 8;
            StructureVariant sv;
            if (type != Trail_Ruins && getVariant(&sv, type, g_mc, g_seed, p.x, p.z, -1)) {
                x = (p.x * 2 + 2 * sv.x + sv.sx - 1) / 2;
                z = (p.z * 2 + 2 * sv.z + sv.sz - 1) / 2;
            }
            if (!sink_add(s, x, z, getBiomeAt(&g, 4, x >> 2, 319 >> 2, z >> 2)))
                return 0;
        }
    return 1;
}

// 廃坑: チャンクごとに 0.4% (原点から 80 チャンク以内はさらに少ない)
static int mineshaft_chunk(int cx, int cz)
{
    MT mt;
    mt_seed(&mt, (g_chunk_a * (uint32_t)cx) ^ (g_chunk_b * (uint32_t)cz) ^ g_seed32, 3);
    mt_next(&mt);
    if (mt_float(&mt) >= 0.004f)
        return 0;
    int far = abs(cx) > abs(cz) ? abs(cx) : abs(cz);
    return mt_int(&mt, 80) < far;
}

// 砂漠の井戸: 装飾用の乱数でチャンクごとに 1/500。位置はチャンク内の (dx, dz)。
static int desert_well_chunk(int cx, int cz, int *x, int *z)
{
    uint32_t ds = ((uint32_t)cx * (g_chunk_a | 1u) + (uint32_t)cz * (g_chunk_b | 1u)) ^ g_seed32;
    ds = ((ds >> 2) + (ds << 6) + (uint32_t)-1160484816 - 1640531527u) ^ ds;
    MT mt;
    mt_seed(&mt, ds, 3);
    if (mt_int(&mt, 500) != 0)
        return 0;
    int dz = mt_int(&mt, 16);
    int dx = mt_int(&mt, 16);
    *x = cx * 16 + dx;
    *z = cz * 16 + dz;
    return 1;
}

static int scan_chunks(int kind, Sink *s, int part, int nparts)
{
    int cx0 = s->x0 >> 4, cx1 = s->x1 >> 4;
    int cza, czb;
    band(s->z0 >> 4, s->z1 >> 4, part, nparts, &cza, &czb);
    for (int cz = cza; cz <= czb; cz++)
        for (int cx = cx0; cx <= cx1; cx++) {
            if (kind == BE_MINESHAFT) {
                if (!mineshaft_chunk(cx, cz))
                    continue;
                if (!sink_add(s, cx * 16 + 8, cz * 16 + 8, surface_biome(cx, cz)))
                    return 0;
            } else {
                int x, z;
                if (!desert_well_chunk(cx, cz, &x, &z))
                    continue;
                int id = getBiomeAt(&g, 4, x >> 2, 319 >> 2, z >> 2);
                if (id != desert)
                    continue;
                if (!sink_add(s, x, z, id))
                    return 0;
            }
        }
    return 1;
}

// 村のチャンクか (要塞の計算用)
static int is_village_chunk(int cx, int cz)
{
    const Config *c = &CONFIGS[BE_VILLAGE];
    int rx = floordiv_i(cx, c->spacing), rz = floordiv_i(cz, c->spacing);
    int vx, vz, biome;
    region_chunk(c, rx, rz, &vx, &vz);
    return vx == cx && vz == cz && village_ok(cx, cz, &biome);
}

// 要塞: 統合版では原点の周りの村の下に必ず 3 つできる。ワールドのシードで
// 角度と距離 (40〜55 チャンク) を決め、その周り 16×16 チャンクに村があれば
// そこに置く。見つからなければ角度を 45° 回して距離を 4 チャンク伸ばす。
// (それ以外にも要塞はランダムに生成されるが、その式は公開・検証されていない)
static int scan_strongholds(Sink *s)
{
    MT mt;
    mt_seed(&mt, g_seed32, 2);
    double angle = 2.0 * M_PI * mt_float(&mt);
    int dist = 40 + mt_int(&mt, 16);
    int count = 0;
    for (int tries = 0; count < 3 && tries < 100; tries++) {
        int cx = (int)floor(cos(angle) * dist);
        int cz = (int)floor(sin(angle) * dist);
        int found = 0;
        for (int x = cx - 8; x < cx + 8 && !found; x++)
            for (int z = cz - 8; z < cz + 8 && !found; z++)
                if (is_village_chunk(x, z)) {
                    found = 1;
                    if (!sink_add(s, x * 16 + 8, z * 16 + 8, surface_biome(x, z)))
                        return 0;
                }
        if (found) {
            count++;
            angle += 0.6 * M_PI;
            dist += 8;
        } else {
            angle += 0.25 * M_PI;
            dist += 4;
        }
    }
    return 1;
}

int be_scan(int kind, int x0, int z0, int x1, int z1,
            int part, int nparts, BEHit *out, int max)
{
    if (!be_kind_available(kind))
        return -1;
    if (nparts < 1 || part < 0 || part >= nparts)
        return -1;
    Sink s = { out, max, 0, x0, z0, x1, z1 };
    switch (CONFIGS[kind].spread) {
    case SPREAD_JAVA:
        scan_java(kind, &s, part, nparts);
        break;
    case SPREAD_CHUNK:
        scan_chunks(kind, &s, part, nparts);
        break;
    case SPREAD_SPECIAL:
        if (part == 0)
            scan_strongholds(&s);
        break;
    default:
        scan_regions(kind, &s, part, nparts);
        break;
    }
    return s.n;
}

int be_biome_grid(int x0, int z0, int w, int h, int scale, int *out)
{
    // scale (4/16/64/256) ブロックごとの地表のバイオーム。x0, z0 はブロック座標。
    Range r = { scale, floordiv_i(x0, scale), floordiv_i(z0, scale), w, h, 319 >> 2, 1 };
    int *cache = allocCache(&g, r);
    if (!cache)
        return -1;
    int err = genBiomes(&g, cache, r);
    if (!err)
        memcpy(out, cache, sizeof(int) * (size_t)w * (size_t)h);
    free(cache);
    return err;
}

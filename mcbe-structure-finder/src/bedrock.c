// 統合版の構造物配置。定数と規則は、実機で確認された座標と照合されている
// オープンソースの統合版ファインダー (SeedFinder: github.com/zebedelu/SeedFinder,
// MCBE-seedcracker: github.com/Alist2930/MCBE-seedcracker,
// mc-mapper: github.com/BrendonKing32/mc-mapper) に揃えている。
#include "bedrock.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "end.h"
#include "finders.h"
#include "generator.h"
#include "mt.h"

static Generator g;  // 地上
static int g_mc;
static int g_old_nether_fossil;  // 1.21.90 より前のネザーの化石の規則
static uint64_t g_seed;
static uint32_t g_seed32;  // MT を使う配置は seed の下位 32 ビットだけで決まる
static uint32_t g_chunk_a, g_chunk_b;  // チャンク単位の乱数に使う係数

// --- 統合版のネザーのバイオーム ---
// Java 版 (cubiomes) とは別物。MT で作ったパーリンノイズ 2 つ (温度・湿度) を
// 高さによらず 2 次元で取り、5 つのバイオームの基準点のうち一番近いものになる。
// 実機のワールドデータのバイオームと 4×4 ブロックの区画の中心で全部一致する。
typedef struct { double a, b, c, amp, lac; uint8_t d[512]; } NPerlin;
typedef struct { NPerlin oct[4]; } NNoise;  // 2 オクターブ × 2 組
static NNoise g_temp, g_humid;

static void nperlin_init(NPerlin *p, MT *mt)
{
    p->a = (double)(float)(mt_next_full(mt) * 2.3283064365386963e-10) * 256.0;
    p->b = (double)(float)(mt_next_full(mt) * 2.3283064365386963e-10) * 256.0;
    p->c = (double)(float)(mt_next_full(mt) * 2.3283064365386963e-10) * 256.0;
    for (int i = 0; i < 256; i++)
        p->d[i] = (uint8_t)i;
    for (int i = 0; i < 256; i++) {
        int j = (int)(mt_next_full(mt) % (uint32_t)(256 - i)) + i;
        uint8_t t = p->d[i];
        p->d[i] = p->d[j];
        p->d[j] = t;
    }
    memcpy(p->d + 256, p->d, 256);
}

static double ngrad(int h, double x, double y, double z)
{
    switch (h & 15) {
    case 0: return x + y;   case 1: return -x + y;  case 2: return x - y;   case 3: return -x - y;
    case 4: return x + z;   case 5: return -x + z;  case 6: return x - z;   case 7: return -x - z;
    case 8: return y + z;   case 9: return -y + z;  case 10: return y - z;  case 11: return -y - z;
    case 12: return x + y;  case 13: return -y + z; case 14: return -x + y; default: return -y - z;
    }
}

static double nlerp(double t, double a, double b) { return a + t * (b - a); }
static double nfade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

static double nperlin_sample(const NPerlin *p, double x, double y, double z)
{
    x += p->a;
    y += p->b;
    z += p->c;
    int ix = (int)floor(x), iy = (int)floor(y), iz = (int)floor(z);
    double fx = x - ix, fy = y - iy, fz = z - iz;
    const uint8_t *d = p->d;
    int X = ix & 255, Y = iy & 255, Z = iz & 255;
    int i = d[X], j = d[X + 1];
    int k = d[(i + Y) & 255], l = d[(i + Y + 1) & 255];
    int m = d[(j + Y) & 255], n = d[(j + Y + 1) & 255];
    double u = nfade(fx), v = nfade(fy), w = nfade(fz);
    double e0 = nlerp(u, ngrad(d[(k + Z) & 255], fx, fy, fz), ngrad(d[(m + Z) & 255], fx - 1, fy, fz));
    double e1 = nlerp(u, ngrad(d[(l + Z) & 255], fx, fy - 1, fz), ngrad(d[(n + Z) & 255], fx - 1, fy - 1, fz));
    double e2 = nlerp(u, ngrad(d[(k + Z + 1) & 255], fx, fy, fz - 1), ngrad(d[(m + Z + 1) & 255], fx - 1, fy, fz - 1));
    double e3 = nlerp(u, ngrad(d[(l + Z + 1) & 255], fx, fy - 1, fz - 1), ngrad(d[(n + Z + 1) & 255], fx - 1, fy - 1, fz - 1));
    return nlerp(w, nlerp(v, e0, e1), nlerp(v, e2, e3));
}

static double nwrap(double x) { return x - floor(x / 33554432.0 + 0.5) * 33554432.0; }

static void nnoise_init(NNoise *n, uint32_t seed)
{
    MT mt;
    mt_seed_full(&mt, seed);
    for (int i = 0; i < 4; i++) {
        nperlin_init(&n->oct[i], &mt);
        n->oct[i].lac = i % 2 ? 1.0 / 128 : 1.0 / 64;
        n->oct[i].amp = i % 2 ? 2.0 / 3 : 1.0 / 3;
    }
}

static double nnoise_sample(const NNoise *n, double x, double z)
{
    double v[2] = { 0, 0 };
    for (int i = 0; i < 4; i++) {
        const NPerlin *p = &n->oct[i];
        if (i == 2) {  // 2 組目は少しずらした縮尺で取る
            x *= 337.0 / 331.0;
            z *= 337.0 / 331.0;
        }
        v[i / 2] += p->amp * nperlin_sample(p, nwrap(x * p->lac), 0.0, nwrap(z * p->lac));
    }
    return (v[0] + v[1]) * 1.25;
}

// (qx, qz) は 4 ブロック単位の座標。戻り値は cubiomes のバイオーム ID。
static int nether_biome(int qx, int qz)
{
    static const float P[5][3] = {
        { 0, 0, 0 }, { 0, -0.5f, 0 }, { 0.4f, 0, 0 },
        { 0, 0.5f, 0.375f * 0.375f }, { -0.5f, 0, 0.175f * 0.175f },
    };
    static const int ID[5] = { nether_wastes, soul_sand_valley, crimson_forest, warped_forest, basalt_deltas };
    float t = (float)nnoise_sample(&g_temp, qx, qz);
    float h = (float)nnoise_sample(&g_humid, qx, qz);
    int best = 0;
    float dmin = INFINITY;
    for (int i = 0; i < 5; i++) {
        float dt = P[i][0] - t, dh = P[i][1] - h, d = dt * dt + dh * dh + P[i][2];
        if (d < dmin) {
            dmin = d;
            best = i;
        }
    }
    return ID[best];
}

// --- 構造物ごとの設定 ---
enum { SPREAD_UNIFORM, SPREAD_TRIANGULAR, SPREAD_JAVA, SPREAD_CHUNK, SPREAD_SPECIAL, SPREAD_END };

typedef struct {
    int salt;
    int spacing;   // 領域の一辺 (チャンク)
    int range;     // 領域内で位置が動く幅 = spacing - separation (チャンク)
    int spread;
    int cubiomes;  // cubiomes の StructureType (バイオーム判定・Java 配置に使う)
    int min_mc;
    int dim;       // enum BEDim (省略は地上)
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
    // ネザー要塞と砦の遺跡は同じ候補の位置を取り合う (nether_complex)
    [BE_FORTRESS]       = { 30084232, 30, 26, SPREAD_UNIFORM, Fortress, MC_1_18, BE_NETHER },
    [BE_BASTION]        = { 30084232, 30, 26, SPREAD_UNIFORM, Bastion,  MC_1_18, BE_NETHER },
    [BE_SLIME_CHUNK]    = { 0, 1, 1, SPREAD_CHUNK, 0, MC_1_18 },
    [BE_GEODE]          = { 0, 1, 1, SPREAD_CHUNK, 0, MC_1_18 },
    [BE_FOSSIL]         = { 0, 1, 1, SPREAD_CHUNK, 0, MC_1_18 },
    [BE_NETHER_FOSSIL]  = { 0, 1, 1, SPREAD_CHUNK, 0, MC_1_18, BE_NETHER },
    // エンドシティとエンドシップは end.c で同じ計算をする
    [BE_END_CITY]       = { 10387313, 20, 9, SPREAD_END, End_City, MC_1_18, BE_END },
    [BE_END_SHIP]       = { 10387313, 20, 9, SPREAD_END, End_City, MC_1_18, BE_END },
    [BE_END_GATEWAY]    = { 0, 0, 0, SPREAD_SPECIAL, End_Gateway, MC_1_18, BE_END },
    [BE_END_GATEWAY_OUTER] = { 0, 1, 1, SPREAD_CHUNK, End_Gateway, MC_1_18, BE_END },
};

int be_setup(uint64_t seed, int mc, int bv)
{
    if (mc < MC_1_18 || mc > MC_NEWEST)
        return -1;
    g_mc = mc;
    g_old_nether_fossil = bv ? bv < 12190 : mc <= MC_1_21_WD;
    g_seed = seed;
    g_seed32 = (uint32_t)seed;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);
    nnoise_init(&g_temp, g_seed32);
    nnoise_init(&g_humid, g_seed32 + 1);
    end_setup(g_seed32);
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

int be_kind_dim(int kind)
{
    return kind >= 0 && kind < BE_KIND_NUM ? CONFIGS[kind].dim : -1;
}

// --- 位置の計算 ---

static int floordiv_i(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static uint32_t region_seed(const Config *c, int rx, int rz)
{
    return (uint32_t)((uint64_t)(int64_t)rx * 341873128712ull
                    + (uint64_t)(int64_t)rz * 132897987541ull
                    + g_seed + (uint64_t)(int64_t)c->salt);
}

// 領域 (rx, rz) の生成候補のチャンク座標。
static void region_chunk(const Config *c, int rx, int rz, int *cx, int *cz)
{
    uint32_t rs = region_seed(c, rx, rz);
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

// ネザー要塞か砦の遺跡か。候補の位置 (X, Z) の次の乱数で 6 回に 2 回が要塞になる。
// 砦の遺跡は玄武岩の三角州にはできず、Java 版と違って要塞にも変わらない (何もできない)。
// 砦の遺跡ならさらに向き・形 (*type) を引く。戻り値は BE_FORTRESS / BE_BASTION / -1。
static int nether_complex(int cx, int cz, int biome, int *type)
{
    const Config *c = &CONFIGS[BE_FORTRESS];
    MT mt;
    mt_seed(&mt, region_seed(c, floordiv_i(cx, c->spacing), floordiv_i(cz, c->spacing)), 5);
    mt_next(&mt);
    mt_next(&mt);
    if (mt_int(&mt, 6) < 2)
        return BE_FORTRESS;
    mt_next(&mt);  // 向き
    *type = mt_int(&mt, 4);
    return biome == basalt_deltas ? -1 : BE_BASTION;
}

static int viable(int kind, int cx, int cz, int *biome, int *info)
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
    case BE_FORTRESS:
    case BE_BASTION:
        *biome = nether_biome(cx * 4 + 2, cz * 4 + 2);
        return nether_complex(cx, cz, *biome, info) == kind;
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

typedef struct { BEHit *out; int max, n; int x0, z0, x1, z1; } Sink;

static int sink_add_info(Sink *s, int x, int z, int biome, int info)
{
    if (x < s->x0 || x > s->x1 || z < s->z0 || z > s->z1)
        return 1;
    if (s->n >= s->max)
        return 0;
    s->out[s->n] = (BEHit){ x, z, biome, info };
    s->n++;
    return 1;
}

static int sink_add(Sink *s, int x, int z, int biome) { return sink_add_info(s, x, z, biome, 0); }

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
            int cx, cz, biome, info = 0;
            region_chunk(c, rx, rz, &cx, &cz);
            int x = cx * 16 + 8, z = cz * 16 + 8;
            if (x < s->x0 || x > s->x1 || z < s->z0 || z > s->z1)
                continue;
            if (!viable(kind, cx, cz, &biome, &info))
                continue;
            if (!sink_add_info(s, x, z, biome, info))
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

// 装飾 (feature rule) の乱数の種。salt は規則の名前を FNV-1 (64 ビット) にした下位 32 ビット。
static uint32_t deco_seed(int cx, int cz, uint32_t salt)
{
    uint32_t ds = ((uint32_t)cx * (g_chunk_a | 1u) + (uint32_t)cz * (g_chunk_b | 1u)) ^ g_seed32;
    return ((ds >> 2) + (ds << 6) + salt - 1640531527u) ^ ds;
}

// 砂漠の井戸: 装飾用の乱数でチャンクごとに 1/500。位置はチャンク内の (dx, dz)。
static int desert_well_chunk(int cx, int cz, int *x, int *z)
{
    MT mt;
    mt_seed(&mt, deco_seed(cx, cz, 3134482480u), 3);
    if (mt_int(&mt, 500) != 0)
        return 0;
    int dz = mt_int(&mt, 16);
    int dx = mt_int(&mt, 16);
    *x = cx * 16 + dx;
    *z = cz * 16 + dz;
    return 1;
}

// スライムチャンク: チャンク座標だけで決まり、ワールドのシード値は使わない (10 個に 1 個)
static int slime_chunk(int cx, int cz)
{
    MT mt;
    mt_seed(&mt, (uint32_t)cx * 0x1f1f1f1fu ^ (uint32_t)cz, 1);
    return mt_next(&mt) % 10 == 0;
}

// ジオード: 24 チャンクに 1 つ。バイオームは問わない。中心の高さまで決まる。
static int geode_chunk(int cx, int cz, int *y)
{
    MT mt;
    mt_seed(&mt, deco_seed(cx, cz, 1974035328u), 2);
    if (mt_int(&mt, 24) != 0)
        return 0;
    *y = -58 + mt_int(&mt, 88) + 5;
    return 1;
}

// 化石: 石炭の化石と深層岩の化石 (ダイヤモンド鉱石入り) が、それぞれ 64 チャンクに 1 つ。
// 砂漠か沼地 (深層岩の化石は 1.21.60 からマングローブの沼地も) だけ。
// バイオームはチャンクの角の地表で見る。
static int fossil_hits(int cx, int cz, Sink *s)
{
    static const uint32_t SALT[2] = { 2116138073u, 877307113u };
    int biome = -1;
    for (int t = 0; t < 2; t++) {
        MT mt;
        mt_seed(&mt, deco_seed(cx, cz, SALT[t]), 2);
        if (mt_int(&mt, 64) != 0)
            continue;
        int ry = t ? -64 + mt_int(&mt, 56) : mt_int(&mt, 320);
        if (biome < 0)
            biome = getBiomeAt(&g, 4, cx * 4, 319 >> 2, cz * 4);
        if (biome != desert && biome != swamp && !(t && biome == mangrove_swamp && g_mc > MC_1_21_WD))
            continue;
        if (!sink_add_info(s, cx * 16 + 8, cz * 16 + 8, biome, t | (ry + 64) << 1))
            return 0;
    }
    return 1;
}

// ネザーの化石: ソウルサンドの谷だけ。1.21.90 から確率と高さの規則が変わった。
// (x, z) は化石の骨の角のどれか。化石は y から下に落ちて地面に乗る。
static int nether_fossil_chunk(int cx, int cz, int *x, int *z, int *y)
{
    MT mt;
    mt_seed(&mt, deco_seed(cx, cz, 4277440457u), 4);
    if (g_old_nether_fossil ? mt_int(&mt, 6) >= 2 : mt_int(&mt, 2) >= 1)
        return 0;
    *x = cx * 16 + mt_int(&mt, 16);
    *z = cz * 16 + mt_int(&mt, 16);
    *y = g_old_nether_fossil ? mt_int(&mt, 124) : 32 + mt_int(&mt, 93);
    return nether_biome(*x >> 2, *z >> 2) == soul_sand_valley;
}

// チャンク (cx, cz) にできるものを s に書く。s が一杯になったら 0。
static int chunk_hits(int kind, int cx, int cz, Sink *s)
{
    int x, y, z;
    switch (kind) {
    case BE_MINESHAFT:
        return !mineshaft_chunk(cx, cz) || sink_add(s, cx * 16 + 8, cz * 16 + 8, surface_biome(cx, cz));
    case BE_SLIME_CHUNK:
        return !slime_chunk(cx, cz) || sink_add(s, cx * 16 + 8, cz * 16 + 8, -1);
    case BE_DESERT_WELL:
        if (!desert_well_chunk(cx, cz, &x, &z) || getBiomeAt(&g, 4, x >> 2, 319 >> 2, z >> 2) != desert)
            return 1;
        return sink_add(s, x, z, desert);
    case BE_GEODE:
        return !geode_chunk(cx, cz, &y) || sink_add_info(s, cx * 16 + 5, cz * 16 + 5, -1, y);
    case BE_FOSSIL:
        return fossil_hits(cx, cz, s);
    case BE_NETHER_FOSSIL:
        return !nether_fossil_chunk(cx, cz, &x, &z, &y) || sink_add_info(s, x, z, soul_sand_valley, y);
    case BE_END_GATEWAY_OUTER:
        return !end_gateway(cx, cz, &x, &y, &z) || sink_add_info(s, x, z, -1, y);
    }
    return 1;
}

static int scan_chunks(int kind, Sink *s, int part, int nparts)
{
    // 外側の島のエンドゲートウェイは隣のチャンク (+x, +z) にはみ出すことがある
    int lo = kind == BE_END_GATEWAY_OUTER;
    int cx0 = (s->x0 >> 4) - lo, cx1 = s->x1 >> 4;
    int cza, czb;
    band((s->z0 >> 4) - lo, s->z1 >> 4, part, nparts, &cza, &czb);
    // 幅 32 チャンクの縦の帯ごとに進むと、エンドの地形を覚えておく表 (end.c) に収まる
    for (int sx = cx0; sx <= cx1; sx += 32)
        for (int cz = cza; cz <= czb; cz++)
            for (int cx = sx; cx <= cx1 && cx < sx + 32; cx++)
                if (!chunk_hits(kind, cx, cz, s))
                    return 0;
    return 1;
}

// エンドシティ (20 チャンク四方の領域ごと) と、そのエンドシップ
static int scan_end_cities(int kind, Sink *s, int part, int nparts)
{
    int blocks = CONFIGS[kind].spacing * 16;
    int rx0 = floordiv_i(s->x0, blocks) - 1, rx1 = floordiv_i(s->x1, blocks) + 1;
    int rz0 = floordiv_i(s->z0, blocks) - 1, rz1 = floordiv_i(s->z1, blocks) + 1;
    int za, zb;
    band(rz0, rz1, part, nparts, &za, &zb);
    for (int rz = za; rz <= zb; rz++)
        for (int rx = rx0; rx <= rx1; rx++) {
            EndCity c;
            if (!end_city(rx, rz, &c))
                continue;
            if (kind == BE_END_CITY ? !sink_add_info(s, c.x, c.z, -1, c.y)
                                    : c.ship && !sink_add_info(s, c.ex, c.ez, -1, c.ey))
                return 0;
        }
    return 1;
}

static int scan_ring_gateways(Sink *s)
{
    for (int kill = 1; kill <= 20; kill++) {
        int x, z;
        end_ring_gateway(kill, &x, &z);
        if (!sink_add_info(s, x, z, -1, kill))
            return 0;
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
        if (part == 0 && kind == BE_STRONGHOLD)
            scan_strongholds(&s);
        else if (part == 0)
            scan_ring_gateways(&s);
        break;
    case SPREAD_END:
        scan_end_cities(kind, &s, part, nparts);
        break;
    default:
        scan_regions(kind, &s, part, nparts);
        break;
    }
    return s.n;
}

int be_biome_grid(int dim, int x0, int z0, int w, int h, int scale, int *out)
{
    // scale (4/16/64/256) ブロックごとのバイオーム。x0, z0 はブロック座標。
    // 地上は y=319 で取って地表のバイオームにする。
    if (dim == BE_NETHER) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                out[j * w + i] = nether_biome(floordiv_i(x0 + i * scale + scale / 2, 4),
                                              floordiv_i(z0 + j * scale + scale / 2, 4));
        return 0;
    }
    if (dim == BE_END) {
        // エンドは 1 つのバイオームだけなので、島の高さを Java 版のエンドのバイオームの
        // 区切りで塗り分けて島の形を見せる
        int lx = INT_MIN, lz = INT_MIN, id = 0;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int cx = floordiv_i(x0 + i * scale + scale / 2, 16), cz = floordiv_i(z0 + j * scale + scale / 2, 16);
                if (cx != lx || cz != lz) {
                    float v = end_island(cx, cz);
                    id = v > 40 ? end_highlands : v >= 0 ? end_midlands : v < -20 ? small_end_islands : end_barrens;
                    lx = cx;
                    lz = cz;
                }
                out[j * w + i] = id;
            }
        return 0;
    }
    const Generator *gen = &g;
    Range r = { scale, floordiv_i(x0, scale), floordiv_i(z0, scale), w, h, 319 >> 2, 1 };
    int *cache = allocCache(gen, r);
    if (!cache)
        return -1;
    int err = genBiomes(gen, cache, r);
    if (!err)
        memcpy(out, cache, sizeof(int) * (size_t)w * (size_t)h);
    free(cache);
    return err;
}

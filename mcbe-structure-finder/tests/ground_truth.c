// 既知の座標との照合テスト。./build.sh test で実行する。
//  - シード 6666 の村: 実機で確認された 8 か所と、川に落ちて村ができない 3 か所
//    (SeedFinder の server/tests/test_scan_village_groundtruth.py より)
//  - シード 4294972605: Chunkbase 26.0 の村・神殿など
//    (SeedFinder の server/tests/fixtures_crack64.json より)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "bedrock.h"
#include "biomes.h"

static int failures;

static BEHit hits[1 << 16];

static int scan(uint64_t seed, int kind, int r)
{
    be_setup(seed, MC_NEWEST, 0);
    return be_scan(kind, -r, -r, r, r, 0, 1, hits, 1 << 16);
}

static int has(int n, int x, int z)
{
    for (int i = 0; i < n; i++)
        if (hits[i].x == x && hits[i].z == z)
            return 1;
    return 0;
}

static void expect(int cond, const char *what, int x, int z)
{
    printf("%s %s (%d, %d)\n", cond ? "ok  " : "FAIL", what, x, z);
    if (!cond)
        failures++;
}

int main(void)
{
    static const int real6666[][2] = {
        {-168, 56}, {744, -344}, {-312, -1000}, {744, -872},
        {248, 1144}, {-312, 1208}, {1256, -328}, {56, -1480},
    };
    static const int river6666[][2] = { {264, -296}, {-776, -872}, {776, -1368} };
    int n = scan(6666, BE_VILLAGE, 1700);
    for (int i = 0; i < 8; i++)
        expect(has(n, real6666[i][0], real6666[i][1]), "6666 village", real6666[i][0], real6666[i][1]);
    for (int i = 0; i < 3; i++)
        expect(!has(n, river6666[i][0], river6666[i][1]), "6666 no village (river)", river6666[i][0], river6666[i][1]);

    static const struct { int kind, x, z; const char *what; } cb[] = {
        { BE_VILLAGE, 296, 232, "4294972605 snowy village" },
        { BE_VILLAGE, 760, 216, "4294972605 taiga village" },
        { BE_VILLAGE, 1368, -392, "4294972605 plains village" },
        { BE_VILLAGE, 1256, -5816, "4294972605 savanna village" },
        { BE_VILLAGE, -9128, -13352, "4294972605 desert village" },
        { BE_DESERT_PYRAMID, -3048, -296, "4294972605 desert pyramid" },
        { BE_IGLOO, -280, 104, "4294972605 igloo" },
        { BE_JUNGLE_TEMPLE, 2584, -1288, "4294972605 jungle temple" },
        { BE_SWAMP_HUT, 2136, -280, "4294972605 swamp hut" },
    };
    for (size_t i = 0; i < sizeof cb / sizeof *cb; i++) {
        n = scan(4294972605ull, cb[i].kind, 14000);
        expect(has(n, cb[i].x, cb[i].z), cb[i].what, cb[i].x, cb[i].z);
    }

    // 試練の間は Java 版と同じ配置。座標は開始ピースの中心で、Chunkbase と一致する。
    static const int tc[][2] = { {-505, -281}, {263, -311}, {-359, 199}, {231, 169} };
    n = scan(4294972605ull, BE_TRIAL_CHAMBERS, 2000);
    for (int i = 0; i < 4; i++) {
        int found = 0;
        for (int j = 0; j < n; j++)
            if (hits[j].x == tc[i][0] && hits[j].z == tc[i][1])
                found = 1;
        expect(found, "4294972605 trial chambers", tc[i][0], tc[i][1]);
    }

    // 下位 32 ビットが同じシードは MT 配置の構造物が同じ位置になる
    int a = scan(5309, BE_RUINED_PORTAL, 5000);
    int b = scan(5309 + (1ull << 32), BE_RUINED_PORTAL, 5000);
    expect(a == b && a > 0, "ruined portals depend only on the low 32 bits", a, b);

    // ネザー要塞: BDS 1.26.52 の /locate structure fortress (シード 20261003) が返した
    // 開始チャンクの角 (acacia-rs/sapling の structure/nether.rs より)。±100 万まで届く。
    static const int loc[][2] = {
        {-8160, -8080}, {-5200, 240}, {240, 32}, {0, 3840}, {-1920, -3808}, {6480, 6432},
        {123648, -98656}, {-300208, 250000}, {999744, 999968},
    };
    for (size_t i = 0; i < sizeof loc / sizeof *loc; i++) {
        int x = loc[i][0] + 8, z = loc[i][1] + 8;
        be_setup(20261003, MC_NEWEST, 0);
        n = be_scan(BE_FORTRESS, x - 1, z - 1, x + 1, z + 1, 0, 1, hits, 1 << 16);
        expect(has(n, x, z), "20261003 fortress (BDS /locate)", x, z);
    }

    // 実機のワールドデータで要塞の開始ピースの角 (チャンクの角 + 2) が分かっているもの
    static const struct { uint64_t seed; int x, z; } fort[] = {
        { 46942827301ull, 530, -462 }, { 46942827301ull, -382, -478 },
        { 270819980, -398, -830 }, { 4174604509ull, 66, -878 },
    };
    for (size_t i = 0; i < sizeof fort / sizeof *fort; i++) {
        n = scan(fort[i].seed, BE_FORTRESS, 1000);
        expect(has(n, fort[i].x + 6, fort[i].z + 6), "fortress (world save)", fort[i].x, fort[i].z);
    }

    // 砦の遺跡: ワールドデータの黒石レンガの塊の中心と、チェストの戦利品で分かる形
    // (0 橋, 1 宝物庫, 2 ホグリン小屋)。開始チャンクの中央から 48 ブロック以内に出ること。
    static const struct { uint64_t seed; int x, z, type; } bas[] = {
        { (uint64_t)-7030675145895473660ll, -209, 180, 0 }, { 2514425250209177457ull, 379, 143, 0 },
        { 46942827301ull, 973, -735, 1 }, { 270819980, 13, -638, 1 }, { 2514425250209177457ull, 750, -3, 1 },
        { 46942827301ull, 1209, -184, 2 }, { 2514425250209177457ull, 869, -121, 2 },
        { (uint64_t)-6555947771831759025ll, 72, -76, 2 },
    };
    for (size_t i = 0; i < sizeof bas / sizeof *bas; i++) {
        n = scan(bas[i].seed, BE_BASTION, 2000);
        int found = 0;
        for (int j = 0; j < n; j++)
            if (hypot(hits[j].x - bas[i].x, hits[j].z - bas[i].z) <= 48 && hits[j].info == bas[i].type)
                found = 1;
        expect(found, "bastion and its type (world save)", bas[i].x, bas[i].z);
    }

    // 砦の遺跡になるはずの候補が玄武岩の三角州だった領域は、ワールドデータでも空 (要塞にもならない)
    static const struct { uint64_t seed; int x, z; } empty[] = {
        { 46942827301ull, 71 * 16 + 8, 8 * 16 + 8 }, { 757067671151835873ull, -6 * 16 + 8, -9 * 16 + 8 },
    };
    for (size_t i = 0; i < sizeof empty / sizeof *empty; i++) {
        be_setup(empty[i].seed, MC_NEWEST, 0);
        int x = empty[i].x, z = empty[i].z;
        int nb = be_scan(BE_BASTION, x - 1, z - 1, x + 1, z + 1, 0, 1, hits, 1 << 16);
        int nf = be_scan(BE_FORTRESS, x - 1, z - 1, x + 1, z + 1, 0, 1, hits, 1 << 16);
        expect(nb == 0 && nf == 0, "empty basalt region (world save)", x, z);
    }

    // ネザーのバイオーム: ワールドデータの 4×4 区画の中心のバイオーム
    static const struct { uint64_t seed; int x, z, biome; } nb[] = {
        { 757067671151835873ull, 754, -2910, nether_wastes }, { 757067671151835873ull, 794, -2726, soul_sand_valley },
        { 757067671151835873ull, 754, -558, crimson_forest }, { 757067671151835873ull, 922, -2166, warped_forest },
        { 757067671151835873ull, 754, -2414, basalt_deltas }, { 270819980, -542, -830, soul_sand_valley },
        { 270819980, -558, 34, crimson_forest }, { 270819980, 1330, -1886, warped_forest },
        { 270819980, -550, -758, basalt_deltas },
    };
    for (size_t i = 0; i < sizeof nb / sizeof *nb; i++) {
        int id = -1;
        be_setup(nb[i].seed, MC_NEWEST, 0);
        be_biome_grid(BE_NETHER, nb[i].x - 2, nb[i].z - 2, 1, 1, 4, &id);
        expect(id == nb[i].biome, "nether biome (world save)", nb[i].x, nb[i].z);
    }

    // スライムチャンク (rbedrock と slime-finder-pe のテストより)。シード値によらない。
    static const struct { int cx, cz, slime; } sl[] = {
        { 3, 1, 1 }, { -61, -59, 1 }, { -1, 0, 1 }, { 109, 3, 1 },
        { 0, 0, 0 }, { -61, -60, 0 }, { 110, 3, 0 },
    };
    for (size_t i = 0; i < sizeof sl / sizeof *sl; i++) {
        int x = sl[i].cx * 16 + 8, z = sl[i].cz * 16 + 8;
        be_setup(12345, MC_NEWEST, 0);
        n = be_scan(BE_SLIME_CHUNK, x, z, x, z, 0, 1, hits, 1 << 16);
        expect(n == sl[i].slime, sl[i].slime ? "slime chunk" : "not a slime chunk", sl[i].cx, sl[i].cz);
    }

    // 以下、ワールドデータは実機のワールド (GitHub で公開されているもの) から読んだ座標。

    // ジオード: 紫水晶の塊の箱の中心。予想の中心から各方向 3 ブロック以内 (高さも)。
    static const struct { uint64_t seed; int x0, x1, y0, y1, z0, z1; } geo[] = {
        { (uint64_t)-7030675145895473660ll, 34, 42, -36, -28, -47, -39 },
        { (uint64_t)-1179604948955851051ll, 17, 24, -29, -21, 114, 122 },
        { (uint64_t)-1864014236ll, -31, -23, -29, -21, 1, 10 },
        { 3899951799ull, 241, 249, -55, -48, 273, 281 },
    };
    for (size_t i = 0; i < sizeof geo / sizeof *geo; i++) {
        int x = (geo[i].x0 + geo[i].x1) / 2, y = (geo[i].y0 + geo[i].y1) / 2, z = (geo[i].z0 + geo[i].z1) / 2;
        be_setup(geo[i].seed, MC_NEWEST, 0);
        n = be_scan(BE_GEODE, x - 3, z - 3, x + 3, z + 3, 0, 1, hits, 1 << 16);
        expect(n == 1 && abs(hits[0].info - y) <= 3, "geode (world save)", x, z);
    }

    // 化石: 骨の箱があるチャンクに出ること。最後の 2 つは深層岩の化石で、
    // 1 つ目はマングローブの沼地 (1.21.60 から) にある。
    static const struct { uint64_t seed; int x, z, mc, type; } fos[] = {
        { (uint64_t)-7030675145895473660ll, -1051, 4790, MC_NEWEST, 1 },
        { 2514425250209177457ull, -759, -584, MC_NEWEST, 0 },
        { 20260509, -105, -1051, MC_NEWEST, 0 },
        { 270819980, 3107, 181, MC_1_18, 0 },
        { 20260509, -964, 3986, MC_NEWEST, 1 },
        { 7752123454623461ull, 182, -10, MC_NEWEST, 1 },
    };
    for (size_t i = 0; i < sizeof fos / sizeof *fos; i++) {
        int x = (fos[i].x >> 4) * 16 + 8, z = (fos[i].z >> 4) * 16 + 8;
        be_setup(fos[i].seed, fos[i].mc, 0);
        n = be_scan(BE_FOSSIL, x, z, x, z, 0, 1, hits, 1 << 16);
        int found = 0;
        for (int j = 0; j < n; j++)
            found |= (hits[j].info & 1) == fos[i].type;
        expect(found, "fossil (world save)", fos[i].x, fos[i].z);
    }
    // マングローブの沼地の深層岩の化石は 1.21.50 以前にはできない
    be_setup(20260509, MC_1_21_WD, 0);
    n = be_scan(BE_FOSSIL, -968, 3976, -968, 3976, 0, 1, hits, 1 << 16);
    expect(n == 0, "no mangrove deepslate fossil before 1.21.60", -968, 3976);

    // ネザーの化石: 規則の点は骨の箱の角。1.21.90 から規則が変わった
    static const struct { uint64_t seed; int x0, x1, z0, z1, bv; } nfo[] = {
        { 8160919894141867865ull, 230, 234, 17, 17, 265000 },
        { 6748101232908240875ull, -44, -42, -46, -42, 265000 },
        { 46942827301ull, 27, 30, -229, -227, 12100 },
        { 2514425250209177457ull, -141, -136, 130, 136, 12100 },
    };
    for (size_t i = 0; i < sizeof nfo / sizeof *nfo; i++) {
        be_setup(nfo[i].seed, MC_NEWEST, nfo[i].bv);
        n = be_scan(BE_NETHER_FOSSIL, nfo[i].x0, nfo[i].z0, nfo[i].x1, nfo[i].z1, 0, 1, hits, 1 << 16);
        expect(n == 1, "nether fossil (world save)", nfo[i].x0, nfo[i].z0);
    }

    // エンドシティ: BDS 1.26.52 の /locate structure end_city (シード 20261003)
    static const int city[][2] = { {-888, -888}, {1352, 88}, {-1192, 56}, {-216, 1016}, {-264, -1224}, {664, 1640} };
    n = scan(20261003, BE_END_CITY, 2000);
    for (int i = 0; i < 6; i++)
        expect(has(n, city[i][0], city[i][1]), "20261003 end city (BDS /locate)", city[i][0], city[i][1]);

    // エンドシップのエリトラの額縁 (ワールドデータ。エリトラを取ったあとの空の額縁も含む)
    static const struct { uint64_t seed; int x, y, z; } ely[] = {
        { (uint64_t)-3522037124396410601ll, 665, 136, 1805 }, { (uint64_t)-3522037124396410601ll, 1411, 146, 985 },
        { 2514425250209177457ull, -1266, 102, -265 }, { 2514425250209177457ull, -2408, 138, -1222 },
        { (uint64_t)-7325351736649913706ll, -1012, 129, 2906 }, { 757067671151835873ull, 2487, 110, -942 },
    };
    for (size_t i = 0; i < sizeof ely / sizeof *ely; i++) {
        be_setup(ely[i].seed, MC_NEWEST, 0);
        n = be_scan(BE_END_SHIP, ely[i].x, ely[i].z, ely[i].x, ely[i].z, 0, 1, hits, 1 << 16);
        expect(n == 1 && hits[0].info == ely[i].y, "end ship elytra frame (world save)", ely[i].x, ely[i].z);
    }

    // 中央の島のエンドゲートウェイ: ドラゴンを 1 回倒したワールドのゲートウェイ
    static const struct { uint64_t seed; int x, z; } ring[] = {
        { 2514425250209177457ull, 77, 56 }, { 46942827301ull, -91, -29 }, { (uint64_t)-7325351736649913706ll, -29, 91 },
        { (uint64_t)-3522037124396410601ll, 29, 91 }, { (uint64_t)-1622343805870408444ll, 96, 0 }, { 4174604509ull, -77, 56 },
    };
    for (size_t i = 0; i < sizeof ring / sizeof *ring; i++) {
        n = scan(ring[i].seed, BE_END_GATEWAY, 200);
        int found = 0;
        for (int j = 0; j < n; j++)
            found |= hits[j].x == ring[i].x && hits[j].z == ring[i].z && hits[j].info == 1;
        expect(n == 20 && found, "first end gateway (world save)", ring[i].x, ring[i].z);
    }
    // 5 回倒したワールド (下位 32 ビット 2618525652): 出た順番どおり
    static const int order[5][2] = { {56, -77}, {29, 91}, {-29, 91}, {-29, -91}, {77, 56} };
    n = scan(2618525652u, BE_END_GATEWAY, 200);
    for (int k = 0; k < 5; k++) {
        int found = 0;
        for (int j = 0; j < n; j++)
            found |= hits[j].x == order[k][0] && hits[j].z == order[k][1] && hits[j].info == k + 1;
        expect(found, "end gateway kill order (world save)", order[k][0], order[k][1]);
    }

    // 外側の島のエンドゲートウェイ: BDS 1.26.52 と、ワールドデータ
    static const struct { uint64_t seed; int x, y, z; } gw[] = {
        { 20261003, 1188, 65, -97 }, { 20261018, 1953, 66, 181 },
        { 46942827301ull, -1055, 65, -871 }, { 46942827301ull, -927, 66, -2151 },
        { (uint64_t)-3522037124396410601ll, 836, 73, 941 }, { 2514425250209177457ull, 2549, 61, 2540 },
    };
    for (size_t i = 0; i < sizeof gw / sizeof *gw; i++) {
        be_setup(gw[i].seed, MC_NEWEST, 0);
        n = be_scan(BE_END_GATEWAY_OUTER, gw[i].x, gw[i].z, gw[i].x, gw[i].z, 0, 1, hits, 1 << 16);
        expect(n == 1 && hits[0].info == gw[i].y, "outer end gateway", gw[i].x, gw[i].z);
    }

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}

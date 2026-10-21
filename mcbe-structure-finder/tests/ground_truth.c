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
    be_setup(seed, MC_NEWEST);
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
        be_setup(20261003, MC_NEWEST);
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
        be_setup(empty[i].seed, MC_NEWEST);
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
        be_setup(nb[i].seed, MC_NEWEST);
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
        be_setup(12345, MC_NEWEST);
        n = be_scan(BE_SLIME_CHUNK, x, z, x, z, 0, 1, hits, 1 << 16);
        expect(n == sl[i].slime, sl[i].slime ? "slime chunk" : "not a slime chunk", sl[i].cx, sl[i].cz);
    }

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}

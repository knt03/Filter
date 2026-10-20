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

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}

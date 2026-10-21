// コマンドライン版: シード値を渡すと範囲内の構造物を中心から近い順に CSV で出力する。
//   ./mcbe-structures <シード値> [--range 10000] [--center X Z] [--mc 1.21] [--kinds 0,1,6]
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bedrock.h"
#include "biomes.h"
#include "util.h"

static const char *KIND_NAMES[BE_KIND_NUM] = {
    "村", "砂漠のピラミッド", "ジャングルの寺院", "ウィッチの小屋", "イグルー",
    "海底神殿", "森の洋館", "ピリジャーの前哨基地", "難破船", "海底遺跡",
    "埋もれた宝", "荒廃したポータル", "古代都市", "旅路の遺跡", "試練の間",
    "放棄されたキャンプ", "廃坑", "要塞", "砂漠の井戸",
};

typedef struct { int kind; BEHit hit; double dist; } Row;

static int by_distance(const void *a, const void *b)
{
    double d = ((const Row *)a)->dist - ((const Row *)b)->dist;
    return (d > 0) - (d < 0);
}

// 数字ならそのまま、文字列ならゲームと同じく Java の String.hashCode を使う
static uint64_t parse_seed(const char *s)
{
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (*s && !*end && errno != ERANGE)
        return (uint64_t)v;
    int32_t h = 0;
    for (const char *p = s; *p; p++)
        h = 31 * h + (unsigned char)*p;
    return (uint64_t)(int64_t)h;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <seed> [--range N] [--center X Z] [--mc 1.21] [--kinds 0,1,...]\n", argv[0]);
        return 1;
    }
    uint64_t seed = parse_seed(argv[1]);
    int range = 10000, cx = 0, cz = 0, mc = MC_NEWEST;
    int want[BE_KIND_NUM];
    for (int k = 0; k < BE_KIND_NUM; k++)
        want[k] = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--range") && i + 1 < argc)
            range = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--center") && i + 2 < argc) {
            cx = atoi(argv[++i]);
            cz = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--mc") && i + 1 < argc) {
            mc = str2mc(argv[++i]);
            if (mc == MC_UNDEF) {
                fprintf(stderr, "unknown version %s\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(argv[i], "--kinds") && i + 1 < argc) {
            for (int k = 0; k < BE_KIND_NUM; k++)
                want[k] = 0;
            for (char *t = strtok(argv[++i], ","); t; t = strtok(NULL, ","))
                if (atoi(t) >= 0 && atoi(t) < BE_KIND_NUM)
                    want[atoi(t)] = 1;
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 1;
        }
    }
    if (be_setup(seed, mc) != 0) {
        fprintf(stderr, "unsupported version (1.18 or later)\n");
        return 1;
    }
    enum { MAX = 1 << 21 };
    BEHit *hits = malloc(sizeof(BEHit) * MAX);
    Row *rows = malloc(sizeof(Row) * MAX);
    int total = 0;
    for (int k = 0; k < BE_KIND_NUM; k++) {
        if (!want[k] || !be_kind_available(k))
            continue;
        int n = be_scan(k, cx - range, cz - range, cx + range, cz + range, 0, 1, hits, MAX - total);
        for (int i = 0; i < n; i++)
            rows[total++] = (Row){ k, hits[i], hypot(hits[i].x - cx, hits[i].z - cz) };
    }
    qsort(rows, total, sizeof(Row), by_distance);
    static const char *DIM_NAMES[BE_DIM_NUM] = { "オーバーワールド", "ネザー", "エンド" };
    printf("種類,次元,X,Z,距離,バイオーム\n");
    for (int i = 0; i < total; i++) {
        const BEHit *h = &rows[i].hit;
        printf("%s,%s,%d,%d,%.0f,%s\n", KIND_NAMES[rows[i].kind], DIM_NAMES[be_kind_dim(rows[i].kind)],
               h->x, h->z, rows[i].dist, h->biome >= 0 ? biome2str(mc, h->biome) : "");
    }
    free(hits);
    free(rows);
    return 0;
}

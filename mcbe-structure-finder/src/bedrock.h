// 統合版 (Bedrock Edition) の構造物配置エンジン。
// バイオームは cubiomes (Java 版の生成器) を使う: 1.18 以降は同じシード値なら
// Java 版と統合版で地形・バイオームが一致するため。構造物の配置は統合版独自の
// 乱数 (メルセンヌ・ツイスタ) で計算する。
#ifndef MCBE_BEDROCK_H
#define MCBE_BEDROCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// UI と共有する構造物の種類 (番号は web/app.js と一致させること)
enum BEKind {
    BE_VILLAGE,          // 村
    BE_DESERT_PYRAMID,   // 砂漠のピラミッド
    BE_JUNGLE_TEMPLE,    // ジャングルの寺院
    BE_SWAMP_HUT,        // ウィッチの小屋
    BE_IGLOO,            // イグルー
    BE_MONUMENT,         // 海底神殿
    BE_MANSION,          // 森の洋館
    BE_OUTPOST,          // ピリジャーの前哨基地
    BE_SHIPWRECK,        // 難破船
    BE_OCEAN_RUIN,       // 海底遺跡
    BE_TREASURE,         // 埋もれた宝
    BE_RUINED_PORTAL,    // 荒廃したポータル
    BE_ANCIENT_CITY,     // 古代都市
    BE_TRAIL_RUINS,      // 旅路の遺跡
    BE_TRIAL_CHAMBERS,   // 試練の間
    BE_ABANDONED_CAMP,   // 放棄されたキャンプ
    BE_MINESHAFT,        // 廃坑
    BE_STRONGHOLD,       // 要塞 (村の下に必ずできる 3 つ)
    BE_DESERT_WELL,      // 砂漠の井戸
    BE_KIND_NUM
};

// 1 件の結果。x, z はブロック座標、biome は cubiomes のバイオーム ID。
typedef struct {
    int x, z;
    int biome;
} BEHit;

// シード値と cubiomes のバージョン定数 (MC_1_18 以降) で初期化する。0 で成功。
int be_setup(uint64_t seed, int mc);

// その種類がこのバージョンに存在するか。
int be_kind_available(int kind);

// ブロック範囲 [x0,x1]×[z0,z1] にある kind の構造物を out に書く。
// 計算を分割して進捗を出せるよう、走査する行 (z 方向) を part/nparts 番目の
// 帯だけに絞る (全部を一度に計算するなら part=0, nparts=1)。
// 戻り値は件数、存在しない種類なら -1。
int be_scan(int kind, int x0, int z0, int x1, int z1,
            int part, int nparts, BEHit *out, int max);

// 地図用: (x0, z0) から scale ブロックごとに w×h 個の地表のバイオーム ID を out に書く。
// scale は 4, 16, 64, 256 のどれか。0 で成功。
int be_biome_grid(int x0, int z0, int w, int h, int scale, int *out);

#ifdef __cplusplus
}
#endif

#endif

// 統合版のエンド。統合版のエンドは Java 版 1.12 のエンドの生成を、float と
// メルセンヌ・ツイスタ (シード値の下位 32 ビット) で計算したもの。Java 版 1.13 以降
// (cubiomes) とは島の形も場所も違う。
#ifndef MCBE_END_H
#define MCBE_END_H

#include <stdint.h>

void end_setup(uint32_t seed);

// チャンク (cx, cz) の島の高さ (Java 版の getIslandHeightValue)。40 を超えると高地。
float end_island(int cx, int cz);

// 領域 (rx, rz) (20 チャンク四方) のエンドシティ。あれば 1。
// 位置は開始チャンクの中央 (x, z) と土台の高さ y。エンドシップがあれば ship = 1 で、
// (ex, ey, ez) がエリトラの額縁の位置。
typedef struct { int x, y, z; int ship; int ex, ey, ez; } EndCity;
int end_city(int rx, int rz, EndCity *c);

// 外側の島に自然にできるエンドゲートウェイ。チャンク (cx, cz) の装飾で置かれれば 1。
// 走査の前に end_gateway_area でその範囲 (チャンク座標) を知らせておく。
void end_gateway_area(int cx0, int cz0, int cx1, int cz1);
int end_gateway(int cx, int cz, int *x, int *y, int *z);

// ドラゴンを kill 回目 (1〜20) に倒したときに中央の島にできるエンドゲートウェイ (y = 75)
void end_ring_gateway(int kill, int *x, int *z);

#endif

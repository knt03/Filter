// メルセンヌ・ツイスタ (MT19937)。統合版の生成はほぼすべてこれで乱数を作る。
#ifndef MCBE_MT_H
#define MCBE_MT_H

#include <stdint.h>

// 先頭の数個の出力だけを作る。i 番目の出力には状態 i, i+1, i+397 が要る。
typedef struct { uint32_t s[624]; int i; } MT;

static inline void mt_seed(MT *mt, uint32_t seed, int n)
{
    mt->s[0] = seed;
    for (int k = 1; k < n + 397; k++)
        mt->s[k] = 1812433253u * (mt->s[k - 1] ^ (mt->s[k - 1] >> 30)) + (uint32_t)k;
    mt->i = 0;
}

static inline uint32_t mt_twist(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t y = (a & 0x80000000u) | (b & 0x7fffffffu);
    return c ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0);
}

static inline uint32_t mt_temper(uint32_t y)
{
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    return y ^ (y >> 18);
}

static inline uint32_t mt_next(MT *mt)
{
    int k = mt->i++;
    return mt_temper(mt_twist(mt->s[k], mt->s[k + 1], mt->s[k + 397]));
}

// 624 個より多く引くとき用 (状態を更新しながら何個でも作る)
static inline void mt_seed_full(MT *mt, uint32_t seed)
{
    mt_seed(mt, seed, 624 - 397);
    mt->i = 624;
}

static inline uint32_t mt_next_full(MT *mt)
{
    if (mt->i >= 624) {
        for (int k = 0; k < 624; k++)
            mt->s[k] = mt_twist(mt->s[k], mt->s[(k + 1) % 624], mt->s[(k + 397) % 624]);
        mt->i = 0;
    }
    return mt_temper(mt->s[mt->i++]);
}

static inline int mt_int(MT *mt, int n) { return (int)(mt_next(mt) % (uint32_t)n); }
static inline float mt_float(MT *mt) { return (float)(mt_next(mt) * 2.3283064365386963e-10); }

// 上から 5 ビット目 (Java の nextBoolean にあたる)
static inline int mt_bool_full(MT *mt) { return (mt_next_full(mt) >> 27) & 1; }
static inline int mt_int_full(MT *mt, int n) { return (int)(mt_next_full(mt) % (uint32_t)n); }

#endif

#ifndef SUBSTRING_SEARCH_H
#define SUBSTRING_SEARCH_H

#include <stddef.h>
#include <stdint.h>

typedef int (*match_fn)(size_t position, void *context);

enum {
    SEARCH_OK = 0,
    SEARCH_NO_MEMORY = 1,
    SEARCH_BAD_ARGUMENT = 2,
    SEARCH_OUTPUT_ERROR = 3,
    SEARCH_NOT_IMPLEMENTED = 4
};

#define RK_BASE UINT64_C(256)
#define RK_MOD UINT64_C(1000000007)

/* Polynomial hash: эхний байт нь хамгийн өндөр зэргийн гишүүн.
 * length>0 үед data нь length байттай хүчинтэй массив байна.
 * Хоосон цувааны hash=0; буцаах утга 0..RK_MOD-1.
 */
uint64_t rk_hash(const unsigned char *data, size_t length);

/* m>0 урттай цонхыг нэг байтаар баруун тийш шилжүүлнэ.
 * current<RK_MOD; high_power=RK_BASE^(m-1) % RK_MOD.
 * outgoing нь гарах эхний, incoming нь орох сүүлийн байт.
 */
uint64_t rk_roll(uint64_t current, unsigned char outgoing,
                 unsigned char incoming, uint64_t high_power);

/* Бүх олдцын эхлэх байт индексийг өсөх дарааллаар emit-д дамжуулна.
 * Давхцсан олдцыг мөн бүртгэнэ. n, m нь байтаар өгсөн урт; NUL байт
 * текст болон pattern-ийн дотор байж болно. m заавал эерэг байна.
 * n > 0 үед text, үргэлж pattern болон emit хүчинтэй байх ёстой.
 * Hash тэнцүү бүрт байт бүрээр нь шалгах шаардлагатай (memcmp зөвшөөрнө).
 * emit алдаа буцаавал шууд зогсоож SEARCH_OUTPUT_ERROR буцаана.
 */
int search_all(const unsigned char *text, size_t n,
               const unsigned char *pattern, size_t m,
               match_fn emit, void *context);

#endif

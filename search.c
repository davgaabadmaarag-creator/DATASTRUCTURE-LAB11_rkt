#include "search.h"

/* TODO 1: Polynomial hash-ийг Horner-ийн аргаар O(length) хугацаанд бод.
 * Дараагийн байтыг нэмэх бүрдээ RK_MOD модуль авна.
 */
uint64_t rk_hash(const unsigned char *data, size_t length)
{
    (void)data;
    (void)length;
    return 0;
}

/* TODO 2: Цонхны hash-ийг O(1) хугацаанд шинэчил.
 * Хуучин эхний байтын high_power-тэй гишүүнийг хасаад, нэг зэргээр
 * шилжүүлэн шинэ байтыг нэмнэ. Модультай хасалт сөрөг болж болохыг
 * анхаар; unsigned хальж буурсан утга нь зөв модуль гэсэн үг биш.
 */
uint64_t rk_roll(uint64_t current, unsigned char outgoing,
                 unsigned char incoming, uint64_t high_power)
{
    (void)current;
    (void)outgoing;
    (void)incoming;
    (void)high_power;
    return 0;
}

/* TODO 3: Rolling hash-аар бүх олдцыг хай.
 *
 * - search.h дахь оролт болон буцаах утгын гэрээг мөрдөнө.
 * - Pattern ба эхний цонхны hash, high_power-ийг нэг удаа тооцно.
 * - Hash таарсан бүрт байт бүрээр баталгаажуулна (memcmp зөвшөөрнө).
 *   Hash тэнцүү байлаа ч өөр текст байж болно.
 * - Жинхэнэ match бүрт emit(start, context) дуудна.
 * - Цонхыг нэг байтаар шилжүүлж, давхцсан match-үүдийг ч олно.
 *
 * Hash тооцоолол ба шилжилт O(n+m). Байт бүрээр баталгаажуулах
 * нэмэлт хугацаа O(H*m), H = hash тэнцсэн цонхны тоо.
 * Нийт хамгийн муу хугацаа O(n*m); тогтмол модуль хэрэглэж байгаа
 * учраас дурын оролтод шугаман хугацаа гэж батлахгүй.
 * Нэмэлт санах ой O(1). Emit-ийн өөрийн ажлыг хугацаанд оруулаагүй.
 * main() аль хэдийн -i сонголтын нормчлолыг хийсэн байна.
 */
int search_all(const unsigned char *text, size_t n,
               const unsigned char *pattern, size_t m,
               match_fn emit, void *context)
{
    (void)text;
    (void)n;
    (void)pattern;
    (void)m;
    (void)emit;
    (void)context;
    return SEARCH_NOT_IMPLEMENTED;
}

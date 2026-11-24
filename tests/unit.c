/* Independent correctness checks. The oracle is deliberately brute force;
   it is not an implementation of the assigned search algorithm. */
#define LAB_RK 1
#include "search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const size_t *expected;
    size_t expected_count;
    size_t seen;
    const char *name;
} Capture;

static int capture(size_t position, void *context)
{
    Capture *out = context;
    if (out->seen >= out->expected_count || out->expected[out->seen] != position) {
        fprintf(stderr, "%s: unexpected match at %zu (match #%zu)\n", out->name, position, out->seen);
        return 1;
    }
    out->seen++;
    return 0;
}

static int check_case(const char *name, const unsigned char *text, size_t n,
                      const unsigned char *pattern, size_t m)
{
    size_t *expected = malloc((n + 1) * sizeof(*expected));
    if (!expected) {
        fprintf(stderr, "Test harness allocation failed\n");
        return 0;
    }
    size_t count = 0;
    if (m <= n) {
        for (size_t start = 0; start <= n - m; start++) {
            if (memcmp(text + start, pattern, m) == 0)
                expected[count++] = start;
        }
    }
    Capture out = {expected, count, 0, name};
    int status = search_all(text, n, pattern, m, capture, &out);
    int good = status == SEARCH_OK && out.seen == count;
    if (!good)
        fprintf(stderr, "%s: status=%d matches=%zu expected=%zu\n", name, status, out.seen, count);
    free(expected);
    return good;
}

#define TEXT_CASE(label, text, pattern) \
    check_case(label, (const unsigned char *)(text), sizeof(text) - 1, \
               (const unsigned char *)(pattern), sizeof(pattern) - 1)

#if defined(LAB_KMP)
static int check_dfa(const unsigned char *pattern, size_t m)
{
    size_t *dfa = malloc(256 * m * sizeof(*dfa));
    if (!dfa)
        return 0;
    for (size_t i = 0; i < 256 * m; i++)
        dfa[i] = SIZE_MAX;
    size_t restart = SIZE_MAX;
    kmp_build_dfa(pattern, m, dfa, &restart);
    /* Definition oracle: append c to the known prefix and inspect suffixes. */
    unsigned char sequence[32];
    for (size_t state = 0; state < m; state++) {
        memcpy(sequence, pattern, state);
        for (size_t c = 0; c < 256; c++) {
            sequence[state] = (unsigned char)c;
            size_t expected = state + 1;
            while (expected > 0 && memcmp(pattern, sequence + state + 1 - expected, expected) != 0)
                expected--;
            if (dfa[c * m + state] != expected) {
                fprintf(stderr, "DFA[%zu][%zu]=%zu, expected %zu\n", c, state, dfa[c * m + state], expected);
                free(dfa);
                return 0;
            }
        }
    }
    size_t expected_restart = m - 1;
    while (expected_restart > 0 && memcmp(pattern, pattern + m - expected_restart, expected_restart) != 0)
        expected_restart--;
    free(dfa);
    if (restart != expected_restart) {
        fprintf(stderr, "DFA restart=%zu, expected %zu\n", restart, expected_restart);
        return 0;
    }
    return 1;
}

static int prep(void)
{
    const unsigned char high[] = {0, 255, 0, 127};
    const unsigned char single[] = {255};
    return check_dfa((const unsigned char *)"ABABAC", 6)
        && check_dfa((const unsigned char *)"aaaa", 4)
        && check_dfa(high, sizeof(high)) && check_dfa(single, sizeof(single));
}
#elif defined(LAB_BM)
static int prep(void)
{
    ptrdiff_t right[256];
    bm_build_rightmost((const unsigned char *)"ABACAB", 6, right);
    for (size_t c = 0; c < 256; c++) {
        ptrdiff_t expected = c == 'A' ? 4 : c == 'B' ? 5 : c == 'C' ? 3 : -1;
        if (right[c] != expected) {
            fprintf(stderr, "right[%zu]=%td, expected %td\n", c, right[c], expected);
            return 0;
        }
    }
    const unsigned char high[] = {0, 255, 10, 0, 128, 255};
    bm_build_rightmost(high, sizeof(high), right);
    for (size_t c = 0; c < 256; c++) {
        ptrdiff_t expected = c == 0 ? 3 : c == 255 ? 5 : c == 10 ? 2 : c == 128 ? 4 : -1;
        if (right[c] != expected) {
            fprintf(stderr, "Repeated right[%zu]=%td, expected %td\n", c, right[c], expected);
            return 0;
        }
    }
    return 1;
}
#elif defined(LAB_RK)
/* Direct O(m) polynomial evaluation is an oracle for rolling-window updates. */
static uint64_t hash_oracle(const unsigned char *data, size_t m)
{
    uint64_t result = 0;
    uint64_t power = 1;
    for (size_t j = m; j > 0; j--) {
        result = (result + (uint64_t)data[j - 1] * power) % UINT64_C(1000000007);
        power = power * UINT64_C(256) % UINT64_C(1000000007);
    }
    return result;
}

static int prep(void)
{
    const unsigned char high[] = {0, 255, 128, 1};
    if (rk_hash((const unsigned char *)"ABRACADABRA", 11) != UINT64_C(712830176)
        || rk_hash((const unsigned char *)"abc", 3) != UINT64_C(6382179)
        || rk_hash(high, sizeof(high)) != UINT64_C(16744449)
        || rk_hash((const unsigned char *)"", 0) != 0) {
        fprintf(stderr, "Polynomial hash values are incorrect\n");
        return 0;
    }
    const unsigned char data[] = {255, 0, 128, 1, 254, 13, 10, 97, 65, 255, 23, 78};
    for (size_t m = 1; m <= 7; m++) {
        uint64_t power = 1;
        for (size_t i = 1; i < m; i++)
            power = power * UINT64_C(256) % UINT64_C(1000000007);
        uint64_t current = hash_oracle(data, m);
        for (size_t start = 1; start + m <= sizeof(data); start++) {
            current = rk_roll(current, data[start - 1], data[start + m - 1], power);
            if (current != hash_oracle(data + start, m)) {
                fprintf(stderr, "Rolling hash mismatch for m=%zu, start=%zu\n", m, start);
                return 0;
            }
        }
    }
    return 1;
}
#else
#error Choose one laboratory algorithm.
#endif

static int basic(void)
{
    return TEXT_CASE("first and repeated", "xxneedle--needle", "needle")
        && TEXT_CASE("not present", "abcdefgh", "xyz")
        && TEXT_CASE("whole input", "same", "same")
        && TEXT_CASE("one byte", "xxaxaxa", "a")
        && TEXT_CASE("last byte", "abcde", "e");
}

static int overlap(void)
{
    return TEXT_CASE("aaa overlap", "aaaaa", "aaa")
        && TEXT_CASE("periodic overlap", "ababababa", "ababa")
        && TEXT_CASE("newline overlap", "a\na\na\n", "a\na")
        && TEXT_CASE("border fallback", "ABABABACABABAC", "ABABAC")
        && TEXT_CASE("banana offsets", "banana", "ana");
}

static int stop_at_first(size_t position, void *context)
{
    size_t *calls = context;
    (void)position;
    (*calls)++;
    return 1;
}

static int count_only(size_t position, void *context)
{
    size_t *calls = context;
    (void)position;
    (*calls)++;
    return 0;
}

static int boundary(void)
{
    const unsigned char binary[] = {0, 255, 0, 128, 255, 0, 128, 0};
    const unsigned char pattern[] = {255, 0, 128};
    unsigned char all_bytes[512];
    for (size_t i = 0; i < sizeof(all_bytes); i++)
        all_bytes[i] = (unsigned char)(i % 256);
    const unsigned char wrap[] = {254, 255, 0, 1};
    if (!TEXT_CASE("empty text", "", "a")
        || !TEXT_CASE("pattern longer than text", "ab", "abcde")
        || !check_case("NULL empty text", NULL, 0, (const unsigned char *)"x", 1)
        || !check_case("binary pattern and text", binary, sizeof(binary), pattern, sizeof(pattern))
        || !check_case("all 256 byte values", all_bytes, sizeof(all_bytes), wrap, sizeof(wrap)))
        return 0;
    size_t calls = 0;
    int status = search_all((const unsigned char *)"aaaaa", 5, (const unsigned char *)"aa", 2,
                            stop_at_first, &calls);
    if (status != SEARCH_OUTPUT_ERROR || calls != 1) {
        fprintf(stderr, "Callback failure: status=%d calls=%zu\n", status, calls);
        return 0;
    }
    calls = 0;
    if (search_all((const unsigned char *)"abc", 3, (const unsigned char *)"", 0,
                   count_only, &calls) != SEARCH_BAD_ARGUMENT
        || search_all(NULL, 3, (const unsigned char *)"x", 1,
                      count_only, &calls) != SEARCH_BAD_ARGUMENT
        || search_all((const unsigned char *)"abc", 3, NULL, 1,
                      count_only, &calls) != SEARCH_BAD_ARGUMENT
        || search_all((const unsigned char *)"abc", 3, (const unsigned char *)"a", 1,
                      NULL, NULL) != SEARCH_BAD_ARGUMENT || calls != 0) {
        fprintf(stderr, "Invalid arguments must return SEARCH_BAD_ARGUMENT without emitting\n");
        return 0;
    }
    return 1;
}

static uint32_t random_state = UINT32_C(0x7eabc123);

static uint32_t random_u32(void)
{
    uint32_t x = random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    random_state = x;
    return x;
}

static int randomized(void)
{
    /* These two different 8-byte strings have equal hashes under the supplied
       RK constants (171385198). Equality of hashes alone is not a match. */
    if (!TEXT_CASE("collision without real match", "6SWQzr1U", "tQXQTBAA")
        || !TEXT_CASE("collision then real match", "6SWQzr1U--tQXQTBAA--6SWQzr1U", "tQXQTBAA"))
        return 0;
#if defined(LAB_RK)
    if (rk_hash((const unsigned char *)"tQXQTBAA", 8) != UINT64_C(171385198)
        || rk_hash((const unsigned char *)"6SWQzr1U", 8) != UINT64_C(171385198)) {
        fprintf(stderr, "Collision fixture hash values do not match the specified constants\n");
        return 0;
    }
#endif
    for (size_t iteration = 0; iteration < 300; iteration++) {
        unsigned char text[385];
        unsigned char pattern[49];
        size_t n = (size_t)(random_u32() % 385);
        size_t m = 1 + (size_t)(random_u32() % 48);
        unsigned alphabet = iteration % 3 == 0 ? 3 : 256;
        for (size_t i = 0; i < n; i++)
            text[i] = (unsigned char)(random_u32() % alphabet);
        for (size_t i = 0; i < m; i++)
            pattern[i] = (unsigned char)(random_u32() % alphabet);
        if (iteration % 2 == 0 && m <= n) {
            size_t start = random_u32() % (n - m + 1);
            memcpy(pattern, text + start, m);
        }
        char label[64];
        snprintf(label, sizeof(label), "random %zu n=%zu m=%zu", iteration, n, m);
        if (!check_case(label, text, n, pattern, m))
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: unit prep|basic|overlap|boundary|random\n");
        return 2;
    }
    int good = 0;
    if (strcmp(argv[1], "prep") == 0) good = prep();
    else if (strcmp(argv[1], "basic") == 0) good = basic();
    else if (strcmp(argv[1], "overlap") == 0) good = overlap();
    else if (strcmp(argv[1], "boundary") == 0) good = boundary();
    else if (strcmp(argv[1], "random") == 0) good = randomized();
    else {
        fprintf(stderr, "Unknown mode: %s\n", argv[1]);
        return 2;
    }
    if (good) printf("PASS %s\n", argv[1]);
    return good ? 0 : 1;
}

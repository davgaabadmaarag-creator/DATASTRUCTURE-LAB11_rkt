/*
 * Бэлэн командын интерфейс. Оюутны хэрэгжүүлэх алгоритм search.c дотор.
 * C11; текстийг byte массив гэж уншина, уртыг тусад нь дамжуулна.
 */
#include "search.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>

/* Windows UTF-16 command line -> the UTF-8 byte representation used by search. */
static char *utf8_argument(const wchar_t *wide)
{
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                    wide, -1, NULL, 0, NULL, NULL);
    if (length == 0)
        return NULL;
    char *value = malloc((size_t)length);
    if (value == NULL)
        return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                            value, length, NULL, NULL) == 0) {
        free(value);
        return NULL;
    }
    return value;
}

/* Open Unicode paths without relying on the machine's ANSI code page. */
static FILE *open_binary_input(const char *path)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    path, -1, NULL, 0);
    if (length == 0) {
        errno = EINVAL;
        return NULL;
    }
    if ((size_t)length > SIZE_MAX / sizeof(wchar_t)) {
        errno = ENOMEM;
        return NULL;
    }
    wchar_t *wide = malloc((size_t)length * sizeof(*wide));
    if (wide == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            wide, length) == 0) {
        free(wide);
        errno = EINVAL;
        return NULL;
    }
    FILE *input = _wfopen(wide, L"rb");
    int saved_errno = errno;
    free(wide);
    errno = saved_errno;
    return input;
}
#else
static FILE *open_binary_input(const char *path)
{
    return fopen(path, "rb");
}
#endif

typedef struct {
    const unsigned char *text;
    size_t n;
    size_t m;
    size_t scanned;
    size_t line;
    size_t previous;
    int have_previous;
} OutputContext;

static void usage(FILE *stream, const char *program)
{
    fprintf(stream, "Usage: %s [-i] [--] PATTERN FILE\n", program);
    fprintf(stream, "  -i  Ignore ASCII letter case; -- ends option parsing.\n");
}

/* fread()-ийн буцаасан byte-ийн тоогоор уншина. EOF-ийг өгөгдөлд нэмэхгүй. */
static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *input = open_binary_input(path);
    if (input == NULL) {
        perror(path);
        return NULL;
    }
    size_t capacity = 65536;
    size_t used = 0;
    unsigned char *data = malloc(capacity);
    if (data == NULL) {
        fprintf(stderr, "Cannot allocate input buffer.\n");
        fclose(input);
        return NULL;
    }
    for (;;) {
        if (used == capacity) {
            if (capacity > SIZE_MAX / 2) {
                fprintf(stderr, "Input is too large.\n");
                free(data);
                fclose(input);
                return NULL;
            }
            size_t next_capacity = capacity * 2;
            unsigned char *next = realloc(data, next_capacity);
            if (next == NULL) {
                fprintf(stderr, "Cannot grow input buffer.\n");
                free(data);
                fclose(input);
                return NULL;
            }
            data = next;
            capacity = next_capacity;
        }
        size_t received = fread(data + used, 1, capacity - used, input);
        used += received;
        if (received == 0) {
            if (ferror(input)) {
                fprintf(stderr, "Cannot read input file: %s\n", path);
                free(data);
                fclose(input);
                return NULL;
            }
            break;
        }
    }
    if (fclose(input) != 0) {
        fprintf(stderr, "Cannot close input file: %s\n", path);
        free(data);
        return NULL;
    }
    *length = used;
    return data;
}

static unsigned char ascii_lower(unsigned char value)
{
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z')
        return (unsigned char)(value + ('a' - 'A'));
    return value;
}

/* Байрлалууд өсөхөөр ирэх тул мөрүүдийг давхар тоолохгүй. */
static int emit_context(size_t position, void *context)
{
    OutputContext *out = context;
    if (position > out->n || out->m > out->n - position ||
        (out->have_previous && position <= out->previous)) {
        fprintf(stderr, "search_all returned an invalid or duplicate position.\n");
        return 1;
    }
    while (out->scanned < position) {
        if (out->text[out->scanned] == (unsigned char)'\n')
            out->line++;
        out->scanned++;
    }
    size_t left = position > 5 ? position - 5 : 0;
    size_t after_match = position + out->m;
    size_t remaining = out->n - after_match;
    size_t right = after_match + (remaining < 5 ? remaining : 5);

    if (printf("%zu: ", out->line) < 0 ||
        fwrite(out->text + left, 1, right - left, stdout) != right - left ||
        putchar('\n') == EOF) {
        fprintf(stderr, "Cannot write output.\n");
        return 1;
    }
    out->previous = position;
    out->have_previous = 1;
    return 0;
}

static int run_program(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout, argv[0]);
        return 0;
    }
    int arg = 1;
    int ignore_case = 0;
    if (arg < argc && strcmp(argv[arg], "-i") == 0) {
        ignore_case = 1;
        arg++;
    }
    if (arg < argc && strcmp(argv[arg], "--") == 0) {
        arg++;
    } else if (arg < argc && argv[arg][0] == '-' && argv[arg][1] != '\0') {
        usage(stderr, argv[0]);
        return 2;
    }
    if (argc - arg != 2) {
        usage(stderr, argv[0]);
        return 2;
    }

    const unsigned char *pattern = (const unsigned char *)argv[arg];
    size_t m = strlen(argv[arg]);
    if (m == 0) {
        fprintf(stderr, "PATTERN must not be empty.\n");
        return 2;
    }
    size_t n = 0;
    unsigned char *original = read_file(argv[arg + 1], &n);
    if (original == NULL)
        return 2;

    unsigned char *folded_text = NULL;
    unsigned char *folded_pattern = NULL;
    const unsigned char *text_for_search = original;
    const unsigned char *pattern_for_search = pattern;
    if (ignore_case) {
        folded_text = malloc(n == 0 ? 1 : n);
        folded_pattern = malloc(m);
        if (folded_text == NULL || folded_pattern == NULL) {
            fprintf(stderr, "Cannot allocate case-folding buffers.\n");
            free(folded_text);
            free(folded_pattern);
            free(original);
            return 2;
        }
        for (size_t i = 0; i < n; i++)
            folded_text[i] = ascii_lower(original[i]);
        for (size_t i = 0; i < m; i++)
            folded_pattern[i] = ascii_lower(pattern[i]);
        text_for_search = folded_text;
        pattern_for_search = folded_pattern;
    }

    OutputContext output = {original, n, m, 0, 0, 0, 0};
    int status = search_all(text_for_search, n, pattern_for_search, m,
                            emit_context, &output);
    if (status == SEARCH_NOT_IMPLEMENTED)
        fprintf(stderr, "Complete the TODO functions in search.c first.\n");
    else if (status == SEARCH_NO_MEMORY)
        fprintf(stderr, "Search could not allocate its working memory.\n");
    else if (status == SEARCH_BAD_ARGUMENT)
        fprintf(stderr, "Search rejected its arguments or input size.\n");
    else if (status != SEARCH_OK && status != SEARCH_OUTPUT_ERROR)
        fprintf(stderr, "Search failed with status %d.\n", status);

    free(folded_pattern);
    free(folded_text);
    free(original);
    if (fflush(stdout) != 0) {
        fprintf(stderr, "Cannot flush output.\n");
        return 2;
    }
    return status == SEARCH_OK ? 0 : 2;
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    (void)argc;
    (void)argv;
    /* Preserve real CR, LF and NUL bytes; disable CRT LF -> CRLF conversion. */
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        perror("Cannot set stdout to binary mode");
        return 2;
    }
    int wide_argc = 0;
    LPWSTR *wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv == NULL || wide_argc < 1) {
        fprintf(stderr, "Cannot read the Unicode command line.\n");
        if (wide_argv != NULL)
            LocalFree(wide_argv);
        return 2;
    }
    size_t count = (size_t)wide_argc;
    if (count > SIZE_MAX / sizeof(char *) - 1) {
        fprintf(stderr, "Too many command line arguments.\n");
        LocalFree(wide_argv);
        return 2;
    }
    char **utf8_argv = calloc(count + 1, sizeof(*utf8_argv));
    if (utf8_argv == NULL) {
        fprintf(stderr, "Cannot allocate command line arguments.\n");
        LocalFree(wide_argv);
        return 2;
    }
    int converted = 1;
    for (size_t i = 0; i < count; ++i) {
        utf8_argv[i] = utf8_argument(wide_argv[i]);
        if (utf8_argv[i] == NULL) {
            converted = 0;
            break;
        }
    }
    LocalFree(wide_argv);
    int result = 2;
    if (converted)
        result = run_program(wide_argc, utf8_argv);
    else
        fprintf(stderr, "Cannot convert command line arguments to UTF-8.\n");
    for (size_t i = 0; i < count; ++i)
        free(utf8_argv[i]);
    free(utf8_argv);
    return result;
#else
    return run_program(argc, argv);
#endif
}


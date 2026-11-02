/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../win_language.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks;
#define CHECK(condition) do {                                                \
    ++checks;                                                               \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);           \
        exit(1);                                                            \
    }                                                                       \
} while (0)

static void test_primary_language(void)
{
    static const struct {
        const wchar_t *preferred;
        const char *expected;
    } cases[] = {
        { NULL, "en_US" }, { L"", "en_US" }, { L"z", "en_US" },
        { L"zh", "zh_CN" }, { L"ZH", "zh_CN" },
        { L"zh-CN", "zh_CN" }, { L"zh-SG", "zh_CN" },
        { L"zh-TW", "zh_CN" }, { L"zh-HK", "zh_CN" },
        { L"zh-Hans", "zh_CN" }, { L"zh-Hant", "zh_CN" },
        { L"zh-Hant-HK", "zh_CN" }, { L"ZH_hAnT_TW", "zh_CN" },
        { L"zh_CN", "zh_CN" }, { L"en", "en_US" },
        { L"en-US", "en_US" }, { L"en-GB", "en_US" },
        { L"EN_us", "en_US" }, { L"ja-JP", "en_US" },
        { L"fr-FR", "en_US" }, { L"ar-SA", "en_US" },
        { L"zho", "en_US" }, { L"zhongwen", "en_US" },
        { L"en-US\0zh-CN\0", "en_US" },
        { L"ja-JP\0zh-TW\0en-US\0", "en_US" },
        { L"zh-TW\0en-US\0", "zh_CN" },
        { L"\0zh-CN\0", "en_US" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        CHECK(strcmp(haoke_language_from_primary(cases[i].preferred),
                     cases[i].expected) == 0);

    wchar_t long_name[32768];
    wmemset(long_name, L'x', sizeof(long_name) / sizeof(long_name[0]) - 1);
    long_name[0] = L'z'; long_name[1] = L'h'; long_name[2] = L'-';
    long_name[32767] = L'\0';
    CHECK(strcmp(haoke_language_from_primary(long_name), "zh_CN") == 0);
    long_name[0] = L'j'; long_name[1] = L'a';
    CHECK(strcmp(haoke_language_from_primary(long_name), "en_US") == 0);
}

static void test_override_and_restart_policy(void)
{
    CHECK(strcmp(haoke_select_language(NULL, L"zh-Hant-TW\0en-US\0"),
                 "zh_CN") == 0);
    CHECK(strcmp(haoke_select_language(NULL, L"en-US\0zh-CN\0"),
                 "en_US") == 0);
    CHECK(strcmp(haoke_select_language(L"auto", L"zh-HK\0en-US\0"),
                 "zh_CN") == 0);
    CHECK(strcmp(haoke_select_language(L"AUTO", L"ja-JP\0zh-CN\0"),
                 "en_US") == 0);
    CHECK(strcmp(haoke_select_language(L"en-GB", L"zh-CN\0"),
                 "en_US") == 0);
    CHECK(strcmp(haoke_select_language(L"zh-TW", L"en-US\0"),
                 "zh_CN") == 0);
    CHECK(strcmp(haoke_select_language(L"auto-extra", L"zh-CN\0"),
                 "en_US") == 0);
    CHECK(strcmp(haoke_select_language(NULL, NULL), "en_US") == 0);
    /* Consecutive calls represent relaunches after a system-language change;
     * an earlier result must never become sticky process-global state. */
    CHECK(strcmp(haoke_select_language(L"auto", L"zh-TW\0"), "zh_CN") == 0);
    CHECK(strcmp(haoke_select_language(L"auto", L"en-US\0zh-TW\0"), "en_US") == 0);
    CHECK(strcmp(haoke_select_language(L"auto", L"zh-Hans-CN\0"), "zh_CN") == 0);
}

static void test_option_parser(void)
{
    static const struct {
        const wchar_t *argument;
        const wchar_t *next;
        int consumed;
        const wchar_t *expected;
    } cases[] = {
        { L"--language=zh_CN", L"影片.mkv", 1, L"zh_CN" },
        { L"--language=en-US", NULL, 1, L"en-US" },
        { L"--language=auto", L"--fullscreen", 1, L"auto" },
        { L"--language", L"zh-Hant-TW", 2, L"zh-Hant-TW" },
        { L"--language", L"en", 2, L"en" },
        { L"--language", L"auto", 2, L"auto" },
        { L"--language", NULL, -1, NULL },
        { L"--language", L"", -1, NULL },
        { L"--language", L"--fullscreen", -1, NULL },
        { L"--language", L"-I", -1, NULL },
        { L"--language=", L"影片.mkv", -1, NULL },
        { L"--languagebogus", L"影片.mkv", 0, NULL },
        { L"--languages=zh_CN", L"影片.mkv", 0, NULL },
        { L"--Language=zh_CN", NULL, 0, NULL },
        { L"--lang", NULL, 0, NULL },
        { L"--", L"--language", 0, NULL },
        { L"C:\\电影\\--language=影片.mkv", NULL, 0, NULL },
        { L"电影.mkv", NULL, 0, NULL },
        { L"", NULL, 0, NULL }, { NULL, NULL, 0, NULL },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const wchar_t *value = L"stale value";
        CHECK(haoke_parse_language_option(cases[i].argument, cases[i].next,
                                          &value) == cases[i].consumed);
        CHECK(cases[i].expected == NULL ? value == NULL
              : value != NULL && wcscmp(value, cases[i].expected) == 0);
    }
    CHECK(haoke_parse_language_option(L"--language=zh_CN", NULL, NULL) == 1);
}

static void test_media_preservation_and_repeated_options(void)
{
    const wchar_t *argv[] = {
        L"vlc.exe", L"--language=en", L"C:\\电影\\第一 部.mkv",
        L"--language", L"zh-Hant", L"D:\\素材\\第二部.mkv",
        L"--language=auto", L"--fullscreen"
    };
    const wchar_t *kept[8];
    const wchar_t *language = NULL;
    size_t kept_count = 0;
    for (size_t i = 1; i < sizeof(argv) / sizeof(argv[0]); )
    {
        const wchar_t *value = NULL;
        const int consumed = haoke_parse_language_option(
            argv[i], i + 1 < sizeof(argv) / sizeof(argv[0]) ? argv[i + 1] : NULL,
            &value);
        CHECK(consumed >= 0);
        if (consumed != 0)
        {
            language = value;
            i += (size_t)consumed;
        }
        else
            kept[kept_count++] = argv[i++];
    }
    CHECK(kept_count == 3);
    CHECK(kept[0] == argv[2]);
    CHECK(kept[1] == argv[5]);
    CHECK(kept[2] == argv[7]);
    CHECK(wcscmp(language, L"auto") == 0);
    CHECK(strcmp(haoke_select_language(language, L"zh-HK\0en-US\0"), "zh_CN") == 0);
    CHECK(strcmp(haoke_select_language(language, L"ja-JP\0zh-HK\0"), "en_US") == 0);

    const wchar_t *next = L"--fullscreen";
    const wchar_t *value = L"stale value";
    CHECK(haoke_parse_language_option(L"--language", next, &value) == -1);
    CHECK(value == NULL);
    CHECK(wcscmp(next, L"--fullscreen") == 0);
}

int main(void)
{
    test_primary_language();
    test_override_and_restart_policy();
    test_option_parser();
    test_media_preservation_and_repeated_options();
    printf("PASS: %u language and argument-contract checks\n", checks);
    return 0;
}

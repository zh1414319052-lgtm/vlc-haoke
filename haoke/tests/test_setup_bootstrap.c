/* GPL-2.0-or-later. Native quoting/language helper checks: no installer is run. */
#define wWinMain unused_setup_entry_for_tests
#include "../setup_bootstrap.c"
#undef wWinMain

static unsigned int checks;
#define CHECK(test) do { ++checks; if (!(test)) { fprintf(stderr, "FAIL line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    const wchar_t *args[] = {L"wizard.exe", L"", L"simple", L"a b", L"C:\\测试 空间\\",
        L"a\"quoted\"value", L"\\\\server\\folder", L"/DIR=D:\\安装 目录", L"/LOG=D:\\log files\\setup.log"};
    wchar_t command[MAX_COMMAND] = {0};
    size_t used = 0;
    for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); ++i)
        CHECK(quote_argument(command, &used, args[i]));
    int count = 0;
    wchar_t **roundtrip = CommandLineToArgvW(command, &count);
    CHECK(roundtrip != NULL);
    CHECK(count == (int)(sizeof(args) / sizeof(args[0])));
    for (int i = 0; i < count; ++i) CHECK(wcscmp(args[i], roundtrip[i]) == 0);
    LocalFree(roundtrip);

    wchar_t *normal[] = {L"Setup.exe", L"/DIR=D:\\测试 目录", L"/LOG=D:\\a b.log"};
    unsigned char pass[8] = {0};
    const wchar_t *override = NULL;
    CHECK(collect_arguments(3, normal, pass, &override));
    CHECK(override == NULL && pass[1] && pass[2]);
    CHECK(strcmp(haoke_select_language(override, L"ja-JP\0zh-CN\0"), "en_US") == 0);

    wchar_t *overrides[] = {L"Setup.exe", L"/LANG=zh-TW", L"--language", L"en_US", L"/DIR=D:\\x"};
    memset(pass, 0, sizeof(pass));
    CHECK(collect_arguments(5, overrides, pass, &override));
    CHECK(wcscmp(override, L"en_US") == 0);
    CHECK(!pass[1] && !pass[2] && !pass[3] && pass[4]);
    CHECK(strcmp(haoke_select_language(override, L"zh-CN\0en-US\0"), "en_US") == 0);

    wchar_t *automatic[] = {L"Setup.exe", L"/lang=auto"};
    CHECK(collect_arguments(2, automatic, pass, &override));
    CHECK(strcmp(haoke_select_language(override, L"zh-Hant-HK\0en-US\0"), "zh_CN") == 0);
    const wchar_t *bad[] = {L"--language", L"--language=", L"/LANG=", L"/LANG"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        wchar_t *values[] = {L"Setup.exe", (wchar_t *)bad[i]};
        CHECK(!collect_arguments(2, values, pass, &override));
    }
    wchar_t *huge = calloc(MAX_COMMAND + 10, sizeof(*huge));
    CHECK(huge != NULL);
    for (int i = 0; i < MAX_COMMAND + 8; ++i) huge[i] = L'\\';
    command[0] = 0; used = 0;
    CHECK(!quote_argument(command, &used, huge));
    CHECK(used < MAX_COMMAND && command[used] == 0);
    free(huge);
    printf("PASS %u native Setup argument/language/bounds checks; no installer executed\n", checks);
    return 0;
}

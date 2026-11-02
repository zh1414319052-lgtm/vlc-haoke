/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Native launcher for the real, signed Inno installer.
 * This is not the player entry. It never installs payload files itself.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "win_language.h"
#include "setup_payload_hash.h"

#define MAX_COMMAND 32760
#define PAYLOAD_ID 101

static int append(wchar_t *dst, size_t *used, wchar_t ch)
{
    if (*used >= MAX_COMMAND - 1) return 0;
    dst[(*used)++] = ch;
    dst[*used] = 0;
    return 1;
}

/* Windows CommandLineToArgvW quoting; no shell is involved. */
static int quote_argument(wchar_t *dst, size_t *used, const wchar_t *arg)
{
    size_t slashes = 0;
    if (*used && !append(dst, used, L' ')) return 0;
    if (!append(dst, used, L'"')) return 0;
    for (;; ++arg) {
        if (*arg == L'\\') { ++slashes; continue; }
        if (*arg == L'"' || *arg == 0) {
            size_t count = slashes * 2 + (*arg == L'"');
            while (count--) if (!append(dst, used, L'\\')) return 0;
        } else {
            while (slashes--) if (!append(dst, used, L'\\')) return 0;
        }
        slashes = 0;
        if (*arg == 0) break;
        if (!append(dst, used, *arg)) return 0;
    }
    return append(dst, used, L'"');
}

static int collect_arguments(int argc, wchar_t **argv, unsigned char *pass,
                             const wchar_t **override)
{
    *override = NULL;
    for (int i = 1; i < argc; ++i) {
        const wchar_t *value = NULL;
        int consumed = haoke_parse_language_option(argv[i], i + 1 < argc ? argv[i + 1] : NULL, &value);
        if (consumed < 0) return 0;
        if (consumed) { *override = value; if (consumed == 2) ++i; continue; }
        if (_wcsnicmp(argv[i], L"/LANG=", 6) == 0) {
            if (!argv[i][6]) return 0;
            *override = argv[i] + 6;
            continue;
        }
        if (_wcsicmp(argv[i], L"/LANG") == 0) return 0;
        pass[i] = 1;
    }
    return 1;
}

static wchar_t *preferred_languages(void)
{
    ULONG count = 0, length = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, NULL, &length)
        || length < 2 || length > 32768) return NULL;
    wchar_t *result = calloc(length, sizeof(*result));
    if (!result) return NULL;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, result, &length)) {
        free(result); return NULL;
    }
    return result;
}

static void failure(int chinese, DWORD code)
{
    wchar_t text[256];
    swprintf(text, sizeof(text) / sizeof(text[0]),
        chinese ? L"无法启动安装向导。错误代码：%lu。请重新运行完整的安装包。"
                : L"Unable to start the installation wizard. Error code: %lu. Run the complete installer again.",
        (unsigned long)code);
    MessageBoxW(NULL, text, chinese ? L"全能影音播放器安装程序" : L"VLC media player - Haoke setup",
        MB_OK | MB_ICONERROR);
}

static int hash_file(const wchar_t *path, unsigned char digest[32])
{
    int good = 0;
    HANDLE input = INVALID_HANDLE_VALUE;
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR object = NULL;
    DWORD object_size = 0, received = 0, count = 0;
    unsigned char buffer[65536];
    input = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (input == INVALID_HANDLE_VALUE) goto done;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size,
        sizeof(object_size), &received, 0) < 0) goto done;
    object = malloc(object_size);
    if (!object || BCryptCreateHash(algorithm, &hash, object, object_size, NULL, 0, 0) < 0) goto done;
    for (;;) {
        if (!ReadFile(input, buffer, sizeof(buffer), &count, NULL)) goto done;
        if (!count) break;
        if (BCryptHashData(hash, buffer, count, 0) < 0) goto done;
    }
    good = BCryptFinishHash(hash, digest, 32, 0) >= 0;
done:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    free(object);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    return good;
}

static int digest_matches(const unsigned char digest[32])
{
    char actual[65];
    for (size_t i = 0; i < 32; ++i) sprintf(actual + 2 * i, "%02x", digest[i]);
    actual[64] = 0;
    return strcmp(actual, HAOKE_SETUP_PAYLOAD_SHA256) == 0;
}

/* A protected, exclusively created directory owned by the launching user.
 * The DACL is not applied to any existing directory or system configuration.
 */
static int create_private_directory(wchar_t directory[MAX_PATH])
{
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    LPWSTR sid = NULL;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    DWORD needed = 0;
    int good = 0;
    wchar_t temp[MAX_PATH], sddl[512];
    DWORD temp_length = GetTempPathW(MAX_PATH, temp);
    if (temp_length == 0 || temp_length >= MAX_PATH || temp_length > MAX_PATH - 80) goto done;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) goto done;
    GetTokenInformation(token, TokenUser, NULL, 0, &needed);
    if (!needed || needed > 65536) goto done;
    user = malloc(needed);
    if (!user || !GetTokenInformation(token, TokenUser, user, needed, &needed)) goto done;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid)) goto done;
    if (wcslen(sid) > 200) goto done;
    swprintf(sddl, sizeof(sddl) / sizeof(sddl[0]), L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;%ls)", sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1,
        &descriptor, NULL)) goto done;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
    for (unsigned int attempt = 0; attempt < 8; ++attempt) {
        unsigned char random[16];
        wchar_t suffix[33];
        if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) goto done;
        for (size_t i = 0; i < sizeof(random); ++i)
            swprintf(suffix + i * 2, 3, L"%02x", random[i]);
        swprintf(directory, MAX_PATH, L"%lshaoke-vlc-setup-%ls", temp, suffix);
        if (CreateDirectoryW(directory, &attributes)) { good = 1; break; }
        if (GetLastError() != ERROR_ALREADY_EXISTS) goto done;
    }
done:
    if (descriptor) LocalFree(descriptor);
    if (sid) LocalFree(sid);
    free(user);
    if (token) CloseHandle(token);
    return good;
}

static int write_payload(HINSTANCE instance, const wchar_t *path)
{
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(PAYLOAD_ID), RT_RCDATA);
    if (!resource) return 0;
    DWORD length = SizeofResource(instance, resource);
    HGLOBAL loaded = LoadResource(instance, resource);
    const unsigned char *data = loaded ? LockResource(loaded) : NULL;
    if (!data || length != HAOKE_SETUP_PAYLOAD_SIZE) { SetLastError(ERROR_CRC); return 0; }
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    int good = 1;
    for (DWORD offset = 0; offset < length;) {
        DWORD remaining = length - offset;
        DWORD amount = remaining > 65536 ? 65536 : remaining, written = 0;
        if (!WriteFile(file, data + offset, amount, &written, NULL) || written != amount) { good = 0; break; }
        offset += amount;
    }
    if (good) good = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    unsigned char digest[32];
    if (good) good = hash_file(path, digest) && digest_matches(digest);
    if (!good) SetLastError(ERROR_CRC);
    return good;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command, int show)
{
    (void)previous; (void)command; (void)show;
    int argc = 0, exit_code = 2, created = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t *primary = preferred_languages();
    const wchar_t *override = NULL;
    int chinese = strcmp(haoke_select_language(NULL, primary), "zh_CN") == 0;
    DWORD error = ERROR_INVALID_PARAMETER;
    wchar_t directory[MAX_PATH] = {0}, payload[MAX_PATH] = {0};
    wchar_t *child_command = calloc(MAX_COMMAND, sizeof(*child_command));
    unsigned char *pass = argc > 0 ? calloc((size_t)argc, 1) : NULL;
    if (!argv || argc < 1 || !child_command || !pass) { error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    if (!collect_arguments(argc, argv, pass, &override)) goto done;
    chinese = strcmp(haoke_select_language(override, primary), "zh_CN") == 0;
    if (!create_private_directory(directory)) { error = GetLastError(); goto done; }
    created = 1;
    swprintf(payload, MAX_PATH, L"%ls\\HaokeVLCWizard.exe", directory);
    if (!write_payload(instance, payload)) { error = GetLastError(); goto done; }
    size_t used = 0;
    if (!quote_argument(child_command, &used, payload) ||
        !quote_argument(child_command, &used, chinese ? L"/LANG=zh_CN" : L"/LANG=en_US")) {
        error = ERROR_BAD_LENGTH; goto done;
    }
    for (int i = 1; i < argc; ++i)
        if (pass[i] && !quote_argument(child_command, &used, argv[i])) { error = ERROR_BAD_LENGTH; goto done; }
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    if (!CreateProcessW(payload, child_command, NULL, NULL, FALSE, 0, NULL,
        directory, &startup, &process)) { error = GetLastError(); goto done; }
    CloseHandle(process.hThread);
    DWORD result = 2;
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process.hProcess, &result)) { exit_code = (int)result; error = 0; }
    else error = GetLastError();
    CloseHandle(process.hProcess);
done:
    if (payload[0]) DeleteFileW(payload);
    if (created) RemoveDirectoryW(directory); /* Never recursively delete a temp tree. */
    free(pass);
    free(child_command);
    free(primary);
    if (argv) LocalFree(argv);
    if (error) failure(chinese, error);
    return exit_code;
}

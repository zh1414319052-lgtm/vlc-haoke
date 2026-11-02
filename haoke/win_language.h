/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Haoke Windows startup language policy for VLC.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This header has no Windows, environment or persistent-state dependencies.
 * The caller obtains the Windows preferred-UI-language MULTI_SZ on each launch
 * and supplies it here. Only its first NUL-terminated name is inspected.
 */
#ifndef HAOKE_WIN_LANGUAGE_H
#define HAOKE_WIN_LANGUAGE_H

#include <stddef.h>
#include <wchar.h>

static inline wchar_t haoke_language_ascii_lower(wchar_t c)
{
    return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
}

/* Chinese variants use the existing Simplified Chinese catalogue. English
 * and every unsupported/unknown primary language use the English UI.
 * Do not search subsequent MULTI_SZ entries for a supported language.
 */
static inline const char *haoke_language_from_primary(const wchar_t *primary)
{
    if (primary != NULL && haoke_language_ascii_lower(primary[0]) == L'z'
        && haoke_language_ascii_lower(primary[1]) == L'h'
        && (primary[2] == L'\0' || primary[2] == L'-' || primary[2] == L'_'))
        return "zh_CN";
    return "en_US";
}

static inline int haoke_language_is_auto(const wchar_t *value)
{
    static const wchar_t expected[] = L"auto";
    if (value == NULL)
        return 0;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        if (haoke_language_ascii_lower(value[i]) != expected[i])
            return 0;
    return 1;
}

/* NULL means no command-line override. "auto" explicitly restores the current
 * system primary language; it is never cached as the first detected locale.
 */
static inline const char *haoke_select_language(const wchar_t *explicit_value,
                                               const wchar_t *system_multisz)
{
    return haoke_language_from_primary(
        explicit_value == NULL || haoke_language_is_auto(explicit_value)
            ? system_multisz : explicit_value);
}

/* Return the number of argv entries consumed (1 or 2), 0 for an unrelated
 * option, and -1 for a language option with a missing/empty value.
 * The output borrows argv storage and is NULL on 0/-1. The caller must stop on
 * -1 without consuming next_argument. For repeated valid options, retaining
 * the last returned value gives conventional last-option-wins behaviour.
 */
static inline int haoke_parse_language_option(const wchar_t *argument,
                                             const wchar_t *next_argument,
                                             const wchar_t **value)
{
    static const wchar_t option[] = L"--language";
    const size_t option_length = sizeof(option) / sizeof(option[0]) - 1;
    if (value != NULL)
        *value = NULL;
    if (argument == NULL || wcsncmp(argument, option, option_length) != 0)
        return 0;

    if (argument[option_length] == L'=')
    {
        const wchar_t *inline_value = argument + option_length + 1;
        if (*inline_value == L'\0')
            return -1;
        if (value != NULL)
            *value = inline_value;
        return 1;
    }
    if (argument[option_length] != L'\0')
        return 0;

    if (next_argument == NULL || *next_argument == L'\0'
        || next_argument[0] == L'-')
        return -1;
    if (value != NULL)
        *value = next_argument;
    return 2;
}

#endif /* HAOKE_WIN_LANGUAGE_H */

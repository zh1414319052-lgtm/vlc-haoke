/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-License-Identifier: GPL-2.0-or-later
 * GCC 13 / mingw-w64 11 compatibility for the pinned official Qt archive.
 * Newer mingw-w64 CRTs supply this accessor for x64 msvcrt, whose DLL exports
 * __argc as data rather than exporting __p___argc as a function.
 * Keep Qt's accessor semantics and the same msvcrt state; do not mix in UCRT.
 * See mingw-w64's 2024-06 "Provide __p___argc() ... all CRT import libraries".
 */
#include <stdlib.h>

static int *__cdecl haoke_msvcrt_argc(void)
{
    return &__argc;
}

int *(__cdecl *__imp___p___argc)(void) = haoke_msvcrt_argc;

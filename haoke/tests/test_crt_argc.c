/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern int *(__cdecl *__imp___p___argc)(void);

int main(int argc, char **argv)
{
    (void)argv;
    assert(__imp___p___argc != NULL);
    assert(__imp___p___argc() == &__argc);
    assert(*__imp___p___argc() == argc);
    puts("PASS: native MSVCRT argc accessor matches the real process arguments");
    return 0;
}

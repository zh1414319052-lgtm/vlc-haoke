/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later.
 * The pinned 2026 contrib uses mingw-w64's new non-underscore fstat64i32
 * spelling. Ubuntu 24.04 mingw-w64 v11 only provides the MSVCRT spelling.
 * Both use the fixed struct _stat64i32 ABI; never map to variable struct stat.
 * This adapter is needed by libgcrypt's optional random seed-file code.
 */
#include <sys/stat.h>
#include <errno.h>
int __cdecl fstat64i32(int fd, struct _stat64i32 *buffer)
{
    int result = _fstat64i32(fd, buffer);
    if (result == 0 && buffer->st_size < 0) {
        errno = EOVERFLOW;
        return -1;
    }
    return result;
}

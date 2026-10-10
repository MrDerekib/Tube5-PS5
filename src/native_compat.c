/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <dlfcn.h>

/* Native title containers cannot provide POSIX module-path discovery.
 * OpenSSL treats 0 as "no module information"; local playback never loads plugins.
 * Matches the documented native-title adapter in examples/update-check/console_curl.c.
 */
int dladdr(const void *address, Dl_info *info)
{
    (void)address;
    (void)info;
    return 0;
}

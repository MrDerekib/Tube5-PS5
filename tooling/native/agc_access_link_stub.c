// SPDX-License-Identifier: GPL-3.0-or-later
// Public ABI declarations, linker only. Never packaged/executed on console.
#include <stdint.h>
int sceAgcInit(uint32_t version) { (void)version; return -1; }
void *sceAgcGetRegisterDefaults(void) { return 0; }

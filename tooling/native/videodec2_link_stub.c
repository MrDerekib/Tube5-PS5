// SPDX-License-Identifier: GPL-3.0-or-later
// Linker-only declarations, never packaged or executed on the console.
int sceVideodec2QueryComputeMemoryInfo(void *memory)
{
    (void)memory;
    return -1;
}
int sceVideodec2QueryDecoderMemoryInfo(const void *config, void *memory)
{
    (void)config;
    (void)memory;
    return -1;
}

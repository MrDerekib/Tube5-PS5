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
int sceVideodec2AllocateComputeQueue(const void *config, const void *memory, void **queue)
{
    (void)config;
    (void)memory;
    (void)queue;
    return -1;
}
int sceVideodec2ReleaseComputeQueue(void *queue)
{
    (void)queue;
    return -1;
}
int sceVideodec2CreateDecoder(const void *config, const void *memory, void **decoder)
{
    (void)config;
    (void)memory;
    (void)decoder;
    return -1;
}
int sceVideodec2DeleteDecoder(void *decoder)
{
    (void)decoder;
    return -1;
}
int sceVideodec2Reset(void *decoder)
{
    (void)decoder;
    return -1;
}
int sceVideodec2Decode(void *decoder, void *input, void *frame, void *output)
{
    (void)decoder;
    (void)input;
    (void)frame;
    (void)output;
    return -1;
}
int sceVideodec2Flush(void *decoder, void *frame, void *output)
{
    (void)decoder;
    (void)frame;
    (void)output;
    return -1;
}

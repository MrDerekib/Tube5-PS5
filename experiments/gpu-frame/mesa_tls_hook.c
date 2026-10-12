// SPDX-License-Identifier: GPL-3.0-or-later
// Mesa's query objects weakly reference a C++ TLS initializer hook. The
// context pointer has a static null initializer, so there is no runtime work.
// The native ELF converter requires even weak references to resolve locally.
void _ZTH23_mesa_glapi_tls_Context(void) {}

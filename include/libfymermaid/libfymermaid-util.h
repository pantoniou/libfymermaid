/*
 * libfymermaid-util.h - portability and utility declarations for libfymermaid
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBFYMERMAID_UTIL_H
#define LIBFYMERMAID_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FYMM_EXPORT - mark a symbol as part of the shared-library public ABI.
 * On GCC/Clang (>= 4) it overrides -fvisibility=hidden for the annotated
 * symbol; elsewhere it expands to nothing. */
#if defined(__GNUC__) && __GNUC__ >= 4
#define FYMM_EXPORT __attribute__((visibility("default")))
#else
#define FYMM_EXPORT /* nothing */
#endif

/* fymm_library_version() - the library version string, e.g. "0.0.1" */
const char *
fymm_library_version(void)
	FYMM_EXPORT;

/* fymm_free() - release memory handed out by the library
 *
 * Every libfymermaid entry point that returns a heap buffer (the renderers'
 * char * results) expects it back through here rather than free(3), so a
 * caller linked against a different allocator stays correct.
 */
void
fymm_free(void *ptr)
	FYMM_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYMERMAID_UTIL_H */

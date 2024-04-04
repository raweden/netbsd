/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Raweden @github 2024.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __RTLD_WASM_RTLD_H_
#define __RTLD_WASM_RTLD_H_

#include <sys/stdint.h>
#include <sys/stdbool.h>

// check what private flags are used for segments in rtld.c
#define _RTLD_SEGMENT_NOT_EXPORTED (1 << 2)
#define _RTLD_SEGMENT_ZERO_FILL (1 << 3)

struct rtld_state_common {
    uint32_t ld_mutex;
    uint32_t ld_state;
    uint32_t dsovec_size;
    struct wasm_module_rt **dsovec;
    struct wasm_module_rt *objlist;     // head of the linked list
    struct wasm_module_rt *objtail;    // last loaded object in the linked list
    struct wasm_module_rt *objmain;    // main executable
    struct wasm_module_rt *objself;     // reference to the object/module of the rtld module itself.
    const char *error_message;
    struct _rtld_search_path *ld_paths;
    struct _rtld_search_path *ld_default_paths;
    uint32_t objcount;
    uint32_t objloads;
    bool    ld_trust;	                /* False for setuid and setgid programs */
    // private parts of rtld_state are declared in rtld.c
};

#endif /* __RTLD_WASM_RTLD_H_ */
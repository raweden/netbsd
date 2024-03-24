/*-
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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/lwp.h>
#include <wasm/wasm_inst.h>

#define SCHEDULER_MAX 128

struct async_task {
    void (*fn)(void *);
    void *arg;
};

// the location of this struct needs to be exported in kernel descriptor.
struct async_task_queue {
    uint32_t queue_size;
    uintptr_t queue[SCHEDULER_MAX];
} scheduler_tasks = {
    .queue_size = SCHEDULER_MAX
};

// lwp_machdep.c
int wasm_lwp_wait(lwp_t *, int64_t);

void *
__scheduler_taskque(void)
{
    return (void *)&scheduler_tasks;
}

void
wasm_scheduler(void)
{
    struct lwp *l;
    //uintptr_t *queue;
    struct async_task *task;
    uintptr_t tasks[SCHEDULER_MAX];
    uint32_t ret, count;

    l = (struct lwp *)curlwp;

    while (true) {
        count = 0;

        for (int i = 0; i < SCHEDULER_MAX; i++) {
            ret = atomic_xchg32((uint32_t *)&scheduler_tasks.queue[i], 0);
            if (ret != 0) {
                tasks[count++] = ret;
            }
        }

        if (count > 0) {
            for (int i = 0; i < count; i++) {
                task = (struct async_task *)tasks[i];
                if (task->fn == NULL) {
                    continue;
                }
                task->fn(task->arg);
            }
        }

        wasm_lwp_wait(l, -1);
    }
}
/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <nvgpu/ltc.h>
#include <nvgpu/dma.h>

#include "gk20a/gk20a.h"
#include "gk20a/gr_gk20a.h"

int nvgpu_init_ltc_support(struct gk20a *g)
{
	nvgpu_spinlock_init(&g->ltc_enabled_lock);

	g->mm.ltc_enabled_current = true;
	g->mm.ltc_enabled_target = true;

	if (g->ops.ltc.init_fs_state)
		g->ops.ltc.init_fs_state(g);

	return 0;
}

void nvgpu_ltc_sync_enabled(struct gk20a *g)
{
	nvgpu_spinlock_acquire(&g->ltc_enabled_lock);
	if (g->mm.ltc_enabled_current != g->mm.ltc_enabled_target) {
		g->ops.ltc.set_enabled(g, g->mm.ltc_enabled_target);
		g->mm.ltc_enabled_current = g->mm.ltc_enabled_target;
	}
	nvgpu_spinlock_release(&g->ltc_enabled_lock);
}

int nvgpu_ltc_alloc_cbc(struct gk20a *g, size_t compbit_backing_size)
{
	struct gr_gk20a *gr = &g->gr;
	unsigned long flags = 0;

	if (!nvgpu_iommuable(g))
		flags = NVGPU_DMA_FORCE_CONTIGUOUS;

	return nvgpu_dma_alloc_flags_sys(g,
					 flags,
					 compbit_backing_size,
					 &gr->compbit_store.mem);
}

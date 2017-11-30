/*
 * GK20A memory management
 *
 * Copyright (c) 2011-2017, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef MM_GK20A_H
#define MM_GK20A_H

#include <nvgpu/nvgpu_mem.h>
#include <nvgpu/allocator.h>
#include <nvgpu/vm.h>
#include <nvgpu/list.h>
#include <nvgpu/rbtree.h>
#include <nvgpu/kref.h>

struct gpfifo_desc {
	struct nvgpu_mem mem;
	u32 entry_num;

	u32 get;
	u32 put;

	bool wrap;

	/* if gpfifo lives in vidmem or is forced to go via PRAMIN, first copy
	 * from userspace to pipe and then from pipe to gpu buffer */
	void *pipe;
};

struct patch_desc {
	struct nvgpu_mem mem;
	u32 data_count;
};

struct zcull_ctx_desc {
	u64 gpu_va;
	u32 ctx_attr;
	u32 ctx_sw_mode;
};

struct pm_ctx_desc {
	struct nvgpu_mem mem;
	u32 pm_mode;
};

struct compbit_store_desc {
	struct nvgpu_mem mem;

	/* The value that is written to the hardware. This depends on
	 * on the number of ltcs and is not an address. */
	u64 base_hw;
};

struct gk20a_buffer_state {
	struct nvgpu_list_node list;

	/* The valid compbits and the fence must be changed atomically. */
	struct nvgpu_mutex lock;

	/* Offset of the surface within the dma-buf whose state is
	 * described by this struct (one dma-buf can contain multiple
	 * surfaces with different states). */
	size_t offset;

	/* A bitmask of valid sets of compbits (0 = uncompressed). */
	u32 valid_compbits;

	/* The ZBC color used on this buffer. */
	u32 zbc_color;

	/* This struct reflects the state of the buffer when this
	 * fence signals. */
	struct gk20a_fence *fence;
};

static inline struct gk20a_buffer_state *
gk20a_buffer_state_from_list(struct nvgpu_list_node *node)
{
	return (struct gk20a_buffer_state *)
		((uintptr_t)node - offsetof(struct gk20a_buffer_state, list));
};

struct priv_cmd_queue {
	struct nvgpu_mem mem;
	u32 size;	/* num of entries in words */
	u32 put;	/* put for priv cmd queue */
	u32 get;	/* get for priv cmd queue */
};

struct priv_cmd_entry {
	bool valid;
	struct nvgpu_mem *mem;
	u32 off;	/* offset in mem, in u32 entries */
	u64 gva;
	u32 get;	/* start of entry in queue */
	u32 size;	/* in words */
};

struct gk20a;
struct channel_gk20a;

int gk20a_mm_fb_flush(struct gk20a *g);
void gk20a_mm_l2_flush(struct gk20a *g, bool invalidate);
void gk20a_mm_cbc_clean(struct gk20a *g);
void gk20a_mm_l2_invalidate(struct gk20a *g);

#define dev_from_vm(vm) dev_from_gk20a(vm->mm->g)

void gk20a_mm_ltc_isr(struct gk20a *g);

bool gk20a_mm_mmu_debug_mode_enabled(struct gk20a *g);

int gk20a_mm_mmu_vpr_info_fetch(struct gk20a *g);

int gk20a_alloc_inst_block(struct gk20a *g, struct nvgpu_mem *inst_block);
void gk20a_init_inst_block(struct nvgpu_mem *inst_block, struct vm_gk20a *vm,
		u32 big_page_size);
int gk20a_init_mm_setup_hw(struct gk20a *g);

u64 gk20a_locked_gmmu_map(struct vm_gk20a *vm,
			  u64 map_offset,
			  struct nvgpu_sgt *sgt,
			  u64 buffer_offset,
			  u64 size,
			  int pgsz_idx,
			  u8 kind_v,
			  u32 ctag_offset,
			  u32 flags,
			  int rw_flag,
			  bool clear_ctags,
			  bool sparse,
			  bool priv,
			  struct vm_gk20a_mapping_batch *batch,
			  enum nvgpu_aperture aperture);

void gk20a_locked_gmmu_unmap(struct vm_gk20a *vm,
			     u64 vaddr,
			     u64 size,
			     int pgsz_idx,
			     bool va_allocated,
			     int rw_flag,
			     bool sparse,
			     struct vm_gk20a_mapping_batch *batch);

/* vm-as interface */
struct nvgpu_as_alloc_space_args;
struct nvgpu_as_free_space_args;
int gk20a_vm_release_share(struct gk20a_as_share *as_share);
int gk20a_vm_bind_channel(struct gk20a_as_share *as_share,
			  struct channel_gk20a *ch);
int __gk20a_vm_bind_channel(struct vm_gk20a *vm, struct channel_gk20a *ch);

void pde_range_from_vaddr_range(struct vm_gk20a *vm,
					      u64 addr_lo, u64 addr_hi,
					      u32 *pde_lo, u32 *pde_hi);
int gk20a_mm_pde_coverage_bit_count(struct vm_gk20a *vm);
u32 gk20a_mm_get_iommu_bit(struct gk20a *g);

const struct gk20a_mmu_level *gk20a_mm_get_mmu_levels(struct gk20a *g,
						      u32 big_page_size);
void gk20a_mm_init_pdb(struct gk20a *g, struct nvgpu_mem *mem,
		struct vm_gk20a *vm);

extern const struct gk20a_mmu_level gk20a_mm_levels_64k[];
extern const struct gk20a_mmu_level gk20a_mm_levels_128k[];

enum gmmu_pgsz_gk20a gk20a_get_pde_pgsz(struct gk20a *g,
					const struct gk20a_mmu_level *l,
					struct nvgpu_gmmu_pd *pd, u32 pd_idx);
enum gmmu_pgsz_gk20a gk20a_get_pte_pgsz(struct gk20a *g,
					const struct gk20a_mmu_level *l,
					struct nvgpu_gmmu_pd *pd, u32 pd_idx);
#endif /* MM_GK20A_H */

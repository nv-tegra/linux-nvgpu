/*
 * drivers/video/tegra/host/gk20a/ltc_gk20a.c
 *
 * GK20A Graphics
 *
 * Copyright (c) 2011-2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>

#include "hw_ltc_gk20a.h"
#include "hw_proj_gk20a.h"

#include "ltc_common.c"

static int gk20a_ltc_init_comptags(struct gk20a *g, struct gr_gk20a *gr)
{
	/* max memory size (MB) to cover */
	u32 max_size = gr->max_comptag_mem;
	/* one tag line covers 128KB */
	u32 max_comptag_lines = max_size << 3;

	u32 hw_max_comptag_lines =
		ltc_ltcs_ltss_cbc_ctrl3_clear_upper_bound_init_v();

	u32 cbc_param =
		gk20a_readl(g, ltc_ltcs_ltss_cbc_param_r());
	u32 comptags_per_cacheline =
		ltc_ltcs_ltss_cbc_param_comptags_per_cache_line_v(cbc_param);
	u32 slices_per_fbp =
		ltc_ltcs_ltss_cbc_param_slices_per_fbp_v(cbc_param);
	u32 cacheline_size =
		512 << ltc_ltcs_ltss_cbc_param_cache_line_size_v(cbc_param);

	u32 compbit_backing_size;

	int err;

	gk20a_dbg_fn("");

	if (max_comptag_lines == 0) {
		gr->compbit_store.size = 0;
		return 0;
	}

	if (max_comptag_lines > hw_max_comptag_lines)
		max_comptag_lines = hw_max_comptag_lines;

	/* no hybird fb */
	compbit_backing_size =
		DIV_ROUND_UP(max_comptag_lines, comptags_per_cacheline) *
		cacheline_size * slices_per_fbp * gr->num_fbps;

	/* aligned to 2KB * num_fbps */
	compbit_backing_size +=
		gr->num_fbps << ltc_ltcs_ltss_cbc_base_alignment_shift_v();

	/* must be a multiple of 64KB */
	compbit_backing_size = roundup(compbit_backing_size, 64*1024);

	max_comptag_lines =
		(compbit_backing_size * comptags_per_cacheline) /
		cacheline_size * slices_per_fbp * gr->num_fbps;

	if (max_comptag_lines > hw_max_comptag_lines)
		max_comptag_lines = hw_max_comptag_lines;

	gk20a_dbg_info("compbit backing store size : %d",
		compbit_backing_size);
	gk20a_dbg_info("max comptag lines : %d",
		max_comptag_lines);

	if (IS_ENABLED(CONFIG_GK20A_PHYS_PAGE_TABLES))
		err = gk20a_ltc_alloc_phys_cbc(g, compbit_backing_size);
	else
		err = gk20a_ltc_alloc_virt_cbc(g, compbit_backing_size);

	if (err)
		return err;

	gk20a_allocator_init(&gr->comp_tags, "comptag",
			      1, /* start */
			      max_comptag_lines - 1, /* length*/
			      1); /* align */

	gr->comptags_per_cacheline = comptags_per_cacheline;
	gr->slices_per_fbp = slices_per_fbp;
	gr->cacheline_size = cacheline_size;

	return 0;
}

static int gk20a_ltc_cbc_ctrl(struct gk20a *g, enum gk20a_cbc_op op,
			      u32 min, u32 max)
{
	int err = 0;
	struct gr_gk20a *gr = &g->gr;
	u32 fbp, slice, ctrl1, val, hw_op = 0;
	unsigned long end_jiffies = jiffies +
		msecs_to_jiffies(gk20a_get_gr_idle_timeout(g));
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	u32 slices_per_fbp =
		ltc_ltcs_ltss_cbc_param_slices_per_fbp_v(
			gk20a_readl(g, ltc_ltcs_ltss_cbc_param_r()));

	gk20a_dbg_fn("");

	if (gr->compbit_store.size == 0)
		return 0;

	mutex_lock(&g->mm.l2_op_lock);

	if (op == gk20a_cbc_op_clear) {
		gk20a_writel(g, ltc_ltcs_ltss_cbc_ctrl2_r(),
			     ltc_ltcs_ltss_cbc_ctrl2_clear_lower_bound_f(min));
		gk20a_writel(g, ltc_ltcs_ltss_cbc_ctrl3_r(),
			     ltc_ltcs_ltss_cbc_ctrl3_clear_upper_bound_f(max));
		hw_op = ltc_ltcs_ltss_cbc_ctrl1_clear_active_f();
	} else if (op == gk20a_cbc_op_clean) {
		hw_op = ltc_ltcs_ltss_cbc_ctrl1_clean_active_f();
	} else if (op == gk20a_cbc_op_invalidate) {
		hw_op = ltc_ltcs_ltss_cbc_ctrl1_invalidate_active_f();
	} else {
		BUG_ON(1);
	}

	gk20a_writel(g, ltc_ltcs_ltss_cbc_ctrl1_r(),
		     gk20a_readl(g, ltc_ltcs_ltss_cbc_ctrl1_r()) | hw_op);

	for (fbp = 0; fbp < gr->num_fbps; fbp++) {
		for (slice = 0; slice < slices_per_fbp; slice++) {

			delay = GR_IDLE_CHECK_DEFAULT;

			ctrl1 = ltc_ltc0_lts0_cbc_ctrl1_r() +
				fbp * proj_ltc_stride_v() +
				slice * proj_lts_stride_v();

			do {
				val = gk20a_readl(g, ctrl1);
				if (!(val & hw_op))
					break;

				usleep_range(delay, delay * 2);
				delay = min_t(u32, delay << 1,
					GR_IDLE_CHECK_MAX);

			} while (time_before(jiffies, end_jiffies) ||
					!tegra_platform_is_silicon());

			if (!time_before(jiffies, end_jiffies)) {
				gk20a_err(dev_from_gk20a(g),
					   "comp tag clear timeout\n");
				err = -EBUSY;
				goto out;
			}
		}
	}
out:
	mutex_unlock(&g->mm.l2_op_lock);
	return 0;
}


#ifdef CONFIG_DEBUG_FS
static void gk20a_ltc_sync_debugfs(struct gk20a *g)
{
	u32 reg_f = ltc_ltcs_ltss_tstg_set_mgmt_2_l2_bypass_mode_enabled_f();

	spin_lock(&g->debugfs_lock);
	if (g->mm.ltc_enabled != g->mm.ltc_enabled_debug) {
		u32 reg = gk20a_readl(g, ltc_ltcs_ltss_tstg_set_mgmt_2_r());
		if (g->mm.ltc_enabled_debug)
			/* bypass disabled (normal caching ops)*/
			reg &= ~reg_f;
		else
			/* bypass enabled (no caching) */
			reg |= reg_f;

		gk20a_writel(g, ltc_ltcs_ltss_tstg_set_mgmt_2_r(), reg);
		g->mm.ltc_enabled = g->mm.ltc_enabled_debug;
	}
	spin_unlock(&g->debugfs_lock);
}
#endif

static void gk20a_ltc_init_fs_state(struct gk20a *g)
{
	gk20a_dbg_info("initialize gk20a L2");

	g->max_ltc_count = g->ltc_count = 1;
}

void gk20a_ltc_isr(struct gk20a *g)
{
	u32 intr;

	intr = gk20a_readl(g, ltc_ltc0_ltss_intr_r());
	gk20a_err(dev_from_gk20a(g), "ltc: %08x\n", intr);
	gk20a_writel(g, ltc_ltc0_ltss_intr_r(), intr);
}

/* Flushes the compression bit cache as well as "data".
 * Note: the name here is a bit of a misnomer.  ELPG uses this
 * internally... but ELPG doesn't have to be on to do it manually.
 */
static void gk20a_mm_g_elpg_flush_locked(struct gk20a *g)
{
	u32 data;
	s32 retry = 100;

	gk20a_dbg_fn("");

	/* Make sure all previous writes are committed to the L2. There's no
	   guarantee that writes are to DRAM. This will be a sysmembar internal
	   to the L2. */
	gk20a_writel(g, ltc_ltcs_ltss_g_elpg_r(),
		     ltc_ltcs_ltss_g_elpg_flush_pending_f());
	do {
		data = gk20a_readl(g, ltc_ltc0_ltss_g_elpg_r());

		if (ltc_ltc0_ltss_g_elpg_flush_v(data) ==
		    ltc_ltc0_ltss_g_elpg_flush_pending_v()) {
			gk20a_dbg_info("g_elpg_flush 0x%x", data);
			retry--;
			usleep_range(20, 40);
		} else
			break;
	} while (retry >= 0 || !tegra_platform_is_silicon());

	if (retry < 0)
		gk20a_warn(dev_from_gk20a(g),
			    "g_elpg_flush too many retries");

}

void gk20a_init_ltc(struct gpu_ops *gops)
{
	gops->ltc.determine_L2_size_bytes = gk20a_determine_L2_size_bytes;
	gops->ltc.set_max_ways_evict_last = gk20a_ltc_set_max_ways_evict_last;
	gops->ltc.init_comptags = gk20a_ltc_init_comptags;
	gops->ltc.cbc_ctrl = gk20a_ltc_cbc_ctrl;
	gops->ltc.set_zbc_color_entry = gk20a_ltc_set_zbc_color_entry;
	gops->ltc.set_zbc_depth_entry = gk20a_ltc_set_zbc_depth_entry;
	gops->ltc.clear_zbc_color_entry = gk20a_ltc_clear_zbc_color_entry;
	gops->ltc.clear_zbc_depth_entry = gk20a_ltc_clear_zbc_depth_entry;
	gops->ltc.init_zbc = gk20a_ltc_init_zbc;
	gops->ltc.init_cbc = gk20a_ltc_init_cbc;
#ifdef CONFIG_DEBUG_FS
	gops->ltc.sync_debugfs = gk20a_ltc_sync_debugfs;
#endif
	gops->ltc.elpg_flush = gk20a_mm_g_elpg_flush_locked;
	gops->ltc.init_fs_state = gk20a_ltc_init_fs_state;
	gops->ltc.isr = gk20a_ltc_isr;
}

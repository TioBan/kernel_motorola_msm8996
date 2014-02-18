/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Brad Volkin <bradley.d.volkin@intel.com>
 *
 */

#include "i915_drv.h"

/**
 * DOC: i915 batch buffer command parser
 *
 * Motivation:
 * Certain OpenGL features (e.g. transform feedback, performance monitoring)
 * require userspace code to submit batches containing commands such as
 * MI_LOAD_REGISTER_IMM to access various registers. Unfortunately, some
 * generations of the hardware will noop these commands in "unsecure" batches
 * (which includes all userspace batches submitted via i915) even though the
 * commands may be safe and represent the intended programming model of the
 * device.
 *
 * The software command parser is similar in operation to the command parsing
 * done in hardware for unsecure batches. However, the software parser allows
 * some operations that would be noop'd by hardware, if the parser determines
 * the operation is safe, and submits the batch as "secure" to prevent hardware
 * parsing.
 *
 * Threats:
 * At a high level, the hardware (and software) checks attempt to prevent
 * granting userspace undue privileges. There are three categories of privilege.
 *
 * First, commands which are explicitly defined as privileged or which should
 * only be used by the kernel driver. The parser generally rejects such
 * commands, though it may allow some from the drm master process.
 *
 * Second, commands which access registers. To support correct/enhanced
 * userspace functionality, particularly certain OpenGL extensions, the parser
 * provides a whitelist of registers which userspace may safely access (for both
 * normal and drm master processes).
 *
 * Third, commands which access privileged memory (i.e. GGTT, HWS page, etc).
 * The parser always rejects such commands.
 *
 * The majority of the problematic commands fall in the MI_* range, with only a
 * few specific commands on each ring (e.g. PIPE_CONTROL and MI_FLUSH_DW).
 *
 * Implementation:
 * Each ring maintains tables of commands and registers which the parser uses in
 * scanning batch buffers submitted to that ring.
 *
 * Since the set of commands that the parser must check for is significantly
 * smaller than the number of commands supported, the parser tables contain only
 * those commands required by the parser. This generally works because command
 * opcode ranges have standard command length encodings. So for commands that
 * the parser does not need to check, it can easily skip them. This is
 * implementated via a per-ring length decoding vfunc.
 *
 * Unfortunately, there are a number of commands that do not follow the standard
 * length encoding for their opcode range, primarily amongst the MI_* commands.
 * To handle this, the parser provides a way to define explicit "skip" entries
 * in the per-ring command tables.
 *
 * Other command table entries map fairly directly to high level categories
 * mentioned above: rejected, master-only, register whitelist. The parser
 * implements a number of checks, including the privileged memory checks, via a
 * general bitmasking mechanism.
 */

#define STD_MI_OPCODE_MASK  0xFF800000
#define STD_3D_OPCODE_MASK  0xFFFF0000
#define STD_2D_OPCODE_MASK  0xFFC00000
#define STD_MFX_OPCODE_MASK 0xFFFF0000

#define CMD(op, opm, f, lm, fl, ...)				\
	{							\
		.flags = (fl) | ((f) ? CMD_DESC_FIXED : 0),	\
		.cmd = { (op), (opm) }, 			\
		.length = { (lm) },				\
		__VA_ARGS__					\
	}

/* Convenience macros to compress the tables */
#define SMI STD_MI_OPCODE_MASK
#define S3D STD_3D_OPCODE_MASK
#define S2D STD_2D_OPCODE_MASK
#define SMFX STD_MFX_OPCODE_MASK
#define F true
#define S CMD_DESC_SKIP
#define R CMD_DESC_REJECT
#define W CMD_DESC_REGISTER
#define B CMD_DESC_BITMASK
#define M CMD_DESC_MASTER

/*            Command                          Mask   Fixed Len   Action
	      ---------------------------------------------------------- */
static const struct drm_i915_cmd_descriptor common_cmds[] = {
	CMD(  MI_NOOP,                          SMI,    F,  1,      S  ),
	CMD(  MI_USER_INTERRUPT,                SMI,    F,  1,      R  ),
	CMD(  MI_WAIT_FOR_EVENT,                SMI,    F,  1,      M  ),
	CMD(  MI_ARB_CHECK,                     SMI,    F,  1,      S  ),
	CMD(  MI_REPORT_HEAD,                   SMI,    F,  1,      S  ),
	CMD(  MI_SUSPEND_FLUSH,                 SMI,    F,  1,      S  ),
	CMD(  MI_SEMAPHORE_MBOX,                SMI,   !F,  0xFF,   R  ),
	CMD(  MI_STORE_DWORD_INDEX,             SMI,   !F,  0xFF,   R  ),
	CMD(  MI_LOAD_REGISTER_IMM(1),          SMI,   !F,  0xFF,   W,
	      .reg = { .offset = 1, .mask = 0x007FFFFC }               ),
	CMD(  MI_STORE_REGISTER_MEM(1),         SMI,   !F,  0xFF,   W | B,
	      .reg = { .offset = 1, .mask = 0x007FFFFC },
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_LOAD_REGISTER_MEM,             SMI,   !F,  0xFF,   W | B,
	      .reg = { .offset = 1, .mask = 0x007FFFFC },
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_BATCH_BUFFER_START,            SMI,   !F,  0xFF,   S  ),
};

static const struct drm_i915_cmd_descriptor render_cmds[] = {
	CMD(  MI_FLUSH,                         SMI,    F,  1,      S  ),
	CMD(  MI_ARB_ON_OFF,                    SMI,    F,  1,      R  ),
	CMD(  MI_PREDICATE,                     SMI,    F,  1,      S  ),
	CMD(  MI_TOPOLOGY_FILTER,               SMI,    F,  1,      S  ),
	CMD(  MI_DISPLAY_FLIP,                  SMI,   !F,  0xFF,   R  ),
	CMD(  MI_SET_CONTEXT,                   SMI,   !F,  0xFF,   R  ),
	CMD(  MI_URB_CLEAR,                     SMI,   !F,  0xFF,   S  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0xFF,   R  ),
	CMD(  MI_CLFLUSH,                       SMI,   !F,  0x3FF,  B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_REPORT_PERF_COUNT,             SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 1,
			.mask = MI_REPORT_PERF_COUNT_GGTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_CONDITIONAL_BATCH_BUFFER_END,  SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  GFX_OP_3DSTATE_VF_STATISTICS,     S3D,    F,  1,      S  ),
	CMD(  PIPELINE_SELECT,                  S3D,    F,  1,      S  ),
	CMD(  MEDIA_VFE_STATE,			S3D,   !F,  0xFFFF, B,
	      .bits = {{
			.offset = 2,
			.mask = MEDIA_VFE_STATE_MMIO_ACCESS_MASK,
			.expected = 0,
	      }},						       ),
	CMD(  GPGPU_OBJECT,                     S3D,   !F,  0xFF,   S  ),
	CMD(  GPGPU_WALKER,                     S3D,   !F,  0xFF,   S  ),
	CMD(  GFX_OP_3DSTATE_SO_DECL_LIST,      S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_PIPE_CONTROL(5),           S3D,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 1,
			.mask = (PIPE_CONTROL_MMIO_WRITE | PIPE_CONTROL_NOTIFY),
			.expected = 0,
	      },
	      {
			.offset = 1,
		        .mask = PIPE_CONTROL_GLOBAL_GTT_IVB,
			.expected = 0,
			.condition_offset = 1,
			.condition_mask = PIPE_CONTROL_POST_SYNC_OP_MASK,
	      }},						       ),
};

static const struct drm_i915_cmd_descriptor hsw_render_cmds[] = {
	CMD(  MI_SET_PREDICATE,                 SMI,    F,  1,      S  ),
	CMD(  MI_RS_CONTROL,                    SMI,    F,  1,      S  ),
	CMD(  MI_URB_ATOMIC_ALLOC,              SMI,    F,  1,      S  ),
	CMD(  MI_RS_CONTEXT,                    SMI,    F,  1,      S  ),
	CMD(  MI_LOAD_SCAN_LINES_INCL,          SMI,   !F,  0x3F,   M  ),
	CMD(  MI_LOAD_SCAN_LINES_EXCL,          SMI,   !F,  0x3F,   R  ),
	CMD(  MI_LOAD_REGISTER_REG,             SMI,   !F,  0xFF,   R  ),
	CMD(  MI_RS_STORE_DATA_IMM,             SMI,   !F,  0xFF,   S  ),
	CMD(  MI_LOAD_URB_MEM,                  SMI,   !F,  0xFF,   S  ),
	CMD(  MI_STORE_URB_MEM,                 SMI,   !F,  0xFF,   S  ),
	CMD(  GFX_OP_3DSTATE_DX9_CONSTANTF_VS,  S3D,   !F,  0x7FF,  S  ),
	CMD(  GFX_OP_3DSTATE_DX9_CONSTANTF_PS,  S3D,   !F,  0x7FF,  S  ),

	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_VS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_GS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_HS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_DS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_PS,  S3D,   !F,  0x1FF,  S  ),
};

static const struct drm_i915_cmd_descriptor video_cmds[] = {
	CMD(  MI_ARB_ON_OFF,                    SMI,    F,  1,      R  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0x3F,   R  ),
	CMD(  MI_FLUSH_DW,                      SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_FLUSH_DW_NOTIFY,
			.expected = 0,
	      },
	      {
			.offset = 1,
			.mask = MI_FLUSH_DW_USE_GTT,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      }},						       ),
	CMD(  MI_CONDITIONAL_BATCH_BUFFER_END,  SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	/*
	 * MFX_WAIT doesn't fit the way we handle length for most commands.
	 * It has a length field but it uses a non-standard length bias.
	 * It is always 1 dword though, so just treat it as fixed length.
	 */
	CMD(  MFX_WAIT,                         SMFX,   F,  1,      S  ),
};

static const struct drm_i915_cmd_descriptor vecs_cmds[] = {
	CMD(  MI_ARB_ON_OFF,                    SMI,    F,  1,      R  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0x3F,   R  ),
	CMD(  MI_FLUSH_DW,                      SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_FLUSH_DW_NOTIFY,
			.expected = 0,
	      },
	      {
			.offset = 1,
			.mask = MI_FLUSH_DW_USE_GTT,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      }},						       ),
	CMD(  MI_CONDITIONAL_BATCH_BUFFER_END,  SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
};

static const struct drm_i915_cmd_descriptor blt_cmds[] = {
	CMD(  MI_DISPLAY_FLIP,                  SMI,   !F,  0xFF,   R  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0x3FF,  B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0x3F,   R  ),
	CMD(  MI_FLUSH_DW,                      SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_FLUSH_DW_NOTIFY,
			.expected = 0,
	      },
	      {
			.offset = 1,
			.mask = MI_FLUSH_DW_USE_GTT,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      }},						       ),
	CMD(  COLOR_BLT,                        S2D,   !F,  0x3F,   S  ),
	CMD(  SRC_COPY_BLT,                     S2D,   !F,  0x3F,   S  ),
};

static const struct drm_i915_cmd_descriptor hsw_blt_cmds[] = {
	CMD(  MI_LOAD_SCAN_LINES_INCL,          SMI,   !F,  0x3F,   M  ),
	CMD(  MI_LOAD_SCAN_LINES_EXCL,          SMI,   !F,  0x3F,   R  ),
};

#undef CMD
#undef SMI
#undef S3D
#undef S2D
#undef SMFX
#undef F
#undef S
#undef R
#undef W
#undef B
#undef M

static const struct drm_i915_cmd_table gen7_render_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ render_cmds, ARRAY_SIZE(render_cmds) },
};

static const struct drm_i915_cmd_table hsw_render_ring_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ render_cmds, ARRAY_SIZE(render_cmds) },
	{ hsw_render_cmds, ARRAY_SIZE(hsw_render_cmds) },
};

static const struct drm_i915_cmd_table gen7_video_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ video_cmds, ARRAY_SIZE(video_cmds) },
};

static const struct drm_i915_cmd_table hsw_vebox_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ vecs_cmds, ARRAY_SIZE(vecs_cmds) },
};

static const struct drm_i915_cmd_table gen7_blt_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ blt_cmds, ARRAY_SIZE(blt_cmds) },
};

static const struct drm_i915_cmd_table hsw_blt_ring_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ blt_cmds, ARRAY_SIZE(blt_cmds) },
	{ hsw_blt_cmds, ARRAY_SIZE(hsw_blt_cmds) },
};

/*
 * Register whitelists, sorted by increasing register offset.
 *
 * Some registers that userspace accesses are 64 bits. The register
 * access commands only allow 32-bit accesses. Hence, we have to include
 * entries for both halves of the 64-bit registers.
 */

/* Convenience macro for adding 64-bit registers */
#define REG64(addr) (addr), (addr + sizeof(u32))

static const u32 gen7_render_regs[] = {
	REG64(HS_INVOCATION_COUNT),
	REG64(DS_INVOCATION_COUNT),
	REG64(IA_VERTICES_COUNT),
	REG64(IA_PRIMITIVES_COUNT),
	REG64(VS_INVOCATION_COUNT),
	REG64(GS_INVOCATION_COUNT),
	REG64(GS_PRIMITIVES_COUNT),
	REG64(CL_INVOCATION_COUNT),
	REG64(CL_PRIMITIVES_COUNT),
	REG64(PS_INVOCATION_COUNT),
	REG64(PS_DEPTH_COUNT),
	REG64(GEN7_SO_NUM_PRIMS_WRITTEN(0)),
	REG64(GEN7_SO_NUM_PRIMS_WRITTEN(1)),
	REG64(GEN7_SO_NUM_PRIMS_WRITTEN(2)),
	REG64(GEN7_SO_NUM_PRIMS_WRITTEN(3)),
	GEN7_SO_WRITE_OFFSET(0),
	GEN7_SO_WRITE_OFFSET(1),
	GEN7_SO_WRITE_OFFSET(2),
	GEN7_SO_WRITE_OFFSET(3),
};

static const u32 gen7_blt_regs[] = {
	BCS_SWCTRL,
};

static const u32 ivb_master_regs[] = {
	FORCEWAKE_MT,
	DERRMR,
	GEN7_PIPE_DE_LOAD_SL(PIPE_A),
	GEN7_PIPE_DE_LOAD_SL(PIPE_B),
	GEN7_PIPE_DE_LOAD_SL(PIPE_C),
};

static const u32 hsw_master_regs[] = {
	FORCEWAKE_MT,
	DERRMR,
};

#undef REG64

static u32 gen7_render_get_cmd_length_mask(u32 cmd_header)
{
	u32 client = (cmd_header & INSTR_CLIENT_MASK) >> INSTR_CLIENT_SHIFT;
	u32 subclient =
		(cmd_header & INSTR_SUBCLIENT_MASK) >> INSTR_SUBCLIENT_SHIFT;

	if (client == INSTR_MI_CLIENT)
		return 0x3F;
	else if (client == INSTR_RC_CLIENT) {
		if (subclient == INSTR_MEDIA_SUBCLIENT)
			return 0xFFFF;
		else
			return 0xFF;
	}

	DRM_DEBUG_DRIVER("CMD: Abnormal rcs cmd length! 0x%08X\n", cmd_header);
	return 0;
}

static u32 gen7_bsd_get_cmd_length_mask(u32 cmd_header)
{
	u32 client = (cmd_header & INSTR_CLIENT_MASK) >> INSTR_CLIENT_SHIFT;
	u32 subclient =
		(cmd_header & INSTR_SUBCLIENT_MASK) >> INSTR_SUBCLIENT_SHIFT;

	if (client == INSTR_MI_CLIENT)
		return 0x3F;
	else if (client == INSTR_RC_CLIENT) {
		if (subclient == INSTR_MEDIA_SUBCLIENT)
			return 0xFFF;
		else
			return 0xFF;
	}

	DRM_DEBUG_DRIVER("CMD: Abnormal bsd cmd length! 0x%08X\n", cmd_header);
	return 0;
}

static u32 gen7_blt_get_cmd_length_mask(u32 cmd_header)
{
	u32 client = (cmd_header & INSTR_CLIENT_MASK) >> INSTR_CLIENT_SHIFT;

	if (client == INSTR_MI_CLIENT)
		return 0x3F;
	else if (client == INSTR_BC_CLIENT)
		return 0xFF;

	DRM_DEBUG_DRIVER("CMD: Abnormal blt cmd length! 0x%08X\n", cmd_header);
	return 0;
}

static void validate_cmds_sorted(struct intel_ring_buffer *ring)
{
	int i;

	if (!ring->cmd_tables || ring->cmd_table_count == 0)
		return;

	for (i = 0; i < ring->cmd_table_count; i++) {
		const struct drm_i915_cmd_table *table = &ring->cmd_tables[i];
		u32 previous = 0;
		int j;

		for (j = 0; j < table->count; j++) {
			const struct drm_i915_cmd_descriptor *desc =
				&table->table[i];
			u32 curr = desc->cmd.value & desc->cmd.mask;

			if (curr < previous)
				DRM_ERROR("CMD: table not sorted ring=%d table=%d entry=%d cmd=0x%08X prev=0x%08X\n",
					  ring->id, i, j, curr, previous);

			previous = curr;
		}
	}
}

static void check_sorted(int ring_id, const u32 *reg_table, int reg_count)
{
	int i;
	u32 previous = 0;

	for (i = 0; i < reg_count; i++) {
		u32 curr = reg_table[i];

		if (curr < previous)
			DRM_ERROR("CMD: table not sorted ring=%d entry=%d reg=0x%08X prev=0x%08X\n",
				  ring_id, i, curr, previous);

		previous = curr;
	}
}

static void validate_regs_sorted(struct intel_ring_buffer *ring)
{
	check_sorted(ring->id, ring->reg_table, ring->reg_count);
	check_sorted(ring->id, ring->master_reg_table, ring->master_reg_count);
}

/**
 * i915_cmd_parser_init_ring() - set cmd parser related fields for a ringbuffer
 * @ring: the ringbuffer to initialize
 *
 * Optionally initializes fields related to batch buffer command parsing in the
 * struct intel_ring_buffer based on whether the platform requires software
 * command parsing.
 */
void i915_cmd_parser_init_ring(struct intel_ring_buffer *ring)
{
	if (!IS_GEN7(ring->dev))
		return;

	switch (ring->id) {
	case RCS:
		if (IS_HASWELL(ring->dev)) {
			ring->cmd_tables = hsw_render_ring_cmds;
			ring->cmd_table_count =
				ARRAY_SIZE(hsw_render_ring_cmds);
		} else {
			ring->cmd_tables = gen7_render_cmds;
			ring->cmd_table_count = ARRAY_SIZE(gen7_render_cmds);
		}

		ring->reg_table = gen7_render_regs;
		ring->reg_count = ARRAY_SIZE(gen7_render_regs);

		if (IS_HASWELL(ring->dev)) {
			ring->master_reg_table = hsw_master_regs;
			ring->master_reg_count = ARRAY_SIZE(hsw_master_regs);
		} else {
			ring->master_reg_table = ivb_master_regs;
			ring->master_reg_count = ARRAY_SIZE(ivb_master_regs);
		}

		ring->get_cmd_length_mask = gen7_render_get_cmd_length_mask;
		break;
	case VCS:
		ring->cmd_tables = gen7_video_cmds;
		ring->cmd_table_count = ARRAY_SIZE(gen7_video_cmds);
		ring->get_cmd_length_mask = gen7_bsd_get_cmd_length_mask;
		break;
	case BCS:
		if (IS_HASWELL(ring->dev)) {
			ring->cmd_tables = hsw_blt_ring_cmds;
			ring->cmd_table_count = ARRAY_SIZE(hsw_blt_ring_cmds);
		} else {
			ring->cmd_tables = gen7_blt_cmds;
			ring->cmd_table_count = ARRAY_SIZE(gen7_blt_cmds);
		}

		ring->reg_table = gen7_blt_regs;
		ring->reg_count = ARRAY_SIZE(gen7_blt_regs);

		if (IS_HASWELL(ring->dev)) {
			ring->master_reg_table = hsw_master_regs;
			ring->master_reg_count = ARRAY_SIZE(hsw_master_regs);
		} else {
			ring->master_reg_table = ivb_master_regs;
			ring->master_reg_count = ARRAY_SIZE(ivb_master_regs);
		}

		ring->get_cmd_length_mask = gen7_blt_get_cmd_length_mask;
		break;
	case VECS:
		ring->cmd_tables = hsw_vebox_cmds;
		ring->cmd_table_count = ARRAY_SIZE(hsw_vebox_cmds);
		/* VECS can use the same length_mask function as VCS */
		ring->get_cmd_length_mask = gen7_bsd_get_cmd_length_mask;
		break;
	default:
		DRM_ERROR("CMD: cmd_parser_init with unknown ring: %d\n",
			  ring->id);
		BUG();
	}

	validate_cmds_sorted(ring);
	validate_regs_sorted(ring);
}

static const struct drm_i915_cmd_descriptor*
find_cmd_in_table(const struct drm_i915_cmd_table *table,
		  u32 cmd_header)
{
	int i;

	for (i = 0; i < table->count; i++) {
		const struct drm_i915_cmd_descriptor *desc = &table->table[i];
		u32 masked_cmd = desc->cmd.mask & cmd_header;
		u32 masked_value = desc->cmd.value & desc->cmd.mask;

		if (masked_cmd == masked_value)
			return desc;
	}

	return NULL;
}

/*
 * Returns a pointer to a descriptor for the command specified by cmd_header.
 *
 * The caller must supply space for a default descriptor via the default_desc
 * parameter. If no descriptor for the specified command exists in the ring's
 * command parser tables, this function fills in default_desc based on the
 * ring's default length encoding and returns default_desc.
 */
static const struct drm_i915_cmd_descriptor*
find_cmd(struct intel_ring_buffer *ring,
	 u32 cmd_header,
	 struct drm_i915_cmd_descriptor *default_desc)
{
	u32 mask;
	int i;

	for (i = 0; i < ring->cmd_table_count; i++) {
		const struct drm_i915_cmd_descriptor *desc;

		desc = find_cmd_in_table(&ring->cmd_tables[i], cmd_header);
		if (desc)
			return desc;
	}

	mask = ring->get_cmd_length_mask(cmd_header);
	if (!mask)
		return NULL;

	BUG_ON(!default_desc);
	default_desc->flags = CMD_DESC_SKIP;
	default_desc->length.mask = mask;

	return default_desc;
}

static bool valid_reg(const u32 *table, int count, u32 addr)
{
	if (table && count != 0) {
		int i;

		for (i = 0; i < count; i++) {
			if (table[i] == addr)
				return true;
		}
	}

	return false;
}

static u32 *vmap_batch(struct drm_i915_gem_object *obj)
{
	int i;
	void *addr = NULL;
	struct sg_page_iter sg_iter;
	struct page **pages;

	pages = drm_malloc_ab(obj->base.size >> PAGE_SHIFT, sizeof(*pages));
	if (pages == NULL) {
		DRM_DEBUG_DRIVER("Failed to get space for pages\n");
		goto finish;
	}

	i = 0;
	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents, 0) {
		pages[i] = sg_page_iter_page(&sg_iter);
		i++;
	}

	addr = vmap(pages, i, 0, PAGE_KERNEL);
	if (addr == NULL) {
		DRM_DEBUG_DRIVER("Failed to vmap pages\n");
		goto finish;
	}

finish:
	if (pages)
		drm_free_large(pages);
	return (u32*)addr;
}

/**
 * i915_needs_cmd_parser() - should a given ring use software command parsing?
 * @ring: the ring in question
 *
 * Only certain platforms require software batch buffer command parsing, and
 * only when enabled via module paramter.
 *
 * Return: true if the ring requires software command parsing
 */
bool i915_needs_cmd_parser(struct intel_ring_buffer *ring)
{
	drm_i915_private_t *dev_priv = ring->dev->dev_private;

	/* No command tables indicates a platform without parsing */
	if (!ring->cmd_tables)
		return false;

	/*
	 * XXX: VLV is Gen7 and therefore has cmd_tables, but has PPGTT
	 * disabled. That will cause all of the parser's PPGTT checks to
	 * fail. For now, disable parsing when PPGTT is off.
	 */
	if (!dev_priv->mm.aliasing_ppgtt)
		return false;

	return (i915.enable_cmd_parser == 1);
}

#define LENGTH_BIAS 2

/**
 * i915_parse_cmds() - parse a submitted batch buffer for privilege violations
 * @ring: the ring on which the batch is to execute
 * @batch_obj: the batch buffer in question
 * @batch_start_offset: byte offset in the batch at which execution starts
 * @is_master: is the submitting process the drm master?
 *
 * Parses the specified batch buffer looking for privilege violations as
 * described in the overview.
 *
 * Return: non-zero if the parser finds violations or otherwise fails
 */
int i915_parse_cmds(struct intel_ring_buffer *ring,
		    struct drm_i915_gem_object *batch_obj,
		    u32 batch_start_offset,
		    bool is_master)
{
	int ret = 0;
	u32 *cmd, *batch_base, *batch_end;
	struct drm_i915_cmd_descriptor default_desc = { 0 };
	int needs_clflush = 0;

	ret = i915_gem_obj_prepare_shmem_read(batch_obj, &needs_clflush);
	if (ret) {
		DRM_DEBUG_DRIVER("CMD: failed to prep read\n");
		return ret;
	}

	batch_base = vmap_batch(batch_obj);
	if (!batch_base) {
		DRM_DEBUG_DRIVER("CMD: Failed to vmap batch\n");
		i915_gem_object_unpin_pages(batch_obj);
		return -ENOMEM;
	}

	if (needs_clflush)
		drm_clflush_virt_range((char *)batch_base, batch_obj->base.size);

	cmd = batch_base + (batch_start_offset / sizeof(*cmd));
	batch_end = cmd + (batch_obj->base.size / sizeof(*batch_end));

	while (cmd < batch_end) {
		const struct drm_i915_cmd_descriptor *desc;
		u32 length;

		if (*cmd == MI_BATCH_BUFFER_END)
			break;

		desc = find_cmd(ring, *cmd, &default_desc);
		if (!desc) {
			DRM_DEBUG_DRIVER("CMD: Unrecognized command: 0x%08X\n",
					 *cmd);
			ret = -EINVAL;
			break;
		}

		if (desc->flags & CMD_DESC_FIXED)
			length = desc->length.fixed;
		else
			length = ((*cmd & desc->length.mask) + LENGTH_BIAS);

		if ((batch_end - cmd) < length) {
			DRM_DEBUG_DRIVER("CMD: Command length exceeds batch length: 0x%08X length=%d batchlen=%td\n",
					 *cmd,
					 length,
					 batch_end - cmd);
			ret = -EINVAL;
			break;
		}

		if (desc->flags & CMD_DESC_REJECT) {
			DRM_DEBUG_DRIVER("CMD: Rejected command: 0x%08X\n", *cmd);
			ret = -EINVAL;
			break;
		}

		if ((desc->flags & CMD_DESC_MASTER) && !is_master) {
			DRM_DEBUG_DRIVER("CMD: Rejected master-only command: 0x%08X\n",
					 *cmd);
			ret = -EINVAL;
			break;
		}

		if (desc->flags & CMD_DESC_REGISTER) {
			u32 reg_addr = cmd[desc->reg.offset] & desc->reg.mask;

			if (!valid_reg(ring->reg_table,
				       ring->reg_count, reg_addr)) {
				if (!is_master ||
				    !valid_reg(ring->master_reg_table,
					       ring->master_reg_count,
					       reg_addr)) {
					DRM_DEBUG_DRIVER("CMD: Rejected register 0x%08X in command: 0x%08X (ring=%d)\n",
							 reg_addr,
							 *cmd,
							 ring->id);
					ret = -EINVAL;
					break;
				}
			}
		}

		if (desc->flags & CMD_DESC_BITMASK) {
			int i;

			for (i = 0; i < MAX_CMD_DESC_BITMASKS; i++) {
				u32 dword;

				if (desc->bits[i].mask == 0)
					break;

				if (desc->bits[i].condition_mask != 0) {
					u32 offset =
						desc->bits[i].condition_offset;
					u32 condition = cmd[offset] &
						desc->bits[i].condition_mask;

					if (condition == 0)
						continue;
				}

				dword = cmd[desc->bits[i].offset] &
					desc->bits[i].mask;

				if (dword != desc->bits[i].expected) {
					DRM_DEBUG_DRIVER("CMD: Rejected command 0x%08X for bitmask 0x%08X (exp=0x%08X act=0x%08X) (ring=%d)\n",
							 *cmd,
							 desc->bits[i].mask,
							 desc->bits[i].expected,
							 dword, ring->id);
					ret = -EINVAL;
					break;
				}
			}

			if (ret)
				break;
		}

		cmd += length;
	}

	if (cmd >= batch_end) {
		DRM_DEBUG_DRIVER("CMD: Got to the end of the buffer w/o a BBE cmd!\n");
		ret = -EINVAL;
	}

	vunmap(batch_base);

	i915_gem_object_unpin_pages(batch_obj);

	return ret;
}

/*
 * Copyright (C) 2011 Google, Inc
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/msm_ion.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <soc/qcom/scm.h>


static struct rb_root secure_root;
DEFINE_MUTEX(secure_buffer_mutex);

struct secure_meta {
	struct rb_node node;
	struct sg_table *table;
	struct kref ref;
	enum cp_mem_usage usage;
};

struct cp2_mem_chunks {
	u32 chunk_list;
	u32 chunk_list_size;
	u32 chunk_size;
} __attribute__ ((__packed__));

struct cp2_lock_req {
	struct cp2_mem_chunks chunks;
	u32 mem_usage;
	u32 lock;
} __attribute__ ((__packed__));

#define MEM_PROTECT_LOCK_ID2		0x0A
#define MEM_PROTECT_LOCK_ID2_FLAT	0x11
#define V2_CHUNK_SIZE		SZ_1M
#define FEATURE_ID_CP 12

static void secure_meta_add(struct secure_meta *meta)
{
	struct rb_root *root = &secure_root;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct secure_meta *entry;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct secure_meta, node);

		if (meta->table < entry->table) {
			p = &(*p)->rb_left;
		} else if (meta->table > entry->table) {
			p = &(*p)->rb_right;
		} else {
			pr_err("%s: table %p already exists\n", __func__,
				entry->table);
			BUG();
		}
	}

	rb_link_node(&meta->node, parent, p);
	rb_insert_color(&meta->node, root);
}


static struct secure_meta *secure_meta_lookup(struct sg_table *table)
{
	struct rb_root *root = &secure_root;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct secure_meta *entry = NULL;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct secure_meta, node);

		if (table < entry->table)
			p = &(*p)->rb_left;
		else if (table > entry->table)
			p = &(*p)->rb_right;
		else
			return entry;
	}

	return NULL;
}


static int secure_buffer_change_chunk(u32 chunks,
				u32 nchunks,
				u32 chunk_size,
				enum cp_mem_usage usage,
				int lock)
{
	struct cp2_lock_req request;
	u32 resp;
	int ret;
	struct scm_desc desc = {0};

	desc.args[0] = request.chunks.chunk_list = chunks;
	desc.args[1] = request.chunks.chunk_list_size = nchunks;
	desc.args[2] = request.chunks.chunk_size = chunk_size;
	desc.args[3] = request.mem_usage = usage;
	desc.args[4] = request.lock = lock;
	desc.args[5] = 0;
	desc.arginfo = SCM_ARGS(6, SCM_RW, SCM_VAL, SCM_VAL, SCM_VAL, SCM_VAL,
				SCM_VAL);

	kmap_flush_unused();
	kmap_atomic_flush_unused();

	if (!is_scm_armv8()) {
		ret = scm_call(SCM_SVC_MP, MEM_PROTECT_LOCK_ID2,
				&request, sizeof(request), &resp, sizeof(resp));
	} else {
		ret = scm_call2(SCM_SIP_FNID(SCM_SVC_MP,
				MEM_PROTECT_LOCK_ID2_FLAT), &desc);
		resp = desc.ret[0];
	}

	return ret;
}



static int secure_buffer_change_table(struct sg_table *table,
				enum cp_mem_usage usage,
				int lock)
{
	int i, j;
	int ret = -EINVAL;
	u32 *chunk_list;
	struct scatterlist *sg;

	for_each_sg(table->sgl, sg, table->nents, i) {
		int nchunks;
		int size = sg->length;
		int chunk_list_len;
		phys_addr_t chunk_list_phys;

		/*
		 * This should theoretically be a phys_addr_t but the protocol
		 * indicates this should be a u32.
		 */
		u32 base;
		u64 tmp = sg_dma_address(sg);
		WARN((tmp >> 32) & 0xffffffff,
			"%s: there are ones in the upper 32 bits of the sg at %p! They will be truncated! Address: 0x%llx\n",
			__func__, sg, tmp);
		if (unlikely(!size || (size % V2_CHUNK_SIZE))) {
			WARN(1,
				"%s: chunk %d has invalid size: 0x%x. Must be a multiple of 0x%x\n",
				__func__, i, size, V2_CHUNK_SIZE);
			return -EINVAL;
		}

		base = (u32)tmp;

		nchunks = size / V2_CHUNK_SIZE;
		chunk_list_len = sizeof(u32)*nchunks;

		chunk_list = kzalloc(chunk_list_len, GFP_KERNEL);

		if (!chunk_list)
			return -ENOMEM;

		chunk_list_phys = virt_to_phys(chunk_list);
		for (j = 0; j < nchunks; j++)
			chunk_list[j] = base + j * V2_CHUNK_SIZE;

		/*
		 * Flush the chunk list before sending the memory to the
		 * secure environment to ensure the data is actually present
		 * in RAM
		 */
		dmac_flush_range(chunk_list, chunk_list + chunk_list_len);

		ret = secure_buffer_change_chunk(virt_to_phys(chunk_list),
				nchunks, V2_CHUNK_SIZE, usage, lock);

		kfree(chunk_list);
	}

	return ret;
}

int msm_ion_secure_table(struct sg_table *table, enum cp_mem_usage usage,
			int flags)
{
	struct secure_meta *meta;
	int ret;

	mutex_lock(&secure_buffer_mutex);
	meta = secure_meta_lookup(table);

	if (meta) {
		kref_get(&meta->ref);
		ret = 0;
	} else {
		meta = kzalloc(sizeof(*meta), GFP_KERNEL);

		if (!meta) {
			ret = -ENOMEM;
			goto out;
		}

		meta->table = table;
		meta->usage = usage;
		kref_init(&meta->ref);

		ret = secure_buffer_change_table(table, usage, 1);
		if (!ret)
			secure_meta_add(meta);
		else
			kfree(meta);
	}
out:
	mutex_unlock(&secure_buffer_mutex);

	return ret;

}

int msm_ion_secure_buffer(struct ion_client *client, struct ion_handle *handle,
			enum cp_mem_usage usage, int flags)
{
	struct sg_table *table;
	int ret;

	table = ion_sg_table(client, handle);

	if (IS_ERR_OR_NULL(table)) {
		ret = -EINVAL;
		goto out;
	}

	ret = msm_ion_secure_table(table, usage, flags);
out:
	return ret;
}
EXPORT_SYMBOL(msm_ion_secure_buffer);

static void msm_secure_buffer_release(struct kref *kref)
{
	struct secure_meta *meta = container_of(kref, struct secure_meta,
						ref);

	rb_erase(&meta->node, &secure_root);
	secure_buffer_change_table(meta->table, meta->usage, 0);
	kfree(meta);
}

int msm_ion_unsecure_table(struct sg_table *table)
{
	struct secure_meta *meta;
	int ret = 0;

	mutex_lock(&secure_buffer_mutex);
	meta = secure_meta_lookup(table);

	if (!meta) {
		ret = -EINVAL;
		goto out;
	}

	kref_put(&meta->ref, msm_secure_buffer_release);

out:
	mutex_unlock(&secure_buffer_mutex);
	return ret;

}

int msm_ion_unsecure_buffer(struct ion_client *client,
			    struct ion_handle *handle)
{
	struct sg_table *table;
	int ret = 0;

	table = ion_sg_table(client, handle);

	if (IS_ERR_OR_NULL(table)) {
		WARN(1, "Could not get table for handle %p to unsecure\n",
			handle);
		ret = -EINVAL;
		goto out;
	}

	msm_ion_unsecure_table(table);

out:
	return ret;
}
EXPORT_SYMBOL(msm_ion_unsecure_buffer);

#define MAKE_CP_VERSION(major, minor, patch) \
	(((major & 0x3FF) << 22) | ((minor & 0x3FF) << 12) | (patch & 0xFFF))

bool msm_secure_v2_is_supported(void)
{
	int version = scm_get_feat_version(FEATURE_ID_CP);

	/*
	 * if the version is < 1.1.0 then dynamic buffer allocation is
	 * not supported
	 */
	return version >= MAKE_CP_VERSION(1, 1, 0);
}

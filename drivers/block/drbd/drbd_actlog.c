/*
   drbd_actlog.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2003-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 2003-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2003-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include <linux/slab.h>
#include <linux/crc32c.h>
#include <linux/drbd.h>
#include <linux/drbd_limits.h>
#include <linux/dynamic_debug.h>
#include "drbd_int.h"
#include "drbd_wrappers.h"

/* all fields on disc in big endian */
struct __packed al_transaction_on_disk {
	/* don't we all like magic */
	__be32	magic;

	/* to identify the most recent transaction block
	 * in the on disk ring buffer */
	__be32	tr_number;

	/* checksum on the full 4k block, with this field set to 0. */
	__be32	crc32c;

	/* type of transaction, special transaction types like:
	 * purge-all, set-all-idle, set-all-active, ... to-be-defined */
	__be16	transaction_type;

	/* we currently allow only a few thousand extents,
	 * so 16bit will be enough for the slot number. */

	/* how many updates in this transaction */
	__be16	n_updates;

	/* maximum slot number, "al-extents" in drbd.conf speak.
	 * Having this in each transaction should make reconfiguration
	 * of that parameter easier. */
	__be16	context_size;

	/* slot number the context starts with */
	__be16	context_start_slot_nr;

	/* Some reserved bytes.  Expected usage is a 64bit counter of
	 * sectors-written since device creation, and other data generation tag
	 * supporting usage */
	__be32	__reserved[4];

	/* --- 36 byte used --- */

	/* Reserve space for up to AL_UPDATES_PER_TRANSACTION changes
	 * in one transaction, then use the remaining byte in the 4k block for
	 * context information.  "Flexible" number of updates per transaction
	 * does not help, as we have to account for the case when all update
	 * slots are used anyways, so it would only complicate code without
	 * additional benefit.
	 */
	__be16	update_slot_nr[AL_UPDATES_PER_TRANSACTION];

	/* but the extent number is 32bit, which at an extent size of 4 MiB
	 * allows to cover device sizes of up to 2**54 Byte (16 PiB) */
	__be32	update_extent_nr[AL_UPDATES_PER_TRANSACTION];

	/* --- 420 bytes used (36 + 64*6) --- */

	/* 4096 - 420 = 3676 = 919 * 4 */
	__be32	context[AL_CONTEXT_PER_TRANSACTION];
};

struct update_odbm_work {
	struct drbd_work w;
	unsigned int enr;
};

struct update_al_work {
	struct drbd_work w;
	struct completion event;
	int err;
};

struct drbd_atodb_wait {
	atomic_t           count;
	struct completion  io_done;
	struct drbd_conf   *mdev;
	int                error;
};


int w_al_write_transaction(struct drbd_work *, int);

static int _drbd_md_sync_page_io(struct drbd_conf *mdev,
				 struct drbd_backing_dev *bdev,
				 struct page *page, sector_t sector,
				 int rw, int size)
{
	struct bio *bio;
	struct drbd_md_io md_io;
	int ok;

	md_io.mdev = mdev;
	init_completion(&md_io.event);
	md_io.error = 0;

	if ((rw & WRITE) && !test_bit(MD_NO_FUA, &mdev->flags))
		rw |= REQ_FUA | REQ_FLUSH;
	rw |= REQ_SYNC;

	bio = bio_alloc_drbd(GFP_NOIO);
	bio->bi_bdev = bdev->md_bdev;
	bio->bi_sector = sector;
	ok = (bio_add_page(bio, page, size, 0) == size);
	if (!ok)
		goto out;
	bio->bi_private = &md_io;
	bio->bi_end_io = drbd_md_io_complete;
	bio->bi_rw = rw;

	if (drbd_insert_fault(mdev, (rw & WRITE) ? DRBD_FAULT_MD_WR : DRBD_FAULT_MD_RD))
		bio_endio(bio, -EIO);
	else
		submit_bio(rw, bio);
	wait_for_completion(&md_io.event);
	ok = bio_flagged(bio, BIO_UPTODATE) && md_io.error == 0;

 out:
	bio_put(bio);
	return ok;
}

int drbd_md_sync_page_io(struct drbd_conf *mdev, struct drbd_backing_dev *bdev,
			 sector_t sector, int rw)
{
	int ok;
	struct page *iop = mdev->md_io_page;

	D_ASSERT(mutex_is_locked(&mdev->md_io_mutex));

	BUG_ON(!bdev->md_bdev);

	dev_dbg(DEV, "meta_data io: %s [%d]:%s(,%llus,%s)\n",
	     current->comm, current->pid, __func__,
	     (unsigned long long)sector, (rw & WRITE) ? "WRITE" : "READ");

	if (sector < drbd_md_first_sector(bdev) ||
	    sector + 7 > drbd_md_last_sector(bdev))
		dev_alert(DEV, "%s [%d]:%s(,%llus,%s) out of range md access!\n",
		     current->comm, current->pid, __func__,
		     (unsigned long long)sector, (rw & WRITE) ? "WRITE" : "READ");

	ok = _drbd_md_sync_page_io(mdev, bdev, iop, sector, rw, MD_BLOCK_SIZE);
	if (unlikely(!ok)) {
		dev_err(DEV, "drbd_md_sync_page_io(,%llus,%s) failed!\n",
		    (unsigned long long)sector, (rw & WRITE) ? "WRITE" : "READ");
		return 0;
	}

	return ok;
}

static struct lc_element *_al_get(struct drbd_conf *mdev, unsigned int enr)
{
	struct lc_element *al_ext;
	struct lc_element *tmp;
	int wake;

	spin_lock_irq(&mdev->al_lock);
	tmp = lc_find(mdev->resync, enr/AL_EXT_PER_BM_SECT);
	if (unlikely(tmp != NULL)) {
		struct bm_extent  *bm_ext = lc_entry(tmp, struct bm_extent, lce);
		if (test_bit(BME_NO_WRITES, &bm_ext->flags)) {
			wake = !test_and_set_bit(BME_PRIORITY, &bm_ext->flags);
			spin_unlock_irq(&mdev->al_lock);
			if (wake)
				wake_up(&mdev->al_wait);
			return NULL;
		}
	}
	al_ext = lc_get(mdev->act_log, enr);
	spin_unlock_irq(&mdev->al_lock);
	return al_ext;
}

void drbd_al_begin_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = (sector >> (AL_EXTENT_SHIFT-9));
	struct lc_element *al_ext;
	struct update_al_work al_work;

	D_ASSERT(atomic_read(&mdev->local_cnt) > 0);

	wait_event(mdev->al_wait, (al_ext = _al_get(mdev, enr)));

	if (al_ext->lc_number != enr) {
		/* drbd_al_write_transaction(mdev,al_ext,enr);
		 * recurses into generic_make_request(), which
		 * disallows recursion, bios being serialized on the
		 * current->bio_tail list now.
		 * we have to delegate updates to the activity log
		 * to the worker thread. */

		/* Serialize multiple transactions.
		 * This uses test_and_set_bit, memory barrier is implicit.
		 * Optimization potential:
		 * first check for transaction number > old transaction number,
		 * so not all waiters have to lock/unlock.  */
		wait_event(mdev->al_wait, lc_try_lock_for_transaction(mdev->act_log));

		/* Double check: it may have been committed by someone else,
		 * while we have been waiting for the lock. */
		if (al_ext->lc_number != enr) {
			init_completion(&al_work.event);
			al_work.w.cb = w_al_write_transaction;
			al_work.w.mdev = mdev;
			drbd_queue_work_front(&mdev->tconn->data.work, &al_work.w);
			wait_for_completion(&al_work.event);

			mdev->al_writ_cnt++;

			spin_lock_irq(&mdev->al_lock);
			/* FIXME
			if (al_work.err)
				we need an "lc_cancel" here;
			*/
			lc_committed(mdev->act_log);
			spin_unlock_irq(&mdev->al_lock);
		}
		lc_unlock(mdev->act_log);
		wake_up(&mdev->al_wait);
	}
}

void drbd_al_complete_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = (sector >> (AL_EXTENT_SHIFT-9));
	struct lc_element *extent;
	unsigned long flags;

	spin_lock_irqsave(&mdev->al_lock, flags);

	extent = lc_find(mdev->act_log, enr);

	if (!extent) {
		spin_unlock_irqrestore(&mdev->al_lock, flags);
		dev_err(DEV, "al_complete_io() called on inactive extent %u\n", enr);
		return;
	}

	if (lc_put(mdev->act_log, extent) == 0)
		wake_up(&mdev->al_wait);

	spin_unlock_irqrestore(&mdev->al_lock, flags);
}

#if (PAGE_SHIFT + 3) < (AL_EXTENT_SHIFT - BM_BLOCK_SHIFT)
/* Currently BM_BLOCK_SHIFT, BM_EXT_SHIFT and AL_EXTENT_SHIFT
 * are still coupled, or assume too much about their relation.
 * Code below will not work if this is violated.
 * Will be cleaned up with some followup patch.
 */
# error FIXME
#endif

static unsigned int al_extent_to_bm_page(unsigned int al_enr)
{
	return al_enr >>
		/* bit to page */
		((PAGE_SHIFT + 3) -
		/* al extent number to bit */
		 (AL_EXTENT_SHIFT - BM_BLOCK_SHIFT));
}

static unsigned int rs_extent_to_bm_page(unsigned int rs_enr)
{
	return rs_enr >>
		/* bit to page */
		((PAGE_SHIFT + 3) -
		/* al extent number to bit */
		 (BM_EXT_SHIFT - BM_BLOCK_SHIFT));
}

int
w_al_write_transaction(struct drbd_work *w, int unused)
{
	struct update_al_work *aw = container_of(w, struct update_al_work, w);
	struct drbd_conf *mdev = w->mdev;
	struct al_transaction_on_disk *buffer;
	struct lc_element *e;
	sector_t sector;
	int i, mx;
	unsigned extent_nr;
	unsigned crc = 0;

	if (!get_ldev(mdev)) {
		dev_err(DEV, "disk is %s, cannot start al transaction\n",
			drbd_disk_str(mdev->state.disk));
		aw->err = -EIO;
		complete(&((struct update_al_work *)w)->event);
		return 1;
	}

	/* The bitmap write may have failed, causing a state change. */
	if (mdev->state.disk < D_INCONSISTENT) {
		dev_err(DEV,
			"disk is %s, cannot write al transaction\n",
			drbd_disk_str(mdev->state.disk));
		aw->err = -EIO;
		complete(&((struct update_al_work *)w)->event);
		put_ldev(mdev);
		return 1;
	}

	mutex_lock(&mdev->md_io_mutex); /* protects md_io_buffer, al_tr_cycle, ... */
	buffer = page_address(mdev->md_io_page);

	memset(buffer, 0, sizeof(*buffer));
	buffer->magic = cpu_to_be32(DRBD_AL_MAGIC);
	buffer->tr_number = cpu_to_be32(mdev->al_tr_number);

	i = 0;

	/* Even though no one can start to change this list
	 * once we set the LC_LOCKED -- from drbd_al_begin_io(),
	 * lc_try_lock_for_transaction() --, someone may still
	 * be in the process of changing it. */
	spin_lock_irq(&mdev->al_lock);
	list_for_each_entry(e, &mdev->act_log->to_be_changed, list) {
		if (i == AL_UPDATES_PER_TRANSACTION) {
			i++;
			break;
		}
		buffer->update_slot_nr[i] = cpu_to_be16(e->lc_index);
		buffer->update_extent_nr[i] = cpu_to_be32(e->lc_new_number);
		if (e->lc_number != LC_FREE)
			drbd_bm_mark_for_writeout(mdev,
					al_extent_to_bm_page(e->lc_number));
		i++;
	}
	spin_unlock_irq(&mdev->al_lock);
	BUG_ON(i > AL_UPDATES_PER_TRANSACTION);

	buffer->n_updates = cpu_to_be16(i);
	for ( ; i < AL_UPDATES_PER_TRANSACTION; i++) {
		buffer->update_slot_nr[i] = cpu_to_be16(-1);
		buffer->update_extent_nr[i] = cpu_to_be32(LC_FREE);
	}

	buffer->context_size = cpu_to_be16(mdev->act_log->nr_elements);
	buffer->context_start_slot_nr = cpu_to_be16(mdev->al_tr_cycle);

	mx = min_t(int, AL_CONTEXT_PER_TRANSACTION,
		   mdev->act_log->nr_elements - mdev->al_tr_cycle);
	for (i = 0; i < mx; i++) {
		unsigned idx = mdev->al_tr_cycle + i;
		extent_nr = lc_element_by_index(mdev->act_log, idx)->lc_number;
		buffer->context[i] = cpu_to_be32(extent_nr);
	}
	for (; i < AL_CONTEXT_PER_TRANSACTION; i++)
		buffer->context[i] = cpu_to_be32(LC_FREE);

	mdev->al_tr_cycle += AL_CONTEXT_PER_TRANSACTION;
	if (mdev->al_tr_cycle >= mdev->act_log->nr_elements)
		mdev->al_tr_cycle = 0;

	sector =  mdev->ldev->md.md_offset
		+ mdev->ldev->md.al_offset
		+ mdev->al_tr_pos * (MD_BLOCK_SIZE>>9);

	crc = crc32c(0, buffer, 4096);
	buffer->crc32c = cpu_to_be32(crc);

	if (drbd_bm_write_hinted(mdev))
		aw->err = -EIO;
		/* drbd_chk_io_error done already */
	else if (!drbd_md_sync_page_io(mdev, mdev->ldev, sector, WRITE)) {
		aw->err = -EIO;
		drbd_chk_io_error(mdev, 1, true);
	} else {
		/* advance ringbuffer position and transaction counter */
		mdev->al_tr_pos = (mdev->al_tr_pos + 1) % (MD_AL_SECTORS*512/MD_BLOCK_SIZE);
		mdev->al_tr_number++;
	}

	mutex_unlock(&mdev->md_io_mutex);
	complete(&((struct update_al_work *)w)->event);
	put_ldev(mdev);

	return 1;
}

/* FIXME
 * reading of the activity log,
 * and potentially dirtying of the affected bitmap regions,
 * should be done from userland only.
 * DRBD would simply always attach with an empty activity log,
 * and refuse to attach to something that looks like a crashed primary.
 */

/**
 * drbd_al_read_tr() - Read a single transaction from the on disk activity log
 * @mdev:	DRBD device.
 * @bdev:	Block device to read form.
 * @b:		pointer to an al_transaction.
 * @index:	On disk slot of the transaction to read.
 *
 * Returns -1 on IO error, 0 on checksum error and 1 upon success.
 */
static int drbd_al_read_tr(struct drbd_conf *mdev,
			   struct drbd_backing_dev *bdev,
			   int index)
{
	struct al_transaction_on_disk *b = page_address(mdev->md_io_page);
	sector_t sector;
	u32 crc;

	sector =  bdev->md.md_offset
		+ bdev->md.al_offset
		+ index * (MD_BLOCK_SIZE>>9);

	/* Dont process error normally,
	 * as this is done before disk is attached! */
	if (!drbd_md_sync_page_io(mdev, bdev, sector, READ))
		return -1;

	if (!expect(b->magic == cpu_to_be32(DRBD_AL_MAGIC)))
		return 0;

	if (!expect(be16_to_cpu(b->n_updates) <= AL_UPDATES_PER_TRANSACTION))
		return 0;

	if (!expect(be16_to_cpu(b->context_size) <= DRBD_AL_EXTENTS_MAX))
		return 0;

	if (!expect(be16_to_cpu(b->context_start_slot_nr) < DRBD_AL_EXTENTS_MAX))
		return 0;

	crc = be32_to_cpu(b->crc32c);
	b->crc32c = 0;
	if (!expect(crc == crc32c(0, b, 4096)))
		return 0;

	return 1;
}

/**
 * drbd_al_read_log() - Restores the activity log from its on disk representation.
 * @mdev:	DRBD device.
 * @bdev:	Block device to read form.
 *
 * Returns 1 on success, returns 0 when reading the log failed due to IO errors.
 */
int drbd_al_read_log(struct drbd_conf *mdev, struct drbd_backing_dev *bdev)
{
	struct al_transaction_on_disk *b;
	int i;
	int rv;
	int mx;
	int active_extents = 0;
	int transactions = 0;
	int found_valid = 0;
	int from = 0;
	int to = 0;
	u32 from_tnr = 0;
	u32 to_tnr = 0;
	u32 cnr;

	/* Note that this is expected to be called with a newly created,
	 * clean and all unused activity log of the "expected size".
	 */

	/* lock out all other meta data io for now,
	 * and make sure the page is mapped.
	 */
	mutex_lock(&mdev->md_io_mutex);
	b = page_address(mdev->md_io_page);

	/* Always use the full ringbuffer space for now.
	 * possible optimization: read in all of it,
	 * then scan the in-memory pages. */

	mx = (MD_AL_SECTORS*512/MD_BLOCK_SIZE);

	/* Find the valid transaction in the log */
	for (i = 0; i < mx; i++) {
		rv = drbd_al_read_tr(mdev, bdev, i);
		/* invalid data in that block */
		if (rv == 0)
			continue;

		/* IO error */
		if (rv == -1) {
			mutex_unlock(&mdev->md_io_mutex);
			return 0;
		}

		cnr = be32_to_cpu(b->tr_number);
		if (++found_valid == 1) {
			from = i;
			to = i;
			from_tnr = cnr;
			to_tnr = cnr;
			continue;
		}

		D_ASSERT(cnr != to_tnr);
		D_ASSERT(cnr != from_tnr);
		if ((int)cnr - (int)from_tnr < 0) {
			D_ASSERT(from_tnr - cnr + i - from == mx);
			from = i;
			from_tnr = cnr;
		}
		if ((int)cnr - (int)to_tnr > 0) {
			D_ASSERT(cnr - to_tnr == i - to);
			to = i;
			to_tnr = cnr;
		}
	}

	if (!found_valid) {
		dev_warn(DEV, "No usable activity log found.\n");
		mutex_unlock(&mdev->md_io_mutex);
		return 1;
	}

	/* Read the valid transactions.
	 * dev_info(DEV, "Reading from %d to %d.\n",from,to); */
	i = from;
	while (1) {
		struct lc_element *e;
		unsigned j, n, slot, extent_nr;

		rv = drbd_al_read_tr(mdev, bdev, i);
		if (!expect(rv != 0))
			goto cancel;
		if (rv == -1) {
			mutex_unlock(&mdev->md_io_mutex);
			return 0;
		}

		/* deal with different transaction types.
		 * not yet implemented */
		if (!expect(b->transaction_type == 0))
			goto cancel;

		/* on the fly re-create/resize activity log?
		 * will be a special transaction type flag. */
		if (!expect(be16_to_cpu(b->context_size) == mdev->act_log->nr_elements))
			goto cancel;
		if (!expect(be16_to_cpu(b->context_start_slot_nr) < mdev->act_log->nr_elements))
			goto cancel;

		/* We are the only user of the activity log right now,
		 * don't actually need to take that lock. */
		spin_lock_irq(&mdev->al_lock);

		/* first, apply the context, ... */
		for (j = 0, slot = be16_to_cpu(b->context_start_slot_nr);
		     j < AL_CONTEXT_PER_TRANSACTION &&
		     slot < mdev->act_log->nr_elements; j++, slot++) {
			extent_nr = be32_to_cpu(b->context[j]);
			e = lc_element_by_index(mdev->act_log, slot);
			if (e->lc_number != extent_nr) {
				if (extent_nr != LC_FREE)
					active_extents++;
				else
					active_extents--;
			}
			lc_set(mdev->act_log, extent_nr, slot);
		}

		/* ... then apply the updates,
		 * which override the context information.
		 * drbd_al_read_tr already did the rangecheck
		 * on n <= AL_UPDATES_PER_TRANSACTION */
		n = be16_to_cpu(b->n_updates);
		for (j = 0; j < n; j++) {
			slot = be16_to_cpu(b->update_slot_nr[j]);
			extent_nr = be32_to_cpu(b->update_extent_nr[j]);
			if (!expect(slot < mdev->act_log->nr_elements))
				break;
			e = lc_element_by_index(mdev->act_log, slot);
			if (e->lc_number != extent_nr) {
				if (extent_nr != LC_FREE)
					active_extents++;
				else
					active_extents--;
			}
			lc_set(mdev->act_log, extent_nr, slot);
		}
		spin_unlock_irq(&mdev->al_lock);

		transactions++;

cancel:
		if (i == to)
			break;
		i++;
		if (i >= mx)
			i = 0;
	}

	mdev->al_tr_number = to_tnr+1;
	mdev->al_tr_pos = (to + 1) % (MD_AL_SECTORS*512/MD_BLOCK_SIZE);

	/* ok, we are done with it */
	mutex_unlock(&mdev->md_io_mutex);

	dev_info(DEV, "Found %d transactions (%d active extents) in activity log.\n",
	     transactions, active_extents);

	return 1;
}

/**
 * drbd_al_apply_to_bm() - Sets the bitmap to dirty(1) where covered by active AL extents
 * @mdev:	DRBD device.
 */
void drbd_al_apply_to_bm(struct drbd_conf *mdev)
{
	unsigned int enr;
	unsigned long add = 0;
	char ppb[10];
	int i, tmp;

	wait_event(mdev->al_wait, lc_try_lock(mdev->act_log));

	for (i = 0; i < mdev->act_log->nr_elements; i++) {
		enr = lc_element_by_index(mdev->act_log, i)->lc_number;
		if (enr == LC_FREE)
			continue;
		tmp = drbd_bm_ALe_set_all(mdev, enr);
		dynamic_dev_dbg(DEV, "AL: set %d bits in extent %u\n", tmp, enr);
		add += tmp;
	}

	lc_unlock(mdev->act_log);
	wake_up(&mdev->al_wait);

	dev_info(DEV, "Marked additional %s as out-of-sync based on AL.\n",
	     ppsize(ppb, Bit2KB(add)));
}

static int _try_lc_del(struct drbd_conf *mdev, struct lc_element *al_ext)
{
	int rv;

	spin_lock_irq(&mdev->al_lock);
	rv = (al_ext->refcnt == 0);
	if (likely(rv))
		lc_del(mdev->act_log, al_ext);
	spin_unlock_irq(&mdev->al_lock);

	return rv;
}

/**
 * drbd_al_shrink() - Removes all active extents form the activity log
 * @mdev:	DRBD device.
 *
 * Removes all active extents form the activity log, waiting until
 * the reference count of each entry dropped to 0 first, of course.
 *
 * You need to lock mdev->act_log with lc_try_lock() / lc_unlock()
 */
void drbd_al_shrink(struct drbd_conf *mdev)
{
	struct lc_element *al_ext;
	int i;

	D_ASSERT(test_bit(__LC_LOCKED, &mdev->act_log->flags));

	for (i = 0; i < mdev->act_log->nr_elements; i++) {
		al_ext = lc_element_by_index(mdev->act_log, i);
		if (al_ext->lc_number == LC_FREE)
			continue;
		wait_event(mdev->al_wait, _try_lc_del(mdev, al_ext));
	}

	wake_up(&mdev->al_wait);
}

static int w_update_odbm(struct drbd_work *w, int unused)
{
	struct update_odbm_work *udw = container_of(w, struct update_odbm_work, w);
	struct drbd_conf *mdev = w->mdev;

	if (!get_ldev(mdev)) {
		if (__ratelimit(&drbd_ratelimit_state))
			dev_warn(DEV, "Can not update on disk bitmap, local IO disabled.\n");
		kfree(udw);
		return 1;
	}

	drbd_bm_write_page(mdev, rs_extent_to_bm_page(udw->enr));
	put_ldev(mdev);

	kfree(udw);

	if (drbd_bm_total_weight(mdev) <= mdev->rs_failed) {
		switch (mdev->state.conn) {
		case C_SYNC_SOURCE:  case C_SYNC_TARGET:
		case C_PAUSED_SYNC_S: case C_PAUSED_SYNC_T:
			drbd_resync_finished(mdev);
		default:
			/* nothing to do */
			break;
		}
	}
	drbd_bcast_sync_progress(mdev);

	return 1;
}


/* ATTENTION. The AL's extents are 4MB each, while the extents in the
 * resync LRU-cache are 16MB each.
 * The caller of this function has to hold an get_ldev() reference.
 *
 * TODO will be obsoleted once we have a caching lru of the on disk bitmap
 */
static void drbd_try_clear_on_disk_bm(struct drbd_conf *mdev, sector_t sector,
				      int count, int success)
{
	struct lc_element *e;
	struct update_odbm_work *udw;

	unsigned int enr;

	D_ASSERT(atomic_read(&mdev->local_cnt));

	/* I simply assume that a sector/size pair never crosses
	 * a 16 MB extent border. (Currently this is true...) */
	enr = BM_SECT_TO_EXT(sector);

	e = lc_get(mdev->resync, enr);
	if (e) {
		struct bm_extent *ext = lc_entry(e, struct bm_extent, lce);
		if (ext->lce.lc_number == enr) {
			if (success)
				ext->rs_left -= count;
			else
				ext->rs_failed += count;
			if (ext->rs_left < ext->rs_failed) {
				dev_err(DEV, "BAD! sector=%llus enr=%u rs_left=%d "
				    "rs_failed=%d count=%d\n",
				     (unsigned long long)sector,
				     ext->lce.lc_number, ext->rs_left,
				     ext->rs_failed, count);
				dump_stack();

				lc_put(mdev->resync, &ext->lce);
				drbd_force_state(mdev, NS(conn, C_DISCONNECTING));
				return;
			}
		} else {
			/* Normally this element should be in the cache,
			 * since drbd_rs_begin_io() pulled it already in.
			 *
			 * But maybe an application write finished, and we set
			 * something outside the resync lru_cache in sync.
			 */
			int rs_left = drbd_bm_e_weight(mdev, enr);
			if (ext->flags != 0) {
				dev_warn(DEV, "changing resync lce: %d[%u;%02lx]"
				     " -> %d[%u;00]\n",
				     ext->lce.lc_number, ext->rs_left,
				     ext->flags, enr, rs_left);
				ext->flags = 0;
			}
			if (ext->rs_failed) {
				dev_warn(DEV, "Kicking resync_lru element enr=%u "
				     "out with rs_failed=%d\n",
				     ext->lce.lc_number, ext->rs_failed);
			}
			ext->rs_left = rs_left;
			ext->rs_failed = success ? 0 : count;
			/* we don't keep a persistent log of the resync lru,
			 * we can commit any change right away. */
			lc_committed(mdev->resync);
		}
		lc_put(mdev->resync, &ext->lce);
		/* no race, we are within the al_lock! */

		if (ext->rs_left == ext->rs_failed) {
			ext->rs_failed = 0;

			udw = kmalloc(sizeof(*udw), GFP_ATOMIC);
			if (udw) {
				udw->enr = ext->lce.lc_number;
				udw->w.cb = w_update_odbm;
				udw->w.mdev = mdev;
				drbd_queue_work_front(&mdev->tconn->data.work, &udw->w);
			} else {
				dev_warn(DEV, "Could not kmalloc an udw\n");
			}
		}
	} else {
		dev_err(DEV, "lc_get() failed! locked=%d/%d flags=%lu\n",
		    mdev->resync_locked,
		    mdev->resync->nr_elements,
		    mdev->resync->flags);
	}
}

void drbd_advance_rs_marks(struct drbd_conf *mdev, unsigned long still_to_go)
{
	unsigned long now = jiffies;
	unsigned long last = mdev->rs_mark_time[mdev->rs_last_mark];
	int next = (mdev->rs_last_mark + 1) % DRBD_SYNC_MARKS;
	if (time_after_eq(now, last + DRBD_SYNC_MARK_STEP)) {
		if (mdev->rs_mark_left[mdev->rs_last_mark] != still_to_go &&
		    mdev->state.conn != C_PAUSED_SYNC_T &&
		    mdev->state.conn != C_PAUSED_SYNC_S) {
			mdev->rs_mark_time[next] = now;
			mdev->rs_mark_left[next] = still_to_go;
			mdev->rs_last_mark = next;
		}
	}
}

/* clear the bit corresponding to the piece of storage in question:
 * size byte of data starting from sector.  Only clear a bits of the affected
 * one ore more _aligned_ BM_BLOCK_SIZE blocks.
 *
 * called by worker on C_SYNC_TARGET and receiver on SyncSource.
 *
 */
void __drbd_set_in_sync(struct drbd_conf *mdev, sector_t sector, int size,
		       const char *file, const unsigned int line)
{
	/* Is called from worker and receiver context _only_ */
	unsigned long sbnr, ebnr, lbnr;
	unsigned long count = 0;
	sector_t esector, nr_sectors;
	int wake_up = 0;
	unsigned long flags;

	if (size <= 0 || !IS_ALIGNED(size, 512) || size > DRBD_MAX_BIO_SIZE) {
		dev_err(DEV, "drbd_set_in_sync: sector=%llus size=%d nonsense!\n",
				(unsigned long long)sector, size);
		return;
	}
	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	esector = sector + (size >> 9) - 1;

	if (!expect(sector < nr_sectors))
		return;
	if (!expect(esector < nr_sectors))
		esector = nr_sectors - 1;

	lbnr = BM_SECT_TO_BIT(nr_sectors-1);

	/* we clear it (in sync).
	 * round up start sector, round down end sector.  we make sure we only
	 * clear full, aligned, BM_BLOCK_SIZE (4K) blocks */
	if (unlikely(esector < BM_SECT_PER_BIT-1))
		return;
	if (unlikely(esector == (nr_sectors-1)))
		ebnr = lbnr;
	else
		ebnr = BM_SECT_TO_BIT(esector - (BM_SECT_PER_BIT-1));
	sbnr = BM_SECT_TO_BIT(sector + BM_SECT_PER_BIT-1);

	if (sbnr > ebnr)
		return;

	/*
	 * ok, (capacity & 7) != 0 sometimes, but who cares...
	 * we count rs_{total,left} in bits, not sectors.
	 */
	count = drbd_bm_clear_bits(mdev, sbnr, ebnr);
	if (count && get_ldev(mdev)) {
		drbd_advance_rs_marks(mdev, drbd_bm_total_weight(mdev));
		spin_lock_irqsave(&mdev->al_lock, flags);
		drbd_try_clear_on_disk_bm(mdev, sector, count, true);
		spin_unlock_irqrestore(&mdev->al_lock, flags);

		/* just wake_up unconditional now, various lc_chaged(),
		 * lc_put() in drbd_try_clear_on_disk_bm(). */
		wake_up = 1;
		put_ldev(mdev);
	}
	if (wake_up)
		wake_up(&mdev->al_wait);
}

/*
 * this is intended to set one request worth of data out of sync.
 * affects at least 1 bit,
 * and at most 1+DRBD_MAX_BIO_SIZE/BM_BLOCK_SIZE bits.
 *
 * called by tl_clear and drbd_send_dblock (==drbd_make_request).
 * so this can be _any_ process.
 */
int __drbd_set_out_of_sync(struct drbd_conf *mdev, sector_t sector, int size,
			    const char *file, const unsigned int line)
{
	unsigned long sbnr, ebnr, lbnr, flags;
	sector_t esector, nr_sectors;
	unsigned int enr, count = 0;
	struct lc_element *e;

	if (size <= 0 || !IS_ALIGNED(size, 512) || size > DRBD_MAX_BIO_SIZE) {
		dev_err(DEV, "sector: %llus, size: %d\n",
			(unsigned long long)sector, size);
		return 0;
	}

	if (!get_ldev(mdev))
		return 0; /* no disk, no metadata, no bitmap to set bits in */

	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	esector = sector + (size >> 9) - 1;

	if (!expect(sector < nr_sectors))
		goto out;
	if (!expect(esector < nr_sectors))
		esector = nr_sectors - 1;

	lbnr = BM_SECT_TO_BIT(nr_sectors-1);

	/* we set it out of sync,
	 * we do not need to round anything here */
	sbnr = BM_SECT_TO_BIT(sector);
	ebnr = BM_SECT_TO_BIT(esector);

	/* ok, (capacity & 7) != 0 sometimes, but who cares...
	 * we count rs_{total,left} in bits, not sectors.  */
	spin_lock_irqsave(&mdev->al_lock, flags);
	count = drbd_bm_set_bits(mdev, sbnr, ebnr);

	enr = BM_SECT_TO_EXT(sector);
	e = lc_find(mdev->resync, enr);
	if (e)
		lc_entry(e, struct bm_extent, lce)->rs_left += count;
	spin_unlock_irqrestore(&mdev->al_lock, flags);

out:
	put_ldev(mdev);

	return count;
}

static
struct bm_extent *_bme_get(struct drbd_conf *mdev, unsigned int enr)
{
	struct lc_element *e;
	struct bm_extent *bm_ext;
	int wakeup = 0;
	unsigned long rs_flags;

	spin_lock_irq(&mdev->al_lock);
	if (mdev->resync_locked > mdev->resync->nr_elements/2) {
		spin_unlock_irq(&mdev->al_lock);
		return NULL;
	}
	e = lc_get(mdev->resync, enr);
	bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
	if (bm_ext) {
		if (bm_ext->lce.lc_number != enr) {
			bm_ext->rs_left = drbd_bm_e_weight(mdev, enr);
			bm_ext->rs_failed = 0;
			lc_committed(mdev->resync);
			wakeup = 1;
		}
		if (bm_ext->lce.refcnt == 1)
			mdev->resync_locked++;
		set_bit(BME_NO_WRITES, &bm_ext->flags);
	}
	rs_flags = mdev->resync->flags;
	spin_unlock_irq(&mdev->al_lock);
	if (wakeup)
		wake_up(&mdev->al_wait);

	if (!bm_ext) {
		if (rs_flags & LC_STARVING)
			dev_warn(DEV, "Have to wait for element"
			     " (resync LRU too small?)\n");
		BUG_ON(rs_flags & LC_LOCKED);
	}

	return bm_ext;
}

static int _is_in_al(struct drbd_conf *mdev, unsigned int enr)
{
	int rv;

	spin_lock_irq(&mdev->al_lock);
	rv = lc_is_used(mdev->act_log, enr);
	spin_unlock_irq(&mdev->al_lock);

	return rv;
}

/**
 * drbd_rs_begin_io() - Gets an extent in the resync LRU cache and sets it to BME_LOCKED
 * @mdev:	DRBD device.
 * @sector:	The sector number.
 *
 * This functions sleeps on al_wait. Returns 0 on success, -EINTR if interrupted.
 */
int drbd_rs_begin_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = BM_SECT_TO_EXT(sector);
	struct bm_extent *bm_ext;
	int i, sig;
	int sa = 200; /* Step aside 200 times, then grab the extent and let app-IO wait.
			 200 times -> 20 seconds. */

retry:
	sig = wait_event_interruptible(mdev->al_wait,
			(bm_ext = _bme_get(mdev, enr)));
	if (sig)
		return -EINTR;

	if (test_bit(BME_LOCKED, &bm_ext->flags))
		return 0;

	for (i = 0; i < AL_EXT_PER_BM_SECT; i++) {
		sig = wait_event_interruptible(mdev->al_wait,
					       !_is_in_al(mdev, enr * AL_EXT_PER_BM_SECT + i) ||
					       test_bit(BME_PRIORITY, &bm_ext->flags));

		if (sig || (test_bit(BME_PRIORITY, &bm_ext->flags) && sa)) {
			spin_lock_irq(&mdev->al_lock);
			if (lc_put(mdev->resync, &bm_ext->lce) == 0) {
				bm_ext->flags = 0; /* clears BME_NO_WRITES and eventually BME_PRIORITY */
				mdev->resync_locked--;
				wake_up(&mdev->al_wait);
			}
			spin_unlock_irq(&mdev->al_lock);
			if (sig)
				return -EINTR;
			if (schedule_timeout_interruptible(HZ/10))
				return -EINTR;
			if (sa && --sa == 0)
				dev_warn(DEV,"drbd_rs_begin_io() stepped aside for 20sec."
					 "Resync stalled?\n");
			goto retry;
		}
	}
	set_bit(BME_LOCKED, &bm_ext->flags);
	return 0;
}

/**
 * drbd_try_rs_begin_io() - Gets an extent in the resync LRU cache, does not sleep
 * @mdev:	DRBD device.
 * @sector:	The sector number.
 *
 * Gets an extent in the resync LRU cache, sets it to BME_NO_WRITES, then
 * tries to set it to BME_LOCKED. Returns 0 upon success, and -EAGAIN
 * if there is still application IO going on in this area.
 */
int drbd_try_rs_begin_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = BM_SECT_TO_EXT(sector);
	const unsigned int al_enr = enr*AL_EXT_PER_BM_SECT;
	struct lc_element *e;
	struct bm_extent *bm_ext;
	int i;

	spin_lock_irq(&mdev->al_lock);
	if (mdev->resync_wenr != LC_FREE && mdev->resync_wenr != enr) {
		/* in case you have very heavy scattered io, it may
		 * stall the syncer undefined if we give up the ref count
		 * when we try again and requeue.
		 *
		 * if we don't give up the refcount, but the next time
		 * we are scheduled this extent has been "synced" by new
		 * application writes, we'd miss the lc_put on the
		 * extent we keep the refcount on.
		 * so we remembered which extent we had to try again, and
		 * if the next requested one is something else, we do
		 * the lc_put here...
		 * we also have to wake_up
		 */
		e = lc_find(mdev->resync, mdev->resync_wenr);
		bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
		if (bm_ext) {
			D_ASSERT(!test_bit(BME_LOCKED, &bm_ext->flags));
			D_ASSERT(test_bit(BME_NO_WRITES, &bm_ext->flags));
			clear_bit(BME_NO_WRITES, &bm_ext->flags);
			mdev->resync_wenr = LC_FREE;
			if (lc_put(mdev->resync, &bm_ext->lce) == 0)
				mdev->resync_locked--;
			wake_up(&mdev->al_wait);
		} else {
			dev_alert(DEV, "LOGIC BUG\n");
		}
	}
	/* TRY. */
	e = lc_try_get(mdev->resync, enr);
	bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
	if (bm_ext) {
		if (test_bit(BME_LOCKED, &bm_ext->flags))
			goto proceed;
		if (!test_and_set_bit(BME_NO_WRITES, &bm_ext->flags)) {
			mdev->resync_locked++;
		} else {
			/* we did set the BME_NO_WRITES,
			 * but then could not set BME_LOCKED,
			 * so we tried again.
			 * drop the extra reference. */
			bm_ext->lce.refcnt--;
			D_ASSERT(bm_ext->lce.refcnt > 0);
		}
		goto check_al;
	} else {
		/* do we rather want to try later? */
		if (mdev->resync_locked > mdev->resync->nr_elements-3)
			goto try_again;
		/* Do or do not. There is no try. -- Yoda */
		e = lc_get(mdev->resync, enr);
		bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
		if (!bm_ext) {
			const unsigned long rs_flags = mdev->resync->flags;
			if (rs_flags & LC_STARVING)
				dev_warn(DEV, "Have to wait for element"
				     " (resync LRU too small?)\n");
			BUG_ON(rs_flags & LC_LOCKED);
			goto try_again;
		}
		if (bm_ext->lce.lc_number != enr) {
			bm_ext->rs_left = drbd_bm_e_weight(mdev, enr);
			bm_ext->rs_failed = 0;
			lc_committed(mdev->resync);
			wake_up(&mdev->al_wait);
			D_ASSERT(test_bit(BME_LOCKED, &bm_ext->flags) == 0);
		}
		set_bit(BME_NO_WRITES, &bm_ext->flags);
		D_ASSERT(bm_ext->lce.refcnt == 1);
		mdev->resync_locked++;
		goto check_al;
	}
check_al:
	for (i = 0; i < AL_EXT_PER_BM_SECT; i++) {
		if (lc_is_used(mdev->act_log, al_enr+i))
			goto try_again;
	}
	set_bit(BME_LOCKED, &bm_ext->flags);
proceed:
	mdev->resync_wenr = LC_FREE;
	spin_unlock_irq(&mdev->al_lock);
	return 0;

try_again:
	if (bm_ext)
		mdev->resync_wenr = enr;
	spin_unlock_irq(&mdev->al_lock);
	return -EAGAIN;
}

void drbd_rs_complete_io(struct drbd_conf *mdev, sector_t sector)
{
	unsigned int enr = BM_SECT_TO_EXT(sector);
	struct lc_element *e;
	struct bm_extent *bm_ext;
	unsigned long flags;

	spin_lock_irqsave(&mdev->al_lock, flags);
	e = lc_find(mdev->resync, enr);
	bm_ext = e ? lc_entry(e, struct bm_extent, lce) : NULL;
	if (!bm_ext) {
		spin_unlock_irqrestore(&mdev->al_lock, flags);
		if (__ratelimit(&drbd_ratelimit_state))
			dev_err(DEV, "drbd_rs_complete_io() called, but extent not found\n");
		return;
	}

	if (bm_ext->lce.refcnt == 0) {
		spin_unlock_irqrestore(&mdev->al_lock, flags);
		dev_err(DEV, "drbd_rs_complete_io(,%llu [=%u]) called, "
		    "but refcnt is 0!?\n",
		    (unsigned long long)sector, enr);
		return;
	}

	if (lc_put(mdev->resync, &bm_ext->lce) == 0) {
		bm_ext->flags = 0; /* clear BME_LOCKED, BME_NO_WRITES and BME_PRIORITY */
		mdev->resync_locked--;
		wake_up(&mdev->al_wait);
	}

	spin_unlock_irqrestore(&mdev->al_lock, flags);
}

/**
 * drbd_rs_cancel_all() - Removes all extents from the resync LRU (even BME_LOCKED)
 * @mdev:	DRBD device.
 */
void drbd_rs_cancel_all(struct drbd_conf *mdev)
{
	spin_lock_irq(&mdev->al_lock);

	if (get_ldev_if_state(mdev, D_FAILED)) { /* Makes sure ->resync is there. */
		lc_reset(mdev->resync);
		put_ldev(mdev);
	}
	mdev->resync_locked = 0;
	mdev->resync_wenr = LC_FREE;
	spin_unlock_irq(&mdev->al_lock);
	wake_up(&mdev->al_wait);
}

/**
 * drbd_rs_del_all() - Gracefully remove all extents from the resync LRU
 * @mdev:	DRBD device.
 *
 * Returns 0 upon success, -EAGAIN if at least one reference count was
 * not zero.
 */
int drbd_rs_del_all(struct drbd_conf *mdev)
{
	struct lc_element *e;
	struct bm_extent *bm_ext;
	int i;

	spin_lock_irq(&mdev->al_lock);

	if (get_ldev_if_state(mdev, D_FAILED)) {
		/* ok, ->resync is there. */
		for (i = 0; i < mdev->resync->nr_elements; i++) {
			e = lc_element_by_index(mdev->resync, i);
			bm_ext = lc_entry(e, struct bm_extent, lce);
			if (bm_ext->lce.lc_number == LC_FREE)
				continue;
			if (bm_ext->lce.lc_number == mdev->resync_wenr) {
				dev_info(DEV, "dropping %u in drbd_rs_del_all, apparently"
				     " got 'synced' by application io\n",
				     mdev->resync_wenr);
				D_ASSERT(!test_bit(BME_LOCKED, &bm_ext->flags));
				D_ASSERT(test_bit(BME_NO_WRITES, &bm_ext->flags));
				clear_bit(BME_NO_WRITES, &bm_ext->flags);
				mdev->resync_wenr = LC_FREE;
				lc_put(mdev->resync, &bm_ext->lce);
			}
			if (bm_ext->lce.refcnt != 0) {
				dev_info(DEV, "Retrying drbd_rs_del_all() later. "
				     "refcnt=%d\n", bm_ext->lce.refcnt);
				put_ldev(mdev);
				spin_unlock_irq(&mdev->al_lock);
				return -EAGAIN;
			}
			D_ASSERT(!test_bit(BME_LOCKED, &bm_ext->flags));
			D_ASSERT(!test_bit(BME_NO_WRITES, &bm_ext->flags));
			lc_del(mdev->resync, &bm_ext->lce);
		}
		D_ASSERT(mdev->resync->used == 0);
		put_ldev(mdev);
	}
	spin_unlock_irq(&mdev->al_lock);

	return 0;
}

/**
 * drbd_rs_failed_io() - Record information on a failure to resync the specified blocks
 * @mdev:	DRBD device.
 * @sector:	The sector number.
 * @size:	Size of failed IO operation, in byte.
 */
void drbd_rs_failed_io(struct drbd_conf *mdev, sector_t sector, int size)
{
	/* Is called from worker and receiver context _only_ */
	unsigned long sbnr, ebnr, lbnr;
	unsigned long count;
	sector_t esector, nr_sectors;
	int wake_up = 0;

	if (size <= 0 || !IS_ALIGNED(size, 512) || size > DRBD_MAX_BIO_SIZE) {
		dev_err(DEV, "drbd_rs_failed_io: sector=%llus size=%d nonsense!\n",
				(unsigned long long)sector, size);
		return;
	}
	nr_sectors = drbd_get_capacity(mdev->this_bdev);
	esector = sector + (size >> 9) - 1;

	if (!expect(sector < nr_sectors))
		return;
	if (!expect(esector < nr_sectors))
		esector = nr_sectors - 1;

	lbnr = BM_SECT_TO_BIT(nr_sectors-1);

	/*
	 * round up start sector, round down end sector.  we make sure we only
	 * handle full, aligned, BM_BLOCK_SIZE (4K) blocks */
	if (unlikely(esector < BM_SECT_PER_BIT-1))
		return;
	if (unlikely(esector == (nr_sectors-1)))
		ebnr = lbnr;
	else
		ebnr = BM_SECT_TO_BIT(esector - (BM_SECT_PER_BIT-1));
	sbnr = BM_SECT_TO_BIT(sector + BM_SECT_PER_BIT-1);

	if (sbnr > ebnr)
		return;

	/*
	 * ok, (capacity & 7) != 0 sometimes, but who cares...
	 * we count rs_{total,left} in bits, not sectors.
	 */
	spin_lock_irq(&mdev->al_lock);
	count = drbd_bm_count_bits(mdev, sbnr, ebnr);
	if (count) {
		mdev->rs_failed += count;

		if (get_ldev(mdev)) {
			drbd_try_clear_on_disk_bm(mdev, sector, count, false);
			put_ldev(mdev);
		}

		/* just wake_up unconditional now, various lc_chaged(),
		 * lc_put() in drbd_try_clear_on_disk_bm(). */
		wake_up = 1;
	}
	spin_unlock_irq(&mdev->al_lock);
	if (wake_up)
		wake_up(&mdev->al_wait);
}

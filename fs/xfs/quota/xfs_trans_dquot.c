/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"

#include "xfs_qm.h"

STATIC void	xfs_trans_alloc_dqinfo(xfs_trans_t *);

/*
 * Add the locked dquot to the transaction.
 * The dquot must be locked, and it cannot be associated with any
 * transaction.
 */
void
xfs_trans_dqjoin(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp)
{
	xfs_dq_logitem_t    *lp;

	ASSERT(! XFS_DQ_IS_ADDEDTO_TRX(tp, dqp));
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	ASSERT(XFS_DQ_IS_LOGITEM_INITD(dqp));
	lp = &dqp->q_logitem;

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)(lp));

	/*
	 * Initialize i_transp so we can later determine if this dquot is
	 * associated with this transaction.
	 */
	dqp->q_transp = tp;
}


/*
 * This is called to mark the dquot as needing
 * to be logged when the transaction is committed.  The dquot must
 * already be associated with the given transaction.
 * Note that it marks the entire transaction as dirty. In the ordinary
 * case, this gets called via xfs_trans_commit, after the transaction
 * is already dirty. However, there's nothing stop this from getting
 * called directly, as done by xfs_qm_scall_setqlim. Hence, the TRANS_DIRTY
 * flag.
 */
void
xfs_trans_log_dquot(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp)
{
	xfs_log_item_desc_t	*lidp;

	ASSERT(XFS_DQ_IS_ADDEDTO_TRX(tp, dqp));
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)(&dqp->q_logitem));
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;
}

/*
 * Carry forward whatever is left of the quota blk reservation to
 * the spanky new transaction
 */
STATIC void
xfs_trans_dup_dqinfo(
	xfs_trans_t	*otp,
	xfs_trans_t	*ntp)
{
	xfs_dqtrx_t	*oq, *nq;
	int		i,j;
	xfs_dqtrx_t	*oqa, *nqa;

	if (!otp->t_dqinfo)
		return;

	xfs_trans_alloc_dqinfo(ntp);
	oqa = otp->t_dqinfo->dqa_usrdquots;
	nqa = ntp->t_dqinfo->dqa_usrdquots;

	/*
	 * Because the quota blk reservation is carried forward,
	 * it is also necessary to carry forward the DQ_DIRTY flag.
	 */
	if(otp->t_flags & XFS_TRANS_DQ_DIRTY)
		ntp->t_flags |= XFS_TRANS_DQ_DIRTY;

	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			if (oqa[i].qt_dquot == NULL)
				break;
			oq = &oqa[i];
			nq = &nqa[i];

			nq->qt_dquot = oq->qt_dquot;
			nq->qt_bcount_delta = nq->qt_icount_delta = 0;
			nq->qt_rtbcount_delta = 0;

			/*
			 * Transfer whatever is left of the reservations.
			 */
			nq->qt_blk_res = oq->qt_blk_res - oq->qt_blk_res_used;
			oq->qt_blk_res = oq->qt_blk_res_used;

			nq->qt_rtblk_res = oq->qt_rtblk_res -
				oq->qt_rtblk_res_used;
			oq->qt_rtblk_res = oq->qt_rtblk_res_used;

			nq->qt_ino_res = oq->qt_ino_res - oq->qt_ino_res_used;
			oq->qt_ino_res = oq->qt_ino_res_used;

		}
		oqa = otp->t_dqinfo->dqa_grpdquots;
		nqa = ntp->t_dqinfo->dqa_grpdquots;
	}
}

/*
 * Wrap around mod_dquot to account for both user and group quotas.
 */
STATIC void
xfs_trans_mod_dquot_byino(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	uint		field,
	long		delta)
{
	xfs_mount_t	*mp;

	ASSERT(tp);
	mp = tp->t_mountp;

	if (!XFS_IS_QUOTA_ON(mp) ||
	    ip->i_ino == mp->m_sb.sb_uquotino ||
	    ip->i_ino == mp->m_sb.sb_gquotino)
		return;

	if (tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);

	if (XFS_IS_UQUOTA_ON(mp) && ip->i_udquot) {
		(void) xfs_trans_mod_dquot(tp, ip->i_udquot, field, delta);
	}
	if (XFS_IS_GQUOTA_ON(mp) && ip->i_gdquot) {
		(void) xfs_trans_mod_dquot(tp, ip->i_gdquot, field, delta);
	}
}

STATIC xfs_dqtrx_t *
xfs_trans_get_dqtrx(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp)
{
	int		i;
	xfs_dqtrx_t	*qa;

	for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
		qa = XFS_QM_DQP_TO_DQACCT(tp, dqp);

		if (qa[i].qt_dquot == NULL ||
		    qa[i].qt_dquot == dqp) {
			return (&qa[i]);
		}
	}

	return (NULL);
}

/*
 * Make the changes in the transaction structure.
 * The moral equivalent to xfs_trans_mod_sb().
 * We don't touch any fields in the dquot, so we don't care
 * if it's locked or not (most of the time it won't be).
 */
void
xfs_trans_mod_dquot(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp,
	uint		field,
	long		delta)
{
	xfs_dqtrx_t	*qtrx;

	ASSERT(tp);
	qtrx = NULL;

	if (tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);
	/*
	 * Find either the first free slot or the slot that belongs
	 * to this dquot.
	 */
	qtrx = xfs_trans_get_dqtrx(tp, dqp);
	ASSERT(qtrx);
	if (qtrx->qt_dquot == NULL)
		qtrx->qt_dquot = dqp;

	switch (field) {

		/*
		 * regular disk blk reservation
		 */
	      case XFS_TRANS_DQ_RES_BLKS:
		qtrx->qt_blk_res += (ulong)delta;
		break;

		/*
		 * inode reservation
		 */
	      case XFS_TRANS_DQ_RES_INOS:
		qtrx->qt_ino_res += (ulong)delta;
		break;

		/*
		 * disk blocks used.
		 */
	      case XFS_TRANS_DQ_BCOUNT:
		if (qtrx->qt_blk_res && delta > 0) {
			qtrx->qt_blk_res_used += (ulong)delta;
			ASSERT(qtrx->qt_blk_res >= qtrx->qt_blk_res_used);
		}
		qtrx->qt_bcount_delta += delta;
		break;

	      case XFS_TRANS_DQ_DELBCOUNT:
		qtrx->qt_delbcnt_delta += delta;
		break;

		/*
		 * Inode Count
		 */
	      case XFS_TRANS_DQ_ICOUNT:
		if (qtrx->qt_ino_res && delta > 0) {
			qtrx->qt_ino_res_used += (ulong)delta;
			ASSERT(qtrx->qt_ino_res >= qtrx->qt_ino_res_used);
		}
		qtrx->qt_icount_delta += delta;
		break;

		/*
		 * rtblk reservation
		 */
	      case XFS_TRANS_DQ_RES_RTBLKS:
		qtrx->qt_rtblk_res += (ulong)delta;
		break;

		/*
		 * rtblk count
		 */
	      case XFS_TRANS_DQ_RTBCOUNT:
		if (qtrx->qt_rtblk_res && delta > 0) {
			qtrx->qt_rtblk_res_used += (ulong)delta;
			ASSERT(qtrx->qt_rtblk_res >= qtrx->qt_rtblk_res_used);
		}
		qtrx->qt_rtbcount_delta += delta;
		break;

	      case XFS_TRANS_DQ_DELRTBCOUNT:
		qtrx->qt_delrtb_delta += delta;
		break;

	      default:
		ASSERT(0);
	}
	tp->t_flags |= XFS_TRANS_DQ_DIRTY;
}


/*
 * Given an array of dqtrx structures, lock all the dquots associated
 * and join them to the transaction, provided they have been modified.
 * We know that the highest number of dquots (of one type - usr OR grp),
 * involved in a transaction is 2 and that both usr and grp combined - 3.
 * So, we don't attempt to make this very generic.
 */
STATIC void
xfs_trans_dqlockedjoin(
	xfs_trans_t	*tp,
	xfs_dqtrx_t	*q)
{
	ASSERT(q[0].qt_dquot != NULL);
	if (q[1].qt_dquot == NULL) {
		xfs_dqlock(q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
	} else {
		ASSERT(XFS_QM_TRANS_MAXDQS == 2);
		xfs_dqlock2(q[0].qt_dquot, q[1].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[1].qt_dquot);
	}
}


/*
 * Called by xfs_trans_commit() and similar in spirit to
 * xfs_trans_apply_sb_deltas().
 * Go thru all the dquots belonging to this transaction and modify the
 * INCORE dquot to reflect the actual usages.
 * Unreserve just the reservations done by this transaction.
 * dquot is still left locked at exit.
 */
STATIC void
xfs_trans_apply_dquot_deltas(
	xfs_trans_t		*tp)
{
	int			i, j;
	xfs_dquot_t		*dqp;
	xfs_dqtrx_t		*qtrx, *qa;
	xfs_disk_dquot_t	*d;
	long			totalbdelta;
	long			totalrtbdelta;

	if (! (tp->t_flags & XFS_TRANS_DQ_DIRTY))
		return;

	ASSERT(tp->t_dqinfo);
	qa = tp->t_dqinfo->dqa_usrdquots;
	for (j = 0; j < 2; j++) {
		if (qa[0].qt_dquot == NULL) {
			qa = tp->t_dqinfo->dqa_grpdquots;
			continue;
		}

		/*
		 * Lock all of the dquots and join them to the transaction.
		 */
		xfs_trans_dqlockedjoin(tp, qa);

		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			qtrx = &qa[i];
			/*
			 * The array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;

			ASSERT(XFS_DQ_IS_LOCKED(dqp));
			ASSERT(XFS_DQ_IS_ADDEDTO_TRX(tp, dqp));

			/*
			 * adjust the actual number of blocks used
			 */
			d = &dqp->q_core;

			/*
			 * The issue here is - sometimes we don't make a blkquota
			 * reservation intentionally to be fair to users
			 * (when the amount is small). On the other hand,
			 * delayed allocs do make reservations, but that's
			 * outside of a transaction, so we have no
			 * idea how much was really reserved.
			 * So, here we've accumulated delayed allocation blks and
			 * non-delay blks. The assumption is that the
			 * delayed ones are always reserved (outside of a
			 * transaction), and the others may or may not have
			 * quota reservations.
			 */
			totalbdelta = qtrx->qt_bcount_delta +
				qtrx->qt_delbcnt_delta;
			totalrtbdelta = qtrx->qt_rtbcount_delta +
				qtrx->qt_delrtb_delta;
#ifdef QUOTADEBUG
			if (totalbdelta < 0)
				ASSERT(INT_GET(d->d_bcount, ARCH_CONVERT) >=
				       (xfs_qcnt_t) -totalbdelta);

			if (totalrtbdelta < 0)
				ASSERT(INT_GET(d->d_rtbcount, ARCH_CONVERT) >=
				       (xfs_qcnt_t) -totalrtbdelta);

			if (qtrx->qt_icount_delta < 0)
				ASSERT(INT_GET(d->d_icount, ARCH_CONVERT) >=
				       (xfs_qcnt_t) -qtrx->qt_icount_delta);
#endif
			if (totalbdelta)
				INT_MOD(d->d_bcount, ARCH_CONVERT, (xfs_qcnt_t)totalbdelta);

			if (qtrx->qt_icount_delta)
				INT_MOD(d->d_icount, ARCH_CONVERT, (xfs_qcnt_t)qtrx->qt_icount_delta);

			if (totalrtbdelta)
				INT_MOD(d->d_rtbcount, ARCH_CONVERT, (xfs_qcnt_t)totalrtbdelta);

			/*
			 * Get any default limits in use.
			 * Start/reset the timer(s) if needed.
			 */
			if (d->d_id) {
				xfs_qm_adjust_dqlimits(tp->t_mountp, d);
				xfs_qm_adjust_dqtimers(tp->t_mountp, d);
			}

			dqp->dq_flags |= XFS_DQ_DIRTY;
			/*
			 * add this to the list of items to get logged
			 */
			xfs_trans_log_dquot(tp, dqp);
			/*
			 * Take off what's left of the original reservation.
			 * In case of delayed allocations, there's no
			 * reservation that a transaction structure knows of.
			 */
			if (qtrx->qt_blk_res != 0) {
				if (qtrx->qt_blk_res != qtrx->qt_blk_res_used) {
					if (qtrx->qt_blk_res >
					    qtrx->qt_blk_res_used)
						dqp->q_res_bcount -= (xfs_qcnt_t)
							(qtrx->qt_blk_res -
							 qtrx->qt_blk_res_used);
					else
						dqp->q_res_bcount -= (xfs_qcnt_t)
							(qtrx->qt_blk_res_used -
							 qtrx->qt_blk_res);
				}
			} else {
				/*
				 * These blks were never reserved, either inside
				 * a transaction or outside one (in a delayed
				 * allocation). Also, this isn't always a
				 * negative number since we sometimes
				 * deliberately skip quota reservations.
				 */
				if (qtrx->qt_bcount_delta) {
					dqp->q_res_bcount +=
					      (xfs_qcnt_t)qtrx->qt_bcount_delta;
				}
			}
			/*
			 * Adjust the RT reservation.
			 */
			if (qtrx->qt_rtblk_res != 0) {
				if (qtrx->qt_blk_res != qtrx->qt_blk_res_used) {
					if (qtrx->qt_rtblk_res >
					    qtrx->qt_rtblk_res_used)
					       dqp->q_res_rtbcount -= (xfs_qcnt_t)
						       (qtrx->qt_rtblk_res -
							qtrx->qt_rtblk_res_used);
					else
					       dqp->q_res_rtbcount -= (xfs_qcnt_t)
						       (qtrx->qt_rtblk_res_used -
							qtrx->qt_rtblk_res);
				}
			} else {
				if (qtrx->qt_rtbcount_delta)
					dqp->q_res_rtbcount +=
					    (xfs_qcnt_t)qtrx->qt_rtbcount_delta;
			}

			/*
			 * Adjust the inode reservation.
			 */
			if (qtrx->qt_ino_res != 0) {
				ASSERT(qtrx->qt_ino_res >=
				       qtrx->qt_ino_res_used);
				if (qtrx->qt_ino_res > qtrx->qt_ino_res_used)
					dqp->q_res_icount -= (xfs_qcnt_t)
						(qtrx->qt_ino_res -
						 qtrx->qt_ino_res_used);
			} else {
				if (qtrx->qt_icount_delta)
					dqp->q_res_icount +=
					    (xfs_qcnt_t)qtrx->qt_icount_delta;
			}


#ifdef QUOTADEBUG
			if (qtrx->qt_rtblk_res != 0)
				cmn_err(CE_DEBUG, "RT res %d for 0x%p\n",
					(int) qtrx->qt_rtblk_res, dqp);
#endif
			ASSERT(dqp->q_res_bcount >=
				INT_GET(dqp->q_core.d_bcount, ARCH_CONVERT));
			ASSERT(dqp->q_res_icount >=
				INT_GET(dqp->q_core.d_icount, ARCH_CONVERT));
			ASSERT(dqp->q_res_rtbcount >=
				INT_GET(dqp->q_core.d_rtbcount, ARCH_CONVERT));
		}
		/*
		 * Do the group quotas next
		 */
		qa = tp->t_dqinfo->dqa_grpdquots;
	}
}

/*
 * Release the reservations, and adjust the dquots accordingly.
 * This is called only when the transaction is being aborted. If by
 * any chance we have done dquot modifications incore (ie. deltas) already,
 * we simply throw those away, since that's the expected behavior
 * when a transaction is curtailed without a commit.
 */
STATIC void
xfs_trans_unreserve_and_mod_dquots(
	xfs_trans_t		*tp)
{
	int			i, j;
	xfs_dquot_t		*dqp;
	xfs_dqtrx_t		*qtrx, *qa;
	boolean_t		locked;

	if (!tp->t_dqinfo || !(tp->t_flags & XFS_TRANS_DQ_DIRTY))
		return;

	qa = tp->t_dqinfo->dqa_usrdquots;

	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			qtrx = &qa[i];
			/*
			 * We assume that the array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;
			/*
			 * Unreserve the original reservation. We don't care
			 * about the number of blocks used field, or deltas.
			 * Also we don't bother to zero the fields.
			 */
			locked = B_FALSE;
			if (qtrx->qt_blk_res) {
				xfs_dqlock(dqp);
				locked = B_TRUE;
				dqp->q_res_bcount -=
					(xfs_qcnt_t)qtrx->qt_blk_res;
			}
			if (qtrx->qt_ino_res) {
				if (!locked) {
					xfs_dqlock(dqp);
					locked = B_TRUE;
				}
				dqp->q_res_icount -=
					(xfs_qcnt_t)qtrx->qt_ino_res;
			}

			if (qtrx->qt_rtblk_res) {
				if (!locked) {
					xfs_dqlock(dqp);
					locked = B_TRUE;
				}
				dqp->q_res_rtbcount -=
					(xfs_qcnt_t)qtrx->qt_rtblk_res;
			}
			if (locked)
				xfs_dqunlock(dqp);

		}
		qa = tp->t_dqinfo->dqa_grpdquots;
	}
}

/*
 * This reserves disk blocks and inodes against a dquot.
 * Flags indicate if the dquot is to be locked here and also
 * if the blk reservation is for RT or regular blocks.
 * Sending in XFS_QMOPT_FORCE_RES flag skips the quota check.
 * Returns EDQUOT if quota is exceeded.
 */
STATIC int
xfs_trans_dqresv(
	xfs_trans_t	*tp,
	xfs_mount_t	*mp,
	xfs_dquot_t	*dqp,
	long		nblks,
	long		ninos,
	uint		flags)
{
	int		error;
	xfs_qcnt_t	hardlimit;
	xfs_qcnt_t	softlimit;
	time_t		btimer;
	xfs_qcnt_t	*resbcountp;
	xfs_quotainfo_t	*q = mp->m_quotainfo;

	if (! (flags & XFS_QMOPT_DQLOCK)) {
		xfs_dqlock(dqp);
	}
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	if (flags & XFS_TRANS_DQ_RES_BLKS) {
		hardlimit = INT_GET(dqp->q_core.d_blk_hardlimit, ARCH_CONVERT);
		if (!hardlimit)
			hardlimit = q->qi_bhardlimit;
		softlimit = INT_GET(dqp->q_core.d_blk_softlimit, ARCH_CONVERT);
		if (!softlimit)
			softlimit = q->qi_bsoftlimit;
		btimer = INT_GET(dqp->q_core.d_btimer, ARCH_CONVERT);
		resbcountp = &dqp->q_res_bcount;
	} else {
		ASSERT(flags & XFS_TRANS_DQ_RES_RTBLKS);
		hardlimit = INT_GET(dqp->q_core.d_rtb_hardlimit, ARCH_CONVERT);
		if (!hardlimit)
			hardlimit = q->qi_rtbhardlimit;
		softlimit = INT_GET(dqp->q_core.d_rtb_softlimit, ARCH_CONVERT);
		if (!softlimit)
			softlimit = q->qi_rtbsoftlimit;
		btimer = INT_GET(dqp->q_core.d_rtbtimer, ARCH_CONVERT);
		resbcountp = &dqp->q_res_rtbcount;
	}
	error = 0;

	if ((flags & XFS_QMOPT_FORCE_RES) == 0 &&
	    dqp->q_core.d_id &&
	    XFS_IS_QUOTA_ENFORCED(dqp->q_mount)) {
#ifdef QUOTADEBUG
		cmn_err(CE_DEBUG, "BLK Res: nblks=%ld + resbcount=%Ld"
			  " > hardlimit=%Ld?", nblks, *resbcountp, hardlimit);
#endif
		if (nblks > 0) {
			/*
			 * dquot is locked already. See if we'd go over the
			 * hardlimit or exceed the timelimit if we allocate
			 * nblks.
			 */
			if (hardlimit > 0ULL &&
			     (hardlimit <= nblks + *resbcountp)) {
				error = EDQUOT;
				goto error_return;
			}

			if (softlimit > 0ULL &&
			     (softlimit <= nblks + *resbcountp)) {
				/*
				 * If timer or warnings has expired,
				 * return EDQUOT
				 */
				if ((btimer != 0 && get_seconds() > btimer) ||
				    (dqp->q_core.d_bwarns &&
				     INT_GET(dqp->q_core.d_bwarns, ARCH_CONVERT) >=
				     XFS_QI_BWARNLIMIT(dqp->q_mount))) {
					error = EDQUOT;
					goto error_return;
				}
			}
		}
		if (ninos > 0) {
			hardlimit = INT_GET(dqp->q_core.d_ino_hardlimit, ARCH_CONVERT);
			if (!hardlimit)
				hardlimit = q->qi_ihardlimit;
			softlimit = INT_GET(dqp->q_core.d_ino_softlimit, ARCH_CONVERT);
			if (!softlimit)
				softlimit = q->qi_isoftlimit;
			if (hardlimit > 0ULL &&
			    INT_GET(dqp->q_core.d_icount, ARCH_CONVERT) >= hardlimit) {
				error = EDQUOT;
				goto error_return;
			} else if (softlimit > 0ULL &&
				   INT_GET(dqp->q_core.d_icount, ARCH_CONVERT) >= softlimit) {
				/*
				 * If timer or warnings has expired,
				 * return EDQUOT
				 */
				if ((dqp->q_core.d_itimer &&
				     get_seconds() > INT_GET(dqp->q_core.d_itimer, ARCH_CONVERT)) ||
				    (dqp->q_core.d_iwarns &&
				     INT_GET(dqp->q_core.d_iwarns, ARCH_CONVERT) >=
				     XFS_QI_IWARNLIMIT(dqp->q_mount))) {
					error = EDQUOT;
					goto error_return;
				}
			}
		}
	}

	/*
	 * Change the reservation, but not the actual usage.
	 * Note that q_res_bcount = q_core.d_bcount + resv
	 */
	(*resbcountp) += (xfs_qcnt_t)nblks;
	if (ninos != 0)
		dqp->q_res_icount += (xfs_qcnt_t)ninos;

	/*
	 * note the reservation amt in the trans struct too,
	 * so that the transaction knows how much was reserved by
	 * it against this particular dquot.
	 * We don't do this when we are reserving for a delayed allocation,
	 * because we don't have the luxury of a transaction envelope then.
	 */
	if (tp) {
		ASSERT(tp->t_dqinfo);
		ASSERT(flags & XFS_QMOPT_RESBLK_MASK);
		if (nblks != 0)
			xfs_trans_mod_dquot(tp, dqp,
					    flags & XFS_QMOPT_RESBLK_MASK,
					    nblks);
		if (ninos != 0)
			xfs_trans_mod_dquot(tp, dqp,
					    XFS_TRANS_DQ_RES_INOS,
					    ninos);
	}
	ASSERT(dqp->q_res_bcount >= INT_GET(dqp->q_core.d_bcount, ARCH_CONVERT));
	ASSERT(dqp->q_res_rtbcount >= INT_GET(dqp->q_core.d_rtbcount, ARCH_CONVERT));
	ASSERT(dqp->q_res_icount >= INT_GET(dqp->q_core.d_icount, ARCH_CONVERT));

error_return:
	if (! (flags & XFS_QMOPT_DQLOCK)) {
		xfs_dqunlock(dqp);
	}
	return (error);
}


/*
 * Given a dquot(s), make disk block and/or inode reservations against them.
 * The fact that this does the reservation against both the usr and
 * grp quotas is important, because this follows a both-or-nothing
 * approach.
 *
 * flags = XFS_QMOPT_DQLOCK indicate if dquot(s) need to be locked.
 *	   XFS_QMOPT_FORCE_RES evades limit enforcement. Used by chown.
 *	   XFS_TRANS_DQ_RES_BLKS reserves regular disk blocks
 *	   XFS_TRANS_DQ_RES_RTBLKS reserves realtime disk blocks
 * dquots are unlocked on return, if they were not locked by caller.
 */
int
xfs_trans_reserve_quota_bydquots(
	xfs_trans_t	*tp,
	xfs_mount_t	*mp,
	xfs_dquot_t	*udqp,
	xfs_dquot_t	*gdqp,
	long		nblks,
	long		ninos,
	uint		flags)
{
	int		resvd;

	if (! XFS_IS_QUOTA_ON(mp))
		return (0);

	if (tp && tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);

	ASSERT(flags & XFS_QMOPT_RESBLK_MASK);
	resvd = 0;

	if (udqp) {
		if (xfs_trans_dqresv(tp, mp, udqp, nblks, ninos, flags))
			return (EDQUOT);
		resvd = 1;
	}

	if (gdqp) {
		if (xfs_trans_dqresv(tp, mp, gdqp, nblks, ninos, flags)) {
			/*
			 * can't do it, so backout previous reservation
			 */
			if (resvd) {
				flags |= XFS_QMOPT_FORCE_RES;
				xfs_trans_dqresv(tp, mp, udqp,
						 -nblks, -ninos, flags);
			}
			return (EDQUOT);
		}
	}

	/*
	 * Didnt change anything critical, so, no need to log
	 */
	return (0);
}


/*
 * Lock the dquot and change the reservation if we can.
 * This doesn't change the actual usage, just the reservation.
 * The inode sent in is locked.
 *
 * Returns 0 on success, EDQUOT or other errors otherwise
 */
STATIC int
xfs_trans_reserve_quota_nblks(
	xfs_trans_t	*tp,
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	long		nblks,
	long		ninos,
	uint		type)
{
	int		error;

	if (!XFS_IS_QUOTA_ON(mp))
		return (0);

	ASSERT(ip->i_ino != mp->m_sb.sb_uquotino);
	ASSERT(ip->i_ino != mp->m_sb.sb_gquotino);

	ASSERT(XFS_ISLOCKED_INODE_EXCL(ip));
	ASSERT(XFS_IS_QUOTA_RUNNING(ip->i_mount));
	ASSERT((type & ~XFS_QMOPT_FORCE_RES) == XFS_TRANS_DQ_RES_RTBLKS ||
	       (type & ~XFS_QMOPT_FORCE_RES) == XFS_TRANS_DQ_RES_BLKS);

	/*
	 * Reserve nblks against these dquots, with trans as the mediator.
	 */
	error = xfs_trans_reserve_quota_bydquots(tp, mp,
						 ip->i_udquot, ip->i_gdquot,
						 nblks, ninos,
						 type);
	return (error);
}

/*
 * This routine is called to allocate a quotaoff log item.
 */
xfs_qoff_logitem_t *
xfs_trans_get_qoff_item(
	xfs_trans_t		*tp,
	xfs_qoff_logitem_t	*startqoff,
	uint			flags)
{
	xfs_qoff_logitem_t	*q;

	ASSERT(tp != NULL);

	q = xfs_qm_qoff_logitem_init(tp->t_mountp, startqoff, flags);
	ASSERT(q != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)q);

	return (q);
}


/*
 * This is called to mark the quotaoff logitem as needing
 * to be logged when the transaction is committed.  The logitem must
 * already be associated with the given transaction.
 */
void
xfs_trans_log_quotaoff_item(
	xfs_trans_t		*tp,
	xfs_qoff_logitem_t	*qlp)
{
	xfs_log_item_desc_t	*lidp;

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t *)qlp);
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;
}

STATIC void
xfs_trans_alloc_dqinfo(
	xfs_trans_t	*tp)
{
	(tp)->t_dqinfo = kmem_zone_zalloc(xfs_Gqm->qm_dqtrxzone, KM_SLEEP);
}

STATIC void
xfs_trans_free_dqinfo(
	xfs_trans_t	*tp)
{
	if (!tp->t_dqinfo)
		return;
	kmem_zone_free(xfs_Gqm->qm_dqtrxzone, (tp)->t_dqinfo);
	(tp)->t_dqinfo = NULL;
}

xfs_dqtrxops_t	xfs_trans_dquot_ops = {
	.qo_dup_dqinfo			= xfs_trans_dup_dqinfo,
	.qo_free_dqinfo			= xfs_trans_free_dqinfo,
	.qo_mod_dquot_byino		= xfs_trans_mod_dquot_byino,
	.qo_apply_dquot_deltas		= xfs_trans_apply_dquot_deltas,
	.qo_reserve_quota_nblks		= xfs_trans_reserve_quota_nblks,
	.qo_reserve_quota_bydquots	= xfs_trans_reserve_quota_bydquots,
	.qo_unreserve_and_mod_dquots	= xfs_trans_unreserve_and_mod_dquots,
};

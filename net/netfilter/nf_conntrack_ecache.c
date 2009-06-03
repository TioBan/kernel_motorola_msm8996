/* Event cache for netfilter. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>

static DEFINE_MUTEX(nf_ct_ecache_mutex);

struct nf_ct_event_notifier *nf_conntrack_event_cb __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_event_cb);

struct nf_exp_event_notifier *nf_expect_event_cb __read_mostly;
EXPORT_SYMBOL_GPL(nf_expect_event_cb);

/* deliver cached events and clear cache entry - must be called with locally
 * disabled softirqs */
static inline void
__nf_ct_deliver_cached_events(struct nf_conntrack_ecache *ecache)
{
	struct nf_ct_event_notifier *notify;

	rcu_read_lock();
	notify = rcu_dereference(nf_conntrack_event_cb);
	if (notify == NULL)
		goto out_unlock;

	if (nf_ct_is_confirmed(ecache->ct) && !nf_ct_is_dying(ecache->ct)
	    && ecache->events) {
		struct nf_ct_event item = {
			.ct 	= ecache->ct,
			.pid	= 0,
			.report	= 0
		};

		notify->fcn(ecache->events, &item);
	}

	ecache->events = 0;
	nf_ct_put(ecache->ct);
	ecache->ct = NULL;

out_unlock:
	rcu_read_unlock();
}

/* Deliver all cached events for a particular conntrack. This is called
 * by code prior to async packet handling for freeing the skb */
void nf_ct_deliver_cached_events(const struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	struct nf_conntrack_ecache *ecache;

	local_bh_disable();
	ecache = per_cpu_ptr(net->ct.ecache, raw_smp_processor_id());
	if (ecache->ct == ct)
		__nf_ct_deliver_cached_events(ecache);
	local_bh_enable();
}
EXPORT_SYMBOL_GPL(nf_ct_deliver_cached_events);

/* Deliver cached events for old pending events, if current conntrack != old */
void __nf_ct_event_cache_init(struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	struct nf_conntrack_ecache *ecache;

	/* take care of delivering potentially old events */
	ecache = per_cpu_ptr(net->ct.ecache, raw_smp_processor_id());
	BUG_ON(ecache->ct == ct);
	if (ecache->ct)
		__nf_ct_deliver_cached_events(ecache);
	/* initialize for this conntrack/packet */
	ecache->ct = ct;
	nf_conntrack_get(&ct->ct_general);
}
EXPORT_SYMBOL_GPL(__nf_ct_event_cache_init);

/* flush the event cache - touches other CPU's data and must not be called
 * while packets are still passing through the code */
void nf_ct_event_cache_flush(struct net *net)
{
	struct nf_conntrack_ecache *ecache;
	int cpu;

	for_each_possible_cpu(cpu) {
		ecache = per_cpu_ptr(net->ct.ecache, cpu);
		if (ecache->ct)
			nf_ct_put(ecache->ct);
	}
}

int nf_conntrack_ecache_init(struct net *net)
{
	net->ct.ecache = alloc_percpu(struct nf_conntrack_ecache);
	if (!net->ct.ecache)
		return -ENOMEM;
	return 0;
}

void nf_conntrack_ecache_fini(struct net *net)
{
	free_percpu(net->ct.ecache);
}

int nf_conntrack_register_notifier(struct nf_ct_event_notifier *new)
{
	int ret = 0;
	struct nf_ct_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_conntrack_event_cb);
	if (notify != NULL) {
		ret = -EBUSY;
		goto out_unlock;
	}
	rcu_assign_pointer(nf_conntrack_event_cb, new);
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;

out_unlock:
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_register_notifier);

void nf_conntrack_unregister_notifier(struct nf_ct_event_notifier *new)
{
	struct nf_ct_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_conntrack_event_cb);
	BUG_ON(notify != new);
	rcu_assign_pointer(nf_conntrack_event_cb, NULL);
	mutex_unlock(&nf_ct_ecache_mutex);
}
EXPORT_SYMBOL_GPL(nf_conntrack_unregister_notifier);

int nf_ct_expect_register_notifier(struct nf_exp_event_notifier *new)
{
	int ret = 0;
	struct nf_exp_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_expect_event_cb);
	if (notify != NULL) {
		ret = -EBUSY;
		goto out_unlock;
	}
	rcu_assign_pointer(nf_expect_event_cb, new);
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;

out_unlock:
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_register_notifier);

void nf_ct_expect_unregister_notifier(struct nf_exp_event_notifier *new)
{
	struct nf_exp_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_expect_event_cb);
	BUG_ON(notify != new);
	rcu_assign_pointer(nf_expect_event_cb, NULL);
	mutex_unlock(&nf_ct_ecache_mutex);
}
EXPORT_SYMBOL_GPL(nf_ct_expect_unregister_notifier);

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/tcp.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <net/inetpeer.h>
#include <net/tcp.h>

int sysctl_tcp_fastopen __read_mostly = TFO_CLIENT_ENABLE;

struct tcp_fastopen_context __rcu *tcp_fastopen_ctx;

static DEFINE_SPINLOCK(tcp_fastopen_ctx_lock);

void tcp_fastopen_init_key_once(bool publish)
{
	static u8 key[TCP_FASTOPEN_KEY_LENGTH];

	/* tcp_fastopen_reset_cipher publishes the new context
	 * atomically, so we allow this race happening here.
	 *
	 * All call sites of tcp_fastopen_cookie_gen also check
	 * for a valid cookie, so this is an acceptable risk.
	 */
	if (net_get_random_once(key, sizeof(key)) && publish)
		tcp_fastopen_reset_cipher(key, sizeof(key));
}

static void tcp_fastopen_ctx_free(struct rcu_head *head)
{
	struct tcp_fastopen_context *ctx =
	    container_of(head, struct tcp_fastopen_context, rcu);
	crypto_free_cipher(ctx->tfm);
	kfree(ctx);
}

int tcp_fastopen_reset_cipher(void *key, unsigned int len)
{
	int err;
	struct tcp_fastopen_context *ctx, *octx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->tfm = crypto_alloc_cipher("aes", 0, 0);

	if (IS_ERR(ctx->tfm)) {
		err = PTR_ERR(ctx->tfm);
error:		kfree(ctx);
		pr_err("TCP: TFO aes cipher alloc error: %d\n", err);
		return err;
	}
	err = crypto_cipher_setkey(ctx->tfm, key, len);
	if (err) {
		pr_err("TCP: TFO cipher key error: %d\n", err);
		crypto_free_cipher(ctx->tfm);
		goto error;
	}
	memcpy(ctx->key, key, len);

	spin_lock(&tcp_fastopen_ctx_lock);

	octx = rcu_dereference_protected(tcp_fastopen_ctx,
				lockdep_is_held(&tcp_fastopen_ctx_lock));
	rcu_assign_pointer(tcp_fastopen_ctx, ctx);
	spin_unlock(&tcp_fastopen_ctx_lock);

	if (octx)
		call_rcu(&octx->rcu, tcp_fastopen_ctx_free);
	return err;
}

/* Computes the fastopen cookie for the IP path.
 * The path is a 128 bits long (pad with zeros for IPv4).
 *
 * The caller must check foc->len to determine if a valid cookie
 * has been generated successfully.
*/
void tcp_fastopen_cookie_gen(__be32 src, __be32 dst,
			     struct tcp_fastopen_cookie *foc)
{
	__be32 path[4] = { src, dst, 0, 0 };
	struct tcp_fastopen_context *ctx;

	tcp_fastopen_init_key_once(true);

	rcu_read_lock();
	ctx = rcu_dereference(tcp_fastopen_ctx);
	if (ctx) {
		crypto_cipher_encrypt_one(ctx->tfm, foc->val, (__u8 *)path);
		foc->len = TCP_FASTOPEN_COOKIE_SIZE;
	}
	rcu_read_unlock();
}

int tcp_fastopen_create_child(struct sock *sk,
			      struct sk_buff *skb,
			      struct sk_buff *skb_synack,
			      struct request_sock *req)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct request_sock_queue *queue = &inet_csk(sk)->icsk_accept_queue;
	const struct inet_request_sock *ireq = inet_rsk(req);
	struct sock *child;
	int err;

	req->num_retrans = 0;
	req->num_timeout = 0;
	req->sk = NULL;

	child = inet_csk(sk)->icsk_af_ops->syn_recv_sock(sk, skb, req, NULL);
	if (child == NULL) {
		NET_INC_STATS_BH(sock_net(sk),
				 LINUX_MIB_TCPFASTOPENPASSIVEFAIL);
		kfree_skb(skb_synack);
		return -1;
	}
	err = ip_build_and_send_pkt(skb_synack, sk, ireq->ir_loc_addr,
				    ireq->ir_rmt_addr, ireq->opt);
	err = net_xmit_eval(err);
	if (!err)
		tcp_rsk(req)->snt_synack = tcp_time_stamp;
	/* XXX (TFO) - is it ok to ignore error and continue? */

	spin_lock(&queue->fastopenq->lock);
	queue->fastopenq->qlen++;
	spin_unlock(&queue->fastopenq->lock);

	/* Initialize the child socket. Have to fix some values to take
	 * into account the child is a Fast Open socket and is created
	 * only out of the bits carried in the SYN packet.
	 */
	tp = tcp_sk(child);

	tp->fastopen_rsk = req;
	/* Do a hold on the listner sk so that if the listener is being
	 * closed, the child that has been accepted can live on and still
	 * access listen_lock.
	 */
	sock_hold(sk);
	tcp_rsk(req)->listener = sk;

	/* RFC1323: The window in SYN & SYN/ACK segments is never
	 * scaled. So correct it appropriately.
	 */
	tp->snd_wnd = ntohs(tcp_hdr(skb)->window);

	/* Activate the retrans timer so that SYNACK can be retransmitted.
	 * The request socket is not added to the SYN table of the parent
	 * because it's been added to the accept queue directly.
	 */
	inet_csk_reset_xmit_timer(child, ICSK_TIME_RETRANS,
				  TCP_TIMEOUT_INIT, TCP_RTO_MAX);

	/* Add the child socket directly into the accept queue */
	inet_csk_reqsk_queue_add(sk, req, child);

	/* Now finish processing the fastopen child socket. */
	inet_csk(child)->icsk_af_ops->rebuild_header(child);
	tcp_init_congestion_control(child);
	tcp_mtup_init(child);
	tcp_init_metrics(child);
	tcp_init_buffer_space(child);

	/* Queue the data carried in the SYN packet. We need to first
	 * bump skb's refcnt because the caller will attempt to free it.
	 *
	 * XXX (TFO) - we honor a zero-payload TFO request for now.
	 * (Any reason not to?)
	 */
	if (TCP_SKB_CB(skb)->end_seq == TCP_SKB_CB(skb)->seq + 1) {
		/* Don't queue the skb if there is no payload in SYN.
		 * XXX (TFO) - How about SYN+FIN?
		 */
		tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
	} else {
		skb = skb_get(skb);
		skb_dst_drop(skb);
		__skb_pull(skb, tcp_hdr(skb)->doff * 4);
		skb_set_owner_r(skb, child);
		__skb_queue_tail(&child->sk_receive_queue, skb);
		tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		tp->syn_data_acked = 1;
	}
	sk->sk_data_ready(sk);
	bh_unlock_sock(child);
	sock_put(child);
	WARN_ON(req->sk == NULL);
	return 0;
}
EXPORT_SYMBOL(tcp_fastopen_create_child);

static bool tcp_fastopen_queue_check(struct sock *sk)
{
	struct fastopen_queue *fastopenq;

	/* Make sure the listener has enabled fastopen, and we don't
	 * exceed the max # of pending TFO requests allowed before trying
	 * to validating the cookie in order to avoid burning CPU cycles
	 * unnecessarily.
	 *
	 * XXX (TFO) - The implication of checking the max_qlen before
	 * processing a cookie request is that clients can't differentiate
	 * between qlen overflow causing Fast Open to be disabled
	 * temporarily vs a server not supporting Fast Open at all.
	 */
	fastopenq = inet_csk(sk)->icsk_accept_queue.fastopenq;
	if (fastopenq == NULL || fastopenq->max_qlen == 0)
		return false;

	if (fastopenq->qlen >= fastopenq->max_qlen) {
		struct request_sock *req1;
		spin_lock(&fastopenq->lock);
		req1 = fastopenq->rskq_rst_head;
		if ((req1 == NULL) || time_after(req1->expires, jiffies)) {
			spin_unlock(&fastopenq->lock);
			NET_INC_STATS_BH(sock_net(sk),
					 LINUX_MIB_TCPFASTOPENLISTENOVERFLOW);
			return false;
		}
		fastopenq->rskq_rst_head = req1->dl_next;
		fastopenq->qlen--;
		spin_unlock(&fastopenq->lock);
		reqsk_free(req1);
	}
	return true;
}

bool tcp_fastopen_check(struct sock *sk, struct sk_buff *skb,
			struct request_sock *req,
			struct tcp_fastopen_cookie *foc,
			struct tcp_fastopen_cookie *valid_foc)
{
	bool skip_cookie = false;

	if (likely(!fastopen_cookie_present(foc))) {
		/* See include/net/tcp.h for the meaning of these knobs */
		if ((sysctl_tcp_fastopen & TFO_SERVER_ALWAYS) ||
		    ((sysctl_tcp_fastopen & TFO_SERVER_COOKIE_NOT_REQD) &&
		    (TCP_SKB_CB(skb)->end_seq != TCP_SKB_CB(skb)->seq + 1)))
			skip_cookie = true; /* no cookie to validate */
		else
			return false;
	}
	/* A FO option is present; bump the counter. */
	NET_INC_STATS_BH(sock_net(sk), LINUX_MIB_TCPFASTOPENPASSIVE);

	if ((sysctl_tcp_fastopen & TFO_SERVER_ENABLE) == 0 ||
	    !tcp_fastopen_queue_check(sk))
		return false;

	if (skip_cookie) {
		tcp_rsk(req)->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		return true;
	}

	if (foc->len == TCP_FASTOPEN_COOKIE_SIZE) {
		if ((sysctl_tcp_fastopen & TFO_SERVER_COOKIE_NOT_CHKED) == 0) {
			tcp_fastopen_cookie_gen(ip_hdr(skb)->saddr,
						ip_hdr(skb)->daddr, valid_foc);
			if ((valid_foc->len != TCP_FASTOPEN_COOKIE_SIZE) ||
			    memcmp(&foc->val[0], &valid_foc->val[0],
			    TCP_FASTOPEN_COOKIE_SIZE) != 0)
				return false;
			valid_foc->len = -1;
		}
		/* Acknowledge the data received from the peer. */
		tcp_rsk(req)->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		return true;
	} else if (foc->len == 0) { /* Client requesting a cookie */
		tcp_fastopen_cookie_gen(ip_hdr(skb)->saddr,
					ip_hdr(skb)->daddr, valid_foc);
		NET_INC_STATS_BH(sock_net(sk),
		    LINUX_MIB_TCPFASTOPENCOOKIEREQD);
	} else {
		/* Client sent a cookie with wrong size. Treat it
		 * the same as invalid and return a valid one.
		 */
		tcp_fastopen_cookie_gen(ip_hdr(skb)->saddr,
					ip_hdr(skb)->daddr, valid_foc);
	}
	return false;
}
EXPORT_SYMBOL(tcp_fastopen_check);

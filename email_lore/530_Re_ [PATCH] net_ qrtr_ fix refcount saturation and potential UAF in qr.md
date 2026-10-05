# Re: [PATCH] net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove

| Field | Value |
| :--- | :--- |
| From | Simon Horman <horms@kernel.org> |
| To | author-a-alt@example.com |
| Cc | 'Simon Horman' <horms@kernel.org>, mani@kernel.org, davem@davemloft.net, edumazet@google.com, kuba@kernel.org, pabeni@redhat.com, luca@lucaweiss.eu, andersson@kernel.org, linux-arm-msm@vger.kernel.org, netdev@vger.kernel.org, linux-kernel@vger.kernel.org, author-a@example.com |
| Date | Wed, 3 Jun 2026 21:40:39 +0100 |
| Message-ID | `20260603204037.3852840-3-horms@kernel.org` |
| In-Reply-To | `20260530082243.1123402-1-author-a-alt@example.com` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

From: 'Simon Horman' <horms@kernel.org>

This is an AI-generated review of your patch. The human sending this
email has considered the AI review valid, or at least plausible.
Full review at: https://sashiko.dev
---
Subject: net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove

This patch defers the socket reference count decrement in
qrtr_port_remove() until after xa_erase() and synchronize_rcu() to
close a race with concurrent RCU readers, and also switches from
__sock_put() to sock_put().

> diff --git a/net/qrtr/af_qrtr.c b/net/qrtr/af_qrtr.c
> index 7cec6a7859b0..9ddc8657f7db 100644
> --- a/net/qrtr/af_qrtr.c
> +++ b/net/qrtr/af_qrtr.c
> @@ -707,13 +707,13 @@ static void qrtr_port_remove(struct qrtr_sock *ipc)
>  	if (port == QRTR_PORT_CTRL)
>  		port = 0;
>  
> -	__sock_put(&ipc->sk);
> -
>  	xa_erase(&qrtr_ports, port);
>  
>  	/* Ensure that if qrtr_port_lookup() did enter the RCU read section we
>  	 * wait for it to up increment the refcount */
>  	synchronize_rcu();
> +
> +	sock_put(&ipc->sk);
>  }

[Low]
Is the Fixes: tag pointing at the right commit?

> Fixes: a365023a76f2 ("net: qrtr: combine nameservice into main module")

Looking at a365023a76f2, that commit renames qrtr.c to af_qrtr.c and
consolidates the nameservice into the main module; it does not touch the
__sock_put()/xa_erase()/synchronize_rcu() sequence in qrtr_port_remove().

A history search with git log -S '__sock_put(&ipc->sk)' on net/qrtr/
shows this pattern was first introduced by:

  bdabad3e363d ("net: Add Qualcomm IPC router")

Should the Fixes: tag reference bdabad3e363d so stable picks up the fix
on every release in the bdabad3e363d..a365023a76f2 range?

[Low]
Is the rationale given for switching __sock_put() to sock_put() accurate?

The commit message states:

  Furthermore, replace __sock_put() with sock_put() to ensure that the
  socket memory is safely freed if this happens to be the last
  reference, preventing memory leaks.

Can the xarray reference legitimately be the last one at this point?

The two callers of qrtr_port_remove() both keep additional references
alive across the call.

In qrtr_release(), sk is still dereferenced after qrtr_port_remove()
returns:

	if (!sock_flag(sk, SOCK_ZAPPED))
		qrtr_port_remove(ipc);

	skb_queue_purge(&sk->sk_receive_queue);

	release_sock(sk);
	sock_put(sk);

In __qrtr_bind(), the rebind path calls qrtr_port_assign() (which does
sock_hold() for the new port) before qrtr_port_remove():

	rc = qrtr_port_assign(ipc, &port);
	if (rc)
		return rc;

	/* unbind previous, if any */
	if (!zapped)
		qrtr_port_remove(ipc);

If sock_put() ever did free the socket here, qrtr_release()'s subsequent
skb_queue_purge(&sk->sk_receive_queue) and release_sock(sk) would be a
use-after-free, which would indicate a separate refcount-imbalance bug
rather than a memory leak.

Could the commit message be reworded so it doesn't conflate the
__sock_put() to sock_put() change with a memory leak fix?

Simon says: I would go a bit further here. Based on the above
            I think the __sock_put() -> sock_put() should be dropped.

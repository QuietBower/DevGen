# Patch "net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove" has been added to the 5.10-stable tree

| Field | Value |
| :--- | :--- |
| From | Sasha Levin <sashal@kernel.org> |
| To | stable-commits@vger.kernel.org, author-a@example.com |
| Cc | Manivannan Sadhasivam <mani@kernel.org>, "David S. Miller" <davem@davemloft.net>, Eric Dumazet <edumazet@google.com>, Jakub Kicinski <kuba@kernel.org>, Paolo Abeni <pabeni@redhat.com>, Simon Horman <horms@kernel.org> |
| Date | Sat, 13 Jun 2026 08:33:31 -0400 |
| Message-ID | `20260613123331.4067576-1-sashal@kernel.org` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

This is a note to let you know that I've just added the patch titled

    net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove

to the 5.10-stable tree which can be found at:
    http://www.kernel.org/git/?p=linux/kernel/git/stable/stable-queue.git;a=summary

The filename of the patch is:
     net-qrtr-fix-refcount-saturation-and-potential-uaf-i.patch
and it can be found in the queue-5.10 subdirectory.

If you, or anyone else, feels it should not be added to the stable tree,
please let <stable@vger.kernel.org> know about it.



commit 7c5728490616aa7ba54fbca3e7c6aaa1723e37d4
Author: Author A <author-a@example.com>
Date:   Thu Jun 4 14:48:01 2026 +0800

    net: qrtr: fix refcount saturation and potential UAF in qrtr_port_remove
    
    [ Upstream commit a2171131ecda1ed61a594a1eb715e75fdad0fef5 ]
    
    In qrtr_port_remove(), the socket reference count is decremented via
    __sock_put() before the port is removed from the qrtr_ports XArray and
    before the RCU grace period elapses.
    
    This breaks the fundamental RCU update paradigm. It exposes a race
    window where a concurrent RCU reader (such as qrtr_reset_ports() or
    qrtr_port_lookup()) can obtain a pointer to the socket from the XArray,
    and attempt to call sock_hold() on a socket whose reference count has
    already dropped to zero.
    
    This exact race condition was hit during syzkaller fuzzing, leading to
    the following refcount saturation warning and a potential Use-After-Free:
    
      refcount_t: saturated; leaking memory.
      WARNING: CPU: 3 PID: 1273 at lib/refcount.c:22 refcount_warn_saturate+0xae/0x1d0
      Modules linked in: qrtr(+) bochs drm_shmem_helper ...
      Call Trace:
       <TASK>
       qrtr_reset_ports net/qrtr/af_qrtr.c:768 [inline] [qrtr]
       __qrtr_bind.isra.0+0x48b/0x570 net/qrtr/af_qrtr.c:805 [qrtr]
       qrtr_bind+0x17d/0x210 net/qrtr/af_qrtr.c:901 [qrtr]
       kernel_bind+0xe4/0x120 net/socket.c:3592
       qrtr_ns_init+0x1a6/0x380 net/qrtr/ns.c:715 [qrtr]
       qrtr_proto_init+0x3b/0xff0 net/qrtr/af_qrtr.c:169 [qrtr]
       do_one_initcall+0xf5/0x5e0 init/main.c:1283
       ...
       </TASK>
    
    Fix this by deferring the reference count decrement until after the
    xa_erase() and the synchronize_rcu() complete.
    
    (Note: The v1 of this patch incorrectly replaced __sock_put() with
    sock_put(). As Simon Horman pointed out, the callers of qrtr_port_remove()
    still hold a reference to the socket, so freeing the socket memory here
    would lead to a subsequent UAF in the caller. Thus, the __sock_put() is
    kept, but only repositioned to close the RCU race.)
    
    Fixes: bdabad3e363d ("net: Add Qualcomm IPC router")
    Signed-off-by: Author A <author-a@example.com>
    Reviewed-by: Simon Horman <horms@kernel.org>
    Link: `[ANONYMIZED] https://patch.msgid.link/20260604064801.1180388-1-author-a-alt@example.com`
    Signed-off-by: Jakub Kicinski <kuba@kernel.org>
    Signed-off-by: Sasha Levin <sashal@kernel.org>

diff --git a/net/qrtr/af_qrtr.c b/net/qrtr/af_qrtr.c
index fdb7a5a12f0358..fb0398903e4f36 100644
--- a/net/qrtr/af_qrtr.c
+++ b/net/qrtr/af_qrtr.c
@@ -701,13 +701,13 @@ static void qrtr_port_remove(struct qrtr_sock *ipc)
 	if (port == QRTR_PORT_CTRL)
 		port = 0;
 
-	__sock_put(&ipc->sk);
-
 	xa_erase(&qrtr_ports, port);
 
 	/* Ensure that if qrtr_port_lookup() did enter the RCU read section we
 	 * wait for it to up increment the refcount */
 	synchronize_rcu();
+
+	__sock_put(&ipc->sk);
 }
 
 /* Assign port number to socket.

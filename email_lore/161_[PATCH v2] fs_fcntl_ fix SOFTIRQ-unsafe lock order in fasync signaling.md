# [PATCH v2] fs/fcntl: fix SOFTIRQ-unsafe lock order in fasync signaling

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | jlayton@kernel.org, chuck.lever@oracle.com, viro@zeniv.linux.org.uk, brauner@kernel.org |
| Cc | alex.aring@gmail.com, jack@suse.cz, linux-fsdevel@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Sat, 23 May 2026 21:52:10 +0800 |
| Message-ID | `20260523135210.590928-1-author-a-alt@example.com` |
| In-Reply-To | `d15a25fe784b5d4449c7ce4e56f1428d7ff28885.camel@kernel.org` |
| List-ID | <linux-fsdevel.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260523135210.590928-1-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

A SOFTIRQ-safe to SOFTIRQ-unsafe lock order deadlock can occur in
send_sigio() and send_sigurg() when a process group receives a signal.

When FASYNC is configured for a process group (PIDTYPE_PGID), both
functions use read_lock(&tasklist_lock) to traverse the task list.
However, they are frequently called from softirq context:
- send_sigio() via input_inject_event -> kill_fasync
- send_sigurg() via tcp_check_urg -> sk_send_sigurg (NET_RX_SOFTIRQ)

The deadlock is caused by the rwlock writer fairness mechanism:
1. CPU 0 (process context) holds read_lock(&tasklist_lock) in do_wait().
2. CPU 1 (process context) attempts write_lock(&tasklist_lock) in
   fork() or exit() and spins, which blocks all new readers.
3. CPU 0 is interrupted by a softirq (e.g., TCP URG packet reception).
4. The softirq calls send_sigurg() and attempts to acquire
   read_lock(&tasklist_lock), deadlocking because CPU 1 is waiting.

Since PID hashing and do_each_pid_task() traversals are already
RCU-protected, the read_lock on tasklist_lock is no longer strictly
required for safe traversal. Fix this by replacing tasklist_lock with
rcu_read_lock(), aligning the process group signaling path with the
single-PID path. This also mitigates a potential remote denial of
service vector via TCP URG packets.

Lockdep splat:
=====================================================
WARNING: SOFTIRQ-safe -> SOFTIRQ-unsafe lock order detected
[...]
Chain exists of:
  &dev->event_lock --> &f_owner->lock --> tasklist_lock

Possible interrupt unsafe locking scenario:
       CPU0                    CPU1
       ----                    ----
  lock(tasklist_lock);
                           local_irq_disable();
                           lock(&dev->event_lock);
                           lock(&f_owner->lock);
  <Interrupt>
    lock(&dev->event_lock);

*** DEADLOCK ***

Reviewed-by: Jeff Layton <jlayton@kernel.org>
Signed-off-by: Author A <author-a@example.com>
---
Changes in v2:
- Also apply the RCU replacement to send_sigurg() to fix an identical
  deadlock vector triggered by TCP URG packets.
- Add Reviewed-by tag from Jeff Layton.

 fs/fcntl.c | 8 ++++----
 1 file changed, 4 insertions(+), 4 deletions(-)

diff --git a/fs/fcntl.c b/fs/fcntl.c
index beab8080badf..92d643a14196 100644
--- a/fs/fcntl.c
+++ b/fs/fcntl.c
@@ -929,11 +929,11 @@ void send_sigio(struct fown_struct *fown, int fd, int band)
 			send_sigio_to_task(p, fown, fd, band, type);
 		rcu_read_unlock();
 	} else {
-		read_lock(&tasklist_lock);
+		rcu_read_lock();
 		do_each_pid_task(pid, type, p) {
 			send_sigio_to_task(p, fown, fd, band, type);
 		} while_each_pid_task(pid, type, p);
-		read_unlock(&tasklist_lock);
+		rcu_read_unlock();
 	}
  out_unlock_fown:
 	read_unlock_irqrestore(&fown->lock, flags);
@@ -975,11 +975,11 @@ int send_sigurg(struct file *file)
 			send_sigurg_to_task(p, fown, type);
 		rcu_read_unlock();
 	} else {
-		read_lock(&tasklist_lock);
+		rcu_read_lock();
 		do_each_pid_task(pid, type, p) {
 			send_sigurg_to_task(p, fown, type);
 		} while_each_pid_task(pid, type, p);
-		read_unlock(&tasklist_lock);
+		rcu_read_unlock();
 	}
  out_unlock_fown:
 	read_unlock_irqrestore(&fown->lock, flags);
-- 
2.34.1

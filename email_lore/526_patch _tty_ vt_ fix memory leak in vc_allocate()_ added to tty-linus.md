# patch "tty: vt: fix memory leak in vc_allocate()" added to tty-linus

| Field | Value |
| :--- | :--- |
| From | <gregkh@linuxfoundation.org> |
| To | author-a@example.com,gregkh@linuxfoundation.org,stable@kernel.org |
| Date | Wed, 23 Sep 2026 12:01:50 +0200 |
| Message-ID | `2026092350-chevron-resistant-542c@gregkh` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

This is a note to let you know that I've just added the patch titled

    tty: vt: fix memory leak in vc_allocate()

to my tty git tree which can be found at
    git://git.kernel.org/pub/scm/linux/kernel/git/gregkh/tty.git
in the tty-linus branch.

The patch will show up in the next release of the linux-next tree
(usually sometime within the next 24 hours during the week.)

The patch will hopefully also be merged in Linus's tree for the
next -rc kernel release.

If you have any questions about this process, please let me know.


From 5b114bfc1dfd88f9d50822d532f7f1fce0f7388a Mon Sep 17 00:00:00 2001
From: Author A <author-a@example.com>
Date: Mon, 3 Aug 2026 22:45:56 +0800
Subject: tty: vt: fix memory leak in vc_allocate()

If the screen buffer allocation fails in vc_allocate(), the error handling
path jumps to `err_free`. However, this path fails to release the unicode
screen map attached to `vc->uni_pagedict_loc`.

During the early stages of vc_allocate(), the unicode screen map is either
newly allocated via con_set_default_unimap() or shares the default unicode
map from a previously initialized console (which increments its refcount).
If the subsequent kzalloc() for the screen buffer fails, the err_free path
frees the vc structure but leaves the attached uni_pagedict with an
elevated refcount. This results in an unreferenced object memory leak, as
the reference to the dictionary is lost and its refcount can never reach
zero.

This issue was discovered by DevGen (an automated virtual device modeling
fuzzer based on Syzkaller). During fuzzing with kernel fault injection
(failslab) enabled, the fuzzer forcefully failed the kzalloc() for the
screen buffer, exposing this error-handling path leak.

Fix this by checking *vc->uni_pagedict_loc and calling con_free_unimap(vc)
in the err_free path before kfree(vc). This safely decrements the refcount
and releases the dictionary memory if this was the last reference. The
explicit check is added to maintain consistency with other callers.

Fixes: 34902b7f2754 ("tty: vt, get rid of weird source code flow")
Cc: stable <stable@kernel.org>
Signed-off-by: Author A <author-a@example.com>
Link: `[ANONYMIZED] https://patch.msgid.link/20260803144556.163856-1-author-a@example.com`
Signed-off-by: Greg Kroah-Hartman <gregkh@linuxfoundation.org>
---
 drivers/tty/vt/vt.c | 2 ++
 1 file changed, 2 insertions(+)

diff --git a/drivers/tty/vt/vt.c b/drivers/tty/vt/vt.c
index 57edf37495a8..51f8fd103dcf 100644
--- a/drivers/tty/vt/vt.c
+++ b/drivers/tty/vt/vt.c
@@ -1134,6 +1134,8 @@ int vc_allocate(unsigned int currcons)	/* return 0 on success */
 	return 0;
 err_free:
 	visual_deinit(vc);
+	if (*vc->uni_pagedict_loc)
+		con_free_unimap(vc);
 	kfree(vc);
 	vc_cons[currcons].d = NULL;
 	return err;
-- 
2.55.0

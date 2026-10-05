# [PATCH AUTOSEL 7.0-5.10] i2c: dev: prevent integer overflow in I2C_TIMEOUT ioctl

| Field | Value |
| :--- | :--- |
| From | Sasha Levin <sashal@kernel.org> |
| To | patches@lists.linux.dev, stable@vger.kernel.org |
| Cc | Author A <author-a@example.com>, Wolfram Sang <wsa+renesas@sang-engineering.com>, Sasha Levin <sashal@kernel.org>, linux-i2c@vger.kernel.org, linux-kernel@vger.kernel.org |
| Date | Wed, 20 May 2026 07:18:53 -0400 |
| Message-ID | `20260520111944.3424570-21-sashal@kernel.org` |
| In-Reply-To | `20260520111944.3424570-1-sashal@kernel.org` |
| List-ID | <patches.lists.linux.dev> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/20260520111944.3424570-21-sashal@kernel.org/` |

---

From: Author A <author-a@example.com>

[ Upstream commit 617eb7c0961a8dfcfc811844a6396e406b2923ea ]

While fuzzing with Syzkaller, a persistent `schedule_timeout: wrong
timeout value` warning was observed, accompanied by SMBus controller
state machine corruption.

The I2C_TIMEOUT ioctl accepts a user-provided timeout in multiples of
10 ms. The user argument is checked against INT_MAX, but it is
subsequently multiplied by 10 before being passed to msecs_to_jiffies().

A malicious user can pass a large value (e.g., 429496729) that passes
the `arg > INT_MAX` check but overflows when multiplied by 10. This
results in a truncated 32-bit unsigned value that bypasses the
internal `(int)m < 0` check in `msecs_to_jiffies()`.

The truncated value is then assigned to `client->adapter->timeout`
(a signed 32-bit int), which is reinterpreted as a negative number.
When passed to wait_for_completion_timeout(), this negative value
undergoes sign extension to a 64-bit unsigned long, triggering the
`schedule_timeout` warning and causing premature returns. This leaves
the SMBus state machine in an unrecoverable state, constituting a
local Denial of Service (DoS).

Fix this by bounding the user argument to `INT_MAX / 10`.

Signed-off-by: Author A <author-a@example.com>
[wsa: move the comment as well]
Signed-off-by: Wolfram Sang <wsa+renesas@sang-engineering.com>
Signed-off-by: Sasha Levin <sashal@kernel.org>
---

LLM Generated explanations, may be completely bogus:

## Phase Walkthrough

### Phase 1: Commit Message Forensics
Step 1.1 Record: Subsystem `i2c: dev`; action verb `prevent`; intent is
to prevent integer overflow in the userspace `I2C_TIMEOUT` ioctl.

Step 1.2 Record: Tags are `Signed-off-by: Author A
<author-a@example.com>` and `Signed-off-by: Wolfram Sang
<wsa+renesas@sang-engineering.com>` with maintainer edit note `[wsa:
move the comment as well]`. No `Fixes:`, `Reported-by:`, `Tested-by:`,
`Reviewed-by:`, `Link:`, or `Cc: stable` tags were present.

Step 1.3 Record: The body describes a Syzkaller-found `schedule_timeout:
wrong timeout value` warning and SMBus state machine corruption after a
large userspace timeout causes overflow/truncation before storing into
signed `adapter->timeout`. Symptom is local DoS through bad timeout
behavior; root cause is validating `arg` before multiplying by 10.

Step 1.4 Record: This is not hidden cleanup; it is an explicit integer
overflow and local DoS fix.

### Phase 2: Diff Analysis
Step 2.1 Record: One file changed, `drivers/i2c/i2c-dev.c`; committed
diff is 5 insertions and 4 deletions due comment movement. Modified
function: `i2cdev_ioctl()`. Scope: single-file surgical fix.

Step 2.2 Record: Before, `I2C_TIMEOUT` accepted any `arg <= INT_MAX`,
then used `msecs_to_jiffies(arg * 10)`. After, it accepts only `arg <=
INT_MAX / 10`, so the 10 ms unit conversion cannot exceed signed 32-bit
range before assignment to `adapter->timeout`.

Step 2.3 Record: Bug category is integer overflow/truncation and type-
range validation. Broken mechanism: userspace-controlled timeout is
range-checked before scaling, but stored in `struct
i2c_adapter.timeout`, which is `int`.

Step 2.4 Record: Fix quality is high: one validation bound change, no
new API, no refactor. Regression risk is low; it rejects only extreme
timeout values that cannot be represented safely after the documented 10
ms scaling.

### Phase 3: Git History Investigation
Step 3.1 Record: `git blame` shows the existing `arg > INT_MAX` guard
came from `6ebec961d59b` and the `arg * 10` timeout assignment/comment
came from `cd97f39b7cdf`. `git describe` places them at
`v5.0-rc2~14^2~1` and `v2.6.29-rc7~62^2~2`, respectively.

Step 3.2 Record: No `Fixes:` tag, so no tagged introducing commit to
follow. I still inspected `6ebec961d59b`, which originally fixed
negative retry/timeout values and was itself stable-tagged.

Step 3.3 Record: Recent upstream `drivers/i2c/i2c-dev.c` history shows
this candidate plus unrelated cleanup/fix commits; no prerequisite
series was identified.

Step 3.4 Record: `Author A` has this one upstream `drivers/i2c`
commit in the checked history. Wolfram Sang committed it and is the I2C
maintainer in the patch flow.

Step 3.5 Record: No dependent commit found. The patch assumes the long-
existing `I2C_TIMEOUT` case and applies cleanly to the current `v7.0.5`
checkout.

### Phase 4: Mailing List And External Research
Step 4.1 Record: `b4 dig -c 617eb7c0961a8` found the original submission
at ``[ARCHIVE] https://patch.msgid.link/20260427025745.1100768-1-`
author-a@example.com`. `b4 dig -a` found only v1. The submitted
patch was a one-line bound change; the committed version is the
maintainer-adjusted version with comment movement.

Step 4.2 Record: `b4 dig -w` shows recipients: Author A, Wolfram
Sang, `linux-i2c@vger.kernel.org`, and `linux-kernel@vger.kernel.org`.

Step 4.3 Record: No `Link:` or `Reported-by:` tag exists. Public web
searches did not find a syzkaller bug page for this exact issue; the
Syzkaller finding is verified only from the commit/patch text.

Step 4.4 Record: No multi-patch series found; standalone patch.

Step 4.5 Record: Stable-list WebFetch was blocked by lore Anubis, and
web search did not find stable-specific discussion. No evidence of
objections or NAKs was found in the b4-fetched mbox.

### Phase 5: Code Semantic Analysis
Step 5.1 Record: Modified function: `i2cdev_ioctl()`.

Step 5.2 Record: Call path is userspace ioctl on `/dev/i2c-*` through
`i2cdev_fops.unlocked_ioctl = i2cdev_ioctl`; compat ioctls fall back to
`i2cdev_ioctl()` for commands not handled specially, including
`I2C_TIMEOUT`.

Step 5.3 Record: Key callee is `msecs_to_jiffies()`, whose runtime
helper treats negative `unsigned int` values as infinite timeout. The
result is assigned to `struct i2c_adapter.timeout`, verified as `int`.

Step 5.4 Record: Buggy path is reachable from userspace with access to
an i2c-dev node. The corrupted timeout is then used by core retry loops
and many bus drivers, including paths using
`wait_for_completion_timeout()`.

Step 5.5 Record: Similar timeout use is widespread in `drivers/i2c`, but
this specific unchecked userspace scaling pattern was found in
`i2c-dev`.

### Phase 6: Stable Tree Analysis
Step 6.1 Record: Checked `v5.15`, `v6.1`, `v6.6`, `v6.12`, `v6.19`, and
`v7.0`; all contain the vulnerable `arg > INT_MAX` plus
`msecs_to_jiffies(arg * 10)` pattern.

Step 6.2 Record: Expected backport difficulty is clean or trivial. `git
apply --check` succeeded on current `v7.0.5`; checked stable snippets
have matching context.

Step 6.3 Record: No alternate stable fix found by subject/phrase
searches in local git history.

### Phase 7: Subsystem Context
Step 7.1 Record: Subsystem is I2C userspace character-device interface,
`drivers/i2c/i2c-dev.c`. Criticality: important, because it exposes bus
control to userspace and affects any system using `i2c-dev`.

Step 7.2 Record: Subsystem is mature but actively maintained. The
affected ioctl path is long-standing.

### Phase 8: Impact And Risk
Step 8.1 Record: Affected users are systems exposing `/dev/i2c-*` to
userspace, including embedded, hardware-management, and sensor-control
systems.

Step 8.2 Record: Trigger is setting `I2C_TIMEOUT` to a large value
through ioctl, then using affected I2C/SMBus transfer paths. It requires
access to the device node; no capability check is present in the ioctl
path itself.

Step 8.3 Record: Failure mode is at least a kernel `schedule_timeout:
wrong timeout value` warning and broken timeout behavior; commit text
reports SMBus controller state machine corruption and local DoS.
Severity: HIGH.

Step 8.4 Record: Benefit is high because it blocks a fuzzed, userspace-
reachable DoS class bug in maintained stable trees. Risk is very low
because the patch is one range check in one ioctl case.

### Phase 9: Final Synthesis
Step 9.1 Record: Evidence for backporting: real integer overflow,
Syzkaller-reported in commit text, userspace-reachable with i2c-dev
access, high-severity timeout corruption/DoS, tiny fix, maintainer-
applied, vulnerable code present in checked stable trees, clean apply to
`v7.0.5`. Evidence against: no public syzkaller link, no `Reported-
by`/`Tested-by`, and exact SMBus unrecoverable-state details were not
independently reproduced.

Step 9.2 Record: Stable rules: obviously correct yes; fixes real bug
yes; important issue yes, local DoS/bad kernel timeout path; small and
contained yes; no new features/APIs yes; stable applicability yes for
checked trees.

Step 9.3 Record: No exception category needed; this is a normal bug fix,
not device ID/quirk/DT/build/doc.

Step 9.4 Record: The technical merits strongly support stable
backporting.

## Verification
- Phase 1: Parsed `git show 617eb7c0961a8`; confirmed tags and absence
  of `Fixes:`, `Reported-by:`, `Link:`, and stable tag.
- Phase 2: Inspected committed diff; confirmed one-file change in
  `i2cdev_ioctl()`.
- Phase 3: `git blame` identified `6ebec961d59b` and `cd97f39b7cdf`;
  `git describe` placed them before all checked maintained stable trees.
- Phase 3: `git show 6ebec961d59b` confirmed prior timeout/retry
  negative-value fix and stable nomination.
- Phase 4: `b4 dig`, `b4 dig -a`, `b4 dig -w`, and mbox read found one
  v1 patch, proper I2C recipients, and no visible objections.
- Phase 4: WebFetch of lore was blocked by Anubis; stable-list
  discussion and public syzkaller report remain unverified.
- Phase 5: `rg` and file reads confirmed ioctl file operations, compat
  fallback, `msecs_to_jiffies()`, `adapter->timeout` as `int`, and
  downstream timeout uses.
- Phase 6: Checked `v5.15`, `v6.1`, `v6.6`, `v6.12`, `v6.19`, and `v7.0`
  snippets; all contain the vulnerable pattern.
- Phase 6: `git apply --check` confirmed the upstream diff applies to
  current `v7.0.5`.
- Phase 8: `schedule_timeout()` source confirms negative timeout emits
  `wrong timeout value` and returns 0.

**YES**

 drivers/i2c/i2c-dev.c | 9 +++++----
 1 file changed, 5 insertions(+), 4 deletions(-)

diff --git a/drivers/i2c/i2c-dev.c b/drivers/i2c/i2c-dev.c
index 7bbe0263411eb..ccaac5e29f906 100644
--- a/drivers/i2c/i2c-dev.c
+++ b/drivers/i2c/i2c-dev.c
@@ -487,12 +487,13 @@ static long i2cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
 		client->adapter->retries = arg;
 		break;
 	case I2C_TIMEOUT:
-		if (arg > INT_MAX)
+		/*
+		 * For historical reasons, user-space sets the timeout value in
+		 * units of 10 ms.
+		 */
+		if (arg > INT_MAX / 10)
 			return -EINVAL;
 
-		/* For historical reasons, user-space sets the timeout
-		 * value in units of 10 ms.
-		 */
 		client->adapter->timeout = msecs_to_jiffies(arg * 10);
 		break;
 	default:
-- 
2.53.0

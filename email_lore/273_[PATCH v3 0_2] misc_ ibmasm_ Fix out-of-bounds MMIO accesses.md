# [PATCH v3 0/2] misc: ibmasm: Fix out-of-bounds MMIO accesses

| Field | Value |
| :--- | :--- |
| From | author-a-alt@example.com |
| To | arnd@arndb.de, gregkh@linuxfoundation.org |
| Cc | linux-kernel@vger.kernel.org, kees@kernel.org, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Tue, 23 Jun 2026 20:43:02 +0800 |
| Message-ID | `20260623124304.371163-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20260623124304.371163-1-author-a-alt@example.com/` |

---

From: Author A <author-a@example.com>

This patch series fixes two distinct out-of-bounds (OOB) MMIO access
vectors in the ibmasm driver when exposed to malformed or fuzzed hardware
with an undersized BAR 0.

Patch 1 addresses the static OOB access during the probe phase.
Patch 2 addresses the dynamic OOB accesses via malicious hardware MFAs
during runtime interrupts.

Changes in v3:
 - Split the monolithic v2 patch into a 2-patch series to separate the 
   probe-time static checks from the runtime dynamic checks, as requested 
   by Greg KH.

Changes in v2:
 - Added dynamic MFA bounds checking in get_i2o_message().
 - Implemented hardware mailbox deadlock prevention.
 - Fixed potential unsigned integer underflow in bounds check arithmetic.

Author A (2):
  misc: ibmasm: Fix static out-of-bounds MMIO access during probe
  misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA

 drivers/misc/ibmasm/ibmasm.h   |  1 +
 drivers/misc/ibmasm/lowlevel.c | 19 +++++++++++++++----
 drivers/misc/ibmasm/lowlevel.h | 27 +++++++++++++++++++++++++--
 drivers/misc/ibmasm/module.c   | 13 +++++++++++++
 4 files changed, 54 insertions(+), 6 deletions(-)

-- 
2.34.1

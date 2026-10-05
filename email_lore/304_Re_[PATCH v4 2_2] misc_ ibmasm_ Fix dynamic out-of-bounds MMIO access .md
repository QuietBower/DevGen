# Re:[PATCH v4 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA

| Field | Value |
| :--- | :--- |
| From | author-a-alt <author-a-alt@example.com> |
| To | arnd@arndb.de, gregkh@linuxfoundation.org |
| Cc | linux-kernel@vger.kernel.org, kees@kernel.org, stable@vger.kernel.org, "Author A" <author-a@example.com> |
| Date | Sat, 27 Jun 2026 09:34:05 +0800 (CST) |
| Message-ID | `20246781.61e.19f06b66729.Coremail.author-a-alt@example.com` |
| In-Reply-To | `20260624032425.384325-3-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ANONYMIZED] https://lore.kernel.org/all/20246781.61e.19f06b66729.Coremail.author-a-alt@example.com/` |

---



At 2026-06-24 11:24:25, author-a-alt@example.com wrote:
>From: Author A <author-a@example.com>
>
>The ibmasm driver reads dynamic Message Frame Addresses (MFA) from
>hardware queues and uses them directly as offsets to dereference I2O
>messages via get_i2o_message().
>
>If a malformed or fuzzed device provides a malicious MFA, it can cause
>the driver to access memory far beyond the mapped BAR, leading to an
>out-of-bounds (OOB) access and potential kernel panic during runtime.
>
>Fix this by validating the target offset against the actual mapped size
>before dereferencing. This validation strictly accounts for both the
>i2o_header and the dynamic payload size using safe subtraction to prevent
>integer overflow bypasses.
>
>If the bounds check fails, the invalid MFA is released back to the
>inbound queue via set_mfa_inbound() to prevent a hardware mailbox deadlock.
>

Hi all,

The automated review bot recently raised a valid point (link: 
`[ANONYMIZED] https://sashiko.dev/#/patchset/20260624032425.384325-1-author-a-alt%40example.com (author-address patchset permalink)`)
regarding the error path here in Patch 2. I would like to seek the 
maintainers' guidance on a strict trade-off introduced here.

In the current v4 patch, if get_i2o_message() fails the bounds
check, the driver calls set_mfa_inbound() to release the MFA
back to the hardware inbound queue.

We face a dilemma between host availability and hardware safety:

1. Keep v4 behavior (Return the MFA):
This prevents the host driver's command queue from deadlocking
due to slot exhaustion. However, as the bot correctly noted,
writing it back to the INBOUND_QUEUE_PORT submits an uninitialized
frame to the Service Processor, risking undefined hardware states.

2. Drop the MFA entirely:
This prevents the hardware from executing uninitialized commands.
However, it permanently leaks the inbound queue slot. If malformed
MFAs are continuously provided, the hardware queue will exhaust,
causing a deterministic deadlock on the host driver side.

Could the maintainers please advise which failure mode is preferred
for this subsystem in the presence of malformed hardware? I will
prepare the next version based on your decision.

Thanks,
Author A

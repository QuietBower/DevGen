# Re: BUG: drm/ast: soft lockup due to missing timeout in hardware polling (ast_2500_patch_ahb)

| Field | Value |
| :--- | :--- |
| From | Thomas Zimmermann <tzimmermann@suse.de> xsBNBFs50uABCADEHPidWt974CaxBVbrIBwqcq/WURinJ3+2WlIrKWspiP83vfZKaXhFYsdg XH47fDVbPPj+d6tQrw5lPQCyqjwrCPYnq3WlIBnGPJ4/jreTL6V+qfKRDlGLWFjZcsrPJGE0 BeB5BbqP5erN1qylK9i3gPoQjXGhpBpQYwRrEyQyjuvk+Ev0K1Jc5tVDeJAuau3TGNgah4Yc hdHm3bkPjz9EErV85RwvImQ1dptvx6s7xzwXTgGAsaYZsL8WCwDaTuqFa1d1jjlaxg6+tZsB 9GluwvIhSezPgnEmimZDkGnZRRSFiGP8yjqTjjWuf0bSj5rUnTGiyLyRZRNGcXmu6hjlABEB AAHNJ1Rob21hcyBaaW1tZXJtYW5uIDx0emltbWVybWFubkBzdXNlLmRlPsLAjgQTAQgAOAIb AwULCQgHAgYVCgkICwIEFgIDAQIeAQIXgBYhBHIX+6yM6c9jRKFo5WgNwR1TC3ojBQJftODH AAoJEGgNwR1TC3ojx1wH/0hKGWugiqDgLNXLRD/4TfHBEKmxIrmfu9Z5t7vwUKfwhFL6hqvo lXPJJKQpQ2z8+X2vZm/slsLn7J1yjrOsoJhKABDi+3QWWSGkaGwRJAdPVVyJMfJRNNNIKwVb U6B1BkX2XDKDGffF4TxlOpSQzdtNI/9gleOoUA8+jy8knnDYzjBNOZqLG2FuTdicBXblz0Mf vg41gd9kCwYXDnD91rJU8tzylXv03E75NCaTxTM+FBXPmsAVYQ4GYhhgFt8S2UWMoaaABLDe 7l5FdnLdDEcbmd8uLU2CaG4W2cLrUaI4jz2XbkcPQkqTQ3EB67hYkjiEE6Zy3ggOitiQGcqp j//OwE0EWznS4AEIAMYmP4M/V+T5RY5at/g7rUdNsLhWv1APYrh9RQefODYHrNRHUE9eosYb T6XMryR9hT8XlGOYRwKWwiQBoWSDiTMo/Xi29jUnn4BXfI2px2DTXwc22LKtLAgTRjP+qbU6 3Y0xnQN29UGDbYgyyK51DW3H0If2a3JNsheAAK+Xc9baj0LGIc8T9uiEWHBnCH+RdhgATnWW GKdDegUR5BkDfDg5O/FISymJBHx2Dyoklv5g4BzkgqTqwmaYzsl8UxZKvbaxq0zbehDda8lv hFXodNFMAgTLJlLuDYOGLK2AwbrS3Sp0AEbkpdJBb44qVlGm5bApZouHeJ/+n+7r12+lqdsA EQEAAcLAdgQYAQgAIAIbDBYhBHIX+6yM6c9jRKFo5WgNwR1TC3ojBQJftOH6AAoJEGgNwR1T C3ojVSkIALpAPkIJPQoURPb1VWjh34l0HlglmYHvZszJWTXYwavHR8+k6Baa6H7ufXNQtThR yIxJrQLW6rV5lm7TjhffEhxVCn37+cg0zZ3j7zIsSS0rx/aMwi6VhFJA5hfn3T0TtrijKP4A SAQO9xD1Zk9/61JWk8OysuIh7MXkl0fxbRKWE93XeQBhIJHQfnc+YBLprdnxR446Sh8Wn/2D Ya8cavuWf2zrB6cZurs048xe0UbSW5AOSo4V9M0jzYI4nZqTmPxYyXbm30Kvmz0rYVRaitYJ 4kyYYMhuULvrJDMjZRvaNe52tkKAvMevcGdt38H4KSVXAylqyQOW5zvPc4/sq9c= |
| To | author-a-alt@example.com, Dave Airlie <airlied@redhat.com>, Jocelyn Falempe <jfalempe@redhat.com> |
| Cc | Maarten Lankhorst <maarten.lankhorst@linux.intel.com>, Maxime Ripard <mripard@kernel.org>, David Airlie <airlied@gmail.com>, Simona Vetter <simona@ffwll.ch>, dri-devel@lists.freedesktop.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Wed, 13 May 2026 09:50:33 +0200 |
| Message-ID | `b98e1013-25ae-44e1-8905-88f104cc0608@suse.de` |
| In-Reply-To | `20260513073649.352831-1-author-a-alt@example.com` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

Hi

Am 13.05.26 um 09:36 schrieb author-a-alt@example.com:
> From: Author A <author-a@example.com>
>
> Hi all,
>
> While analyzing the AST driver's hardware interactions using our custom device emulation and fuzzing framework (DevGen), we observed a severe soft lockup in the DRM driver.
>
> Although the underlying trigger in our environment was an incomplete emulation of the ASPEED AHB bridge, this highlighted a critical defensive programming gap in the driver itself: a complete lack of timeout mechanisms in the hardware polling loops.
>
> This issue causes a complete system hang (CPU stuck for 143s+) and leads to subsequent I/O starvation and system paralysis (e.g., jbd2 and systemd-journald blocked).
>
> ### Crash Log Snippet
>
> watchdog: BUG: soft lockup - CPU#3 stuck for 143s! [systemd-udevd:162]
> RIP: 0010:ioread32+0x0/0xa0
> Call Trace:
>   <TASK>
>   __ast_read32
>   __ast_mindwm+0x4d/0x80 [ast]
>   ast_2500_patch_ahb+0x9d/0x120 [ast]
>   ast_detect_chip
>   ast_pci_probe+0xa1a/0xb70 [ast]
>   ...
>
> ### Vulnerability Analysis
>
> Even on bare-metal hardware, an unresponsive ASPEED chip, a PCIe bus fault, or unexpected hardware states can cause the kernel to hang forever in the following loops during initialization:
>
> 1. Inside `ast_2500_patch_ahb()`:
>      do {
>          __ast_moutdwm(regs, 0x1e6e2000, 0x1688A8A8);
>          data = __ast_mindwm(regs, 0x1e6e2000);
>      } while (data != 1); // <--- Infinite loop if hardware doesn't respond properly
>
> 2. Inside the underlying I/O accessors `__ast_mindwm()` and `__ast_moutdwm()`:
>      do {
>          data = __ast_read32(regs, 0xf004) & 0xffff0000;
>      } while (data != (r & 0xffff0000)); // <--- Infinite loop
>
> ### Proposed Fix Direction
>
> To prevent the kernel from hanging indefinitely and to gracefully abort the probe (`-ENODEV`) upon hardware failure, these loops must implement a timeout mechanism (e.g., using `readx_poll_timeout` or a loop counter with `udelay`).
>
> Interestingly, other functions in the same file (e.g., `mmc_test`) correctly implement a timeout counter (`if (++timeout > TIMEOUT) return false;`), but the initialization paths mentioned above blindly trust the hardware state.
>
> We are reporting this defect so that the maintainers can decide the appropriate timeout thresholds and implement a safe fallback mechanism across the `ast` driver.
>
> Please let us know if you need more information or the full dmesg log.

Thanks for reporting. Will you send a patch?

Best regards
Thomas

>
> Reported-by: Author A <author-a@example.com>
>
> Best regards,
> Author A
>

-- 
--
Thomas Zimmermann
Graphics Driver Developer
SUSE Software Solutions Germany GmbH
Frankenstr. 146, 90461 Nürnberg, Germany, www.suse.com
GF: Jochen Jaser, Andrew McDonald, Werner Knoblich, (HRB 36809, AG Nürnberg)

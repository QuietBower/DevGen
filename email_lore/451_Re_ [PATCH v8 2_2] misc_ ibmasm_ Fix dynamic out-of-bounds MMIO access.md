# Re: [PATCH v8 2/2] misc: ibmasm: Fix dynamic out-of-bounds MMIO access via malicious MFA

| Field | Value |
| :--- | :--- |
| From | Greg KH <gregkh@linuxfoundation.org> |
| To | Author A <author-a@example.com> |
| Cc | arnd@arndb.de, kees@kernel.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org |
| Date | Fri, 31 Jul 2026 13:18:41 +0200 |
| Message-ID | `2026073113-defy-shortage-2005@gregkh` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/2026073113-defy-shortage-2005@gregkh/` |

---

On Sat, Jul 18, 2026 at 03:32:53PM +0800, Author A wrote:
> The ibmasm driver reads dynamic Message Frame Addresses (MFA) from
> hardware queues and uses them directly as offsets to dereference I2O
> messages via get_i2o_message().

Ah, this is I2O hardware.  Wow, I don't think that's even around
anymore, is it?  Do you have access to it to test this?

thanks,

greg k-h

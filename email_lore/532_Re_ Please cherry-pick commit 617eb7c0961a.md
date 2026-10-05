# Re: Please cherry-pick commit 617eb7c0961a

| Field | Value |
| :--- | :--- |
| From | Sasha Levin <sashal@kernel.org> |
| To | stable@vger.kernel.org |
| Cc | Sasha Levin <sashal@kernel.org>, gregkh@linuxfoundation.org, linux-i2c@vger.kernel.org, wsa+renesas@sang-engineering.com, author-a-alt@example.com |
| Date | Wed, 10 Jun 2026 08:02:40 -0400 |
| Message-ID | `20260610.stable-out-0001.0a78b438@kernel.org` |
| In-Reply-To | `20260609031227.21001-1-author-a-alt@example.com` |
| Source | `[ANONYMIZED] local copy (original archive link removed)` |

---

On Tue, Jun 09, 2026 at 11:12:27AM +0800, Author A wrote:
> Please cherry-pick the following upstream commit into stable trees
> from 5.10 up to 7.0:
>
> 617eb7c0961a8dfcfc811844a6396e406b2923ea
>
> It fixes a local DoS via userspace-controlled integer overflow
> in the I2C_TIMEOUT ioctl.

Now queued for 7.0, 6.18, 6.12, 6.6, 6.1, 5.15 and 5.10.

Thanks,
Sasha

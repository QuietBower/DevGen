# Re: [PATCH v2] i2c: i801: Clamp adapter timeout to prevent system lockup

| Field | Value |
| :--- | :--- |
| From | Andi Shyti <andi.shyti@kernel.org> |
| To | Author A <author-a@example.com> |
| Cc | jdelvare@suse.com, djkurtz@chromium.org, hkallweit1@gmail.com, wsa@kernel.org, linux-i2c@vger.kernel.org, linux-kernel@vger.kernel.org, stable@vger.kernel.org |
| Date | Sat, 25 Jul 2026 00:44:21 +0200 |
| Message-ID | `amPinh8c7GwQx4Le@zenone.zhora.eu` |
| In-Reply-To | `20260723034331.116690-1-author-a@example.com` |
| List-ID | <linux-i2c.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/amPinh8c7GwQx4Le@zenone.zhora.eu/` |

---

Hi Author A,

On Thu, Jul 23, 2026 at 11:43:31AM +0800, Author A wrote:
> Userspace applications with access to /dev/i2c-* can use the I2C_TIMEOUT
> ioctl to manipulate the adap->timeout value. In the i2c-i801 driver,
> this user-controlled timeout is used directly in both the interrupt
> waiting paths (wait_for_completion_timeout) and the polling loops
> (time_after(jiffies, timeout)).
> 
> If a malicious or arbitrarily large timeout value is injected from
> userspace, and the hardware (or an emulated device in a fuzzing/VM
> environment) fails to respond, the driver will block for the entirety
> of that requested timeout while holding the i2c adapter's rt_mutex.

I am not convinced that we should fix this in the i801 driver.

Access to /dev/i2c-* is normally restricted to the administrator.
We cannot protect the kernel from every unreasonable value chosen
by a privileged user.

...

> Fix this by introducing i801_get_timeout(), which clamps the timeout
> to a maximum of HZ (1 second). This is safe and strictly necessary:
> 1. Normal operations complete in milliseconds and are unaffected by
>    this ceiling. The timeout is only consumed during hardware failures.
> 2. Per the SMBus 2.0 specification, the maximum clock low timeout
>    (tTIMEOUT) is 25-35 ms. A 1-second clamp provides immense headroom

The SMBus clock low timeout is not the maximum duration of a
complete transaction. It does not prove that one second is a
safe limit for every user.

The one second limit still looks arbitrary to me.

>    (orders of magnitude) for legitimate transactions while decisively
>    preventing prolonged rt_mutex lock contention and D-state lockups.
> 
> Fixes: 1de93d5d5217 ("i2c: i801: Replace waitqueue with completion API")

This commit did not introduce the problem. It only replaced the
waitqueue with a completion. It looks like a convenient point in
the history rather than the actual commit that introduced the
"issue".

> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>

...

> +/*
> + * Bound the maximum wait time to prevent system lockup if the
> + * userspace passes an arbitrarily large timeout via ioctl.
> + */
> +static inline unsigned long i801_get_timeout(struct i801_priv *priv)
> +{
> +	return min_t(unsigned long, priv->adapter.timeout, HZ);

This also silently ignores the timeout requested by userspace. A
user may request ten seconds and the ioctl will succeed, but the
driver will use only one second. There is no warning or other
indication that the value was clamped.

More generally, once a user has privileged access to a device
node, there are many ways to misuse the hardware. I do not think
adding arbitrary limits to individual drivers is the right way to
address that.

Thanks,
Andi

> +}

# Re: [PATCH] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close()

| Field | Value |
| :--- | :--- |
| From | Luiz Augusto von Dentz <luiz.dentz@gmail.com> |
| To | author-a-alt@example.com |
| Cc | marcel@holtmann.org, linux-bluetooth@vger.kernel.org, linux-serial@vger.kernel.org, linux-kernel@vger.kernel.org, Author A <author-a@example.com> |
| Date | Fri, 15 May 2026 08:37:57 -0400 |
| Message-ID | `CABBYNZLjreYY_BczAQr2G6L=iJjBYKksFp53CairG-6V0Cb0EA@mail.gmail.com` |
| In-Reply-To | `20260513064547.352601-1-author-a-alt@example.com` |
| List-ID | <linux-serial.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/CABBYNZLjreYY_BczAQr2G6L=iJjBYKksFp53CairG-6V0Cb0EA@mail.gmail.com/` |

---

Hi,

On Wed, May 13, 2026 at 2:46 AM <author-a-alt@example.com> wrote:
>
> From: Author A <author-a@example.com>
>
> A Use-After-Free (UAF) vulnerability and a subsequent General Protection
> Fault (GPF) were observed in h5_recv() due to a race condition between
> the initialization of the HCI UART line discipline and concurrent TTY
> hangup via TIOCVHANGUP.
>
> The issue arises because the workqueues (init_ready and write_work) are
> only cancelled if the HCI_UART_PROTO_READY flag is set. However, during
> the protocol initialization phase (HCI_UART_PROTO_INIT), the underlying
> protocol (e.g., H5) may schedule work (such as sending sync/config
> packets). If a hangup occurs before the setup completes and the READY
> flag is set, hci_uart_tty_close() skips the cancel_work_sync() calls
> and proceeds to free the `hu` struct.
>
> When the delayed workqueue finally executes, it blindly dereferences
> the freed `hu` struct, causing ODEBUG warnings and kernel panics.
>
> Fix this by moving the cancel_work_sync() calls outside the
> HCI_UART_PROTO_READY check, ensuring that any pending works are
> unconditionally cancelled before the hci_uart structure is freed.
>
> Signed-off-by: Author A <author-a@example.com>
> ---
>  drivers/bluetooth/hci_ldisc.c | 10 +++++++---
>  1 file changed, 7 insertions(+), 3 deletions(-)
>
> diff --git a/drivers/bluetooth/hci_ldisc.c b/drivers/bluetooth/hci_ldisc.c
> index 275ea865bc29..566e1c525ee2 100644
> --- a/drivers/bluetooth/hci_ldisc.c
> +++ b/drivers/bluetooth/hci_ldisc.c
> @@ -544,14 +544,18 @@ static void hci_uart_tty_close(struct tty_struct *tty)
>         if (hdev)
>                 hci_uart_close(hdev);
>
> +       /*
> +        * Always cancel workqueues unconditionally before freeing the hu
> +        * struct, as they might be active during the PROTO_INIT phase.
> +        */
> +       cancel_work_sync(&hu->init_ready);
> +       cancel_work_sync(&hu->write_work);

Can't we use disable_work_sync? If it frees up at the end, it's
probably best to disable it so it doesn't allow new submissions.

>         if (test_bit(HCI_UART_PROTO_READY, &hu->flags)) {
>                 percpu_down_write(&hu->proto_lock);
>                 clear_bit(HCI_UART_PROTO_READY, &hu->flags);
>                 percpu_up_write(&hu->proto_lock);
>
> -               cancel_work_sync(&hu->init_ready);
> -               cancel_work_sync(&hu->write_work);
> -
>                 if (hdev) {
>                         if (test_bit(HCI_UART_REGISTERED, &hu->flags))
>                                 hci_unregister_dev(hdev);
> --
> 2.34.1
>
>


-- 
Luiz Augusto von Dentz

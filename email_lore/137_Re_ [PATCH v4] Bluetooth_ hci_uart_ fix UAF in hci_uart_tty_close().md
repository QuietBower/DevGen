# Re: [PATCH v4] Bluetooth: hci_uart: fix UAF in hci_uart_tty_close()

| Field | Value |
| :--- | :--- |
| From | Luiz Augusto von Dentz <luiz.dentz@gmail.com> |
| To | author-a-alt@example.com |
| Cc | pmenzel@molgen.mpg.de, marcel@holtmann.org, linux-bluetooth@vger.kernel.org, linux-serial@vger.kernel.org, linux-kernel@vger.kernel.org, greg@kroah.com, stable@vger.kernel.org, Author A <author-a@example.com> |
| Date | Fri, 15 May 2026 12:08:05 -0400 |
| Message-ID | `CABBYNZ+r3gm37FW5WqE79bRp+x9UZsaCtyvfz_FdixqEucAxGw@mail.gmail.com` |
| In-Reply-To | `20260515140548.393865-1-author-a-alt@example.com` |
| List-ID | <stable.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/CABBYNZ+r3gm37FW5WqE79bRp+x9UZsaCtyvfz_FdixqEucAxGw@mail.gmail.com/` |

---

Hi,

On Fri, May 15, 2026 at 10:06 AM <author-a-alt@example.com> wrote:
>
> From: Author A <author-a@example.com>
>
> A Use-After-Free (UAF) vulnerability and a subsequent kernel panic were
> observed in hci_uart_write_work() due to a race condition between the
> initialization of the HCI UART line discipline and concurrent TTY hangup.
>
> This issue was triggered by our custom device emulation and fuzzing
> framework (DevGen) on the v6.18 kernel. Due to the highly timing-dependent
> nature of this race condition (requiring a precise interleaving of
> TIOCVHANGUP and protocol setup), Syzkaller failed to extract a reliable
> standalone C reproducer (reproducer is too unreliable: 0.00).
>
> The crash trace is as follows:
>   ODEBUG: free active (active state 0) object: ffff88804024e870 object type: work_struct hint: hci_uart_write_work+0x0/0x940
>   WARNING: CPU: 0 PID: 338273 at lib/debugobjects.c:612 debug_print_object+0x1a2/0x2b0
>   ...
>   Call Trace:
>    <TASK>
>    debug_check_no_obj_freed+0x3ec/0x520
>    kfree+0x3f0/0x6c0
>    hci_uart_tty_close+0x127/0x2a0
>    tty_ldisc_close+0x113/0x1a0
>    tty_ldisc_kill+0x8e/0x150
>    tty_ldisc_hangup+0x3c1/0x730
>    __tty_hangup.part.0+0x3fd/0x8a0
>    tty_ioctl+0x120f/0x1690
>    __x64_sys_ioctl+0x18f/0x210
>    do_syscall_64+0xcb/0xfa0
>    entry_SYSCALL_64_after_hwframe+0x77/0x7f
>    </TASK>
>
> The issue arises because the workqueues (init_ready and write_work) are
> only flushed/cancelled if the HCI_UART_PROTO_READY flag is set. However,
> during the protocol initialization phase (HCI_UART_PROTO_INIT), the
> underlying protocol may schedule work. If a hangup occurs before the setup
> completes and the READY flag is set, hci_uart_tty_close() skips the
> teardown of these workqueues and proceeds to free the `hu` struct. When
> the scheduled work executes later, it blindly dereferences the freed `hu`
> struct.
>
> Fix this by moving the workqueue teardown outside the HCI_UART_PROTO_READY
> check. Furthermore, use disable_work_sync() instead of cancel_work_sync()
> to unconditionally disable the works. This ensures that any pending works
> are cancelled and no new submissions can occur before the hci_uart
> structure is freed. Note that hu->init_ready and hu->write_work are
> initialized in hci_uart_tty_open(), so it is always safe to call
> disable_work_sync() on them in hci_uart_tty_close(), even if the protocol
> was never fully attached.
>
> Fixes: 3b799254cf6f ("Bluetooth: hci_uart: Cancel init work before unregistering")
> Cc: stable@vger.kernel.org
> Signed-off-by: Author A <author-a@example.com>
> ---
> Changes in v4:
> - Adopted Luiz's suggestion to use disable_work_sync() instead of
>   cancel_work_sync() to prevent new work submissions during teardown.
>
> Changes in v3:
> - Added 'Cc: stable' tag as requested by the stable bot.
>
> Changes in v2:
> - Added KASAN/ODEBUG crash trace.
>
>  drivers/bluetooth/hci_ldisc.c | 12 +++++++++---
>  1 file changed, 9 insertions(+), 3 deletions(-)
>
> diff --git a/drivers/bluetooth/hci_ldisc.c b/drivers/bluetooth/hci_ldisc.c
> index 275ea865bc29..333c1e1503e8 100644
> --- a/drivers/bluetooth/hci_ldisc.c
> +++ b/drivers/bluetooth/hci_ldisc.c
> @@ -544,14 +544,20 @@ static void hci_uart_tty_close(struct tty_struct *tty)
>         if (hdev)
>                 hci_uart_close(hdev);
>
> +       /*
> +        * Disable workqueues unconditionally before freeing the hu
> +        * struct, as they might be active during the PROTO_INIT phase.
> +        * Using disable_work_sync() instead of cancel_work_sync()
> +        * ensures no new submissions can occur.
> +        */
> +       disable_work_sync(&hu->init_ready);
> +       disable_work_sync(&hu->write_work);

Looks like sashiko has a problem with these being after hci_uart_close:

`[ANONYMIZED] https://sashiko.dev/#/patchset/20260515140548.393865-1-author-a-alt%40example.com (author-address patchset permalink)`

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


-- 
Luiz Augusto von Dentz

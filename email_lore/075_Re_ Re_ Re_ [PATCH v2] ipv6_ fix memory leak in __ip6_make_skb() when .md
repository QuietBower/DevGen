# Re: Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty

| Field | Value |
| :--- | :--- |
| From | "Bezdeka, Florian" <florian.bezdeka@siemens.com> |
| To | "willemdebruijn.kernel@gmail.com" <willemdebruijn.kernel@gmail.com>, "author-a@example.com" <author-a@example.com> |
| Cc | "syzbot+e5d6936b9f4545fd88ab@syzkaller.appspotmail.com" <syzbot+e5d6936b9f4545fd88ab@syzkaller.appspotmail.com>, "davem@davemloft.net" <davem@davemloft.net>, "dsahern@kernel.org" <dsahern@kernel.org>, "sd@queasysnail.net" <sd@queasysnail.net>, "linux-kernel@vger.kernel.org" <linux-kernel@vger.kernel.org>, "netdev@vger.kernel.org" <netdev@vger.kernel.org>, "horms@kernel.org" <horms@kernel.org>, "edumazet@google.com" <edumazet@google.com>, "kuba@kernel.org" <kuba@kernel.org>, "pabeni@redhat.com" <pabeni@redhat.com> |
| Date | Fri, 24 Apr 2026 06:56:35 +0000 |
| Message-ID | `68628d54304821fe8410e756ce3f92c60da10e1d.camel@siemens.com` |
| In-Reply-To | `5eabb1a1.7642.19dbd869da3.Coremail.author-a@example.com` |
| List-ID | <netdev.vger.kernel.org> |
| Source | `[ARCHIVE] https://lore.kernel.org/all/68628d54304821fe8410e756ce3f92c60da10e1d.camel@siemens.com/` |

---

On Fri, 2026-04-24 at 11:26 +0800, Author A wrote:
> Hi Sabrina and Jakub,
> 
> Before sending out the v3 patch, I synced my tree to the latest mainline and checked the current state of `ip6_make_skb()`. 
> 
> It turns out that the missing `ip6_cork_release(cork)` in the error path was already naturally resolved by Eric Dumazet's recent refactoring commit:
> b409a7f7176b ("ipv6: colocate inet6_cork in inet_cork_full")
> 
> With Eric's changes, the error handling now correctly calls `ip6_cork_release(cork)` if `ip6_setup_cork()` fails, meaning the memory leak is no longer present in the latest tree.

Fine, so mainline is correct now. What about the stable trees? Did you
check them already?

Florian

> 
> Please disregard my v1 and v2 patches. I am also telling syzbot to close this report based on Eric's commit.
> 
> Thank you all again for your time, the deep code review, and for guiding me to find the true root cause. I learned a huge amount from this discussion!
> 
> #syz fix: ipv6: colocate inet6_cork in inet_cork_full
> 
> Best regards,
> Author A
> 
> 2026-04-24 11:16:30 "Author A" <author-a@example.com> 写道：
> > Hi,
> > 
> > Thank you so much for the review and for pointing me to the correct Fixes tag!
> > 
> > You hit the nail on the head regarding `__ip6_append_data()`. After re-evaluating the code path based on your question, I realize my assumption in v2 was incorrect. `__ip6_append_data()` does indeed guarantee that an skb is queued upon success, making the `skb == NULL` path dead code in this context.
> > 
> > I traced the `failslab` memory leak back to its true origin: the lockless fast path wrapper `ip6_make_skb()`.
> > 
> > Sabrina previously noted that `ip6_setup_cork()` failures correctly release the dst. That is absolutely true for the slow path, where `udp_v6_flush_pending_frames()` eventually handles the cleanup. 
> > 
> > However, in the fast path, `ip6_make_skb()` calls `ip6_setup_cork()`. Inside `ip6_setup_cork()`, `cork->base.dst` is assigned early. If a subsequent memory allocation fails (e.g., `v6_cork->opt = kzalloc(...)` failing due to failslab), it returns an error. `ip6_make_skb()` then directly returns `ERR_PTR(err)` WITHOUT calling `ip6_cork_release(cork)`.
> > 
> > Since `udpv6_sendmsg()` assumes the `dst` reference is stolen by `ip6_make_skb()` and unconditionally jumps to `out_no_dst`, the `dst` is completely leaked.
> > 
> > The fix is simply to add `ip6_cork_release(cork)` in the `ip6_setup_cork()` error path inside `ip6_make_skb()`.
> > 
> > I will submit a v3 patch shortly addressing this true root cause and using your suggested Fixes tag. Thank you again for steering me in the exact right direction!
> > 
> > Best regards,
> > Author A
> > 
> > 
> > > -----原始邮件-----
> > > 发件人: "Willem de Bruijn" <willemdebruijn.kernel@gmail.com>
> > > 发送时间:2026-04-23 22:59:45 (星期四)
> > > 收件人: "Author A" <author-a@example.com>, willemdebruijn.kernel@gmail.com, davem@davemloft.net, dsahern@kernel.org, edumazet@google.com, kuba@kernel.org, pabeni@redhat.com
> > > 抄送: sd@queasysnail.net, horms@kernel.org, netdev@vger.kernel.org, linux-kernel@vger.kernel.org, "Author A" <author-a@example.com>, syzbot+e5d6936b9f4545fd88ab@syzkaller.appspotmail.com
> > > 主题: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty
> > > 
> > > Author A wrote:
> > > > During fuzzing with failslab enabled, a memory leak was observed in the
> > > > IPv6 UDP send path.
> > > > 
> > > > The root cause resides in __ip6_make_skb(). In extremely rare cases
> > > > (such as fault injection or specific empty payload conditions),
> > > 
> > > Can you elaborate on this? Which fault injection lets
> > > __ip6_append_data succeed without writing data?
> > > 
> > > > __ip6_append_data() may succeed but leave the socket's write queue
> > > > empty.
> > > > 
> > > > When __ip6_make_skb() is subsequently called, __skb_dequeue(queue)
> > > > returns NULL. The previous logic handled this by executing a 'goto out;',
> > > > which completely bypassed the call to ip6_cork_release(cork).
> > > > 
> > > > Since the 'cork' structure actively holds a reference to the routing
> > > > entry (dst_entry) and potentially other allocated options, skipping
> > > > the release cleanly leaks these resources.
> > > > 
> > > > Fix this by introducing an 'out_cork_release' label and jumping to it
> > > > when skb is NULL, ensuring the cork state is always properly cleaned up.
> > > > The now-unused 'out' label is also removed to prevent compiler warnings.
> > > > 
> > > > Fixes: 1da177e4c3f4 ("Linux-2.6.12-rc2")
> > > 
> > > I think this is 
> > > 
> > > Fixes: 6422398c2ab0 ("ipv6: introduce ipv6_make_skb")
> > > 
> > > > Reported-by: syzbot+e5d6936b9f4545fd88ab@syzkaller.appspotmail.com
> > > > Signed-off-by: Author A <author-a@example.com>

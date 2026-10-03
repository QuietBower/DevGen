# 🐛 Memory leak in udpv6_sendmsg()

## 📌 Overview
* **Location:** `net/ipv6/udp.c`
* **Current Status:** 👀**Confirmed** 🔄 **Indirectly Fixed**
* **Notes:** Unconsumed `dst_entry` reference caused a memory leak when `ip6_make_skb()` fails early.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>[lore.kernel.org](https://lore.kernel.org/all/20260422105802.486216-1-25181214217@stu.xidian.edu.cn/)</u> |
| Re: [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>[lore.kernel.org](https://lore.kernel.org/all/aei3QDpiToAcYfR1@krikkit/)</u> |
| Re: Re: [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>[lore.kernel.org](https://lore.kernel.org/all/213735e5.6f4b.19db945239c.Coremail.25181214217@stu.xidian.edu.cn/)</u> |
| [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[lore.kernel.org](https://lore.kernel.org/all/20260423082233.514056-1-25181214217@stu.xidian.edu.cn/)</u> |
| Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[lore.kernel.org](https://lore.kernel.org/all/willemdebruijn.kernel.28284ffff84dc@gmail.com/)</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[lore.kernel.org](https://lore.kernel.org/all/2ebe2714.7628.19dbd7d2b62.Coremail.25181214217@stu.xidian.edu.cn/)</u> |
| Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[lore.kernel.org](https://github.com/Wmingyu/DevGen-results/blob/main/email-list/Re%3A%20Re%3A%20Re%3A%20%5BPATCH%20v2%5D%20ipv6%3A%20fix%20memory%20leak%20in%20__ip6_make_skb()%20when%20queue%20is%20empty)</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[lore.kernel.org](https://lore.kernel.org/all/68628d54304821fe8410e756ce3f92c60da10e1d.camel@siemens.com/)</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[lore.kernel.org](https://lore.kernel.org/all/378c89ac.7c26.19dc0263038.Coremail.25181214217@stu.xidian.edu.cn/)</u> |


# 🐛 Memory leak in udpv6_sendmsg()

## 📌 Overview
* **Location:** `net/ipv6/udp.c`
* **Current Status:** 👀**Confirmed** 🔄 **Indirectly Fixed**
* **Notes:** Unconsumed `dst_entry` reference caused a memory leak when `ip6_make_skb()` fails early.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>lore.kernel.org</u> |
| Re: [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>lore.kernel.org</u> |
| Re: Re: [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>lore.kernel.org</u> |
| [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>lore.kernel.org</u> |
| Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>lore.kernel.org</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>lore.kernel.org</u> |
| Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>lore.kernel.org</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>lore.kernel.org</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>lore.kernel.org</u> |


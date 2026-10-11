# 🐛 Memory leak in udpv6_sendmsg()

## 📌 Overview
* **Location:** `net/ipv6/udp.c`
* **Current Status:** 👀**Confirmed** 🔄 **Indirectly Fixed**
* **Notes:** Unconsumed `dst_entry` reference caused a memory leak when `ip6_make_skb()` fails early.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>[mail](../email/065_%5BPATCH%5D%20ipv6_%20udp_%20fix%20memory%20leak%20in%20udpv6_sendmsg%20error%20path.txt)</u> |
| Re: [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>[mail](../email/061_Re_%20%5BPATCH%5D%20ipv6_%20udp_%20fix%20memory%20leak%20in%20udpv6_sendmsg%20error%20path.txt)</u> |
| Re: Re: [PATCH] ipv6: udp: fix memory leak in udpv6_sendmsg error path | <u>[mail](../email/070_Re_%20Re_%20%5BPATCH%5D%20ipv6_%20udp_%20fix%20memory%20leak%20in%20udpv6_sendmsg%20error%20path.txt)</u> |
| [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[mail](../email/071_%5BPATCH%20v2%5D%20ipv6_%20fix%20memory%20leak%20in%20__ip6_make_skb%28%29%20when%20queue%20is%20emp.txt)</u> |
| Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[mail](../email/069_Re_%20%5BPATCH%20v2%5D%20ipv6_%20fix%20memory%20leak%20in%20__ip6_make_skb%28%29%20when%20queue%20is.txt)</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[mail](../email/077_Re_%20Re_%20%5BPATCH%20v2%5D%20ipv6_%20fix%20memory%20leak%20in%20__ip6_make_skb%28%29%20when%20queu.txt)</u> |
| Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[mail](../email/077_Re_%20Re_%20%5BPATCH%20v2%5D%20ipv6_%20fix%20memory%20leak%20in%20__ip6_make_skb%28%29%20when%20queu.txt)</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[mail](../email/075_Re_%20Re_%20Re_%20%5BPATCH%20v2%5D%20ipv6_%20fix%20memory%20leak%20in%20__ip6_make_skb%28%29%20when%20.txt)</u> |
| Re: Re: [PATCH v2] ipv6: fix memory leak in __ip6_make_skb() when queue is empty | <u>[mail](../email/079_Re_%20Re_%20Re_%20Re_%20%5BPATCH%20v2%5D%20ipv6_%20fix%20memory%20leak%20in%20__ip6_make_skb%28%29%20w.txt)</u> |


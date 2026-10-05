# 🐛 Unvalidated negative handle in drm_gem_change_handle_ioctl

## 📌 Overview
* **Location:** `drivers/gpu/drm/drm_gem.c`
* **Current Status:**  👀**Confirmed**  
* **Notes:** Initially investigated via a Syzkaller warning. Analysis revealed a signed integer overflow vulnerability (`INT_MAX + 1` evaluated in `idr_alloc`) and a missing invalid handle check (`handle == 0`). During the v2 patch review, automated tools (Sashiko AI) flagged a pre-existing Critical UAF race condition. Consequently, upstream maintainers disabled the ioctl entirely and added a TODO list for re-enablement. The v2 patch was gracefully dropped, and the edge-case findings were archived on the mailing list for future maintainers.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>[mail](../email_lore/066_%5BPATCH%5D%20drm_gem_%20fix%20warning%20in%20idr_alloc%20due%20to%20unvalidated%20user%20hand.md)</u> |
| Ping:[PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>[mail](../email_lore/066_%5BPATCH%5D%20drm_gem_%20fix%20warning%20in%20idr_alloc%20due%20to%20unvalidated%20user%20hand.md)</u> |
| Re: [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>[mail](../email_lore/212_Re_%20%5BPATCH%5D%20drm_gem_%20fix%20warning%20in%20idr_alloc%20due%20to%20unvalidated%20user%20.md)</u> |
| Re: Re: [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>[mail](../email_lore/066_%5BPATCH%5D%20drm_gem_%20fix%20warning%20in%20idr_alloc%20due%20to%20unvalidated%20user%20hand.md)</u> |
| Re: Re: Re: [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>[mail](../email_lore/066_%5BPATCH%5D%20drm_gem_%20fix%20warning%20in%20idr_alloc%20due%20to%20unvalidated%20user%20hand.md)</u> |
| [PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>[mail](../email_lore/217_Re_%20%5BPATCH%20v2%5D%20drm_gem_%20fix%20signed%20integer%20overflow%20in%20idr_alloc%20end%20p.md)</u> |
| sashiko-bot:Re: [PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>[mail](../email_lore/534_Re_%20%5BPATCH%20v2%5D%20drm_gem_%20fix%20signed%20integer%20overflow%20in%20idr_alloc%20end%20p.md)</u> |
| sashiko-bot:Re:Re: [PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>[mail](../email_lore/537_Re_Re_%20%5BPATCH%20v2%5D%20drm_gem_%20fix%20signed%20integer%20overflow%20in%20idr_alloc%20en.md)</u> |
| Re:[PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>[mail](../email_lore/217_Re_%20%5BPATCH%20v2%5D%20drm_gem_%20fix%20signed%20integer%20overflow%20in%20idr_alloc%20end%20p.md)</u> |
| Re:Re:[PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>[mail](../email_lore/217_Re_%20%5BPATCH%20v2%5D%20drm_gem_%20fix%20signed%20integer%20overflow%20in%20idr_alloc%20end%20p.md)</u> |


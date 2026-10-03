# 🐛 Unvalidated negative handle in drm_gem_change_handle_ioctl

## 📌 Overview
* **Location:** `drivers/gpu/drm/drm_gem.c`
* **Current Status:**  👀**Confirmed**  
* **Notes:** Initially investigated via a Syzkaller warning. Analysis revealed a signed integer overflow vulnerability (`INT_MAX + 1` evaluated in `idr_alloc`) and a missing invalid handle check (`handle == 0`). During the v2 patch review, automated tools (Sashiko AI) flagged a pre-existing Critical UAF race condition. Consequently, upstream maintainers disabled the ioctl entirely and added a TODO list for re-enablement. The v2 patch was gracefully dropped, and the edge-case findings were archived on the mailing list for future maintainers.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>lore.kernel.org</u> |
| Ping:[PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>lore.kernel.org</u> |
| Re: [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>lore.kernel.org</u> |
| Re: Re: [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>lore.kernel.org</u> |
| Re: Re: Re: [PATCH] drm/gem: fix warning in idr_alloc due to unvalidated user handle | <u>lore.kernel.org</u> |
| [PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>lore.kernel.org</u> |
| sashiko-bot:Re: [PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>lore.kernel.org</u> |
| sashiko-bot:Re:Re: [PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>lore.kernel.org</u> |
| Re:[PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>lore.kernel.org</u> |
| Re:Re:[PATCH v2] drm/gem: fix signed integer overflow in idr_alloc end parameter | <u>lore.kernel.org</u> |


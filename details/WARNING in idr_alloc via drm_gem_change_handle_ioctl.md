# 🐛 WARNING in idr_alloc via drm_gem_change_handle_ioctl

## 📌 Overview
* **Location:** `drivers/gpu/drm/drm_gem.c`
* **Current Status:** 👀**Patch Accepted**  [CVE-2026-23149](https://www.cve.org/CVERecord?id=CVE-2026-23149)
* **Notes:** A `WARNING` is triggered in `idr_alloc()` (lib/idr.c) when `drm_gem_change_handle_ioctl()` attempts to allocate or modify a GEM handle. This is likely caused by a lack of proper bounds checking on user-supplied values from the IOCTL, allowing an invalid or negative starting range to be passed directly to the IDR allocator.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [BUG] WARNING in idr_alloc during drm_gem_change_handle_ioctl | <u>[lore.kernel.org](https://lore.kernel.org/all/6db3d523.bb3f.19ba843a1f9.Coremail.wangzhi_xd@stu.xidian.edu.cn/)</u> |
| Re: [BUG] WARNING in idr_alloc during drm_gem_change_handle_ioctl | <u>[lore.kernel.org](https://lore.kernel.org/all/f0209960-7b71-4cf0-8531-b67d63cec68a@ursulin.net/)</u> |
| 1_drm: Do not allow userspace to trigger kernel warnings in drm_gem_change_handle_ioctl() | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/?id=12f15d52d38ac53f7c70ea3d4b3d76afed04e064)</u> |
| 2_drm: Do not allow userspace to trigger kernel warnings in drm_gem_change_handle_ioctl() | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/?id=ae8831ee0fb2f5f41f39722e7b3749d65bb78d08)</u> |


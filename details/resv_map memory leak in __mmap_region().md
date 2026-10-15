# 🐛 resv_map memory leak in __mmap_region()

## 📌 Overview
*  **Location:** `fs/hugetlbfs/inode.c` & `mm/vma.c`
* **Current Status:** ✅ **Accepted** **[CVE-2026-46318](https://www.cve.org/CVERecord?id=CVE-2026-46318)**
* **Notes:** A regression introduced by the VMA iterator refactoring causes a `resv_map` memory leak when VMA creation fails during the `mmap_prepare` phase.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[lore.kernel.org](https://lore.kernel.org/all/20260425070700.562229-1-25181214217@stu.xidian.edu.cn/)</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[lore.kernel.org](https://lore.kernel.org/all/BCFCF1D6-8F39-4E60-AD86-0ECF799FFD9D@linux.dev/)</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[lore.kernel.org](https://lore.kernel.org/all/4155b8e4.8e25.19dcea5b9d4.Coremail.25181214217@stu.xidian.edu.cn/)</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[lore.kernel.org](https://lore.kernel.org/all/ae9tVZbNFtBBet8f@lucifer/)</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[lore.kernel.org](https://lore.kernel.org/all/5c504419.9025.19dcf61abcf.Coremail.25181214217@stu.xidian.edu.cn/)</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[lore.kernel.org](https://lore.kernel.org/all/ae9-UhMBMqEAA4Fg@lucifer/)</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[lore.kernel.org](https://lore.kernel.org/all/2850069c.90bd.19dcfb7d591.Coremail.25181214217@stu.xidian.edu.cn/)</u> |
| Revert "mm/hugetlbfs: update hugetlbfs to use mmap_prepare"  | <u>[lore.kernel.org](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=83f9efcce93f8574be2279090ee2aec58b86cda7)</u> |


# 🐛 resv_map memory leak in __mmap_region()

## 📌 Overview
*  **Location:** `fs/hugetlbfs/inode.c` & `mm/vma.c`
* **Current Status:** ✅ **Accepted** **CVE-2026-46318**
* **Notes:** A regression introduced by the VMA iterator refactoring causes a `resv_map` memory leak when VMA creation fails during the `mmap_prepare` phase.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>lore.kernel.org</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>lore.kernel.org</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>lore.kernel.org</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>lore.kernel.org</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>lore.kernel.org</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>lore.kernel.org</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>lore.kernel.org</u> |
| Revert "mm/hugetlbfs: update hugetlbfs to use mmap_prepare"  | <u>lore.kernel.org</u> |


# 🐛 resv_map memory leak in __mmap_region()

## 📌 Overview
*  **Location:** `fs/hugetlbfs/inode.c` & `mm/vma.c`
* **Current Status:** ✅ **Accepted** **CVE-2026-46318**
* **Notes:** A regression introduced by the VMA iterator refactoring causes a `resv_map` memory leak when VMA creation fails during the `mmap_prepare` phase.

## 🔗 Mailing List Threads & Timeline

This section tracks the complete email correspondence and patch history for this vulnerability.

| Description                                                  | Link                                                         |
| :----------------------------------------------------------- | :----------------------------------------------------------- |
| [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[mail](../email/081_%5BRFC%20PATCH%5D%20mm_hugetlb_%20fix%20resv_map%20memory%20leak%20in%20__mmap_region%20erro.txt)</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[mail](../email/084_Re_%20%5BRFC%20PATCH%5D%20mm_hugetlb_%20fix%20resv_map%20memory%20leak%20in%20__mmap_region%20.txt)</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[mail](../email/086_Re_%20Re_%20%5BRFC%20PATCH%5D%20mm_hugetlb_%20fix%20resv_map%20memory%20leak%20in%20__mmap_reg.txt)</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[mail](../email/083_Re_%20%5BRFC%20PATCH%5D%20mm_hugetlb_%20fix%20resv_map%20memory%20leak%20in%20__mmap_region%20.txt)</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[mail](../email/087_Re_%20Re_%20%5BRFC%20PATCH%5D%20mm_hugetlb_%20fix%20resv_map%20memory%20leak%20in%20__mmap_reg.txt)</u> |
| Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[mail](../email/085_Re_%20%5BRFC%20PATCH%5D%20mm_hugetlb_%20fix%20resv_map%20memory%20leak%20in%20__mmap_region%20.txt)</u> |
| Re: Re: [RFC PATCH] mm/hugetlb: fix resv_map memory leak in __mmap_region error path | <u>[mail](../email/088_Re_%20Re_%20%5BRFC%20PATCH%5D%20mm_hugetlb_%20fix%20resv_map%20memory%20leak%20in%20__mmap_reg.txt)</u> |
| Revert "mm/hugetlbfs: update hugetlbfs to use mmap_prepare" | <u>[mail](../email/121_%5BPATCH%5D%20Revert%20_mm_hugetlbfs_%20update%20hugetlbfs%20to%20use%20mmap_prepare_.txt)</u> |


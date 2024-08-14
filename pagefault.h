#include "globals.h"

BOOL pagefault(PULONG_PTR arbitrary_va, FLUSHER* flusher, PULONG_PTR* batch_vas_for_readins, ULONG64* batch_pfns_for_readins);
BOOL map_to_pfn(PULONG_PTR arbitrary_va, ULONG64 pfn);
BOOL unmap_va(PULONG_PTR arbitrary_va);

// VOID checkPrivilegesPTE(PTE* curr_pte);
VOID rescuePTE(PTE* curr_pte);
BOOL handle_new_pte(PTE* curr_pte, ULONG64 pte_region_index_for_lock);
BOOL handle_on_disk(PTE* curr_pte, ULONG64 pte_region_index_for_lock, FLUSHER* flusher, PULONG_PTR* batch_vas_for_readins, ULONG64* batch_pfns_for_readins);
page_t* recycleOldestPage(ULONG64 pte_region_index_for_lock);
void move_pages_from_standby_to_freelist(ULONG64 pte_region_index_for_lock);
BOOL read_batch_from_disk(PTE* curr_pte, ULONG64 pte_region_index_for_lock, FLUSHER* flusher, PULONG_PTR* batch_vas_for_readins, ULONG64* batch_pfns_for_readins);
#include "globals.h"

BOOL pagefault(PULONG_PTR arbitrary_va);
BOOL map_to_pfn(PULONG_PTR arbitrary_va, ULONG64 pfn);
BOOL unmap_va(PULONG_PTR arbitrary_va);
VOID make_active(PTE* given_pte, ULONG64 pfn);
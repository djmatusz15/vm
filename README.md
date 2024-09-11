# Virtual Memory Manager
## DJ Matusz

This is a virtual memory manager. It allocates a certain amount of memory, maps 
a virtual memory address to these pages of physical memory, and can call them back
to the CPU when needed. Employs freelists, a modified list, zerolists and standby list to
properly "juggle" and reuse the same pages for different processes seemlessly.

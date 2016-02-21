// pageWalk1G.c
// Shilpi Goel <shigoel@gmail.com>

/*

Note that I use the words "physical address" when I really mean
"kernel virtual address". Since all the kernel virtual addresses are
directly mapped to the physical address space (physical address =
__pa(kernel virtual address)) in my set up of 1G pages, I feel
comfortable about blurring the distinction.

 */

// -------------------------------------------------------------------------------

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

// We deal with Kernel Virtual Addresses here.

#define _direct_map(x) (x);

u64 part_select (u64 x, u32 low, u32 high) {

  u64 width, val, mask;
  width = high - low + 1;
  mask = (1UL << width) - 1;
  val =  mask & (x >> low);
  return (val);

}

u64 part_install (u64 val, u64 x, u32 low, u32 high) {

  u64 width, mask, ret;
  width = high - low + 1;
  mask = (1UL << width) - 1;
  ret = (((~(mask << low)) & x) | (val << low));
  return (ret);

}

#define CR3_PDB_SHIFT 12

u64 pml4e_paddr (u64 cr3, u64 vaddr) {
  // Input: Contents of the CR3 register and the virtual address
  // Output: "Physical" address of the entry in PML4 table that corresponds to vaddr

  u64 pml4_table_base_paddr;
  u64 paddr;

  printk(KERN_INFO "\n>>>Inside PML4E_PADDR\n");

  pml4_table_base_paddr = _direct_map((cr3 >> CR3_PDB_SHIFT) << CR3_PDB_SHIFT);
  printk(KERN_INFO "\npml4_table_base_paddr: 0x%llx\n", pml4_table_base_paddr);

  // Address of PML4E:
  // Bits 51:12 are from CR3.
  // Bits 11:3 are bits 47:39 of vaddr.
  // Bits 2:0 are 0.
  paddr = part_install (part_select (vaddr, 39, 47),
			pml4_table_base_paddr, 3, 11);

  printk(KERN_INFO "\nAddress of the PML4E: 0x%llx\n", paddr);

  printk(KERN_INFO "\n<<<Exiting PML4E_PADDR");
  return (paddr);

}

u64 pdpte_paddr (u64 pml4e_paddr, u64 vaddr) {
  // Input: "Physical" address of the PML4E and the virtual address
  // Output: "Physical" address of the entry in PDPT table that corresponds to vaddr

  u64 pdpt_table_base_addr, pml4e, paddr;

  printk(KERN_INFO "\n>>>Inside PDPTE_PADDR\n");

  // Read the PML4E entry from pml4e_paddr:
  pml4e = *((u64 *)pml4e_paddr);
  printk(KERN_INFO "\nPML4E entry contents: 0x%llx\n", pml4e);
  // Return error if the PML4E has the P bit cleared.
  if ((pml4e & 1) == 0)
    printk(KERN_INFO "Error!");
  return -1;

  pdpt_table_base_addr = _direct_map(part_select(pml4e, 12, 51) << 12);

  // Address of PDPTE:
  // Bits 51:12 are from the PML4E.
  // Bits 11:3 are bits 38:30 of vaddr.
  // Bits 2:0 are 0.

  paddr = part_install (part_select (vaddr, 30, 38),
			pdpt_table_base_addr, 3, 11);
  printk(KERN_INFO "\nAddress of the PDPTE: 0x%llx\n", paddr);

  printk(KERN_INFO "\n<<<Exiting PDPTE_PADDR");
  return (paddr);

}

u64 paddr (u64 pdpte_addr, u64 vaddr) {
  // Input: "Physical" address of the PDPTE and the virtual address
  // Output: "Physical" address corresponding to vaddr

  u64 page_base_paddr, pdpte, paddr;

  printk(KERN_INFO "\n>>>Inside PADDR\n");

  // Read the PDPTE from the pte_addr:
  pdpte = *((u64 *)pdpte_addr);
  printk(KERN_INFO "\nPDPTE entry contents: 0x%llx\n", pdpte);
  // Return error if the PDPTE has the P or PS bit cleared.
  if (((pdpte & 1) == 0) || (part_select(pdpte, 7, 7) == 0)) {
    printk(KERN_INFO "Error!");
    return -1;
  }

  page_base_paddr = _direct_map(part_select(pdpte, 30, 51) << 30);

  // "Physical" Address corresponding to vaddr:
  // Bits 51:30 are from the PDPTE.
  // Bits 29:0 are bits 29:0 of vaddr.

  paddr = part_install (part_select (vaddr, 0, 29),
			page_base_paddr, 0, 29);

  printk(KERN_INFO "\n'Physical Address': 0x%llx\n", paddr);
  printk(KERN_INFO "\n<<<Exiting PADDR");
  return (paddr);

}

static int __init pageWalk1G(void) {

  u64 cr3;
  u64 pml4e_src_pa, pdpte_src_pa, src_pa, src_la[134217728]; // (expt 2 30)/8

  // printk(KERN_INFO "\npageWalk1G module being loaded...\n");

  /* At this point, page table for src_la[0], src_la[1] is populated (present = 1) */
  src_la[0] = 42; src_la[1] = 3;

  __asm__ __volatile__
    ( // Get cr3.
     "mov %%cr3, %%rax\n\t"
     "mov %%rax, %0\n\t"
     : "=m"(cr3)
     : // no input
     : "%rax"
      );

  // printk(KERN_INFO "\nCR3: 0x%llx\n", cr3);

  /* Do page walk for src. */

  pml4e_src_pa   = pml4e_paddr(cr3, (u64)&src_la[0]);
  pdpte_src_pa   = pdpte_paddr(pml4e_src_pa, (u64)&src_la[0]);
  if (pdpte_src_pa == -1) return -1;
  src_pa         = paddr(pdpte_src_pa, (u64)&src_la[0]);
  if (src_pa       == -1) return -1;

  if ((u64)&src_la[0] == src_pa)
    return 1; // Success
  return 0; // Failure

/*

  printk("\nLinear address of src_la[0]: 0x%llx, src_la[1]: 0x%llx\n",
	 (u64)&src_la[0], (u64)&src_la[1]);
  printk("\nPhysical address (which is really the kernel virtual address) src_pa: 0x%llx, src_pa+1: 0x%llx\n", src_pa, (src_pa+8));
  printk("\nvalue (from page walk) = 0x%llx, value (from variable) = 0x%llx\n",
	 *((u64 *)src_pa), src_la[0]);
  printk("\nvalue (from page walk) = 0x%llx, value (from variable) = 0x%llx\n",
	 *((u64 *)(src_pa + 8)), src_la[1]);

  return 0;

*/
}

static void __exit pageWalk1G_exit(void) {
	printk(KERN_INFO "\npageWalk1G module being unloaded...\n");
}

module_init(pageWalk1G);
module_exit(pageWalk1G_exit);

MODULE_AUTHOR("Shilpi Goel");
MODULE_LICENSE("Dual MIT/GPL");

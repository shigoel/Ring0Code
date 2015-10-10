// pageWalk.c: A primitive page walking Linux Kernel Module
// Shilpi Goel <shigoel@gmail.com>

/*

In Linux, the macro __pa can be used to convert a physical address to
a virtual address. For converting a physical address to a virtual
address, the macro __va can be used. These macros are defined in
arch/ia64/include/asm/page.h as follows:

# define __pa(x)                ((x) - PAGE_OFFSET)
# define __va(x)                ((x) + PAGE_OFFSET)

This fast "translation" is possible because of the way Linux's virtual
memory map with 4-level tables is set up. See
https://www.kernel.org/doc/Documentation/x86/x86_64/mm.txt for more
details.

In this dumb little module, I go about a page walk the hard way, as
described in Intel manuals --- by following pointers in the paging
data structures that reside in the kernel space. Note that I use the
words "physical address" when I really mean "kernel virtual
address". Since all the kernel virtual addresses are directly mapped
to the physical address space (physical address = __pa(kernel virtual
address)), I feel comfortable about blurring the distinction.

 */

// -------------------------------------------------------------------------------

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

// We deal with Kernel Virtual Addresses here.
//
// for details.
#define _direct_map(x) (x | PAGE_OFFSET);

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

#define PML4_SHIFT  39
#define PDPT_SHIFT  30
#define PD_SHIFT    21
#define PT_SHIFT    12

#define PAGE_SHIFT_4K	PT_SHIFT
#define PAGE_SIZE_4K	(_AC(1,UL) << PAGE_SHIFT_4K)
#define PAGE_MASK_4K	(~(PAGE_SIZE_4K-1))

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

  u64 pdpt_table_base_paddr, pml4e, paddr;

  printk(KERN_INFO "\n>>>Inside PDPTE_PADDR\n");

  // Read the PML4E entry from pml4e_paddr:
  pml4e = *((u64 *)pml4e_paddr);
  printk(KERN_INFO "\nPML4E entry contents: 0x%llx\n", pml4e);
  // Return error if the PML4E has the P bit cleared.
  if ((pml4e & 1) == 0) {
    printk(KERN_INFO "Error!");
    return 0;  // Ugh, 0 indicates error.
  }

  pdpt_table_base_paddr = _direct_map(part_select(pml4e, 12, 51) << 12);

  // Address of PDPTE:
  // Bits 51:12 are from the PML4E.
  // Bits 11:3 are bits 38:30 of vaddr.
  // Bits 2:0 are 0.

  paddr = part_install (part_select (vaddr, 30, 38),
			pdpt_table_base_paddr, 3, 11);
  printk(KERN_INFO "\nAddress of the PDPTE: 0x%llx\n", paddr);

  printk(KERN_INFO "\n<<<Exiting PDPTE_PADDR");
  return (paddr);

}

u64 pdte_paddr (u64 pdpte_paddr, u64 vaddr) {
  // Input: "Physical" address of the PDPTE and the virtual address
  // Output: "Physical" address of the entry in PDT table that corresponds to vaddr

  u64 pdt_table_base_paddr, pdpte, paddr;

  printk(KERN_INFO "\n>>>Inside PDTE_PADDR\n");

  // Read the PDPTE entry from pdpte_paddr:
  pdpte = *((u64 *)pdpte_paddr);
  printk(KERN_INFO "\nPDPTE entry contents: 0x%llx", pdpte);
  // Return error if the PDPTE has the P bit cleared or page size != 0.
  if (((pdpte & 1) == 0) || (part_select(pdpte, 7, 7) != 0)) {
    printk(KERN_INFO "Error!");
    return 0; // Ugh, 0 indicates error.
  }

  pdt_table_base_paddr = _direct_map(part_select(pdpte, 12, 51) << 12);

  // Address of PDTE:
  // Bits 51:12 are from the PDPTE.
  // Bits 11:3 are bits 29:21 of vaddr.
  // Bits 2:0 are 0.

  paddr = part_install (part_select (vaddr, 21, 29),
			pdt_table_base_paddr, 3, 11);

  printk(KERN_INFO "\nAddress of the PDTE: 0x%llx\n", paddr);
  printk(KERN_INFO "\n<<<Exiting PDTE_PADDR");
  return (paddr);

}

u64 pte_paddr (u64 pdte_paddr, u64 vaddr) {
  // Input: "Physical" address of the PDT and the virtual address
  // Output: "Physical" address of the entry in PT table that corresponds to vaddr

  u64 pt_table_base_paddr, pdte, paddr;

  printk(KERN_INFO "\n>>>Inside PTE_PADDR\n");

  // Read the PDTE entry from pdte_paddr:
  pdte = *((u64 *)pdte_paddr);
  printk(KERN_INFO "\nPDTE entry contents: 0x%llx\n", pdte);
  // Return error if the PDTE has the P bit cleared or page size != 0.
  if (((pdte & 1) == 0) ||  (part_select(pdte, 7, 7) != 0)) {
    printk(KERN_INFO "Error!");
    return 0; // Ugh, 0 indicates error.
  }

  pt_table_base_paddr = _direct_map(part_select(pdte, 12, 51) << 12);

  // Address of PTE:
  // Bits 51:12 are from the PDT.
  // Bits 11:3 are bits 20:12 of vaddr.
  // Bits 2:0 are 0.

  paddr = part_install (part_select (vaddr, 12, 20),
			pt_table_base_paddr, 3, 11);

  printk(KERN_INFO "\nAddress of the PTE: 0x%llx\n", paddr);
  printk(KERN_INFO "\n<<<Exiting PTE_PADDR");
  return (paddr);

}

u64 paddr (u64 pte_addr, u64 vaddr) {
  // Input: "Physical" address of the PT and the virtual address
  // Output: "Physical" address of the entry in PT table that corresponds to vaddr

  u64 page_base_paddr, pte, paddr;

  printk(KERN_INFO "\n>>>Inside PADDR\n");

  // Read the PTE from the pte_addr:
  pte = *((u64 *)pte_addr);
  printk(KERN_INFO "\nPTE entry contents: 0x%llx\n", pte);
  // Return error if the PTE has the P bit cleared.
  if ((pte & 1) == 0) {
    printk(KERN_INFO "Error!");
    return 0; // Ugh, 0 indicates error.
  }

  page_base_paddr = _direct_map(part_select(pte, 12, 51) << 12);

  // "Physical" Address corresponding to vaddr:
  // Bits 51:12 are from the PTE.
  // Bits 11:0 are bits 11:0 of vaddr.

  paddr = part_install (part_select (vaddr, 0, 11),
			page_base_paddr, 0, 11);

  printk(KERN_INFO "\n'Physical Address': 0x%llx\n", paddr);
  printk(KERN_INFO "\n<<<Exiting PADDR");
  return (paddr);

}

static int __init pagewalk(void) {

  u64 cr3, pml4e_pa, pdpte_pa, pdte_pa, pte_pa, pa, la[2];

  /* At this point, page table for la[0], la[1] is populated (present = 1) */
  la[0] = 42; la[1] = 3;

  printk(KERN_INFO "\npageWalk module being loaded...\n");

  __asm__ __volatile__
    ( // Get cr3.
     "mov %%cr3, %%rax\n\t"
     "mov %%rax, %0\n\t"
     : "=m"(cr3)
     : // no input
     : "%rax"
      );

  printk(KERN_INFO "\nCR3: 0x%llx\n", cr3);

  /* Do page walk. */

  pml4e_pa   = pml4e_paddr(cr3, (u64)&la[0]);
  pdpte_pa   = pdpte_paddr(pml4e_pa, (u64)&la[0]);
  if (pdpte_pa == 0) goto out;
  pdte_pa    = pdte_paddr(pdpte_pa, (u64)&la[0]);
  if (pdte_pa  == 0) goto out;
  pte_pa     = pte_paddr(pdte_pa, (u64)&la[0]);
  if (pte_pa   == 0) goto out;
  pa         = paddr(pte_pa, (u64)&la[0]);
  if (pa       == 0) goto out;


  if (PAGE_OFFSET == 0xffff880000000000)
    printk("\nPAGE_OFFSET is what you expect it to be: 0x%lx\n", PAGE_OFFSET);
  else
    printk("\n!!! PAGE_OFFSET is NOT what you expect it to be: 0x%lx !!!\n", PAGE_OFFSET);

  printk("\nLinear address of la[0]: 0x%llx, la[1]: 0x%llx\n", (u64)&la[0], (u64)&la[1]);
  printk("\nPhysical address (which is really the kernel virtual address) pa: 0x%llx, pa+1: 0x%llx\n", pa, (pa+8));
  printk("\nvalue (from page walk) = 0x%llx, value (from variable) = 0x%llx\n",
	 *((u64 *)pa), la[0]);
  printk("\nvalue (from page walk) = 0x%llx, value (from variable) = 0x%llx\n",
	 *((u64 *)(pa + 8)), la[1]);

 out:
  return 0;
}

static void __exit pagewalk_exit(void) {
	printk(KERN_INFO "\npageWalk module being unloaded...\n");
}

module_init(pagewalk);
module_exit(pagewalk_exit);

MODULE_AUTHOR("Shilpi Goel");
MODULE_LICENSE("Dual MIT/GPL");

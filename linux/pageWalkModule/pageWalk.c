#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

// We deal with Kernel Virtual Addresses here. See mm.txt for details.
#define _direct_map(x) (x | PAGE_OFFSET);

u64 part_select (u64 x, u32 low, u32 high) {

  u64 width, val, mask;
  width = high - low + 1;
  // printk(KERN_INFO "\npart_select width: %llu\n", width);
  mask = (1UL << width) - 1;
  // printk(KERN_INFO "\npart_select mask: %llx\n", mask);
  val =  mask & (x >> low);
  return (val);

}

u64 part_install (u64 val, u64 x, u32 low, u32 high) {

  u64 width, mask, ret;
  width = high - low + 1;
  // printk(KERN_INFO "\npart_install width: %llu\n", width);
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

// Physical Address Width: 46
// TODO: Will plugging 52 instead of 46 here be okay too?
#define __P_MASK    ((1ULL << 46) - 1)
#define P_PAGE_MASK (((signed long)PAGE_MASK_4K) & __P_MASK)

u64 pml4e_paddr (u64 cr3, u64 vaddr) {
  // Input: Contents of the CR3 register and the virtual address
  // Output: Physical address of the entry in PML4 table that corresponds to vaddr

  u64 pml4_table_base_paddr;
  u64 paddr;

  printk(KERN_INFO "\n>>>Inside PML4E_PADDR\n");

  pml4_table_base_paddr = _direct_map((cr3 >> CR3_PDB_SHIFT) << CR3_PDB_SHIFT);
  printk(KERN_INFO "\npml4_table_base_paddr: %llx\n", pml4_table_base_paddr);

  // Address of PML4E:
  // Bits 51:12 are from CR3.
  // Bits 11:3 are bits 47:39 of vaddr.
  // Bits 2:0 are 0.
  paddr = part_install (part_select (vaddr, 39, 47),
			pml4_table_base_paddr, 3, 11);

  printk(KERN_INFO "\nAddress of the PML4E: %llx\n", paddr);

  printk(KERN_INFO "\n<<<Exiting PML4E_PADDR");
  return (paddr);

}

u64 pdpte_paddr (u64 pml4e_paddr, u64 vaddr) {
  // Input: Physical address of the PML4E and the virtual address
  // Output: Physical address of the entry in PDPT table that corresponds to vaddr

  u64 pdpt_table_base_paddr, pml4e, paddr;

  printk(KERN_INFO "\n>>>Inside PDPTE_PADDR\n");

  // Read the PML4E entry from pml4e_paddr:
  pml4e = *((u64 *)pml4e_paddr);
  printk(KERN_INFO "\nPML4E entry contents: %llx\n", pml4e);
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
  printk(KERN_INFO "\nAddress of the PDPTE: %llx\n", paddr);

  printk(KERN_INFO "\n<<<Exiting PDPTE_PADDR");
  return (paddr);

}

u64 pdte_paddr (u64 pdpte_paddr, u64 vaddr) {
  // Input: Physical address of the PDPTE and the virtual address
  // Output: Physical address of the entry in PDT table that corresponds to vaddr

  u64 pdt_table_base_paddr, pdpte, paddr;

  printk(KERN_INFO "\n>>>Inside PDTE_PADDR\n");

  // Read the PDPTE entry from pdpte_paddr:
  pdpte = *((u64 *)pdpte_paddr);
  printk(KERN_INFO "\nPDPTE entry contents: %llx", pdpte);
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

  printk(KERN_INFO "\nAddress of the PDTE: %llx\n", paddr);
  printk(KERN_INFO "\n<<<Exiting PDTE_PADDR");
  return (paddr);

}

u64 pte_paddr (u64 pdte_paddr, u64 vaddr) {
  // Input: Physical address of the PDT and the virtual address
  // Output: Physical address of the entry in PT table that corresponds to vaddr

  u64 pt_table_base_paddr, pdte, paddr;

  printk(KERN_INFO "\n>>>Inside PTE_PADDR\n");

  // Read the PDTE entry from pdte_paddr:
  pdte = *((u64 *)pdte_paddr);
  printk(KERN_INFO "\nPDTE entry contents: %llx\n", pdte);
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

  printk(KERN_INFO "\nAddress of the PTE: %llx\n", paddr);
  printk(KERN_INFO "\n<<<Exiting PTE_PADDR");
  return (paddr);

}

u64 paddr (u64 pte_addr, u64 vaddr) {
  // Input: Physical address of the PT and the virtual address
  // Output: Physical address of the entry in PT table that corresponds to vaddr

  u64 page_base_paddr, pte, paddr;

  printk(KERN_INFO "\n>>>Inside PADDR\n");

  // Read the PTE from the pte_addr:
  pte = *((u64 *)pte_addr);
  printk(KERN_INFO "\nPTE entry contents: %llx\n", pte);
  // Return error if the PTE has the P bit cleared.
  if ((pte & 1) == 0) {
    printk(KERN_INFO "Error!");
    return 0; // Ugh, 0 indicates error.
  }

  page_base_paddr = _direct_map(part_select(pte, 12, 51) << 12);

  // Physical Address corresponding to vaddr:
  // Bits 51:12 are from the PTE.
  // Bits 11:0 are bits 11:0 of vaddr.

  paddr = part_install (part_select (vaddr, 0, 11),
			page_base_paddr, 0, 11);

  printk(KERN_INFO "\nPhysical Address: %llx\n", paddr);
  printk(KERN_INFO "\n<<<Exiting PADDR");
  return (paddr);

}

static int __init pagewalk(void) {

  u64 cr3, pml4e_pa, pdpte_pa, pdte_pa, pte_pa, pa, la[2];

  /* At this point, page table for la[0], la[1] is populated (present = 1) */
  la[0] = 42; la[1] = 3;

  printk(KERN_INFO "\nPageWalker module being loaded...\n");

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

  printk("\nvalue (from page walk) = %llx, value (from variable) = %llx\n",
	 *((u64 *)pa), la[0]);
  printk("\nvalue (from page walk) = %llx, value (from variable) = %llx\n",
	 *((u64 *)pa + 1), la[1]);

 out:
  return 0;
}

static void __exit pagewalk_exit(void) {
	printk(KERN_INFO "\nPageWalker module being unloaded...\n");
}

module_init(pagewalk);
module_exit(pagewalk_exit);

MODULE_AUTHOR("Shilpi Goel");
MODULE_LICENSE("GPL");

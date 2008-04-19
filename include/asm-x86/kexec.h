#ifndef _KEXEC_H
#define _KEXEC_H

#ifdef CONFIG_X86_32
# define PA_CONTROL_PAGE	0
# define VA_CONTROL_PAGE	1
# define PA_PGD			2
# define VA_PGD			3
# define PA_PTE_0		4
# define VA_PTE_0		5
# define PA_PTE_1		6
# define VA_PTE_1		7
# ifdef CONFIG_X86_PAE
#  define PA_PMD_0		8
#  define VA_PMD_0		9
#  define PA_PMD_1		10
#  define VA_PMD_1		11
#  define PAGES_NR		12
# else
#  define PAGES_NR		8
# endif
#else
# define PA_CONTROL_PAGE	0
# define VA_CONTROL_PAGE	1
# define PA_PGD			2
# define VA_PGD			3
# define PA_PUD_0		4
# define VA_PUD_0		5
# define PA_PMD_0		6
# define VA_PMD_0		7
# define PA_PTE_0		8
# define VA_PTE_0		9
# define PA_PUD_1		10
# define VA_PUD_1		11
# define PA_PMD_1		12
# define VA_PMD_1		13
# define PA_PTE_1		14
# define VA_PTE_1		15
# define PA_TABLE_PAGE		16
# define PAGES_NR		17
#endif

#ifndef __ASSEMBLY__

#include <linux/string.h>

#include <asm/page.h>
#include <asm/ptrace.h>

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 *
 * So far x86_64 is limited to 40 physical address bits.
 */
#ifdef CONFIG_X86_32
/* Maximum physical address we can use pages from */
# define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
# define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
# define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE

# define KEXEC_CONTROL_CODE_SIZE	4096

/* The native architecture */
# define KEXEC_ARCH KEXEC_ARCH_386

/* We can also handle crash dumps from 64 bit kernel. */
# define vmcore_elf_check_arch_cross(x) ((x)->e_machine == EM_X86_64)
#else
/* Maximum physical address we can use pages from */
# define KEXEC_SOURCE_MEMORY_LIMIT      (0xFFFFFFFFFFUL)
/* Maximum address we can reach in physical address mode */
# define KEXEC_DESTINATION_MEMORY_LIMIT (0xFFFFFFFFFFUL)
/* Maximum address we can use for the control pages */
# define KEXEC_CONTROL_MEMORY_LIMIT     (0xFFFFFFFFFFUL)

/* Allocate one page for the pdp and the second for the code */
# define KEXEC_CONTROL_CODE_SIZE  (4096UL + 4096UL)

/* The native architecture */
# define KEXEC_ARCH KEXEC_ARCH_X86_64
#endif

/*
 * CPU does not save ss and sp on stack if execution is already
 * running in kernel mode at the time of NMI occurrence. This code
 * fixes it.
 */
static inline void crash_fixup_ss_esp(struct pt_regs *newregs,
				      struct pt_regs *oldregs)
{
#ifdef CONFIG_X86_32
	newregs->sp = (unsigned long)&(oldregs->sp);
	asm volatile("xorl %%eax, %%eax\n\t"
		     "movw %%ss, %%ax\n\t"
		     :"=a"(newregs->ss));
#endif
}

/*
 * This function is responsible for capturing register states if coming
 * via panic otherwise just fix up the ss and sp if coming via kernel
 * mode exception.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	if (oldregs) {
		memcpy(newregs, oldregs, sizeof(*newregs));
		crash_fixup_ss_esp(newregs, oldregs);
	} else {
#ifdef CONFIG_X86_32
		asm volatile("movl %%ebx,%0" : "=m"(newregs->bx));
		asm volatile("movl %%ecx,%0" : "=m"(newregs->cx));
		asm volatile("movl %%edx,%0" : "=m"(newregs->dx));
		asm volatile("movl %%esi,%0" : "=m"(newregs->si));
		asm volatile("movl %%edi,%0" : "=m"(newregs->di));
		asm volatile("movl %%ebp,%0" : "=m"(newregs->bp));
		asm volatile("movl %%eax,%0" : "=m"(newregs->ax));
		asm volatile("movl %%esp,%0" : "=m"(newregs->sp));
		asm volatile("movl %%ss, %%eax;" :"=a"(newregs->ss));
		asm volatile("movl %%cs, %%eax;" :"=a"(newregs->cs));
		asm volatile("movl %%ds, %%eax;" :"=a"(newregs->ds));
		asm volatile("movl %%es, %%eax;" :"=a"(newregs->es));
		asm volatile("pushfl; popl %0" :"=m"(newregs->flags));
#else
		asm volatile("movq %%rbx,%0" : "=m"(newregs->bx));
		asm volatile("movq %%rcx,%0" : "=m"(newregs->cx));
		asm volatile("movq %%rdx,%0" : "=m"(newregs->dx));
		asm volatile("movq %%rsi,%0" : "=m"(newregs->si));
		asm volatile("movq %%rdi,%0" : "=m"(newregs->di));
		asm volatile("movq %%rbp,%0" : "=m"(newregs->bp));
		asm volatile("movq %%rax,%0" : "=m"(newregs->ax));
		asm volatile("movq %%rsp,%0" : "=m"(newregs->sp));
		asm volatile("movq %%r8,%0" : "=m"(newregs->r8));
		asm volatile("movq %%r9,%0" : "=m"(newregs->r9));
		asm volatile("movq %%r10,%0" : "=m"(newregs->r10));
		asm volatile("movq %%r11,%0" : "=m"(newregs->r11));
		asm volatile("movq %%r12,%0" : "=m"(newregs->r12));
		asm volatile("movq %%r13,%0" : "=m"(newregs->r13));
		asm volatile("movq %%r14,%0" : "=m"(newregs->r14));
		asm volatile("movq %%r15,%0" : "=m"(newregs->r15));
		asm volatile("movl %%ss, %%eax;" :"=a"(newregs->ss));
		asm volatile("movl %%cs, %%eax;" :"=a"(newregs->cs));
		asm volatile("pushfq; popq %0" :"=m"(newregs->flags));
#endif
		newregs->ip = (unsigned long)current_text_addr();
	}
}

#ifdef CONFIG_X86_32
asmlinkage NORET_TYPE void
relocate_kernel(unsigned long indirection_page,
		unsigned long control_page,
		unsigned long start_address,
		unsigned int has_pae) ATTRIB_NORET;
#else
NORET_TYPE void
relocate_kernel(unsigned long indirection_page,
		unsigned long page_list,
		unsigned long start_address) ATTRIB_NORET;
#endif

#endif /* __ASSEMBLY__ */

#endif /* _KEXEC_H */

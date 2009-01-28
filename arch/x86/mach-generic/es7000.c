/*
 * APIC driver for the Unisys ES7000 chipset.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/es7000/apicdef.h>
#include <linux/smp.h>
#include <asm/es7000/apic.h>
#include <asm/es7000/ipi.h>
#include <asm/es7000/mpparse.h>
#include <asm/mach-default/mach_wakecpu.h>

void __init es7000_update_genapic_to_cluster(void)
{
	apic->target_cpus = target_cpus_cluster;
	apic->irq_delivery_mode = INT_DELIVERY_MODE_CLUSTER;
	apic->irq_dest_mode = INT_DEST_MODE_CLUSTER;
	apic->no_balance_irq = NO_BALANCE_IRQ_CLUSTER;

	apic->init_apic_ldr = init_apic_ldr_cluster;

	apic->cpu_mask_to_apicid = cpu_mask_to_apicid_cluster;
}

static int probe_es7000(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

extern void es7000_sw_apic(void);
static void __init enable_apic_mode(void)
{
	es7000_sw_apic();
	return;
}

static __init int
mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	if (mpc->oemptr) {
		struct mpc_oemtable *oem_table =
			(struct mpc_oemtable *)mpc->oemptr;
		if (!strncmp(oem, "UNISYS", 6))
			return parse_unisys_oem((char *)oem_table);
	}
	return 0;
}

#ifdef CONFIG_ACPI
/* Hook from generic ACPI tables.c */
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr = 0;
	int check_dsdt;
	int ret = 0;

	/* check dsdt at first to avoid clear fix_map for oem_addr */
	check_dsdt = es7000_check_dsdt();

	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (check_dsdt)
			ret = parse_unisys_oem((char *)oem_addr);
		else {
			setup_unisys();
			ret = 1;
		}
		/*
		 * we need to unmap it
		 */
		unmap_unisys_acpi_oem_table(oem_addr);
	}
	return ret;
}
#else
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}
#endif

static void vector_allocation_domain(int cpu, cpumask_t *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	*retmask = (cpumask_t){ { [0] = APIC_ALL_CPUS, } };
}

struct genapic apic_es7000 = {

	.name				= "es7000",
	.probe				= probe_es7000,
	.acpi_madt_oem_check		= es7000_acpi_madt_oem_check,
	.apic_id_registered		= es7000_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	/* phys delivery to target CPUs: */
	.irq_dest_mode			= 0,

	.target_cpus			= es7000_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= es7000_check_apicid_used,
	.check_apicid_present		= es7000_check_apicid_present,

	.no_balance_irq			= NO_BALANCE_IRQ,
	.no_ioapic_check		= 0,

	.vector_allocation_domain	= vector_allocation_domain,
	.init_apic_ldr			= init_apic_ldr,

	.ioapic_phys_id_map		= ioapic_phys_id_map,
	.setup_apic_routing		= setup_apic_routing,
	.multi_timer_check		= multi_timer_check,
	.apicid_to_node			= apicid_to_node,
	.cpu_to_logical_apicid		= cpu_to_logical_apicid,
	.cpu_present_to_apicid		= cpu_present_to_apicid,
	.apicid_to_cpu_present		= apicid_to_cpu_present,
	.setup_portio_remap		= setup_portio_remap,
	.check_phys_apicid_present	= check_phys_apicid_present,
	.enable_apic_mode		= enable_apic_mode,
	.phys_pkg_id			= phys_pkg_id,
	.mps_oem_check			= mps_oem_check,

	.get_apic_id			= get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= APIC_ID_MASK,

	.cpu_mask_to_apicid		= cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= cpu_mask_to_apicid_and,

	.send_IPI_mask			= send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= send_IPI_allbutself,
	.send_IPI_all			= send_IPI_all,
	.send_IPI_self			= NULL,

	.wakeup_cpu			= NULL,
	.trampoline_phys_low		= TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= TRAMPOLINE_PHYS_HIGH,
	.wait_for_init_deassert		= wait_for_init_deassert,
	.smp_callin_clear_local_apic	= smp_callin_clear_local_apic,
	.store_NMI_vector		= store_NMI_vector,
	.restore_NMI_vector		= restore_NMI_vector,
	.inquire_remote_apic		= inquire_remote_apic,
};

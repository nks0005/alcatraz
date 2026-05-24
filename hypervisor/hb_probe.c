#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/preempt.h>
#include <linux/namei.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include "asm_helper.h"

#define CPUID_1_ECX_VMX					((u32)0x01 << 5)

#define MSR_IA32_FEATURE_CONTROL_BIT_CONTROL_LOCKED			(0x01 << 0)
#define MSR_IA32_FEATURE_CONTROL_BIT_VMXON_ENABLED_OUTPUTSIDE_SMX	(0x01 << 2)

#define PRLOG "hypervisor_b: "

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#define HB_MSR_FEATURE_CTL	MSR_IA32_FEATURE_CONTROL
#else
#define HB_MSR_FEATURE_CTL	MSR_IA32_FEAT_CTL
#endif

/* VMCS field: link pointer (no shadow VMCS) — hyper_box VM_VMCS_LINK_PTR */
#define VM_VMCS_LINK_PTR	0x2800ULL

struct hb_percpu_vmx {
	void *vmxon_region;
	void *vmcs_region;
	bool vmx_on;
	u64 saved_cr0;
	u64 saved_cr4;
};

static struct hb_percpu_vmx *g_vmx;
static u32 g_vmx_revision;
static atomic_t g_vmx_probe_failed;

static bool is_vmx_supported(void);
static bool is_bios_vmx_allowed(void);
static u32 hb_vmx_revision_id(void);
static void hb_adjust_vmx_cr0_cr4(void);
static int hb_vmx_alloc_regions(void);
static void hb_vmx_free_regions(void);
static int hb_vmx_probe_vmwrite(unsigned int cpu);
static void hb_vmxon_on_cpu(void *info);
static void hb_vmxoff_on_cpu(void *info);
static int hb_vmx_startup_all_cpus(void);
static void hb_vmx_teardown_all_cpus(void);

static bool hb_module_loaded(const char *name)
{
	char path[48];
	struct path p;
	int ret;

	snprintf(path, sizeof(path), "/sys/module/%s", name);
	ret = kern_path(path, 0, &p);
	if (ret)
		return false;
	path_put(&p);
	return true;
}

static bool hb_is_kvm_loaded(void)
{
	return hb_module_loaded("kvm_intel") || hb_module_loaded("kvm");
}

static int __init hypervisor_init(void)
{
	int cpu_count;
	int cpu_id;
	int ret;

	if (!is_vmx_supported()) {
		pr_info(PRLOG "VMX not supported (CPUID)\n");
		return -ENODEV;
	}
	pr_info(PRLOG "VMX supported (CPUID)\n");

	if (!is_bios_vmx_allowed()) {
		pr_info(PRLOG "VMX disabled by BIOS (IA32_FEATURE_CONTROL)\n");
		return -ENODEV;
	}
	pr_info(PRLOG "VMX supported (BIOS)\n");

	if (hb_is_kvm_loaded()) {
		pr_err(PRLOG "kvm/kvm_intel is loaded; VMXON conflicts with KVM. "
			"Run: sudo modprobe -r kvm_intel kvm\n");
		return -EBUSY;
	}

	cpu_count = num_online_cpus();
	cpu_id = smp_processor_id();
	pr_info(PRLOG "online cpus=%d, init on cpu=%d\n", cpu_count, cpu_id);

	g_vmx_revision = hb_vmx_revision_id();
	pr_info(PRLOG "VMCS revision id=0x%08x (IA32_VMX_BASIC)\n", g_vmx_revision);

	ret = hb_vmx_alloc_regions();
	if (ret)
		return ret;

	ret = hb_vmx_startup_all_cpus();
	if (ret) {
		hb_vmx_teardown_all_cpus();
		hb_vmx_free_regions();
		return ret;
	}

	pr_info(PRLOG "all %u online cpus: VMXON/VMCLEAR/VMPTRLD/VMWRITE probe ok "
		"(VMXOFF on module unload)\n", num_online_cpus());
	return 0;
}

static bool is_vmx_supported(void)
{
	u32 eax, ebx, ecx, edx;
	bool vmx_supported;

	cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
	vmx_supported = !!(ecx & CPUID_1_ECX_VMX);
	pr_info(PRLOG "VMX support: %d (cpuid.1.ecx=0x%08x)\n",
		vmx_supported, ecx);

	return vmx_supported;
}

static bool is_bios_vmx_allowed(void)
{
	u64 msr;

	msr = hb_rdmsr(HB_MSR_FEATURE_CTL);
	pr_info(PRLOG "IA32_FEATURE_CONTROL: 0x%016llx\n", msr);

	if (msr & MSR_IA32_FEATURE_CONTROL_BIT_CONTROL_LOCKED) {
		if (!(msr & MSR_IA32_FEATURE_CONTROL_BIT_VMXON_ENABLED_OUTPUTSIDE_SMX)) {
			pr_info(PRLOG "VMX locked off outside SMX in FEATURE_CONTROL\n");
			return false;
		}
	}

	return true;
}

static u32 hb_vmx_revision_id(void)
{
	u64 vmx_basic = hb_rdmsr(MSR_IA32_VMX_BASIC);

	return (u32)(vmx_basic & 0x7fffffff);
}

static void hb_adjust_vmx_cr0_cr4(void)
{
	u64 cr0, cr4;
	u64 cr0_fixed0, cr0_fixed1, cr4_fixed0, cr4_fixed1;

	// MSR_IA32_VMX_...
	cr0_fixed0 = hb_rdmsr(MSR_IA32_VMX_CR0_FIXED0); // do 1
 	cr0_fixed1 = hb_rdmsr(MSR_IA32_VMX_CR0_FIXED1); // do 0

	 cr0 = hb_get_cr0();
	 cr0 |= cr0_fixed0;
	 cr0 &= cr0_fixed1;
	 hb_set_cr0(cr0);

	cr4_fixed0 = hb_rdmsr(MSR_IA32_VMX_CR4_FIXED0); // do 1
	cr4_fixed1 = hb_rdmsr(MSR_IA32_VMX_CR4_FIXED1); // do 0
	
	cr4 = hb_get_cr4();
	cr4 |= cr4_fixed0;
	cr4 &= cr4_fixed1;
	hb_set_cr4(cr4);
}

static int hb_vmx_alloc_regions(void)
{
	unsigned int cpu;

	// 논리 프로세서 별 영역 할당 (per-CPU VMXON/VMCS 영역)
	g_vmx = kcalloc(nr_cpu_ids, sizeof(*g_vmx), GFP_KERNEL);
	if (!g_vmx)
		return -ENOMEM;

	pr_info(PRLOG "g_vmx allocated at %p (size: %zu bytes for %d CPUs)\n", 
		g_vmx, sizeof(*g_vmx) * nr_cpu_ids, nr_cpu_ids);

	for (unsigned int i = 0; i < nr_cpu_ids; ++i) {
		pr_info(PRLOG "g_vmx[%u] at %p\n", i, &g_vmx[i]);
	}

	for_each_online_cpu(cpu) {
		void *vmxon, *vmcs;
		phys_addr_t vmxon_pa, vmcs_pa;

		pr_info(PRLOG "allocating VMXON/VMCS regions for cpu %u...\n", cpu);

		/* VMXON region: 4KB 물리 페이지 1장 (__get_free_page, 4KB 정렬) */
		vmxon = (void *)__get_free_page(GFP_KERNEL);
		/* VMCS region: 4KB 물리 페이지 1장 (VMX 명령은 PA로 참조) */
		vmcs = (void *)__get_free_page(GFP_KERNEL);

		g_vmx[cpu].vmxon_region = vmxon;
		g_vmx[cpu].vmcs_region = vmcs;

		if (!vmxon || !vmcs) {
			pr_err(PRLOG "alloc vmxon/vmcs failed on cpu %u\n", cpu);
			hb_vmx_free_regions();
			return -ENOMEM;
		}

		vmxon_pa = __pa(vmxon);
		vmcs_pa = __pa(vmcs);

		memset(vmxon, 0, PAGE_SIZE);
		memset(vmcs, 0, PAGE_SIZE);

		pr_info(PRLOG "cpu %u: vmxon va=%p pa=0x%llx, vmcs va=%p pa=0x%llx (zeroed)\n",
			cpu, vmxon, (u64)vmxon_pa, vmcs, (u64)vmcs_pa);
	}

	pr_info(PRLOG "All per-CPU VMXON/VMCS allocation complete.\n");

	return 0;
}

static void hb_vmx_free_regions(void)
{
	unsigned int cpu;

	if (!g_vmx)
		return;

	for_each_online_cpu(cpu) {
		if (g_vmx[cpu].vmxon_region) {
			free_page((unsigned long)g_vmx[cpu].vmxon_region);
			g_vmx[cpu].vmxon_region = NULL;
		}
		if (g_vmx[cpu].vmcs_region) {
			free_page((unsigned long)g_vmx[cpu].vmcs_region);
			g_vmx[cpu].vmcs_region = NULL;
		}
	}

	kfree(g_vmx);
	g_vmx = NULL;
}

/*
 * Minimal VMWRITE probe: link pointer (no shadow VMCS) write + VMREAD verify.
 * Must run after VMPTRLD on the same CPU with VMX still on.
 */
static int hb_vmx_probe_vmwrite(unsigned int cpu)
{
	const u64 link_none = 0xffffffffffffffffULL;
	int ret;
	u64 read_back = 0;

	ret = hb_write_vmcs(VM_VMCS_LINK_PTR, link_none);
	if (ret) {
		pr_err(PRLOG "cpu %u: VMWRITE VMCS_LINK_PTR failed (%d)\n", cpu, ret);
		return ret;
	}

	ret = hb_read_vmcs(VM_VMCS_LINK_PTR, &read_back);
	if (ret) {
		pr_err(PRLOG "cpu %u: VMREAD VMCS_LINK_PTR failed (%d)\n", cpu, ret);
		return ret;
	}

	if (read_back != link_none) {
		pr_err(PRLOG "cpu %u: VMCS_LINK_PTR mismatch (wrote %llx, read %llx)\n",
			cpu, link_none, read_back);
		return -EIO;
	}

	pr_info(PRLOG "cpu %u: VMWRITE/VMREAD VMCS_LINK_PTR ok\n", cpu);
	return 0;
}

static void hb_vmxon_on_cpu(void *info)
{
	unsigned int cpu = smp_processor_id();
	struct hb_percpu_vmx *pc;
	u64 saved_cr0, saved_cr4;
	u64 vmxon_pa, vmcs_pa;
	int ret;
	bool vmx_on = false;

	(void)info;

	if (atomic_read(&g_vmx_probe_failed))
		return;

	pc = &g_vmx[cpu];
	if (!pc->vmxon_region || !pc->vmcs_region) {
		atomic_set(&g_vmx_probe_failed, 1);
		return;
	}

	*(u32 *)pc->vmxon_region = g_vmx_revision;
	*(u32 *)pc->vmcs_region = g_vmx_revision;

	/*
	 * VMX root 동안 인터럽트/선점이 들어오면 커널이 즉시 죽을 수 있음.
	 * 프로브만 하고 VMXOFF·CR 복구 후 IRQ를 다시 켠다.
	 */
	preempt_disable();
	local_irq_disable();

	saved_cr0 = hb_get_cr0();
	saved_cr4 = hb_get_cr4();
	pc->saved_cr0 = saved_cr0;
	pc->saved_cr4 = saved_cr4;

	hb_adjust_vmx_cr0_cr4();
	hb_enable_vmx();

	vmxon_pa = (u64)__pa(pc->vmxon_region);
	ret = hb_start_vmx(&vmxon_pa);
	if (ret) {
		pr_err(PRLOG "cpu %u: VMXON failed (%d)\n", cpu, ret);
		atomic_set(&g_vmx_probe_failed, 1);
		goto out_restore_cr;
	}

	vmx_on = true;
	pc->vmx_on = true;

	vmcs_pa = (u64)__pa(pc->vmcs_region);
	ret = hb_clear_vmcs(&vmcs_pa);
	if (ret) {
		pr_err(PRLOG "cpu %u: VMCLEAR failed (%d)\n", cpu, ret);
		atomic_set(&g_vmx_probe_failed, 1);
		goto out_vmxoff;
	}

	ret = hb_load_vmcs(&vmcs_pa);
	if (ret) {
		pr_err(PRLOG "cpu %u: VMPTRLD failed (%d)\n", cpu, ret);
		atomic_set(&g_vmx_probe_failed, 1);
		goto out_vmxoff;
	}

	ret = hb_vmx_probe_vmwrite(cpu);
	if (ret) {
		atomic_set(&g_vmx_probe_failed, 1);
		goto out_vmxoff;
	}

	pr_info(PRLOG "cpu %u: VMXON/VMCLEAR/VMPTRLD/VMWRITE ok (vmcs pa=0x%llx, "
		"VMXOFF on rmmod)\n", cpu, (u64)vmcs_pa);
	goto out_keep_vmx;

out_vmxoff:
	if (vmx_on) {
		hb_stop_vmx();
		vmx_on = false;
		pc->vmx_on = false;
	}
out_restore_cr:
	hb_disable_vmx();
	hb_set_cr4(saved_cr4);
	hb_set_cr0(saved_cr0);
	local_irq_enable();
	preempt_enable();
	return;

out_keep_vmx:
	/* VMXON 유지 — VMXOFF·CR 복구는 hb_vmxoff_on_cpu / module exit */
	local_irq_enable();
	preempt_enable();
}

static void hb_vmxoff_on_cpu(void *info)
{
	unsigned int cpu = smp_processor_id();
	struct hb_percpu_vmx *pc;

	(void)info;

	pc = &g_vmx[cpu];
	if (!pc->vmx_on)
		return;

	preempt_disable();
	local_irq_disable();

	hb_stop_vmx();
	hb_disable_vmx();
	hb_set_cr4(pc->saved_cr4);
	hb_set_cr0(pc->saved_cr0);
	pc->vmx_on = false;

	local_irq_enable();
	preempt_enable();

	pr_info(PRLOG "cpu %u: VMXOFF ok (CR restored)\n", cpu);
}

static int hb_vmx_startup_all_cpus(void)
{
	unsigned int cpu;

	atomic_set(&g_vmx_probe_failed, 0);

	for_each_online_cpu(cpu) {
		pr_info(PRLOG "VMX probe on cpu %u\n", cpu);
		smp_call_function_single(cpu, hb_vmxon_on_cpu, NULL, 1);
		if (atomic_read(&g_vmx_probe_failed))
			return -EIO;
	}

	return 0;
}

static void hb_vmx_teardown_all_cpus(void)
{
	unsigned int cpu;

	if (!g_vmx)
		return;

	for_each_online_cpu(cpu)
		smp_call_function_single(cpu, hb_vmxoff_on_cpu, NULL, 1);
}

static void __exit hypervisor_exit(void)
{
	hb_vmx_teardown_all_cpus();
	hb_vmx_free_regions();
	pr_info(PRLOG "unloaded\n");
}

module_init(hypervisor_init);
module_exit(hypervisor_exit);

MODULE_AUTHOR("nks004@naver.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hypervisor probe: VMXON/VMCLEAR/VMPTRLD/VMWRITE; VMXOFF on unload");

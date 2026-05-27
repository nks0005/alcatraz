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
#include <linux/string.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include "asm_helper.h"

/*
 * 1: VMPTRLD 후 Hyper-box 스타일 guest state VMWRITE 예시 (VMLAUNCH 없음).
 * 0: VM_VMCS_LINK_PTR 프로브만.
 */
#define HB_PROBE_GUEST_STATE_EXAMPLE	1

#define CPUID_1_ECX_VMX					((u32)0x01 << 5)

#define MSR_IA32_FEATURE_CONTROL_BIT_CONTROL_LOCKED			(0x01 << 0)
#define MSR_IA32_FEATURE_CONTROL_BIT_VMXON_ENABLED_OUTPUTSIDE_SMX	(0x01 << 2)

#define PRLOG "hypervisor_b: "

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#define HB_MSR_FEATURE_CTL	MSR_IA32_FEATURE_CONTROL
#else
#define HB_MSR_FEATURE_CTL	MSR_IA32_FEAT_CTL
#endif

/* VMCS guest encodings (hyper_box.h 와 동일) */
#define VM_VMCS_LINK_PTR			0x2800ULL
#define VM_GUEST_ES_SELECTOR			0x800ULL
#define VM_GUEST_CS_SELECTOR			0x802ULL
#define VM_GUEST_SS_SELECTOR			0x804ULL
#define VM_GUEST_DS_SELECTOR			0x806ULL
#define VM_GUEST_FS_SELECTOR			0x808ULL
#define VM_GUEST_GS_SELECTOR			0x80AULL
#define VM_GUEST_LDTR_SELECTOR			0x80CULL
#define VM_GUEST_TR_SELECTOR			0x80EULL
#define VM_GUEST_ES_LIMIT			0x4800ULL
#define VM_GUEST_CS_LIMIT			0x4802ULL
#define VM_GUEST_SS_LIMIT			0x4804ULL
#define VM_GUEST_DS_LIMIT			0x4806ULL
#define VM_GUEST_FS_LIMIT			0x4808ULL
#define VM_GUEST_GS_LIMIT			0x480AULL
#define VM_GUEST_LDTR_LIMIT			0x480CULL
#define VM_GUEST_TR_LIMIT			0x480EULL
#define VM_GUEST_GDTR_LIMIT			0x4810ULL
#define VM_GUEST_IDTR_LIMIT			0x4812ULL
#define VM_GUEST_ES_ACC_RIGHT			0x4814ULL
#define VM_GUEST_CS_ACC_RIGHT			0x4816ULL
#define VM_GUEST_SS_ACC_RIGHT			0x4818ULL
#define VM_GUEST_DS_ACC_RIGHT			0x481AULL
#define VM_GUEST_FS_ACC_RIGHT			0x481CULL
#define VM_GUEST_GS_ACC_RIGHT			0x481EULL
#define VM_GUEST_LDTR_ACC_RIGHT			0x4820ULL
#define VM_GUEST_TR_ACC_RIGHT			0x4822ULL
#define VM_GUEST_INT_STATE			0x4824ULL
#define VM_GUEST_ACTIVITY_STATE			0x4826ULL
#define VM_GUEST_SMBASE				0x4828ULL
#define VM_GUEST_PENDING_DBG_EXCEPTS		0x6822ULL
#define VM_GUEST_DEBUGCTL			0x2802ULL
#define VM_GUEST_PAT				0x2804ULL
#define VM_GUEST_EFER				0x2806ULL
#define VM_GUEST_CR0				0x6800ULL
#define VM_GUEST_CR3				0x6802ULL
#define VM_GUEST_CR4				0x6804ULL
#define VM_GUEST_ES_BASE			0x6806ULL
#define VM_GUEST_CS_BASE			0x6808ULL
#define VM_GUEST_SS_BASE			0x680AULL
#define VM_GUEST_DS_BASE			0x680CULL
#define VM_GUEST_FS_BASE			0x680EULL
#define VM_GUEST_GS_BASE			0x6810ULL
#define VM_GUEST_LDTR_BASE			0x6812ULL
#define VM_GUEST_TR_BASE			0x6814ULL
#define VM_GUEST_GDTR_BASE			0x6816ULL
#define VM_GUEST_IDTR_BASE			0x6818ULL
#define VM_GUEST_DR7				0x681AULL
#define VM_GUEST_RSP				0x681CULL
#define VM_GUEST_RIP				0x681EULL
#define VM_GUEST_RFLAGS				0x6820ULL
#define VM_GUEST_IA32_SYSENTER_CS		0x482AULL
#define VM_GUEST_IA32_SYSENTER_ESP		0x6824ULL
#define VM_GUEST_IA32_SYSENTER_EIP		0x6826ULL

#define HB_MASK_GDT_ACCESS			0x03ULL
#define HB_GUEST_RIP_RSP_PLACEHOLDER		0xffffffffffffffffULL

/* 64-bit LDT/TSS descriptor (hyper_box LDTTSS_DESC64) */
struct hb_ldttss_desc64 {
	u16 limit0;
	u16 base0;
	u8 base1;
	u8 limit1;
	u8 limit2;
	u8 base2;
	u32 base3;
	u32 reserved;
};

struct hb_percpu_vmx {
	void *vmxon_region;
	void *vmcs_region;
	bool vmx_on;
	u64 saved_cr0;
	u64 saved_cr4;
};

#if HB_PROBE_GUEST_STATE_EXAMPLE
/* hb_setup_vm_guest_register() 와 같은 정보를 프로브용으로 축약 */
struct hb_probe_guest_state {
	u64 cr0, cr3, cr4, dr7, rsp, rip, rflags;
	u64 cs_sel, ss_sel, ds_sel, es_sel, fs_sel, gs_sel, ldtr_sel, tr_sel;
	u64 cs_base, ss_base, ds_base, es_base, fs_base, gs_base, ldtr_base, tr_base;
	u32 cs_lim, ss_lim, ds_lim, es_lim, fs_lim, gs_lim, ldtr_lim, tr_lim;
	u32 cs_ar, ss_ar, ds_ar, es_ar, fs_ar, gs_ar, ldtr_ar, tr_ar;
	u64 gdtr_base, idtr_base;
	u32 gdtr_lim, idtr_lim;
	u64 sysenter_cs, sysenter_esp, sysenter_eip;
	u64 debugctl, pat, efer;
	u64 vmcs_link_ptr;
};
#endif

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
#if HB_PROBE_GUEST_STATE_EXAMPLE
static int hb_probe_vmwrite_field(u64 field, u64 value, const char *name,
	unsigned int cpu);
static u64 hb_probe_gdt_desc_base(u64 selector);
static u32 hb_probe_gdt_desc_access(u64 selector);
static void hb_probe_ldtr_tr_fields(u64 selector, u64 *base, u32 *limit, u32 *access);
static void hb_probe_capture_guest_state(struct hb_probe_guest_state *gst);
static int hb_probe_vmwrite_guest_state(unsigned int cpu,
	const struct hb_probe_guest_state *gst);
#endif
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
#if HB_PROBE_GUEST_STATE_EXAMPLE
		"(+ guest state example) "
#endif
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

#if HB_PROBE_GUEST_STATE_EXAMPLE

static int hb_probe_vmwrite_field(u64 field, u64 value, const char *name,
	unsigned int cpu)
{
	int ret = hb_write_vmcs(field, value);

	if (ret)
		pr_err(PRLOG "cpu %u: VMWRITE %s failed (%d)\n", cpu, name, ret);
	return ret;
}

static u64 hb_probe_gdt_desc_base(u64 selector)
{
	struct desc_ptr gdtr;
	struct desc_struct *gdt;

	if (!selector)
		return 0;

	native_store_gdt(&gdtr);
	gdt = (struct desc_struct *)(gdtr.address + (selector & ~HB_MASK_GDT_ACCESS));
	return gdt->base0 | ((u64)gdt->base1 << 16) | ((u64)gdt->base2 << 24);
}

static u32 hb_probe_gdt_desc_access(u64 selector)
{
	struct desc_ptr gdtr;
	struct desc_struct *gdt;
	u32 access;

	if (!selector)
		return 0x10000;

	native_store_gdt(&gdtr);
	gdt = (struct desc_struct *)(gdtr.address + (selector & ~HB_MASK_GDT_ACCESS));
	access = (*((u32 *)gdt + 1)) >> 8;
	return access & 0xF0FF;
}

/*
 * LDTR/TR selector에 대해 GDT의 64-bit LDT/TSS 디스크립터를 파싱한다.
 * VMCS guest의 VM_GUEST_{LDTR,TR}_{BASE,LIMIT,ACC_RIGHT} 에 넣을 값을 채운다.
 * CS/SS/DS/ES 는 일반 desc_struct 로 base/AR 만 읽고 limit 는 0xFFFFFFFF 고정이지만,
 * LDTR·TR 은 limit·base3 가 의미 있어 hb_ldttss_desc64 형식으로 따로 처리한다.
 * selector: hb_get_ldtr() / hb_get_tr() 등 (RPL·TI 포함). 0 이면 미사용 세그먼트.
 */
static void hb_probe_ldtr_tr_fields(u64 selector, u64 *base, u32 *limit, u32 *access)
{
	struct desc_ptr gdtr;
	struct hb_ldttss_desc64 *ent;
	u32 acc;

	if (!selector) {
		/* VMCS: invalid segment — base/limit 0, access 0x10000 (Hyper-box 동일) */
		*base = 0;
		*limit = 0;
		*access = 0x10000;
		return;
	}

	native_store_gdt(&gdtr);
	/* 하위 2비트(RPL, TI) 제거 → GDT 바이트 오프셋 */
	ent = (struct hb_ldttss_desc64 *)(gdtr.address + (selector & ~HB_MASK_GDT_ACCESS));
	*base = ent->base0 | ((u64)ent->base1 << 16) | ((u64)ent->base2 << 24) |
		((u64)ent->base3 << 32);
	*limit = ent->limit0 | ((u32)ent->limit1 << 16);
	/* 디스크립터 상위 dword 에서 VMCS Guest Segment Access Rights 형식 추출 */
	acc = (*((u32 *)ent + 1)) >> 8;
	*access = acc & 0xF0FF;
}

/*
 * 현재 CPU 상태를 guest VMCS에 넣을 값으로 수집 (Hyper-box hb_setup_vm_guest_register).
 * RIP/RSP는 launch 전 placeholder — VMLAUNCH 예시는 hb_vm_launch 참고.
 */
static void hb_probe_capture_guest_state(struct hb_probe_guest_state *gst)
{
	struct desc_ptr gdtr, idtr;

	memset(gst, 0, sizeof(*gst));

	gst->cr0 = hb_get_cr0();
	gst->cr3 = hb_get_cr3();
	gst->cr4 = hb_get_cr4();
	gst->dr7 = hb_get_dr7();
	gst->rflags = hb_get_rflags();
	gst->rsp = HB_GUEST_RIP_RSP_PLACEHOLDER;
	gst->rip = HB_GUEST_RIP_RSP_PLACEHOLDER;

	gst->cs_sel = hb_get_cs();
	gst->ss_sel = hb_get_ss();
	gst->ds_sel = hb_get_ds();
	gst->es_sel = hb_get_es();
	gst->fs_sel = hb_get_fs();
	gst->gs_sel = hb_get_gs();
	gst->ldtr_sel = hb_get_ldtr();
	gst->tr_sel = hb_get_tr();

	gst->cs_base = hb_probe_gdt_desc_base(gst->cs_sel);
	gst->ss_base = hb_probe_gdt_desc_base(gst->ss_sel);
	gst->ds_base = hb_probe_gdt_desc_base(gst->ds_sel);
	gst->es_base = hb_probe_gdt_desc_base(gst->es_sel);
	gst->fs_base = hb_rdmsr(MSR_FS_BASE);
	gst->gs_base = hb_rdmsr(MSR_GS_BASE);

	/* LDTR: GDT 에서 base/limit/AR 전부 수집 */
	hb_probe_ldtr_tr_fields(gst->ldtr_sel, &gst->ldtr_base, &gst->ldtr_lim, &gst->ldtr_ar);
	/* TR 미사용 시 Hyper-box 와 같이 AR=0 (LDTR invalid 의 0x10000 과 다름) */
	if (!gst->tr_sel) {
		gst->tr_base = 0;
		gst->tr_lim = 0;
		gst->tr_ar = 0;
	} else {
		hb_probe_ldtr_tr_fields(gst->tr_sel, &gst->tr_base, &gst->tr_lim, &gst->tr_ar);
	}

	gst->cs_lim = gst->ss_lim = gst->ds_lim = gst->es_lim = 0xFFFFFFFF;
	gst->fs_lim = gst->gs_lim = 0xFFFFFFFF;

	gst->cs_ar = hb_probe_gdt_desc_access(gst->cs_sel);
	gst->ss_ar = hb_probe_gdt_desc_access(gst->ss_sel);
	gst->ds_ar = hb_probe_gdt_desc_access(gst->ds_sel);
	gst->es_ar = hb_probe_gdt_desc_access(gst->es_sel);
	gst->fs_ar = hb_probe_gdt_desc_access(gst->fs_sel);
	gst->gs_ar = hb_probe_gdt_desc_access(gst->gs_sel);

	native_store_gdt(&gdtr);
	store_idt(&idtr);
	gst->gdtr_base = gdtr.address;
	gst->gdtr_lim = gdtr.size;
	gst->idtr_base = idtr.address;
	gst->idtr_lim = idtr.size;

	gst->sysenter_cs = hb_rdmsr(MSR_IA32_SYSENTER_CS);
	gst->sysenter_esp = hb_rdmsr(MSR_IA32_SYSENTER_ESP);
	gst->sysenter_eip = hb_rdmsr(MSR_IA32_SYSENTER_EIP);
	gst->debugctl = 0;
	gst->pat = hb_rdmsr(MSR_IA32_CR_PAT);
	gst->efer = hb_rdmsr(MSR_EFER);
	gst->vmcs_link_ptr = 0xffffffffffffffffULL;
}

static int hb_probe_vmwrite_guest_state(unsigned int cpu,
	const struct hb_probe_guest_state *g)
{
	int ret;

#define VW(field, val, label)					\
	do {							\
		ret = hb_probe_vmwrite_field((field), (val), (label), cpu); \
		if (ret)					\
			return ret;				\
	} while (0)

	VW(VM_GUEST_CR0, g->cr0, "GUEST_CR0");
	VW(VM_GUEST_CR3, g->cr3, "GUEST_CR3");
	VW(VM_GUEST_CR4, g->cr4, "GUEST_CR4");
	VW(VM_GUEST_DR7, g->dr7, "GUEST_DR7");
	VW(VM_GUEST_RSP, g->rsp, "GUEST_RSP");
	VW(VM_GUEST_RIP, g->rip, "GUEST_RIP");
	VW(VM_GUEST_RFLAGS, g->rflags, "GUEST_RFLAGS");

	VW(VM_GUEST_CS_SELECTOR, g->cs_sel, "GUEST_CS");
	VW(VM_GUEST_SS_SELECTOR, g->ss_sel, "GUEST_SS");
	VW(VM_GUEST_DS_SELECTOR, g->ds_sel, "GUEST_DS");
	VW(VM_GUEST_ES_SELECTOR, g->es_sel, "GUEST_ES");
	VW(VM_GUEST_FS_SELECTOR, g->fs_sel, "GUEST_FS");
	VW(VM_GUEST_GS_SELECTOR, g->gs_sel, "GUEST_GS");
	VW(VM_GUEST_LDTR_SELECTOR, g->ldtr_sel, "GUEST_LDTR");
	VW(VM_GUEST_TR_SELECTOR, g->tr_sel, "GUEST_TR");

	VW(VM_GUEST_CS_BASE, g->cs_base, "GUEST_CS_BASE");
	VW(VM_GUEST_SS_BASE, g->ss_base, "GUEST_SS_BASE");
	VW(VM_GUEST_DS_BASE, g->ds_base, "GUEST_DS_BASE");
	VW(VM_GUEST_ES_BASE, g->es_base, "GUEST_ES_BASE");
	VW(VM_GUEST_FS_BASE, g->fs_base, "GUEST_FS_BASE");
	VW(VM_GUEST_GS_BASE, g->gs_base, "GUEST_GS_BASE");
	VW(VM_GUEST_LDTR_BASE, g->ldtr_base, "GUEST_LDTR_BASE");
	VW(VM_GUEST_TR_BASE, g->tr_base, "GUEST_TR_BASE");

	VW(VM_GUEST_CS_LIMIT, g->cs_lim, "GUEST_CS_LIMIT");
	VW(VM_GUEST_SS_LIMIT, g->ss_lim, "GUEST_SS_LIMIT");
	VW(VM_GUEST_DS_LIMIT, g->ds_lim, "GUEST_DS_LIMIT");
	VW(VM_GUEST_ES_LIMIT, g->es_lim, "GUEST_ES_LIMIT");
	VW(VM_GUEST_FS_LIMIT, g->fs_lim, "GUEST_FS_LIMIT");
	VW(VM_GUEST_GS_LIMIT, g->gs_lim, "GUEST_GS_LIMIT");
	VW(VM_GUEST_LDTR_LIMIT, g->ldtr_lim, "GUEST_LDTR_LIMIT");
	VW(VM_GUEST_TR_LIMIT, g->tr_lim, "GUEST_TR_LIMIT");

	VW(VM_GUEST_CS_ACC_RIGHT, g->cs_ar, "GUEST_CS_AR");
	VW(VM_GUEST_SS_ACC_RIGHT, g->ss_ar, "GUEST_SS_AR");
	VW(VM_GUEST_DS_ACC_RIGHT, g->ds_ar, "GUEST_DS_AR");
	VW(VM_GUEST_ES_ACC_RIGHT, g->es_ar, "GUEST_ES_AR");
	VW(VM_GUEST_FS_ACC_RIGHT, g->fs_ar, "GUEST_FS_AR");
	VW(VM_GUEST_GS_ACC_RIGHT, g->gs_ar, "GUEST_GS_AR");
	VW(VM_GUEST_LDTR_ACC_RIGHT, g->ldtr_ar, "GUEST_LDTR_AR");
	VW(VM_GUEST_TR_ACC_RIGHT, g->tr_ar, "GUEST_TR_AR");

	VW(VM_GUEST_GDTR_BASE, g->gdtr_base, "GUEST_GDTR_BASE");
	VW(VM_GUEST_IDTR_BASE, g->idtr_base, "GUEST_IDTR_BASE");
	VW(VM_GUEST_GDTR_LIMIT, g->gdtr_lim, "GUEST_GDTR_LIMIT");
	VW(VM_GUEST_IDTR_LIMIT, g->idtr_lim, "GUEST_IDTR_LIMIT");

	VW(VM_GUEST_DEBUGCTL, g->debugctl, "GUEST_DEBUGCTL");
	VW(VM_GUEST_IA32_SYSENTER_CS, g->sysenter_cs, "GUEST_SYSENTER_CS");
	VW(VM_GUEST_IA32_SYSENTER_ESP, g->sysenter_esp, "GUEST_SYSENTER_ESP");
	VW(VM_GUEST_IA32_SYSENTER_EIP, g->sysenter_eip, "GUEST_SYSENTER_EIP");
	VW(VM_GUEST_PAT, g->pat, "GUEST_PAT");
	VW(VM_GUEST_EFER, g->efer, "GUEST_EFER");

	VW(VM_VMCS_LINK_PTR, g->vmcs_link_ptr, "VMCS_LINK_PTR");
	VW(VM_GUEST_INT_STATE, 0, "GUEST_INT_STATE");
	VW(VM_GUEST_ACTIVITY_STATE, 0, "GUEST_ACTIVITY_STATE");
	VW(VM_GUEST_SMBASE, 0, "GUEST_SMBASE");
	VW(VM_GUEST_PENDING_DBG_EXCEPTS, 0, "GUEST_PENDING_DBG");

#undef VW

	return 0;
}

#endif /* HB_PROBE_GUEST_STATE_EXAMPLE */

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

#if HB_PROBE_GUEST_STATE_EXAMPLE
	{
		struct hb_probe_guest_state gst;
		u64 cr3_read = 0;

		hb_probe_capture_guest_state(&gst);
		ret = hb_probe_vmwrite_guest_state(cpu, &gst);
		if (ret)
			return ret;

		ret = hb_read_vmcs(VM_GUEST_CR3, &cr3_read);
		if (ret) {
			pr_err(PRLOG "cpu %u: VMREAD GUEST_CR3 failed (%d)\n", cpu, ret);
			return ret;
		}
		if (cr3_read != gst.cr3) {
			pr_err(PRLOG "cpu %u: GUEST_CR3 mismatch (wrote %llx, read %llx)\n",
				cpu, gst.cr3, cr3_read);
			return -EIO;
		}
		pr_info(PRLOG "cpu %u: guest state VMWRITE ok (CR3=0x%llx, link=none)\n",
			cpu, cr3_read);
	}
#endif

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

	// VMCS WRITE
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
MODULE_DESCRIPTION("Hypervisor probe: VMXON/VMCLEAR/VMPTRLD/VMWRITE"
#if HB_PROBE_GUEST_STATE_EXAMPLE
	" + guest state example"
#endif
	"; VMXOFF on unload");

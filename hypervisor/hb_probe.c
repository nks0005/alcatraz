#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
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

static bool is_vmx_supported(void);
static bool is_bios_vmx_allowed(void);

static int __init hypervisor_init(void)
{
	// CPU 하드웨어가 VMX를 가지고 있는가?
	if (!is_vmx_supported()) {
		pr_info(PRLOG "VMX not supported (CPUID)\n");
		return -ENODEV;
	}
	pr_info(PRLOG "VMX supported (CPUID)\n");

	// BIOS/펌웨어가 VMX 사용을 허용했는가?
	if (!is_bios_vmx_allowed()) {
		pr_info(PRLOG "VMX disabled by BIOS (IA32_FEATURE_CONTROL)\n");
		return -ENODEV;
	}	

	pr_info(PRLOG "VMX supported (BIOS)\n");



	
	return 0;
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

static void __exit hypervisor_exit(void)
{
	pr_info(PRLOG "unloaded\n");
}

module_init(hypervisor_init);
module_exit(hypervisor_exit);

MODULE_AUTHOR("nks004@naver.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal hypervisor probe (VMX CPUID + FEATURE_CONTROL)");

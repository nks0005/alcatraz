#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/processor.h>

#define CPUID_1_ECX_VMX	((u32)0x01 << 5)

static int __init hypervisor_init(void)
{
	u32 eax, ebx, ecx, edx;
	int vmx_supported;

	cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
	vmx_supported = !!(ecx & CPUID_1_ECX_VMX);
	pr_info("hypervisor_b: VMX support: %d (cpuid.1.ecx=0x%08x)\n",
		vmx_supported, ecx);

	return 0;
}

static void __exit hypervisor_exit(void)
{
	pr_info("hypervisor_b: unloaded\n");
}

module_init(hypervisor_init);
module_exit(hypervisor_exit);

MODULE_AUTHOR("nks004@naver.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal hypervisor probe (VMX CPUID check)");

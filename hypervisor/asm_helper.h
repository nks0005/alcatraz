#ifndef ASM_HELPER_H
#define ASM_HELPER_H

#include <linux/types.h>

u64 hb_rdmsr(u32 msr_index);

u64 hb_get_cr0(void);
void hb_set_cr0(u64 cr0);
u64 hb_get_cr4(void);
void hb_set_cr4(u64 cr4);
void hb_enable_vmx(void);
void hb_disable_vmx(void);
int hb_start_vmx(void *vmxon_region_pa);
void hb_stop_vmx(void);
int hb_clear_vmcs(void *vmcs_region_pa);
int hb_load_vmcs(void *vmcs_region_pa);

#endif /* ASM_HELPER_H */

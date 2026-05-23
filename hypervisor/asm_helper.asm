;
; Minimal asm helper for hypervisor_b (VMXON / VMCS from Hyper-Box)
;

[bits 64]

global hb_rdmsr
global hb_get_cr0
global hb_set_cr0
global hb_get_cr4
global hb_set_cr4
global hb_enable_vmx
global hb_disable_vmx
global hb_start_vmx
global hb_stop_vmx
global hb_clear_vmcs
global hb_load_vmcs

; u64 hb_rdmsr(u32 msr_index)
hb_rdmsr:
	push rdx
	push rcx

	xor rdx, rdx
	xor rax, rax

	mov ecx, edi
	rdmsr

	shl rdx, 32
	or rax, rdx

	pop rcx
	pop rdx
	ret

; u64 hb_get_cr0(void)
hb_get_cr0:
	mov rax, cr0
	ret

; void hb_set_cr0(u64 cr0)
hb_set_cr0:
	mov cr0, rdi
	ret

; u64 hb_get_cr4(void)
hb_get_cr4:
	mov rax, cr4
	ret

; void hb_set_cr4(u64 cr4)
hb_set_cr4:
	mov cr4, rdi
	ret

; void hb_enable_vmx(void)
hb_enable_vmx:
	push rax
	mov rax, cr4
	bts rax, 13
	mov cr4, rax
	pop rax
	ret

; void hb_disable_vmx(void)
hb_disable_vmx:
	push rax
	mov rax, cr4
	btc rax, 13
	mov cr4, rax
	pop rax
	ret

; int hb_start_vmx(void *vmxon_region_pa)  -- RDI = &physical_address
hb_start_vmx:
	vmxon [rdi]
	jc .error
	jz .error
	xor rax, rax
	ret
.error:
	mov rax, -1
	ret

; void hb_stop_vmx(void)
hb_stop_vmx:
	vmxoff
	ret

; int hb_clear_vmcs(void *vmcs_region_pa)
hb_clear_vmcs:
	vmclear [rdi]
	jc .error
	jz .error
	xor rax, rax
	ret
.error:
	mov rax, -1
	ret

; int hb_load_vmcs(void *vmcs_region_pa)
hb_load_vmcs:
	vmptrld [rdi]
	jc .error
	jz .error
	xor rax, rax
	ret
.error:
	mov rax, -1
	ret

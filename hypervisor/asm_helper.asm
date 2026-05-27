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
global hb_write_vmcs
global hb_read_vmcs
global hb_get_cr3
global hb_get_cs
global hb_get_ss
global hb_get_ds
global hb_get_es
global hb_get_fs
global hb_get_gs
global hb_get_ldtr
global hb_get_tr
global hb_get_dr7
global hb_get_rflags

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
	bts rax, 13	; CR4.VMXE(비트 13): VMX 명령 허용 (VMXON 전 필수)
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

; int hb_start_vmx(void *vmxon_region_pa)
; RDI = VMXON 영역 물리주소(PA)를 담은 변수의 주소
hb_start_vmx:
	vmxon [rdi]		; VMX root 모드 진입 (VMXON)
	jc .error		; CF=1: VMXON 실패
	jz .error		; ZF=1: VMXON 실패 (무효 동작)
	xor rax, rax		; 성공 시 0 반환
	ret
.error:
	mov rax, -1		; 실패 시 -1 반환
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

; int hb_write_vmcs(u64 field, u64 value)
hb_write_vmcs:
	vmwrite rdi, rsi
	jc .error_w
	jz .error_w
	xor rax, rax
	ret
.error_w:
	mov rax, -1
	ret

; int hb_read_vmcs(u64 field, u64 *value)
hb_read_vmcs:
	vmread [rsi], rdi
	jc .error_r
	jz .error_r
	xor rax, rax
	ret
.error_r:
	mov rax, -1
	ret

; u64 hb_get_cr3(void)
hb_get_cr3:
	mov rax, cr3
	ret

; u64 hb_get_cs/ss/ds/es/fs/gs(void)
hb_get_cs:
	mov rax, cs
	ret

hb_get_ss:
	mov rax, ss
	ret

hb_get_ds:
	mov rax, ds
	ret

hb_get_es:
	mov rax, es
	ret

hb_get_fs:
	mov rax, fs
	ret

hb_get_gs:
	mov rax, gs
	ret

; u64 hb_get_ldtr(void) / hb_get_tr(void)
hb_get_ldtr:
	sldt rax
	ret

hb_get_tr:
	str rax
	ret

; u64 hb_get_dr7(void)
hb_get_dr7:
	mov rax, dr7
	ret

; u64 hb_get_rflags(void)
hb_get_rflags:
	pushfq
	pop rax
	ret

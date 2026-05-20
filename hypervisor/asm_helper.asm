;
; Minimal asm helper for hypervisor_b (from Hyper-Box asm_helper.asm)
;

[bits 64]

global hb_rdmsr

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

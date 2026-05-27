# VMCS revision (VMWRITE 아님)

VMCS 4KB 영역의 **첫 31비트**는 Intel이 정한 **VMCS revision identifier**다.  
`VMWRITE`가 아니라 **`VMCLEAR` 전에 메모리에 직접** 쓴다.

---

## 값

```c
u64 vmx_basic = rdmsr(IA32_VMX_BASIC);
u32 revision  = (u32)(vmx_basic & 0x7fffffff);
*(u32 *)vmcs_region = revision;
```

hb_probe: `hb_vmx_revision_id()` / `g_vmx_revision`  
Hyper-box: `hb_init_vmx()`에서 `guest_VMCS_log_addr[0] = (u32)vmx_msr`

---

## VMXON 영역 vs VMCS 영역

| 영역 | revision 기록 |
|------|----------------|
| **VMXON** | `VMPTRLD` 전에 VMXON region에도 동일 revision (hb_probe: `*(u32 *)vmxon_region`) |
| **VMCS** | `VMCLEAR` **직전** guest VMCS에 revision |

---

## Shadow VMCS (단계 2 / Alcatraz)

섀도 VMCS를 쓸 때는 revision **bit 31 = 1** (`VM_BIT_REVISION_SHADOW_VMCS`).  
일반 VMCS·링크 없음(`0xFF…`) 프로브에서는 **bit 31 = 0**만 사용.

→ [`guest.md`](guest.md#vmcs-link-pointer)

---

## 관련

- [`../vmxon_clear_load.md`](../vmxon_clear_load.md)
- [`setting.md`](setting.md)

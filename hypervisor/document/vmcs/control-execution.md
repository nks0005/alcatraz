# VM-execution control fields (VMWRITE)

게스트가 **VMX non-root**에서 실행되는 동안의 규칙: 어떤 이벤트에서 **VM exit** 할지, bitmap·EPT·CR 가상화 등.

`hb_setup_vm_control_register()`에서 **비트·주소를 준비** → `hb_setup_vmcs()`에서 `VMWRITE`.

상수: [`hyper_box.h`](../../../Source/hyper_box/hyper_box.h) `VM_CTRL_*`  
준비 코드: [`hypervisor.c`](../../../Source/hyper_box/hypervisor.c) `hb_setup_vm_control_register()`

---

## 핵심 control 워드 (3개 + pin)

| 필드 | MSR 기준 (TRUE_* 권장) | 역할 |
|------|------------------------|------|
| `VM_CTRL_PIN_BASED_VM_EXE_CTRL` | `IA32_VMX_TRUE_PINBASED_CTLS` | 외부 인터럽트, preemption timer 등 |
| `VM_CTRL_PRI_PROC_BASED_EXE_CTRL` | `IA32_VMX_TRUE_PROCBASED_CTLS` | IO/MSR bitmap, secondary, CR exit, MOV DR |
| `VM_CTRL_SEC_PROC_BASED_EXE_CTRL` | `IA32_VMX_PROCBASED_CTLS2` | EPT, VPID, INVPCID, **VMCS shadowing** 등 |

값 계산 패턴 (Hyper-box):

```text
allowed = (rdmsr(TRUE_MSR) | wanted_bits) & 0xFFFFFFFF
         또는 (rdmsr(CTL2) | sec_flags) & 0xFFFFFFFF
```

**비트를 켜면** 아래 주소·보조 필드도 반드시 VMWRITE.

---

## 단계 1 (최소 launch) — 권장 비트

| 항목 | 권장 |
|------|------|
| Pin-based | 기본(대부분 0), preemption timer **off** |
| Primary | **IO bitmap off**, **MSR bitmap off**, secondary **off** 또는 on만 하고 sec에서 EPT off |
| Secondary | EPT **off**, VMCS shadowing **off**, VPID **off** |

→ IO/MSR/EPT/VMREAD/VMWRITE bitmap **주소 필드 생략 가능**.

---

## 단계 2 (Hyper-box) — 추가 비트·주소

### Bitmap / 테이블 주소 (물리 주소로 VMWRITE)

| 필드 | 크기 | 조건 |
|------|------|------|
| `VM_CTRL_IO_BITMAP_A_ADDR` | 4KB | primary: use I/O bitmaps |
| `VM_CTRL_IO_BITMAP_B_ADDR` | 4KB | 동일 |
| `VM_CTRL_MSR_BITMAPS` | 4KB | primary: use MSR bitmaps |
| `VM_CTRL_VMREAD_BITMAP_ADDR` | 4KB | nested: guest VMREAD 가로채기 |
| `VM_CTRL_VMWRITE_BITMAP_ADDR` | 4KB | nested: guest VMWRITE 가로채기 |
| `VM_CTRL_VIRTUAL_APIC_ADDR` | 4KB | virtualize APIC 접근 시 |

할당: `g_io_bitmap_*`, `g_msr_bitmap_*`, `g_vmread/vmwrite_bitmap_*` — 모듈 init에서 `__get_free_page`, 사용 전 `memset(0)`.

### EPT / VPID

| 필드 | 조건 |
|------|------|
| `VM_CTRL_EPT_PTR` | secondary: enable EPT |
| `VM_CTRL_VIRTUAL_PROCESS_ID` | secondary: enable VPID (KVM 충돌 시 `0xFFFF` 등) |

### 예외·CR 가상화

| 필드 | Hyper-box 예 |
|------|----------------|
| `VM_CTRL_EXCEPTION_BITMAP` | `#DB` 등 (HW breakpoint 옵션) |
| `VM_CTRL_CR0_GUEST_HOST_MASK` | 예: `CR0.WP` |
| `VM_CTRL_CR0_READ_SHADOW` | mask와 짝 |
| `VM_CTRL_CR4_GUEST_HOST_MASK` | `VMXE`, `SMEP`, `MCE` 등 |
| `VM_CTRL_CR4_READ_SHADOW` | mask와 짝 |

### 기타 execution (hyper_box는 0)

| 필드 | 비고 |
|------|------|
| `VM_CTRL_CR3_TARGET_VALUE_0`~`3` | CR3 target 기능 시 |
| `VM_CTRL_CR3_TARGET_COUNT` | |
| `VM_CTRL_PAGE_FAULT_ERR_CODE_MASK` / `MATCH` | |
| `VM_CTRL_TSC_OFFSET` | |
| `VM_CTRL_TPR_THRESHOLD` | APIC 가상화 시 |
| `VM_CTRL_EXECUTIVE_VMCS_PTR` | ENCLS 등 특수 |

---

## VMREAD/VMWRITE bitmap (Alcatraz)

게스트가 특정 **VMCS 필드 encoding**에 VMREAD/VMWRITE 하면 exit.

```c
byte_offset = field_number / 8;
bit_offset  = field_number % 8;
vmwrite_bitmap[byte_offset] |= (1 << bit_offset);
```

Hyper-box 예: `VM_HOST_RIP`, `VM_HOST_RSP`, `VM_HOST_CR3` — [`../vmwrite_vmcs.md`](../vmwrite_vmcs.md)

---

## hb_probe 체크

| 단계 | execution control |
|------|-------------------|
| 0 | 없음 |
| 1 | pin/primary/secondary **최소** + exception bitmap 0 + mask/shadow 0 또는 최소 |
| 2 | hyper_box `hb_setup_vm_control_register` 전부 |

---

## 관련

- [`control-exit.md`](control-exit.md)
- [`control-entry.md`](control-entry.md)
- [`setting.md`](setting.md)

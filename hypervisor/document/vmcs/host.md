# Host state area (VMWRITE)

**VM exit** 시 CPU가 복원할 **호스트(VMM) CPU 상태**.  
`VMLAUNCH` 전에 `VMWRITE`로 설정한다.

참고 구현: `hb_setup_vmcs()` host 블록 — [`hypervisor.c`](../../../Source/hyper_box/hypervisor.c)  
필드 상수: [`hyper_box.h`](../../../Source/hyper_box/hyper_box.h) `VM_HOST_*`

---

## 역할

| 항목 | 설명 |
|------|------|
| **VM_HOST_RIP** | VM exit 후 점프할 **핸들러** (예: `hb_vm_exit_callback_stub`) |
| **VM_HOST_RSP** | exit handler용 **스택 top** (per-CPU 스택 권장) |
| **VM_HOST_CR3** | 호스트 페이지 테이블 (시스템 CR3 / 전용 PML4) |
| 세그먼트·테이블 | 호스트가 실제로 쓰는 GDT/IDT/TSS와 일치 |

게스트가 VMX non-root에서 돌 때, **문제가 생기면 여기 RIP로 복귀**한다.

---

## 필드 목록 (Hyper-box 기준)

### 제어·스택·코드

| VMWRITE 필드 | encoding (예) | 값 출처 | 단계 1 |
|--------------|---------------|---------|--------|
| `VM_HOST_CR0` | `0x6C00` | 현재 CR0 (+ VMX fixed bit 반영) | 필수 |
| `VM_HOST_CR3` | `0x6C02` | 호스트 PML4 (물리/논리 정책에 따름) | 필수 |
| `VM_HOST_CR4` | `0x6C04` | CR4.VMXE 등 | 필수 |
| `VM_HOST_RSP` | `0x6C14` | `g_vm_exit_stack_addr[cpu] + size - 4K` 등 | 필수 |
| `VM_HOST_RIP` | `0x6C16` | exit stub 주소 | 필수 |

### 세그먼트 selector (RPL=0, TI=0 형태로 정리)

| 필드 | 단계 1 |
|------|--------|
| `VM_HOST_CS_SELECTOR` | 필수 |
| `VM_HOST_SS_SELECTOR` | 필수 |
| `VM_HOST_DS_SELECTOR` | 권장 |
| `VM_HOST_ES_SELECTOR` | 권장 |
| `VM_HOST_FS_SELECTOR` | 권장 |
| `VM_HOST_GS_SELECTOR` | 권장 |
| `VM_HOST_TR_SELECTOR` | 필수 |

### 베이스·테이블

| 필드 | 단계 1 |
|------|--------|
| `VM_HOST_FS_BASE` | 권장 (MSR `IA32_FS_BASE`) |
| `VM_HOST_GS_BASE` | 권장 |
| `VM_HOST_TR_BASE` | 필수 (GDT에서 TSS) |
| `VM_HOST_GDTR_BASE` | 필수 |
| `VM_HOST_IDTR_BASE` | 필수 |

### MSRs (exit/load control과 맞출 것)

| 필드 | 단계 1 |
|------|--------|
| `VM_HOST_IA32_SYSENTER_CS` | 권장 |
| `VM_HOST_IA32_SYSENTER_ESP` | 권장 |
| `VM_HOST_IA32_SYSENTER_EIP` | 권장 |
| `VM_HOST_PAT` | 권장 |
| `VM_HOST_EFER` | 64비트 호스트 시 권장 |
| `VM_HOST_PERF_GLOBAL_CTRL` | exit control에서 load 시 |

---

## 값 준비 (코드 패턴)

1. **`hb_setup_vm_host_register()`** — 현재 CPU·GDT/IDT·스택에서 구조체 채움  
2. **`hb_setup_vmcs()`** — `hb_write_vmcs(VM_HOST_*, struct->field)`

hb_probe 확장 시: host 구조체를 최소 필드만 채우거나, hyper_box helper를 단계적으로 가져온다.

---

## Nested / Alcatraz 참고

KVM nested가 **guest VMCS**에 `VM_HOST_RIP/RSP/CR3`를 쓰려 하면 VM exit → L0가 **nested VMCS에 대리 기록**하고, 실제 host 필드는 stub/호스트 CR3로 **치환**한다.

→ [`../vmwrite_vmcs.md`](../vmwrite_vmcs.md) 게스트 VMWRITE 절

---

## Launch 직전 보정

`hb_setup_vmcs`에서 이미 `VM_HOST_RSP/RIP`를 썼어도, 스택 정렬·stub 주소는 **`hb_vm_launch` 전** 다시 확인하는 경우가 있다.

→ [`launch-adjust.md`](launch-adjust.md)

---

## 관련

- [`guest.md`](guest.md)
- [`control-exit.md`](control-exit.md) — host 주소 크기·MSR load 비트
- [`setting.md`](setting.md)

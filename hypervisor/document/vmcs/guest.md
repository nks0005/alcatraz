# Guest state area (VMWRITE)

**VM entry** 시 게스트로 넘길 **CPU 스냅샷**.  
`VMLAUNCH` / `VMRESUME` 전에 `VMWRITE`로 설정한다.

참고: `hb_setup_vmcs()` guest 블록 — [`hypervisor.c`](../../../Source/hyper_box/hypervisor.c)  
상수: [`hyper_box.h`](../../../Source/hyper_box/hyper_box.h) `VM_GUEST_*`, `VM_VMCS_LINK_PTR`

구조체: `struct hb_vm_guest_register` — [`hyper_box.h`](../../../Source/hyper_box/hyper_box.h)  
값 수집: `hb_setup_vm_guest_register()` — [`hypervisor.c`](../../../Source/hyper_box/hypervisor.c) L4897~5124

**왜 세그먼트를 GDT/MSR에서 읽어 VMWRITE 하는지:** [`guest-segments-why.md`](guest-segments-why.md)  
**x86 세그먼트 기초 (selector, GDT, 8개 레지):** [`segment.md`](segment.md)

---

## Hyper-box: guest 값 수집 → VMWRITE 매핑

`hb_setup_vm_guest_register()`가 **현재 CPU/GDT**에서 `hb_vm_guest_register`를 채우고,  
`hb_setup_vmcs()`가 같은 필드를 **`hb_write_vmcs(VM_GUEST_*, …)`** 로 VMCS에 쓴다.

### 1) 제어·디버그·플래그

| struct 필드 | 값 출처 (`hb_setup_vm_guest_register`) | VMCS 필드 (`hb_setup_vmcs`) |
|-------------|----------------------------------------|-----------------------------|
| `cr0` | `hb_vm_host_register->cr0` | `VM_GUEST_CR0` |
| `cr3` | `hb_get_cr3()` (현재 CR3) | `VM_GUEST_CR3` |
| `cr4` | `hb_vm_host_register->cr4` | `VM_GUEST_CR4` |
| `dr7` | `HYPERBOX_USE_HW_BREAKPOINT`: 커널 심볼 4개에 HW BP + `hb_encode_dr7` / 아니면 `hb_get_dr7()` | `VM_GUEST_DR7` |
| `rflags` | `hb_get_rflags()` | `VM_GUEST_RFLAGS` |
| `rsp` | **`0xFFFFFFFFFFFFFFFF`** (placeholder) | `VM_GUEST_RSP` → **`hb_vm_launch`에서 재설정** |
| `rip` | **`0xFFFFFFFFFFFFFFFF`** (placeholder) | `VM_GUEST_RIP` → **`hb_vm_launch`에서 재설정** |

### 2) 세그먼트 selector (현재 세그먼트 레지스터)

| struct 필드 | 출처 | VMCS |
|-------------|------|------|
| `cs_selector` … `tr_selector` | `hb_get_cs()` … `hb_get_tr()` | `VM_GUEST_CS_SELECTOR` … `VM_GUEST_TR_SELECTOR` |

### 3) 세그먼트 base

| struct 필드 | 출처 | VMCS |
|-------------|------|------|
| `cs_base_addr` … `ds_base_addr` | `hb_get_desc_base(selector)` | `VM_GUEST_CS_BASE` … `VM_GUEST_DS_BASE` |
| `fs_base_addr` | `rdmsr(MSR_FS_BASE_ADDR)` | `VM_GUEST_FS_BASE` |
| `gs_base_addr` | `rdmsr(MSR_GS_BASE_ADDR)` | `VM_GUEST_GS_BASE` |
| `ldtr_base_addr` | selector==0 → 0, else GDT의 `LDTTSS_DESC` 조합 | `VM_GUEST_LDTR_BASE` |
| `tr_base_addr` | selector==0 → 0, else GDT TSS 디스크립터 | `VM_GUEST_TR_BASE` |

### 4) 세그먼트 limit

| struct 필드 | 출처 | VMCS |
|-------------|------|------|
| `cs_limit` … `gs_limit` | **고정 `0xFFFFFFFF`** | `VM_GUEST_CS_LIMIT` … `VM_GUEST_GS_LIMIT` |
| `ldtr_limit` | selector==0 → 0, else GDT LDT limit | `VM_GUEST_LDTR_LIMIT` |
| `tr_limit` | selector==0 → 0, else GDT TSS limit | `VM_GUEST_TR_LIMIT` |

### 5) 세그먼트 access rights

| struct 필드 | 출처 | VMCS |
|-------------|------|------|
| `cs_access` … `gs_access` | `hb_get_desc_access(selector)` | `VM_GUEST_CS_ACC_RIGHT` … `VM_GUEST_GS_ACC_RIGHT` |
| `ldtr_access` | selector==0 → `0x10000`, else GDT에서 `access & 0xF0FF` | `VM_GUEST_LDTR_ACC_RIGHT` |
| `tr_access` | selector==0 → 0, else GDT TSS `access & 0xF0FF` | `VM_GUEST_TR_ACC_RIGHT` |

### 6) 테이블·MSR·링크

| struct 필드 | 출처 | VMCS |
|-------------|------|------|
| `gdtr_base_addr` | `hb_vm_host_register->gdtr_base_addr` | `VM_GUEST_GDTR_BASE` |
| `idtr_base_addr` | `store_idt(&idtr)` → `idtr.address` | `VM_GUEST_IDTR_BASE` |
| `gdtr_limit` | `native_store_gdt` → `gdtr.size` | `VM_GUEST_GDTR_LIMIT` |
| `idtr_limit` | `idtr.size` | `VM_GUEST_IDTR_LIMIT` |
| `ia32_debug_ctrl` | **0** | `VM_GUEST_DEBUGCTL` |
| `ia32_sys_enter_*` | host와 동일 | `VM_GUEST_IA32_SYSENTER_CS/ESP/EIP` |
| `ia32_perf_global_ctrl` | host와 동일 | `VM_GUEST_PERF_GLOBAL_CTRL` |
| `ia32_pat` | host와 동일 | `VM_GUEST_PAT` |
| `ia32_efer` | host와 동일 | `VM_GUEST_EFER` |
| `vmcs_link_ptr` | **`0xFFFFFFFFFFFFFFFF`** | `VM_VMCS_LINK_PTR` |

### 7) `hb_setup_vm_guest_register`에 없고 `hb_setup_vmcs`에서만 쓰는 필드

| VMCS 필드 | 값 |
|-----------|-----|
| `VM_GUEST_INT_STATE` | 0 |
| `VM_GUEST_ACTIVITY_STATE` | 0 (Active) |
| `VM_GUEST_SMBASE` | 0 |
| `VM_GUEST_PENDING_DBG_EXCEPTS` | 0 |
| `VM_GUEST_VMX_PRE_TIMER_VALUE` | `hb_calc_vm_pre_timer_value()` (preemption timer 옵션 시) |

### 호출 순서

```text
hb_setup_vm_host_register(host)
  → hb_setup_vm_guest_register(guest, host)   /* 스냅샷 */
  → hb_setup_vm_control_register(control, cpu)
  → hb_setup_vmcs(host, guest, control)       /* VMWRITE */
  → hb_vm_launch()                            /* guest RIP/RSP vmwrite + vmlaunch */
```

---

## 역할

| 항목 | 설명 |
|------|------|
| **VM_GUEST_RIP / RSP** | 게스트가 **처음 실행할** 주소·스택 |
| **VM_GUEST_CR0/3/4** | 게스트 페이징·보호 |
| 세그먼트 전체 | selector + base + limit + access rights |
| **VM_VMCS_LINK_PTR** | 섀도 VMCS 연결 (없으면 `0xFF…`) |

---

## VMCS link pointer {#vmcs-link-pointer}

| 항목 | 내용 |
|------|------|
| 필드 | `VM_VMCS_LINK_PTR` (`0x2800`) |
| 섀도 없음 | `0xFFFFFFFFFFFFFFFF` — **hb_probe 현재 프로브** |
| 섀도 사용 (Alcatraz) | nested/shadow VMCS **물리 주소** + 해당 region revision **bit 31 = 1** |

Intel: 링크가 유효하지 않으면 all-ones.  
프로브는 `hb_vmx_probe_vmwrite()`에서 write + `VMREAD` 검증만 수행.

---

## 필드 목록

### GPR·플래그·CR

| 필드 | 단계 1 | 비고 |
|------|--------|------|
| `VM_GUEST_CR0` | 필수 | |
| `VM_GUEST_CR3` | 필수 | |
| `VM_GUEST_CR4` | 필수 | VMXE 등 정책 |
| `VM_GUEST_DR7` | 권장 | hyper_box: `0x400` 등 |
| `VM_GUEST_RSP` | 필수 | launch 직전 재설정 가능 |
| `VM_GUEST_RIP` | 필수 | launch 직전 재설정 가능 |
| `VM_GUEST_RFLAGS` | 필수 | |

### 세그먼트 selector

`VM_GUEST_ES` … `VM_GUEST_TR` selector (`0x800`~`0x80E` 계열)

| 세그먼트 | 단계 1 |
|----------|--------|
| CS, SS, DS, ES, FS, GS | 필수 |
| LDTR, TR | 필수 |

### 세그먼트 base / limit / access

각 세그먼트마다:

- `VM_GUEST_*_BASE` (`0x6806`~)
- `VM_GUEST_*_LIMIT` (`0x4800`~)
- `VM_GUEST_*_ACC_RIGHT` (`0x4814`~)

GDT에서 디스크립터를 읽어 채우는 패턴 (`hb_setup_vm_guest_register`).

### 테이블

| 필드 | 단계 1 |
|------|--------|
| `VM_GUEST_GDTR_BASE` / `VM_GUEST_GDTR_LIMIT` | 필수 |
| `VM_GUEST_IDTR_BASE` / `VM_GUEST_IDTR_LIMIT` | 필수 |

### MSRs

| 필드 | 단계 1 |
|------|--------|
| `VM_GUEST_IA32_SYSENTER_CS/ESP/EIP` | 권장 |
| `VM_GUEST_PAT` | 권장 |
| `VM_GUEST_EFER` | 64비트 게스트 시 필수 |
| `VM_GUEST_DEBUGCTL` | 권장 |
| `VM_GUEST_PERF_GLOBAL_CTRL` | entry control 연동 시 |

### VMX·활동 상태 (hyper_box는 0으로 초기화)

| 필드 | 단계 1 | 비고 |
|------|--------|------|
| `VM_GUEST_INT_STATE` | 필수(0) | 인터럽트 차단 상태 등 |
| `VM_GUEST_ACTIVITY_STATE` | 필수(0) | Active |
| `VM_GUEST_SMBASE` | 0 | |
| `VM_GUEST_PENDING_DBG_EXCEPTS` | 0 | |
| `VM_GUEST_VMX_PRE_TIMER_VALUE` | 선택 | pin-based preemption timer 켤 때만 |

### 링크

| 필드 | 단계 1 |
|------|--------|
| `VM_VMCS_LINK_PTR` | 필수 (`0xFF…`) |

---

## hb_probe 단계별

| 단계 | guest VMWRITE |
|------|----------------|
| 0 (현재) | `VM_VMCS_LINK_PTR` 만 |
| 1 | 위 “필수” 표 전부 + `hb_setup_vm_guest_register` 수준 |
| 2 | + PDPTE, nested 링크, shadow revision bit 31 |

---

## Launch 직전

`hb_vm_launch`가 **현재 RSP**와 **`.success` 레이블**로 guest RIP/RSP를 다시 `vmwrite`한다.

→ [`launch-adjust.md`](launch-adjust.md)

---

## 관련

- [`segment.md`](segment.md) — x86 세그먼트·GDT 기초
- [`guest-segments-why.md`](guest-segments-why.md) — VMCS guest 세그먼트 “왜”, 미러, 체크리스트
- [`host.md`](host.md)
- [`control-entry.md`](control-entry.md)
- [`setting.md`](setting.md)

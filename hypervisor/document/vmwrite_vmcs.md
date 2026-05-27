# VMWRITE / VMCS 구조 비교 (hb_probe · Hyper-box · HyperDbg)

`VMWRITE`는 **current VMCS**에만 쓸 수 있다. current는 **`VMPTRLD` 직후** 정해진다.  
이 문서는 세 구현이 VMCS를 **어떤 레이어로**, **언제**, **어떤 필드까지** 채우는지 비교한다.

관련 문서:

- **필드별 세팅 목록:** [`vmcs/setting.md`](vmcs/setting.md) — guest / host / control 분리 문서
- 앞 단계: [`vmxon_clear_load.md`](vmxon_clear_load.md) — VMXON → VMCLEAR → VMPTRLD
- 다음 단계: [`load_write_launch.md`](load_write_launch.md) — VMLAUNCH / VMRESUME 흐름
- Alcatraz nested: [`../../simple.md`](../../simple.md) — VMCS shadowing / KVM 에뮬

참고 소스 (본 레포):

| 구분 | 경로 |
|------|------|
| **hb_probe** | [`hb_probe.c`](../hb_probe.c), [`asm_helper.asm`](../asm_helper.asm) |
| **Hyper-box (Alcatraz)** | [`Source/hyper_box/hypervisor.c`](../../Source/hyper_box/hypervisor.c), [`hyper_box.h`](../../Source/hyper_box/hyper_box.h), [`asm_helper.asm`](../../Source/hyper_box/asm_helper.asm) |
| **HyperDbg** (외부) | [HyperDbg/hyperhv `Vmx.c`](https://github.com/HyperDbg/HyperDbg/blob/master/hyperdbg/hyperhv/code/vmm/vmx/Vmx.c) |

---

## 한눈에 보기

| 항목 | hb_probe | Hyper-box (Alcatraz) | HyperDbg |
|------|----------|----------------------|----------|
| **목적** | HW·래퍼 검증 (프로브) | L0 하이퍼바이저 + nested KVM | 디버거 VMM (EPT/후킹) |
| **VMWRITE 시점** | `VMPTRLD` 직후 1필드 | 초기 `hb_setup_vmcs` + exit 핸들러 + nested 에뮬 | `VmxSetupVmcs` + exit 후 `VmxVmwrite64` |
| **VMCS 필드 수** | 1 (`VM_VMCS_LINK_PTR`) | 수십~수백 (host/guest/control 전부) | `VmxSetupVmcs` 전체 + 런타임 보정 |
| **Link pointer** | `0xFFFFFFFFFFFFFFFF` (섀도 없음) | guest에 `0xFF…` 또는 nested PA | `~0ULL` (섀도 없음) |
| **VMWRITE bitmap** | 없음 | 있음 (nested VMX exit 유도) | MSR/IO bitmap 위주 (VMWRITE bitmap은 HyperDbg 버전·기능에 따름) |
| **게스트 VMWRITE** | 없음 (VM entry 없음) | KVM `VMWRITE` → **exit → L0가 nested VMCS에 대리 기록** | 게스트가 VMX non-root일 때만 해당 |
| **VMLAUNCH** | 없음 | `hb_vm_launch` | `__vmx_vmlaunch` in `VmxVirtualizeCurrentSystem` |

---

## 공통 골격: asm 래퍼

세 프로젝트 모두 C에서 필드 인코딩·값을 준비하고, **인라인/asm에서 `vmwrite` 한 번** 호출하는 패턴이다.

```nasm
; hypervisor/asm_helper.asm · hyper_box/asm_helper.asm (동일 시그니처)
; int hb_write_vmcs(u64 field, u64 value)
hb_write_vmcs:
    vmwrite rdi, rsi      ; field ← rdi, value ← rsi
    jc .error
    jz .error
    xor rax, rax          ; 성공 0
    ret
```

HyperDbg는 MSVC intrinsic:

```c
// Vmx.c — VmxVmwrite64
return __vmx_vmwrite((size_t)Field, (size_t)FieldValue);
```

**정리:** “VMCS 구조”의 하드웨어 인터페이스는 동일하고, **위에 쌓는 C 레이어의 두께**만 다르다.

---

## VMCS 6개 영역과 VMWRITE 역할

Intel SDM 기준 VMCS 4KB는 대략 아래 6구역이다. **VMWRITE로 채우는 것**은 주로 앞 5개이고, exit information은 **CPU가 VM exit 시 기록**한다.

| 영역 | VMWRITE로 설정? | hb_probe | Hyper-box | HyperDbg |
|------|-----------------|----------|-----------|----------|
| guest state | VMM (entry 전) | ❌ | `hb_setup_vmcs` guest 블록 | `VmxSetupVmcs` + `HvFillGuestSelectorData` |
| host state | VMM (entry 전) | ❌ | `hb_setup_vmcs` host 블록 | `VmxSetupVmcs` host RIP/RSP/CR/세그먼트 |
| VM-execution control | VMM | ❌ | pin/proc/sec + bitmap/EPT/VPID | `HvAdjustControls` + EPT/VPID/IO/MSR bitmap |
| VM-exit control | VMM | ❌ | `VM_CTRL_VM_EXIT_CTRLS` 등 | `VMCS_CTRL_PRIMARY_VMEXIT_CONTROLS` |
| VM-entry control | VMM | ❌ | `VM_CTRL_VM_ENTRY_CTRLS` 등 | `VMCS_CTRL_VMENTRY_CONTROLS` |
| VM-exit information | CPU (exit 후) | ❌ | `VMREAD`로 reason/qual 읽기 | `VMREAD` / 핸들러 |

---

## 1. hb_probe — 최소 VMWRITE 프로브

### 호출 순서 (per-CPU)

```
VMXON → VMCLEAR → VMPTRLD → hb_vmx_probe_vmwrite() → (성공 시 VMXON 유지, 언로드 시 VMXOFF)
```

`hb_vmxon_on_cpu()` 안에서 `VMPTRLD` 직후 한 번만 VMWRITE를 검증한다.

### 구조

| 레이어 | 내용 |
|--------|------|
| **데이터** | `g_vmx[cpu].vmxon_region`, `vmcs_region` (각 4KB, revision만 `*(u32*)` 설정) |
| **VMWRITE** | `VM_VMCS_LINK_PTR` (`0x2800`) = `0xFFFFFFFFFFFFFFFF` |
| **검증** | `hb_read_vmcs`로 동일 값 read-back |
| **의미** | 섀도 VMCS 없음 — Intel이 요구하는 “링크 없음” 기본값 확인 |

```c
// hb_probe.c — hb_vmx_probe_vmwrite()
ret = hb_write_vmcs(VM_VMCS_LINK_PTR, 0xffffffffffffffffULL);
ret = hb_read_vmcs(VM_VMCS_LINK_PTR, &read_back);
```

### 특징

- **host/guest/control 필드는 쓰지 않음** → `VMLAUNCH` 불가 (의도적).
- probe 성공 경로는 **VMXON을 유지** (`out_keep_vmx`) — `load_write_launch.md`의 “곧바로 VMXOFF” 설명보다 **현재 코드는 한 단계 더 진행**한 상태.
- KVM 충돌 검사만 하고, VMWRITE bitmap·nested·EPT 없음.

**학습 포인트:** “`VMPTRLD` 다음에 `vmwrite`가 실제로 먹는지”만 보려면 hb_probe 수준이면 충분하다.

---

## 2. Hyper-box (Alcatraz) — 3단계 VMWRITE 모델

Alcatraz는 **단일 L0 하이퍼바이저**이면서, 게스트(KVM)의 **VMX 명령을 VM exit로 가로채 에뮬**한다. VMWRITE 사용처가 **세 갈래**로 나뉜다.

### 2-1. 초기화: C 구조체 → `hb_setup_vmcs` (일괄 VMWRITE)

**순서** (`hypervisor.c` per-CPU 스레드):

```
hb_init_vmx()          /* VMXON + guest VMCS VMCLEAR + VMPTRLD */
  → hb_setup_vm_host_register()
  → hb_setup_vm_guest_register()
  → hb_setup_vm_control_register()   /* MSR cap, bitmap, EPT — 메모리만 준비 */
  → hb_setup_vmcs()                  /* hb_write_vmcs × N */
  → hb_vm_launch()                   /* asm에서 guest RIP/RSP 재조정 + vmlaunch */
```

`hb_init_vmx()`는 probe와 같이 **revision → VMXON → VMCLEAR → VMPTRLD**까지다.  
차이: VMXON 영역과 **guest VMCS**가 분리(`g_vmx_on_vmcs_*` vs `g_guest_vmcs_*`).

`hb_setup_vmcs()` 내부 블록 (순서 고정):

1. **Host state** — `VM_HOST_CR0/3/4`, `VM_HOST_RSP`, `VM_HOST_RIP`(exit stub), 세그먼트 selector/base, GDTR/IDTR, PAT/EFER …
2. **Guest state** — `VM_GUEST_*` 전부 + `VM_VMCS_LINK_PTR` (초기값은 guest 구조체에서, 보통 `0xFF…`)
3. **VM execution / exit / entry control** — pin, primary/secondary proc-based, exception bitmap, IO/MSR/**VMREAD·VMWRITE bitmap** 주소, EPT pointer, CR mask/shadow …

control 준비는 `hb_setup_vm_control_register()`에서 **가상 주소**로 bitmap을 채운 뒤, `virt_to_phys`로 PA를 넣고, 마지막에 `hb_write_vmcs(VM_CTRL_*_ADDR, pa)` 한다.

**nested 대비 (VMWRITE bitmap):**

```c
/* VM exit when guest tries VMREAD/VMWRITE these host fields */
hb_vm_set_vmread_vmwrite_bitmap(control, VM_HOST_RIP);
hb_vm_set_vmread_vmwrite_bitmap(control, VM_HOST_RSP);
hb_vm_set_vmread_vmwrite_bitmap(control, VM_HOST_CR3);
```

게스트(KVM)가 nested VMCS에 host RIP 등을 쓰려 하면 **VM exit** → L0가 가로챈다.

### 2-2. Launch 직전: asm에서 guest RIP/RSP 재기록

`hb_setup_vmcs`에서 이미 guest RIP/RSP를 썼어도, `hb_vm_launch`는 **현재 호스트 스택/코드 위치**로 다시 맞춘다.

```nasm
; asm_helper.asm — hb_vm_launch
mov rbx, 0x681C    ; VMCS encoding for GUEST_RSP
mov rax, rsp
vmwrite rbx, rax
; GUEST_RIP = .success (게스트 진입점)
vmlaunch
```

HyperDbg의 `VmxSetupVmcs` 끝에서 `VMCS_GUEST_RSP` / `VMCS_GUEST_RIP` / `VMCS_HOST_RSP` / `VMCS_HOST_RIP`를 쓰는 것과 **같은 패턴**이다.

### 2-3. 런타임 VMWRITE

| 상황 | 함수/경로 | 예시 필드 |
|------|-----------|-----------|
| 일반 VM exit 처리 | `hb_vm_exit_callback` | `VM_CTRL_VM_ENTRY_INST_LENGTH`, guest CR/세그먼트, `VM_GUEST_RIP` (+`hb_advance_vm_guest_rip`) |
| 인터럽트/이벤트 주입 | 여러 helper | `VM_CTRL_VM_ENTRY_INT_INFO_FIELD` |
| **게스트 VMWRITE** (nested) | `hb_vm_exit_callback_vmx_inst_type2` | nested VMCS에 기록 + L0 host 필드는 stub/스택/호스트 PML4로 **치환** |
| **VMCS shadowing** | `hb_vm_set_vmcs_shadowing_to_current` | `VM_VMCS_LINK_PTR` = shadow PA, revision bit 31 |
| nested VM exit 복귀 | `hb_vm_exit_callback` (prev_vmcs ≠ guest) | guest VMCS로 `VMPTRLD` 후 RIP/RSP/RFLAGS, link 설정 |

**게스트 `VMWRITE` 에뮬 핵심** (`VM_EXIT_REASON_VMWRITE`):

```
1. exit qualification + inst_info로 목적지(메모리/레지스터)와 VMCS field encoding 파악
2. hb_advance_vm_guest_rip()
3. g_nested_vmcs_ptr[cpu]에 해당하는 nested 슬롯 찾기
4. VM_HOST_RIP/RSP/CR3이면 nested 구조체에 “진짜 값” 저장, dest는 L0용 값으로 바꿈
5. hb_load_vmcs(nested_ptr) → hb_write_vmcs(field, dest) → hb_load_vmcs(prev)
```

→ **CPU current VMCS는 Hyper-box guest VMCS로 유지**하고, KVM이 쓰는 내용은 **링크된 nested(섀도) VMCS**에 반영 ([`simple.md`](../../simple.md)의 Alcatraz 표와 동일).

**`VMPTRLD` 에뮬** (`VM_EXIT_REASON_VMPTRLD`): 하드웨어 `VMPTRLD` 대신 `g_nested_vmcs_ptr` 저장 + `hb_vm_set_vmcs_shadowing_to_current(data)`.

### Alcatraz만의 VMWRITE 관련 자원

| 자원 | 용도 |
|------|------|
| `g_vmwrite_bitmap_addr[]` | control 필드 — 특정 VMCS 필드에 대한 guest VMWRITE 시 exit |
| `g_nested_vmcs_ptr[]` | per-CPU “KVM이 로드했다고 생각하는” nested VMCS PA |
| `hb_nested_vmcs_struct` | nested가 쓰려는 host RIP/RSP/CR3 보관 |
| Shadow VMCS (`HYPERBOX_USE_VMCS_SHADOWING`) | `VM_VMCS_LINK_PTR` + revision bit 31 |

---

## 3. HyperDbg — `VmxSetupVmcs` 중심 단일 VMM

HyperDbg는 **자체 VMM이 L0**이고, 게스트 OS 전체를 VMX non-root로 올린 뒤 **디버깅·EPT 후킹**에 VMWRITE를 쓴다.  
nested KVM 에뮬레이션(Alcatraz)보다 **구조가 단순**하고, HyperBone/Intel 튜토리얼 계열과 거의 같다.

### 가상화 진입 순서 (`VmxVirtualizeCurrentSystem`)

```
VmxClearVmcsState()     /* VMCLEAR */
  → VmxLoadVmcs()       /* VMPTRLD */
  → VmxSetupVmcs()      /* VmxVmwrite64 다수 */
  → __vmx_vmlaunch()
```

실패 시 `VMCS_VM_INSTRUCTION_ERROR` 읽고 `__vmx_off()`.

### `VmxSetupVmcs` 내부 구조 (대략적 순서)

| 단계 | 내용 |
|------|------|
| Host selector | `VMCS_HOST_*_SELECTOR` ← `AsmGetCs/Es/...` |
| Link | `VMCS_GUEST_VMCS_LINK_POINTER` = `~0ULL` |
| Control (MSR adjust) | `HvAdjustControls()` + `VmxVmwrite64` — pin, primary/secondary proc, exit, entry |
| Guest/host CR | 현재 `__readcr0/3/4`, host CR3는 시스템 PTE base |
| 세그먼트 | `HvFillGuestSelectorData()` — ES~TR selector/limit/access/base 일괄 |
| Bitmap / EPT | MSR bitmap, IO bitmap A/B, EPT pointer, VPID |
| Entry point | `VMCS_GUEST_RSP` = `GuestStack`, `VMCS_GUEST_RIP` = `AsmVmxRestoreState` |
| Host entry | `VMCS_HOST_RSP` = 정렬된 VmmStack, `VMCS_HOST_RIP` = `AsmVmexitHandler` |

**Control 비트 조정:** `HvAdjustControls(Requested, MSR_TRUE_XXX)` — Hyper-box의 `(rdmsr(TRUE_CTL) | flags) & mask`와 **동일 목적**.

**런타임 VMWRITE:** exit 핸들러·이벤트 주입·싱글스텝 등에서 `VmxVmwrite64` (예: `VMCS_GUEST_RIP`, `VMCS_GUEST_RFLAGS`, control 재설정).  
Alcatraz처럼 “게스트 VMWRITE 명령을 nested VMCS에 대리”하는 **두 번째 VMCS 트랙은 기본 설계에 없음**.

---

## 흐름도 비교

### hb_probe

```text
[각 CPU] VMXON → VMCLEAR → VMPTRLD
              → VMWRITE(link=FF..) → VMREAD 검증
              → (모듈 유지 시 VMXON 상태, rmmod 시 VMXOFF)
```

### Hyper-box (첫 launch)

```text
hb_init_vmx (VMXON, guest VMCLEAR/VMPTRLD)
  → setup host/guest/control 구조체 (메모리)
  → hb_setup_vmcs (VMWRITE × N)
  → hb_vm_launch (VMWRITE guest RIP/RSP + VMLAUNCH)
  → guest 실행 … VM exit → handler → VMWRITE 보정 → VMRESUME
```

### HyperDbg

```text
VMCLEAR → VMPTRLD → VmxSetupVmcs (VmxVmwrite64 × N) → VMLAUNCH
  → guest … VM exit → AsmVmexitHandler → VmxVmwrite64 … → VMRESUME
```

### Alcatraz nested (게스트 VMWRITE)

```text
KVM(VMWRITE) in non-root
  → VM exit (VMWRITE bitmap 또는 VMX insn exit)
  → L0: nested VMCS에 hb_write_vmcs
  → (current는 여전히 Hyper-box guest VMCS)
```

---

## 설계 차이 요약

| 질문 | hb_probe | Hyper-box | HyperDbg |
|------|----------|-----------|----------|
| 왜 VMWRITE? | 래퍼·HW 동작 확인 | VMCS 완성 + nested 에뮬 | VMM 가동·디버그 |
| current VMCS | probe용 1개 | **guest VMCS 고정** + nested는 link/별도 load | per-core `VmcsRegion` |
| Link pointer | `0xFF…` 고정 테스트 | `0xFF…` 또는 **nested/shadow PA** | `~0ULL` |
| 필드 상수 | `0x2800` 하나 | `hyper_box.h` 전체 encoding | HyperDbg `VMCS_*` enum |
| VMWRITE 가로채기 | 없음 | **bitmap + VM_EXIT VMWRITE** | (기능에 따라 MSR/CR exit) |

---

## hb_probe에서 Hyper-box / HyperDbg로 확장할 때

이미 hb_probe는 **`hb_write_vmcs` / `hb_read_vmcs`** 가 있다. 다음은 Alcatraz·HyperDbg 공통 순서다.

1. **`hyper_box.h`급 필드 encoding** — host/guest/control 상수
2. **`hb_setup_vm_host/guest/control_register`** — 현재 CPU·MSR·GDT에서 구조체 채우기
3. **`hb_setup_vmcs`** — 블록 순서: host → guest → control (link pointer 포함)
4. **`hb_vm_launch`** — guest RIP/RSP, host RIP = exit stub
5. **IRQ/선점** — 상주 시 [`it_req.md`](it_req.md)
6. **(Alcatraz만)** VMWRITE bitmap, nested 테이블, `VM_VMCS_LINK_PTR` shadow 경로

HyperDbg만 따라갈 경우 6번은 생략하고, **EPT + MSR bitmap + `HvAdjustControls`** 에 집중하면 된다.

---

## 참고 링크

- [HyperDbg `Vmx.c` — `VmxSetupVmcs`, `VmxVirtualizeCurrentSystem`](https://github.com/HyperDbg/HyperDbg/blob/master/hyperdbg/hyperhv/code/vmm/vmx/Vmx.c)
- [HyperDbg Doxygen — `VmxVmwrite64`](https://doxygen.hyperdbg.org/_vmx_8h.html)
- [HyperBone `VmxSetupVMCS`](https://github.com/DarthTon/HyperBone/blob/master/src/Arch/Intel/VMX.c) — HyperDbg와 같은 골격의 참고 구현

---

## 관련 문서 맵

```text
vmxon_clear_load.md   … VMXON → VMCLEAR → VMPTRLD
       ↓
vmwrite_vmcs.md       … VMWRITE 구조 비교 (본 문서)
       ↓
load_write_launch.md  … VMLAUNCH / VMRESUME / exit 루프
       ↓
simple.md             … Alcatraz nested / shadowing
```

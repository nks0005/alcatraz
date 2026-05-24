# VMPTRLD → VMWRITE → VMLAUNCH 순서

`hb_probe`가 검증한 **`VMXON → VMCLEAR → VMPTRLD`** 다음에, 본격 하이퍼바이저가 하는 **`VMWRITE`(VMCS 설정)** 와 **`VMLAUNCH` / `VMRESUME`(게스트 실행)** 단계를 정리한 문서이다.

관련 소스:

- [`hb_probe.c`](../hb_probe.c) — 현재는 `VMPTRLD`까지만 (프로브 후 `VMXOFF`)
- [`vmxon_clear_load.md`](vmxon_clear_load.md) — 앞 단계 VMXON / VMCLEAR / VMPTRLD
- [`Source/hyper_box/hypervisor.c`](../../Source/hyper_box/hypervisor.c) — `hb_init_vmx`, `hb_setup_vmcs`, `hb_vm_launch`
- [`Source/hyper_box/asm_helper.asm`](../../Source/hyper_box/asm_helper.asm) — `hb_write_vmcs`, `hb_vm_launch`, `vmresume`
- 임계 구간: [`it_req.md`](it_req.md)

참고 (외부, HyperDbg와 같은 골격):

- HyperBone `VmxEnterRoot` → `VmxSetupVMCS` → `__vmx_vmlaunch` (Intel VT-x 튜토리얼·HyperDbg 계열과 동일 패턴)

---

## 한눈에 보기

| 순서 | 명령/함수 | 하는 일 |
|------|-----------|---------|
| (앞 단계) | `VMCLEAR` → `VMPTRLD` | current VMCS 지정 — [`vmxon_clear_load.md`](vmxon_clear_load.md) |
| **VMCLEAR ↔ VMPTRLD 사이** | (보통 없음) | VMWRITE **불가** (current VMCS 없음) |
| 4 | `VMWRITE` (`hb_write_vmcs`) | host / guest / control VMCS 필드 채우기 |
| 5a | `VMLAUNCH` (`hb_vm_launch`) | **첫** VM entry (launch state = **clear**) |
| 5b | `VMRESUME` | 이미 launch된 VMCS로 **재진입** |
| (반복) | VM exit handler | exit 이유 처리 후 대개 `VMRESUME` |

**정답:** 추후 커스텀 하이퍼바이저도 **`VMPTRLD` → `VMWRITE` → `VMLAUNCH`**(또는 이후 `VMRESUME`) 흐름이 맞다.  
`hb_probe`는 4~5단계를 **의도적으로 생략**하고 하드웨어만 확인한다.

---

## 전체 로드맵 (probe → 본격 구현)

```
[hb_probe — 현재]
  VMXON → VMCLEAR → VMPTRLD → VMXOFF (모듈 언로드 가능 상태로 복귀)

[이후 — hyper_box / HyperDbg 스타일]
  VMXON → VMCLEAR → VMPTRLD
    → VMWRITE (VMCS 커스텀)
    → VMLAUNCH
    → (guest 실행, VMX non-root)
    → VM exit → host handler → VMRESUME (루프)
    → (종료 시) VMXOFF
```

| 구분 | VMXON 유지 | VMWRITE | VMLAUNCH / VMRESUME | VM exit handler |
|------|------------|---------|---------------------|-----------------|
| `hb_probe` | ❌ (곧바로 OFF) | ❌ | ❌ | ❌ |
| `hyper_box` | ✅ | ✅ `hb_setup_vmcs` | ✅ `hb_vm_launch` 등 | ✅ `hb_vm_exit_callback` |

---

## 왜 VMPTRLD 다음에 VMWRITE인가

- **`VMWRITE`** 는 **current VMCS**에만 쓴다. current는 **`VMPTRLD`로 정해진다.**
- 따라서 **`VMCLEAR`와 `VMPTRLD` 사이**에 host RIP, guest CR3 같은 필드를 넣지 **않는다.** (그 사이에는 revision 기록만 `VMCLEAR` **전**에 하는 경우가 많다.)
- 메모리上的으로 VMCS region을 미리 채워 두는 방식은 Intel 모델과 맞지 않으며, 공식 경로는 **명령 `VMWRITE`** 이다.

---

## hyper_box에서의 호출 순서

각 CPU가 하이퍼바이저에 들어갈 때 (`hypervisor.c`):

```c
hb_init_vmx(cpu_id);                    /* VMXON + VMCLEAR + VMPTRLD */

hb_setup_vm_host_register(host_register);
hb_setup_vm_guest_register(guest_register, host_register);
hb_setup_vm_control_register(control_register, cpu_id);
hb_setup_vmcs(host_register, guest_register, control_register);  /* VMWRITE 다수 */

local_irq_save(irqs);
result = hb_vm_launch();                /* VMLAUNCH */
local_irq_restore(irqs);
```

### `hb_init_vmx()` — probe와 동일한 VMX 진입

[`vmxon_clear_load.md`](vmxon_clear_load.md)와 같다: revision → `hb_enable_vmx` → `hb_start_vmx` → `hb_clear_vmcs` → `hb_load_vmcs`.  
여기서 **VMXON은 유지**하고 리턴한다 (probe와 다름).

### `hb_setup_vm_*` — C 구조체에 값 준비

| 함수 | 역할 |
|------|------|
| `hb_setup_vm_host_register` | VM exit 시 **host**가 돌아갈 RIP/RSP/CR/세그먼트 등 수집 |
| `hb_setup_vm_guest_register` | **guest** 초기 CPU 상태 (현재 커널/컨텍스트 기준) |
| `hb_setup_vm_control_register` | pin / proc / exit / entry **control** 비트 (MSR cap에 맞춤) |

이 단계는 아직 VMCS에 안 쓰고, **다음 `hb_setup_vmcs`에서 VMWRITE** 한다.

### `hb_setup_vmcs()` — VMWRITE

`hb_write_vmcs(필드, 값)` 반복으로 VMCS를 채운다. 대략 다음 블록이다.

1. **Host state** — `VM_HOST_CR0/3/4`, `VM_HOST_RSP`, `VM_HOST_RIP`, 세그먼트 selector/base, GDTR/IDTR, EFER 등  
2. **Guest state** — `VM_GUEST_*` (CR, RIP, RSP, 세그먼트, MSRs…)  
3. **VM execution controls** — pin-based, primary/secondary proc-based, exit/entry controls  
4. **기타** — exception bitmap, MSR bitmap, EPT pointer 등 (기능 플래그에 따라)

host **RIP**는 보통 VM exit handler stub (`hb_vm_exit_callback_stub`)을 가리키게 둔다.

### `hb_vm_launch()` — VMLAUNCH

[`asm_helper.asm`](../../Source/hyper_box/asm_helper.asm):

- launch 직전 guest **RSP/RIP**를 `vmwrite`로 한 번 더 맞출 수 있음
- **`vmlaunch`** 실행
- 성공 시 게스트 코드로 점프 (`.success` 레이블 — **VMX non-root**)
- 실패 시 CF/ZF → `-1`(invalid) / `-2`(valid) 반환, `VM_DATA_INST_ERROR`로 원인 확인

---

## VMLAUNCH vs VMRESUME

| 명령 | 사용 시점 | VMCS launch state |
|------|-----------|-------------------|
| **VMLAUNCH** | 그 VMCS로 **처음** guest 진입 | **clear** 여야 함 (`VMCLEAR` 직후 상태) |
| **VMRESUME** | 이미 한 번 launch된 VMCS로 **다시** guest 복귀 | **launched** 여야 함 |

- **`VMCLEAR`** 를 다시 하면 launch state가 **clear**로 돌아가므로, 그 VMCS에는 다시 **`VMLAUNCH`** 가 필요하다.
- VM exit handler (`hb_vm_exit_callback_stub`)는 처리 후 기본적으로 **`vmresume`**, 특수 플래그 시 **`vmlaunch`** 를 다시 쓰기도 한다.

---

## VM exit 이후 루프 (개념)

```
        ┌──────────────┐
        │  VMLAUNCH    │
        │  / VMRESUME  │
        └──────┬───────┘
               ▼
        ┌──────────────┐
        │ Guest 실행    │  (VMX non-root)
        └──────┬───────┘
               │ VM exit
               ▼
        ┌──────────────┐
        │ Host handler  │  (VM_HOST_RIP → stub)
        │ VMREAD/처리   │
        │ VMWRITE 보정  │
        └──────┬───────┘
               ▼
        ┌──────────────┐
        │  VMRESUME    │  (대부분)
        └──────┬───────┘
               └──────────► (guest로 복귀)
```

`hb_probe`에는 이 루프가 **없다** — `VMPTRLD` 성공만 확인하고 `VMXOFF`한다.

---

## `hypervisor/` (hb_probe)에 추가할 때 체크리스트

현재 [`hb_probe.c`](../hb_probe.c) / [`asm_helper.asm`](../asm_helper.asm)에는 `hb_write_vmcs`, `hb_vm_launch`가 **없다**. 다음 단계에서 보통 필요한 것:

| 항목 | 설명 |
|------|------|
| `hb_write_vmcs` / `hb_read_vmcs` | asm에 `vmwrite` / `vmread` 래퍼 (hyper_box 참고) |
| VMCS 필드 상수 | host/guest/control encoding (`hyper_box.h` 등) |
| Host RIP | VM exit stub + 스택 (per-CPU) |
| `hb_setup_vmcs` 또는 동등 로직 | MSR cap에 맞춘 control + host/guest 상태 |
| `hb_vm_launch` | `vmlaunch` + 실패 시 `VM_INSTRUCTION_ERROR` |
| IRQ/선점 | launch·exit 구간은 [`it_req.md`](it_req.md) 수준 이상으로 설계 (상주 시) |
| KVM 충돌 | [`vmx_error.md`](vmx_error.md) — VT-x 단독 사용 |

probe 모듈을 확장할 때도 순서는 **`… → VMPTRLD → (VMWRITE…) → VMLAUNCH`** 를 지키고, probe만 할 때처럼 **`VMPTRLD` 직후 `VMXOFF`** 하지 않도록 주의한다.

---

## 자주 하는 질문

| 질문 | 답 |
|------|-----|
| clear와 load 사이에 VMCS 세팅하나? | **아니오.** 세팅은 **`VMPTRLD` 이후 `VMWRITE`**. |
| probe 다음이 VMWRITE + VMLAUNCH 맞나? | **맞다.** probe는 그 전 단계만 검증. |
| VMWRITE 없이 VMLAUNCH만? | **실패한다.** 필수 host/guest/control이 비어 있음. |
| 첫 진입은 VMLAUNCH만? | **맞다.** 이후 같은 VMCS는 주로 **VMRESUME**. |
| hyper_box와 순서 같나? | **같다.** `hb_init_vmx` → setup → `hb_setup_vmcs` → `hb_vm_launch`. |

---

## 관련 문서 맵

```
vmxon_clear_load.md     … VMXON → VMCLEAR → VMPTRLD (hb_probe 현재 끝)
       ↓
load_write_launch.md    … VMPTRLD → VMWRITE → VMLAUNCH/VMRESUME (본 문서)
       ↓
it_req.md / vmx_error.md … 상주·IRQ·KVM·복구
cr0_cr4.md / msr.md      … VMXON 이전 전제
```

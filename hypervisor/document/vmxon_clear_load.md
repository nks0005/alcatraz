# VMXON → VMCLEAR → VMPTRLD 순서

`hb_probe.c`의 `hb_vmxon_on_cpu()`에서 VMX root에 진입한 뒤 실행하는 **`VMXON`**, **`VMCLEAR`**, **`VMPTRLD`** 세 명령의 역할·호출 순서·전제 조건을 정리한 문서이다.

관련 소스:

- [`hb_probe.c`](../hb_probe.c) — `hb_vmxon_on_cpu()` (250~327행), `hb_vmx_alloc_regions()`
- [`asm_helper.asm`](../asm_helper.asm) — `hb_start_vmx`, `hb_clear_vmcs`, `hb_load_vmcs`, `hb_stop_vmx`
- [`asm_helper.h`](../asm_helper.h) — C 래퍼 선언
- VMXON 전 CR·VMXE: [`cr0_cr4.md`](cr0_cr4.md)
- 임계 구간 IRQ/선점: [`it_req.md`](it_req.md)
- 복구 누락 시 멈춤: [`vmx_error.md`](vmx_error.md)

---

## 한눈에 보기

| 순서 | C 함수 | 어셈블리 | 하는 일 |
|------|--------|----------|---------|
| (전제) | `hb_adjust_vmx_cr0_cr4()`, `hb_enable_vmx()` | `mov cr0/cr4` 등 | VMX 진입용 CR 정렬, **CR4.VMXE = 1** |
| 1 | `hb_start_vmx(&vmxon_pa)` | `vmxon` | **VMX operation** 진입 (VMX root) |
| 2 | `hb_clear_vmcs(&vmcs_pa)` | `vmclear` | VMCS 영역을 **clear(비활성)** 상태로 초기화 |
| 3 | `hb_load_vmcs(&vmcs_pa)` | `vmptrld` | 해당 VMCS를 **current VMCS**로 지정 |
| (종료) | `hb_stop_vmx()` | `vmxoff` | VMX operation 종료 (프로브 후 복귀) |

**정답:** Intel VMX 초기화에서도, 이 프로젝트 프로브에서도 순서는 **`VMXON → VMCLEAR → VMPTRLD`** 가 맞다.  
`VMCLEAR` / `VMPTRLD`는 **VMXON 이후**(VMX operation 안)에서만 실행할 수 있다.

---

## `hb_vmxon_on_cpu()` 전체 흐름

각 **온라인 CPU**마다 `smp_call_function_single(cpu, hb_vmxon_on_cpu, …)`로 한 번씩 호출된다.

```
hb_vmxon_on_cpu()
  → revision ID를 VMXON·VMCS 영역 첫 4바이트에 기록
  → preempt_disable + local_irq_disable          (it_req.md)
  → CR0/CR4 저장
  → hb_adjust_vmx_cr0_cr4() + hb_enable_vmx()      (cr0_cr4.md)
  → VMXON
  → VMCLEAR
  → VMPTRLD
  → (성공 로그)
out_vmxoff:
  → VMXOFF (VMXON에 성공했을 때만)
out_restore_cr:
  → VMXE OFF, CR0/CR4 복구
  → local_irq_enable + preempt_enable
```

모듈 로드 시 `hypervisor_init()`은 **모든 온라인 CPU**에서 위 프로브가 끝난 뒤 `VMXOFF`까지 수행하고 반환한다. 하이퍼바이저를 **상주**시키는 단계는 아니다.

---

## VMXON / VMCS 영역 (메모리)

`hb_vmx_alloc_regions()`에서 CPU마다 **4KB 페이지 2장**을 할당한다.

| 영역 | 용도 | 프로브에서 쓰는 명령 |
|------|------|----------------------|
| `vmxon_region` | VMXON 포인터가 가리키는 **VMXON region** | `VMXON` |
| `vmcs_region` | VMCS 데이터가 들어갈 **VMCS region** | `VMCLEAR`, `VMPTRLD` |

공통 준비:

- 페이지는 `memset(..., 0)`으로 0으로 채운다.
- `VMXON` / `VMCLEAR` / `VMPTRLD` **직전**에 각 영역 **선두 4바이트**에 `IA32_VMX_BASIC`에서 읽은 **VMCS revision identifier**를 쓴다 (`g_vmx_revision`).

```c
*(u32 *)pc->vmxon_region = g_vmx_revision;
*(u32 *)pc->vmcs_region = g_vmx_revision;
```

명령 피연산자는 **가상 주소가 아니라 물리 주소(PA)** 이다. C에서는 PA를 담은 변수의 주소를 asm에 넘긴다.

```c
vmxon_pa = (u64)__pa(pc->vmxon_region);
ret = hb_start_vmx(&vmxon_pa);   // asm: vmxon [rdi]

vmcs_pa = (u64)__pa(pc->vmcs_region);
ret = hb_clear_vmcs(&vmcs_pa);   // asm: vmclear [rdi]
ret = hb_load_vmcs(&vmcs_pa);    // asm: vmptrld [rdi]
```

---

## 1. VMXON (`hb_start_vmx`)

- **효과:** 현재 논리 프로세서가 **VMX root operation**에 들어간다. 이후 VMX 명령(`VMCLEAR`, `VMPTRLD`, `VMREAD`/`VMWRITE` 등)을 쓸 수 있다.
- **전제 (이 프로젝트):** CPUID·`IA32_FEATURE_CONTROL` 통과, per-CPU VMXON 영역 할당·revision 기록, [`cr0_cr4.md`](cr0_cr4.md)의 CR 정렬·VMXE, KVM 미로드 등 — `hypervisor_init()` 상단 참고.
- **실패:** `asm_helper.asm`에서 CF=1 또는 ZF=1이면 `-1` 반환. 프로브는 `goto out_restore_cr` (VMXON 실패 시에는 `VMXOFF` 불필요).

---

## 2. VMCLEAR (`hb_clear_vmcs`)

- **효과:** 지정한 VMCS region을 **clear 상태**(launch state inactive, 내용 초기화)로 만든다.
- **왜 VMXON 다음인가:** `VMCLEAR`는 **VMX operation 중**에만 유효하다. VMXON 전에는 실행할 수 없다.
- **왜 VMPTRLD 전인가:** 새로 할당·0으로 채운 VMCS는 **반드시 VMCLEAR로 초기화**한 뒤 `VMPTRLD`하는 것이 일반적이다. clear 없이 로드하면 무효 VMCS로 실패할 수 있다.

---

## 3. VMPTRLD (`hb_load_vmcs`)

- **효과:** 지정한 VMCS를 **current VMCS pointer**로 로드한다. 이후 같은 CPU에서 `VMREAD`/`VMWRITE`로 이 VMCS 필드를 다룰 수 있다 (프로브는 여기까지 확인하고 필드 설정은 하지 않음).
- **전제:** 대상 VMCS는 보통 직전 **`VMCLEAR`로 clear된 상태**여야 한다.

---

## asm ↔ C 매핑

[`asm_helper.asm`](../asm_helper.asm):

```asm
hb_start_vmx:
    vmxon [rdi]      ; RDI = VMXON region PA를 담은 변수의 주소

hb_clear_vmcs:
    vmclear [rdi]    ; RDI = VMCS region PA를 담은 변수의 주소

hb_load_vmcs:
    vmptrld [rdi]

hb_stop_vmx:
    vmxoff
```

성공 시 `rax = 0`, 실패 시 `rax = -1` (CF/ZF 검사).

---

## 프로브 코드에서의 실패·복구

| 실패 지점 | `goto` | VMXOFF | CR·VMXE·IRQ 복구 |
|-----------|--------|--------|------------------|
| `VMXON` | `out_restore_cr` | 안 함 (`vmx_on == false`) | 함 |
| `VMCLEAR` / `VMPTRLD` | `out_vmxoff` | 함 | `out_restore_cr`에서 함 |

`VMCLEAR` 또는 `VMPTRLD` 실패 시에도 **VMX root에 들어간 상태**이므로 `out_vmxoff`에서 `hb_stop_vmx()` (`VMXOFF`)를 호출한 뒤 CR·인터럽트를 복구해야 한다. 자세한 멈춤 사례는 [`vmx_error.md`](vmx_error.md).

---

## 프로브 vs 실제 하이퍼바이저

| 구분 | `hb_probe` (현재) | 하이퍼바이저 본격 구현 시 (참고) |
|------|-------------------|----------------------------------|
| 목적 | 세 명령이 CPU에서 동작하는지 **검증** | 게스트 실행·VM exit 처리 |
| `VMXON` 후 | 곧바로 `VMXOFF` | CPU별로 VMXON **유지** |
| VMCS | clear + load만 | `VMWRITE`로 host/guest 상태 채운 뒤 `VMLAUNCH`/`VMRESUME` 등 |
| IRQ | 짧은 구간만 `local_irq_disable` | 상주 시 별도 exit handler·스케줄링 설계 필요 |

순서 **`VMXON → VMCLEAR → VMPTRLD`** 는 프로브이든 KVM이든 **새 VMCS를 current로 올릴 때의 기본 골격**은 동일하다.

---

## 자주 하는 질문

| 질문 | 답 |
|------|-----|
| 순서를 `VMCLEAR` → `VMXON` → `VMPTRLD`로 바꿀 수 있나? | **아니오.** `VMCLEAR`/`VMPTRLD`는 VMXON **이후**만 가능하다. |
| `VMPTRLD`만 하고 `VMCLEAR`는 생략? | **비권장.** 새 영역은 VMCLEAR 후 load가 안전하다. |
| `VMXON`과 `VMPTRLD`가 같은 페이지를 가리키나? | **아니오.** VMXON region과 VMCS region은 **서로 다른 4KB 페이지**다. |
| revision ID는 언제 쓰나? | **VMXON / VMCLEAR / VMPTRLD 직전**, 각 해당 영역의 첫 `u32`에 동일한 `g_vmx_revision`. |
| KVM 로드 중에도 되나? | **아니오.** 같은 코어 VT-x 충돌 — [`vmx_error.md`](vmx_error.md), `hb_is_kvm_loaded()` 참고. |

---

## 성공 시 커널 로그 예

```
hypervisor_b: cpu 0: VMXON/VMCLEAR/VMPTRLD ok (vmcs pa=0x........)
hypervisor_b: all N online cpus: VMXON/VMCLEAR/VMPTRLD probe ok (VMXOFF before return)
```

---

## 관련 문서 맵

```
msr.md (CPUID, FEATURE_CONTROL, VMX_BASIC)
  → cr0_cr4.md (VMXON 전 CR)
  → vmxon_clear_load.md (본 문서: VMXON → VMCLEAR → VMPTRLD)
       ↓
  → load_write_launch.md (다음 단계: VMWRITE → VMLAUNCH/VMRESUME)
       ↔ it_req.md (IRQ/preempt 임계 구간)
       ↔ vmx_error.md (복구 누락·KVM 충돌)
```

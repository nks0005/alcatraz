# VMX 프로브 구간의 선점·인터럽트 차단

`hb_probe.c`의 `hb_vmxon_on_cpu()`에서 `VMXON` 직전에 호출하는 **`preempt_disable()`**, **`local_irq_disable()`** 와, 복구 시 **`local_irq_enable()`**, **`preempt_enable()`** 의 용도를 정리한 문서이다.

관련 소스:

- [`hb_probe.c`](../hb_probe.c) — `hb_vmxon_on_cpu()` (277~326행)
- CR0/CR4·VMXE: [`cr0_cr4.md`](cr0_cr4.md)
- 복구 누락 시 OS 멈춤: [`vmx_error.md`](vmx_error.md)

---

## 네 함수 한눈에 보기

| 끄는 함수 | 켜는 함수 | 하는 일 |
|-----------|-----------|---------|
| `preempt_disable()` | `preempt_enable()` | **선점(preemption) 금지** — 이 CPU에서 다른 태스크로 강제 전환 불가 |
| `local_irq_disable()` | `local_irq_enable()` | **현재 CPU의 인터럽트(IRQ) 처리 금지** |

```c
/*
 * VMX root 동안 인터럽트/선점이 들어오면 커널이 즉시 죽을 수 있음.
 * 프로브만 하고 VMXOFF·CR 복구 후 IRQ를 다시 켠다.
 */
preempt_disable();
local_irq_disable();

/* ... VMXON → VMCLEAR → VMPTRLD ... */

local_irq_enable();
preempt_enable();
```

**`local_*`는 현재 논리 CPU만** 대상이다. 다른 CPU의 인터럽트·스케줄링은 그대로 동작한다.

---

## VMX 프로브에서의 위치

[`cr0_cr4.md`](cr0_cr4.md) 흐름 중 **VMX root 진입~복귀** 구간:

```
hb_vmxon_on_cpu()  (각 CPU, smp_call_function_single 콜백)
  → preempt_disable + local_irq_disable     ← 임계 구역 시작
  → CR0/CR4 저장
  → hb_adjust_vmx_cr0_cr4()
  → hb_enable_vmx()
  → VMXON → VMCLEAR → VMPTRLD
out_vmxoff:
  → VMXOFF
out_restore_cr:
  → VMXE OFF, CR0/CR4 복구
  → local_irq_enable + preempt_enable       ← 임계 구역 종료
```

프로브는 하이퍼바이저를 **상주**시키는 것이 아니라, VMX 하드웨어가 동작하는지만 **짧게 확인**한 뒤 VMXOFF·CR 복구로 일반 커널 상태로 되돌린다. 위 네 함수는 그 **짧은 VMX root 구간**을 “아무 것도 끼어들지 못하는 단일 실행 흐름”으로 만든다.

---

## 1. `preempt_disable()` / `preempt_enable()`

### 선점(preemption)이란

Linux 커널은 보통 **협력적 스케줄링**을 한다. 타이머 인터럽트나 `schedule()` 호출 시점에, 더 우선순위가 높은 태스크가 있으면 **현재 태스크를 멈추고 다른 태스크로 CPU를 넘긴다.** 이를 선점이라 한다.

- **`preempt_disable()`** — 이 CPU의 선점 카운터를 올려, 스케줄러가 **현재 태스크를 이 CPU에서 빼지 못하게** 한다.
- **`preempt_enable()`** — 카운터를 내리고, 필요하면 다시 선점을 허용한다.

### VMX 구간에 필요한 이유

`VMXON` 이후 CPU는 **VMX root 모드**에 들어간다. 이 상태에서는:

- CR0/CR4가 VMX용으로 조정되어 있음
- VMCS 포인터가 로드될 수 있음
- 일반 커널·드라이버 코드가 가정하는 CPU 상태와 **다름**

이 상태에서 **다른 태스크로 CPU가 넘어가면**, 새 태스크는 VMX root·변경된 CR을 모른다. 나중에 이 CPU로 돌아와도 상태가 꼬이거나, **패닉·이중 VMXON·잘못된 CR** 등으로 커널이 죽을 수 있다.

→ **“이 CPU에서 VMX 켜고 끄는 동안, 절대 다른 태스크로 넘어가지 않게”** 막는 것이 `preempt_disable()`이다.

---

## 2. `local_irq_disable()` / `local_irq_enable()`

### 인터럽트(IRQ)란

하드웨어·소프트웨어 이벤트(타이머, 디스크, 키보드, **IPI** 등)가 CPU 실행을 끊고 **ISR(인터럽트 서비스 루틴)** 을 실행하게 하는 것이다.

- **`local_irq_disable()`** — 현재 CPU의 인터럽트 플래그(IF)를 내려, **이 코어에서** IRQ 핸들러가 실행되지 않게 한다.
- **`local_irq_enable()`** — IRQ 처리를 다시 허용한다.

### VMX 구간에 필요한 이유

인터럽트가 들어오면 커널은 보통:

1. 현재 실행 중인 코드를 저장하고
2. 해당 IRQ 핸들러(또는 softirq, bottom half)를 실행하고
3. 원래 코드로 복귀

VMX root + 변경된 CR0/CR4 상태에서 IRQ 핸들러가 돌면:

- 핸들러는 **VMX root가 아닌 일반 커널 경로**를 가정
- 타이머 IRQ → 스케줄러 → **선점**으로 이어질 수 있음
- 다른 CPU의 **IPI**로 이 CPU 작업이 중간에 끼어들 수 있음

→ 주석대로 **“인터럽트/선점이 들어오면 커널이 즉시 죽을 수 있음”**. VMXON~VMXOFF 구간을 **원자적(atomic) 임계 구역**처럼 다루기 위해 IRQ를 막는다.

---

## 3. 왜 둘 다 쓰는가

역할이 겹치지 않고 **서로 다른 경로**를 막는다.

| | 선점만 막음 | IRQ만 막음 |
|---|-------------|------------|
| `preempt_disable()` | ✓ (직접) | △ (IRQ 경로 선점 일부 완화) |
| `local_irq_disable()` | △ (IRQ→스케줄 경로) | ✓ (직접) |

- **`preempt_disable()`만** — IRQ는 여전히 들어올 수 있다. 핸들러 안에서 문제가 생기거나, IRQ 안에서 `schedule()`이 호출되면 위험할 수 있다.
- **`local_irq_disable()`만** — IRQ로 인한 선점은 줄지만, voluntary schedule 등 **다른 경로의 선점**은 완전히 막지 못할 수 있다.

VMX처럼 **CPU 모드·특수 레지스터·물리 주소 기반 VMCS**를 건드리는 구간은 **둘 다** 막는 것이 일반적이다.

---

## 4. 복구 순서가 중요한 이유

[`vmx_error.md`](vmx_error.md) 에서 다룬 것처럼, **끄기만 하고 켜지 않으면** 해당 논리 CPU는 IRQ/preempt 꺼진 채 `smp_call_function_single` 콜백만 리턴한다.

| 누락 시 | 결과 |
|---------|------|
| `local_irq_enable()` 미호출 | 타이머·스케줄·IPI 불가 → **시스템 전체 정지**에 가깝게 보임 |
| `preempt_enable()` 미호출 | 선점 불가 상태 유지 → 스케줄링·IPI 처리 이상 |
| VMXOFF / CR 복구 누락 | VMX root·VMXE·CR이 남음 → 이후 커널 동작과 충돌 |

올바른 순서:

```
VMXOFF → VMXE OFF → CR0/CR4 복구 → local_irq_enable() → preempt_enable()
```

**VMX·CR을 먼저 일반 상태로 되돌린 뒤**, IRQ와 선점을 다시 켠다.

---

## 5. 다른 문서와의 관계

| 문서 | 이 문서와의 관계 |
|------|------------------|
| [`cr0_cr4.md`](cr0_cr4.md) | CR 정렬·VMXE는 **임계 구역 안**에서 수행 |
| [`vmx_error.md`](vmx_error.md) | 복구 누락 시 **OS 멈춤** 증상·해결 (실전 트러블슈팅) |
| [`msr.md`](msr.md) | VMXON **전** 플랫폼·BIOS 허용 여부 (MSR/CPUID) |

---

## 6. context switch와 CR0/CR4 — 태스크마다 복원되지 않는다

**질문:** context switch로 CPU 점유가 넘어가면, 프로브가 수정한 CR0/CR4도 다음 태스크에 적용되는가?

**답:** **그렇다.** 다만 “태스크 B의 CR0/CR4로 **교체**된다”가 아니라, **그 논리 CPU 레지스터에 남아 있는 수정값이 태스크 B 실행에도 그대로 적용**된다.

### CR0/CR4는 “태스크 상태”가 아니다

CR(Control Register)는 x86 CPU **하드웨어 레지스터**이다. `hb_adjust_vmx_cr0_cr4()`로 `mov cr0` / `mov cr4`를 하면 값은 **`task_struct`가 아니라 그 순간 그 CPU**에 기록된다.

| 구분 | 저장 위치 | context switch 시 |
|------|-----------|-------------------|
| 범용 레지스터, RSP, RIP, RFLAGS | `task_struct` 등 | 태스크마다 저장·복원 |
| **CR3** (페이지 테이블 베이스) | 태스크(주소공간)별 | **교체됨** |
| **CR0, CR4** | CPU 레지스터 (코어 전역) | **자동 복원 안 됨** |
| VMX root 모드, VMCS 포인터 | CPU 하드웨어 상태 | **자동 복원 안 됨** |

리눅스 커널은 context switch 때 **주소공간(CR3)** 위주로 바꾸고, CR0/CR4는 커널 전체가 공유하는 **코어 단위 설정**으로 다룬다. 태스크마다 CR0/CR4를 `task_struct`에 넣었다가 스케줄 시 복원하지 **않는다**.

### 선점이 일어나면 (preempt_disable 없을 때)

```
CPU 0:  프로브 태스크
          → saved_cr0/cr4 저장
          → hb_adjust_vmx_cr0_cr4()   ← CR0/CR4 VMX용으로 수정
          → VMXON (VMX root 진입)
          → (복구 전) context switch → 태스크 B

CPU 0:  태스크 B 실행
          → CR0/CR4:  VMX용으로 바뀐 값 그대로 (프로브가 넣은 상태)
          → VMX root: VMXOFF 전이면 root 상태도 그대로
          → CR3만:    태스크 B의 페이지 테이블로 교체
```

태스크 B는 자신의 CR0/CR4를 모른 채 **일반 커널 경로**로 실행된다. VMX용 CR 비트·VMX root·VMCS 상태와 맞지 않아 **패닉·#GP·이상 동작**이 날 수 있다.

→ §1의 **`preempt_disable()`** 이 “CPU 점유(실행 주체)만 바꾸고 CR/VMX 상태는 CPU에 남는” 상황을 막는 이유다.

### CR3 vs CR0/CR4 (헷갈리기 쉬운 부분)

| 레지스터 | 의미 | context switch |
|----------|------|----------------|
| **CR3** | “지금 어떤 주소공간(페이지 테이블)을 보나” | 태스크마다 **바뀜** |
| **CR0** | 페이징·보호 모드 등 CPU 동작 모드 | 코어에 **남음** |
| **CR4** | PAE, SMEP, **VMXE** 등 확장 기능 | 코어에 **남음** |

CPU 점유가 A → B로 넘어가도 **CR3만 B용으로 갈아끼우고**, CR0/CR4는 **그 CPU에 이미 깔려 있던 값**을 B도 그대로 쓴다.

### `hb_probe.c`가 CR을 직접 저장·복원하는 이유

커널이 context switch 때 CR0/CR4를 대신 복구해 주지 않으므로, 프로브가 직접 처리한다.

```c
saved_cr0 = hb_get_cr0();
saved_cr4 = hb_get_cr4();

hb_adjust_vmx_cr0_cr4();
hb_enable_vmx();
/* ... VMXON → VMCLEAR → VMPTRLD ... */

hb_disable_vmx();
hb_set_cr4(saved_cr4);
hb_set_cr0(saved_cr0);
```

| 단계 | 목적 |
|------|------|
| `saved_cr0/cr4` 저장 | VMX 진입 **전** 일반 커널 CR 값 보관 |
| `hb_adjust_vmx_cr0_cr4()` + `hb_enable_vmx()` | VMXON 요구에 맞게 **이 CPU** CR 수정 |
| `hb_set_cr0/cr4(saved_*)` | 프로브 종료 후 **같은 CPU**를 일반 커널 CR로 되돌림 |

복구를 생략하면, 그 CPU에서 이후 스케줄되는 **모든 태스크**가 VMX용 CR0/CR4 위에서 돌게 된다.

**한 줄 정리:** CR0/CR4는 per-task가 아니라 **per-CPU**이므로, 선점·IRQ 차단과 **수동 CR 복구**가 함께 필요하다.

---

**한 줄 정리 (전체):** 네 함수는 VMX root 구간을 **“인터럽트도, 태스크 전환도 없는 단일 실행 흐름”** 으로 만드는 잠금 장치이며, CR0/CR4는 context switch로 자동 복원되지 않으므로 프로브 후 **반드시** VMXOFF·CR 복구와 함께 되돌려야 한다.

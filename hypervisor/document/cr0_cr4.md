# CR0 / CR4 와 VMX 진입 전 정렬

`hb_probe.c`에서 `VMXON` 직전에 호출하는 **`hb_adjust_vmx_cr0_cr4()`**, **`hb_enable_vmx()`** 가 무엇을 하는지 정리한 문서이다.

관련 소스:

- [`hb_probe.c`](../hb_probe.c) — `hb_adjust_vmx_cr0_cr4()`, `hb_vmxon_on_cpu()`
- [`asm_helper.asm`](../asm_helper.asm) — `hb_get_cr0` / `hb_set_cr0`, `hb_enable_vmx`, `hb_start_vmx` (`VMXON`)
- 플랫폼·BIOS MSR: [`msr.md`](msr.md)

---

## CR0 / CR4가 하는 일

**CR(Control Register)** 는 x86 CPU의 **동작 모드·확장 기능**을 켜고 끄는 레지스터이다. 
MSR과 달리 **비트 플래그를 CPU에 직접** 반영한다 (`mov cr0`, `mov cr4`).

| 레지스터 | 역할 (요약) | 대표 비트 예 |
|----------|-------------|--------------|
| **CR0** | 기본 시스템 모드 | PE(보호 모드), PG(페이징), WP(쓰기 보호) |
| **CR4** | 확장 기능 | PAE, SMEP/SMAP, **VMXE(bit 13)** 등 |

리눅스 커널은 부팅 후 이미 CR0/CR4를 **일반 커널 실행**에 맞게 설정해 둔다.  
VMX는 그 위에 **“VMX root 모드로 들어가기 위한 추가 전제”**를 Intel이 정의해 두었고, probe는 그 전제를 맞춘 뒤 `VMXON`을 실행한다.

---

## VMX 준비 단계에서의 위치

[`msr.md`](msr.md) 로드맵의 **3번 이후(per-CPU)** 흐름:

```
hypervisor_init()
  → CPUID / IA32_FEATURE_CONTROL     (플랫폼·칩 VMX 허용)
  → VMXON·VMCS 영역 할당, revision 기록

hb_vmxon_on_cpu()  (각 CPU)
  → hb_adjust_vmx_cr0_cr4()   ← CR0/CR4를 FIXED MSR 규칙에 맞춤
  → hb_enable_vmx()           ← CR4.VMXE = 1
  → hb_start_vmx() / VMXON    ← VMX root 진입
  → VMCLEAR / VMPTRLD
```

| 단계 | 무엇을 확인·설정하는가 | 주체 |
|------|------------------------|------|
| CPUID, `IA32_FEATURE_CONTROL` | 이 머신/칩에서 VMX **허용**되는가 | 칩 + BIOS 정책 |
| `hb_adjust_vmx_cr0_cr4` | 이 **코어**의 CR0/CR4가 VMX **진입 형식**인가 | OS/모듈 (FIXED MSR 기준) |
| `hb_enable_vmx` | VMX **명령 사용** 가능 (VMXE) | OS/모듈 |
| `VMXON` | 실제 VMX root 모드 **진입** | CPU |

**정리:** MSR(`FEATURE_CONTROL`)은 “출입 허가”, CR0/CR4 정렬은 “트랙 진입 전 차량 상태 점검”, VMXE는 “VMX 회로 ON”, `VMXON`은 “트랙 진입”에 가깝다.

---

## 왜 CR0/CR4를 MSR에 맞추는가

`VMXON` 실행 시 CPU는 대략 다음을 검사한다.

1. 플랫폼이 VMX를 허용했는가 (`IA32_FEATURE_CONTROL` 등)
2. **CR4.VMXE = 1** 인가
3. **CR0·CR4가 이 CPU 모델이 요구하는 비트 패턴**인가

3번이 맞지 않으면 `VMXON`이 **실패**한다 (#GP 등).  
요구 패턴은 MSR 네 개에 **읽기 전용으로** 적혀 있으며, OS는 CR0/CR4를 **그 규칙에 맞게 수정**한 뒤 `VMXON`한다.

이유 (개념):

- VMX root / guest 전환 시 **페이징·보호·확장 비트**가 일관되어야 함
- VMX 동작 중 CR0/CR4 **일부 비트는 고정(fixed)** 되어 임의 변경 불가 → 진입 **전**에 허용된 조합으로 맞춤
- CPU 모델마다 요구 비트가 다를 수 있어 **고정값을 코드에 박지 않고** MSR을 `rdmsr`로 읽음

---

## FIXED0 / FIXED1 MSR

Intel VMX 관련 MSR (인덱스는 커널 헤더 `MSR_IA32_VMX_*` 참고):

| MSR | 대상 | 의미 |
|-----|------|------|
| `IA32_VMX_CR0_FIXED0` | CR0 | **반드시 1**이어야 하는 비트 마스크 |
| `IA32_VMX_CR0_FIXED1` | CR0 | **허용되는 1** 비트 마스크 (`&`용) |
| `IA32_VMX_CR4_FIXED0` | CR4 | **반드시 1**이어야 하는 비트 |
| `IA32_VMX_CR4_FIXED1` | CR4 | **허용되는 1** 비트 마스크 |

### 적용 공식 (Intel SDM)

```
CR0 = (현재 CR0 | FIXED0) & FIXED1
CR4 = (현재 CR4 | FIXED0) & FIXED1
```

| 연산 | MSR | 효과 (기억하기 쉬운 말) |
|------|-----|-------------------------|
| `\|=` | **FIXED0** | **do 1** — 1이어야 하는 비트 켜기 |
| `&=` | **FIXED1** | **do 0** — FIXED1이 **0인** 비트는 CR에서 **0으로** (1인 비트만 유지) |

`FIXED1`을 “0으로 만들 MSR”이라기보다 **“1로 남겨도 되는 비트만 남기는 AND 마스크”** 로 이해하는 것이 정확하다.

### 프로젝트 구현

```c
// hb_probe.c — hb_adjust_vmx_cr0_cr4()
cr0_fixed0 = hb_rdmsr(MSR_IA32_VMX_CR0_FIXED0);
cr0_fixed1 = hb_rdmsr(MSR_IA32_VMX_CR0_FIXED1);
cr0 = hb_get_cr0();
cr0 |= cr0_fixed0;
cr0 &= cr0_fixed1;
hb_set_cr0(cr0);
// CR4 동일
```

- MSR은 **읽기만** 하고, **쓰는 대상은 CR0/CR4** 이다.
- `hb_get_cr0` / `hb_set_cr0` 등은 [`asm_helper.asm`](../asm_helper.asm)의 `mov cr0, …` 래퍼.

---

## `hb_enable_vmx()` — CR4.VMXE

FIXED MSR과 **별도**로, CR4 **비트 13 (VMXE, VMX Enable)** 를 1로 설정한다.

```asm
; asm_helper.asm — hb_enable_vmx
mov rax, cr4
bts rax, 13      ; VMXE
mov cr4, rax
```

| | `hb_adjust_vmx_cr0_cr4` | `hb_enable_vmx` |
|--|-------------------------|-----------------|
| 대상 | CR0 전체 + CR4 비트 패턴 | CR4 bit 13만 |
| 근거 | `IA32_VMX_CR*_FIXED*` | VMX 명령 사용 조건 |
| 역할 | VMX **진입 전제** 비트 정렬 | VMX **기능 스위치** ON |

`hb_disable_vmx()`는 동일 비트를 끄며, `VMXOFF` 후 정리 시 사용한다.

---

## `hb_vmxon_on_cpu` 호출 순서

```c
*(u32 *)pc->vmxon_region = g_vmx_revision;
*(u32 *)pc->vmcs_region = g_vmx_revision;

hb_adjust_vmx_cr0_cr4();   // 1) FIXED MSR → CR0/CR4
hb_enable_vmx();           // 2) CR4.VMXE
ret = hb_start_vmx(&vmxon_pa);  // 3) VMXON
```

실패 시: `hb_disable_vmx()`, `g_vmx_probe_failed` 설정 등 — [`hb_probe.c`](../hb_probe.c) 참고.

---

## 자주 하는 혼동

| 질문 | 답 |
|------|-----|
| FIXED MSR에 CR 값을 쓰나? | **아니오.** MSR은 **규칙표**, CR0/CR4를 **고친다.** |
| 리눅스가 이미 CR을 켜 두었는데 또? | 대부분 이미 맞음. **부족한 비트만** OR/AND로 보정. |
| `FEATURE_CONTROL`과 같은 건가? | **아니오.** FEATURE는 **플랫폼 정책**, FIXED는 **코어 CR 패턴**. |
| VMXE만 켜면 `VMXON` 되나? | **아니오.** FIXED 정렬 + VMXE + VMXON 영역 등 **모두** 필요. |

---

## 참고

- Intel SDM Vol.3C: VMXON 전 CR0/CR4 조건, VMX fixed CR MSRs
- [`msr.md`](msr.md) — CPUID / `IA32_FEATURE_CONTROL` / VMX 로드맵
- [`vol_3c.md`](../../vol_3c.md) — SDM 요약 (프로젝트 내)

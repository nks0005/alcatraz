# VMXON 프로브 시 OS 멈춤 — 원인과 해결

`hypervisor_b` 모듈에서 `hb_start_vmx()` (`VMXON`) 주석을 풀었을 때 **전체 OS가 멈춘 것처럼** 보였던 현상과, 복구 경로를 넣은 뒤 **오류 없이 프로브가 통과**한 이유를 정리한 문서이다.

관련 소스:

- [`hb_probe.c`](../hb_probe.c) — `hb_vmxon_on_cpu()`, `hb_vmx_startup_all_cpus()`
- [`asm_helper.asm`](../asm_helper.asm) — `hb_start_vmx` (`vmxon`), `hb_stop_vmx` (`vmxoff`)
- CR0/CR4·VMXE: [`cr0_cr4.md`](cr0_cr4.md)
- BIOS·CPUID: [`msr.md`](msr.md)
- VirtualBox·nested: [`hyperV_VBox.md`](hyperV_VBox.md)

---

## 증상

| 상황 | 관측 |
|------|------|
| `hb_start_vmx(&vmxon_pa);` **만** 주석 해제 | `insmod` 직후 데스크톱·터미널 응답 없음, OS 전체 멈춤 |
| `hb_adjust_vmx_cr0_cr4()` + `hb_enable_vmx()` 까지는 실행 | VMXON 직전까지는 로그만 찍히고, 이후 멈춤으로 느껴짐 |
| 복구 블록(`out_vmxoff` ~ `out_restore_cr`)까지 활성화 + KVM 미로드 | `VMXON/VMCLEAR/VMPTRLD ok`, `VMXOFF before return`, `rmmod` 정상 |

**오해하기 쉬운 점:** `hb_start_vmx` 안의 `vmxon`은 **무한 루프가 아니다.** 실패 시 CF/ZF로 `-1`을 반환하고 `ret`한다. “VMXON 명령에서 영원히 대기”가 아니라, **그 전후의 커널 상태(특히 인터럽트)** 가 망가진 경우가 대부분이다.

---

## 원인 1 — 복구 경로 누락 (핵심)

`hb_vmxon_on_cpu()`는 VMX root 진입 **전에** 인터럽트와 선점을 끈다.

```c
preempt_disable();
local_irq_disable();
```

이 상태에서 `VMXON`만 실행하고 아래를 **실행하지 않으면** 해당 논리 CPU는 **영구히 IRQ/preempt 꺼진 채** `smp_call_function_single` 콜백만 리턴한다.

| 누락 시 후과 | 설명 |
|--------------|------|
| `local_irq_enable()` 미호출 | 타이머·스케줄·IPI 처리 불가 → **시스템 전체 정지**에 가깝게 보임 |
| `hb_stop_vmx()` / `hb_disable_vmx()` 미호출 | VMX root·VMXE가 남을 수 있음 (다음 커널 동작과 충돌) |
| `hb_set_cr0/cr4(saved_*)` 미호출 | VMX용으로 바꾼 CR이 복구되지 않음 |

### 잘못된 패턴 (멈춤)

```
preempt_disable + local_irq_disable
  → hb_adjust_vmx_cr0_cr4()
  → hb_enable_vmx()
  → hb_start_vmx()          ← 여기만 주석 해제
  → (out_vmxoff / out_restore_cr 전부 주석)
  → return                  ← IRQ 여전히 OFF
```

### 올바른 패턴 (프로브)

```
preempt_disable + local_irq_disable
  → CR 정렬, VMXE ON
  → VMXON → VMCLEAR → VMPTRLD
out_vmxoff:
  → VMXOFF (vmx_on 이었을 때만)
out_restore_cr:
  → VMXE OFF, CR 복구
  → local_irq_enable()
  → preempt_enable()
```

**정리:** 프로브는 “VMX root에 **잠깐** 들어갔다가 **반드시** 리눅스가 돌아가는 상태로 돌아오는” 코드여야 한다. `VMXON`만 있고 `VMXOFF`·IRQ 복구가 없으면 **OS 버그가 아니라 프로브 코드 버그**다.

---

## 원인 2 — KVM과 VT-x 충돌

`kvm_intel` / `kvm` 모듈이 로드된 상태에서는 같은 물리 코어에서 KVM이 이미 **VMXON**을 사용 중일 수 있다.

| 조건 | 결과 |
|------|------|
| KVM 로드 + 모듈에서 `VMXON` | 실패(CF=1) 또는 **#GP·패닉** (환경에 따라 멈춤처럼 보임) |
| 프로브 전 | `sudo modprobe -r kvm_intel kvm` |

`hb_probe.c`는 KVM 로드 시 `insmod` 전에 `-EBUSY`로 막고 안내 메시지를 낸다.

---

## 원인 3 — 플랫폼·BIOS·nested (부가)

VMXON 자체가 잘못된 경우(실패 반환·#GP)도 있으나, 위 **복구 누락**과 겹치면 “VMXON에서 멈춘다”고 오인하기 쉽다.

| 확인 | 기대 |
|------|------|
| CPUID.1:ECX bit 5 | VMX 지원 (`VMX support: 1`) |
| `IA32_FEATURE_CONTROL` | 예: `0x5` — Lock(bit0) + outside SMX VMXON(bit2) |
| 게스트 `grep vmx /proc/cpuinfo` | nested passthrough ([`hyperV_VBox.md`](hyperV_VBox.md)) |
| 호스트 VBox | `nested HW virtualization: yes` (해당 시) |

---

## 이번에 OS 오류 없이 된 이유 (성공 로그 기준)

다음이 **동시에** 만족되었을 때 `load_test.sh`가 통과했다.

1. **`out_vmxoff` ~ `out_restore_cr` 전체 활성화** — VMXOFF, CR·VMXE 복구, `local_irq_enable()`, `preempt_enable()`
2. **KVM 미로드** (또는 제거 후 테스트) — VT-x 단일 사용자 충돌 없음
3. **FEATURE_CONTROL·CPUID 통과** — BIOS/칩이 VMXON 허용
4. **nested VM에서 `vmx` 노출** — L1에서 `VMXON`이 L0에 의해 처리 가능한 환경

성공 시 커널 로그 예:

```text
hypervisor_b: VMX probe on cpu 0
hypervisor_b: cpu 0: VMXON/VMCLEAR/VMPTRLD ok (vmcs pa=0x........)
...
hypervisor_b: all 10 online cpus: VMXON/VMCLEAR/VMPTRLD probe ok (VMXOFF before return)
```

타임스탬프상 프로브 구간은 **수십 µs 수준**의 짧은 VMX root 진입이다.

---

## `hb_start_vmx` 동작 (참고)

[`asm_helper.asm`](../asm_helper.asm):

```asm
hb_start_vmx:
    vmxon [rdi]     ; RDI = VMXON 영역 PA가 들어 있는 변수의 주소
    jc .error
    jz .error
    xor rax, rax    ; 성공 → 0
    ret
.error:
    mov rax, -1
    ret
```

C 쪽 호출:

```c
vmxon_pa = (u64)__pa(pc->vmxon_region);
ret = hb_start_vmx(&vmxon_pa);
```

- `vmxon` 피연산자는 **물리 주소(PA)**. `&vmxon_pa`에 PA를 넣고 그 주소를 넘기는 방식이 맞다.
- 실패 시 `ret != 0` → `goto out_restore_cr`로 가서 **IRQ 복구까지** 가야 한다 (성공 경로와 동일하게 `out_restore_cr` 필수).

---

## 프로브 범위 (per-CPU)

`hb_vmx_startup_all_cpus()`는 `for_each_online_cpu`로 **온라인 CPU마다** `smp_call_function_single(cpu, hb_vmxon_on_cpu, …)`를 호출한다.  
각 CPU는 **자기 코어**에서만 VMXON/VMXOFF·복구를 수행해야 한다.

실패 시:

- `g_vmx_probe_failed` 설정 → `-EIO`
- `hypervisor_init`에서 `hb_vmx_teardown_all_cpus()` 호출 (안전용 `VMXOFF`, 각 CPU는 프로브 함수 안에서 대부분 이미 정리됨)

---

## 체크리스트 (멈춤 재현·해결)

| # | 확인 |
|---|------|
| 1 | `out_vmxoff` / `out_restore_cr`가 **주석 없이** 연결되어 있는가 |
| 2 | VMXON 실패 분기도 `goto out_restore_cr` 하는가 |
| 3 | 테스트 전 `lsmod \| grep kvm` — 필요 시 `modprobe -r kvm_intel kvm` |
| 4 | `dmesg`에 Oops/BUG 없는지 |
| 5 | VM이면 `/proc/cpuinfo`에 `vmx` 있는지 ([`hyperV_VBox.md`](hyperV_VBox.md)) |

---

## 자주 하는 질문

| 질문 | 답 |
|------|-----|
| `VMXON`만 켜면 왜 OS 전체가 멈추나? | 프로브 CPU에서 **IRQ가 꺼진 채 복구 안 됨** → 스케줄·I/O 마비 |
| `VMXON`이 성공해도 멈출 수 있나? | **복구 없으면 예.** 성공 여부와 무관하게 `local_irq_enable()` 필요 |
| `hb_start_vmx`가 버그인가? | 아니오. **호출 전후(특히 복구) C 경로**가 문제였음 |
| 프로브 성공 = 하이퍼바이저 완성? | 아니오. **짧은 VMX 진입·퇴장 검증**만 한 것 |

---

## 참고

- Intel SDM Vol.3C: `VMXON`, `VMXOFF`, VMXON 영역, 인터럽트와 VMX root
- [`cr0_cr4.md`](cr0_cr4.md) — FIXED MSR, VMXE, 호출 순서
- [`msr.md`](msr.md) — `IA32_FEATURE_CONTROL`

# VMCS 세팅 요약 (hb_probe → VMLAUNCH)

`VMPTRLD`로 **current VMCS**를 정한 뒤, **`VMWRITE`로 필드를 채운 다음** `VMLAUNCH`(또는 `VMRESUME`)한다.  
메모리에 구조체를 직접 채우지 않고, **명령 `VMWRITE`** 가 정식 경로다.

관련 문서:

| 단계 | 문서 |
|------|------|
| VMXON / VMCLEAR / VMPTRLD | [`../vmxon_clear_load.md`](../vmxon_clear_load.md) |
| VMWRITE 비교 (Hyper-box · HyperDbg) | [`../vmwrite_vmcs.md`](../vmwrite_vmcs.md) |
| VMLAUNCH / VMRESUME | [`../load_write_launch.md`](../load_write_launch.md) |

---

## Intel SDM 6영역 vs 이 폴더 문서

| SDM 영역 | VMLAUNCH 전 VMWRITE? | 문서 |
|----------|----------------------|------|
| Guest state | ✅ | [`guest.md`](guest.md) · 세그먼트 [`segment.md`](segment.md) · VMCS [`guest-segments-why.md`](guest-segments-why.md) |
| Host state | ✅ | [`host.md`](host.md) |
| VM-execution control | ✅ | [`control-execution.md`](control-execution.md) |
| VM-exit control | ✅ | [`control-exit.md`](control-exit.md) |
| VM-entry control | ✅ | [`control-entry.md`](control-entry.md) |
| VM-exit information | ❌ (CPU가 exit 시 기록) | [`exit-information.md`](exit-information.md) |

코드에서 말하는 **「control」** 은 보통 execution + exit + entry **세 가지를 합친 말**이다. 별도 “6번째 세팅 파일”은 없다.

---

## VMWRITE 밖에서 하는 것 (필수 전제)

| 항목 | 시점 | 방법 | 문서 |
|------|------|------|------|
| VMCS **revision** | `VMCLEAR` **전** | `*(u32 *)vmcs_region = revision` | [`revision.md`](revision.md) |
| **VMCLEAR** / **VMPTRLD** | VMWRITE 전 | `hb_clear_vmcs` / `hb_load_vmcs` | [`../vmxon_clear_load.md`](../vmxon_clear_load.md) |
| Guest **RIP/RSP** (재조정) | `VMLAUNCH` 직전 | asm `vmwrite` (선택) | [`launch-adjust.md`](launch-adjust.md) |

---

## hb_probe 단계별 — 무엇을 세팅할까

| 단계 | 목표 | 세팅 범위 | 문서 |
|------|------|-----------|------|
| **0 (현재)** | VMWRITE·VMREAD 동작 확인 | `VM_VMCS_LINK_PTR` 만 | [`guest.md`](guest.md#vmcs-link-pointer) |
| **1** | 최소 `VMLAUNCH` (커널 1코어 프로브) | guest/host **필수** + control **최소** (bitmap/EPT 없음) | 아래 [단계 1 체크리스트](#단계-1-최소-vmlaunch) |
| **2** | 상주 하이퍼바이저 (Hyper-box급) | 전 필드 + IO/MSR bitmap, EPT, nested 등 | [`../../Source/hyper_box/hypervisor.c`](../../../Source/hyper_box/hypervisor.c) `hb_setup_vmcs` |

---

## 권장 VMWRITE 순서 (단계 1 이상)

Hyper-box `hb_setup_vmcs()`와 동일한 순서를 따르면 디버깅이 쉽다.

```text
1. Host state          → host.md
2. Guest state         → guest.md  (link pointer 포함)
3. VM-execution ctrl   → control-execution.md
4. VM-exit ctrl        → control-exit.md
5. VM-entry ctrl       → control-entry.md
6. (선택) launch 보정  → launch-adjust.md  — guest RIP/RSP, host RIP/RSP
7. VMLAUNCH
```

**exit information** (`VM_DATA_*`)은 7번 **이후** VM exit 때 CPU가 채운다 → [`exit-information.md`](exit-information.md).

---

## 단계 1: 최소 VMLAUNCH

EPT·nested·IO/MSR bitmap **없이** “게스트로 한 번 들어가기”만 할 때.

### Host (필수)

| 필드 | 이유 |
|------|------|
| `VM_HOST_CR0/3/4` | exit 후 호스트 페이징·보호 |
| `VM_HOST_RSP` | exit handler 스택 |
| `VM_HOST_RIP` | **VM exit handler** 주소 |
| `VM_HOST_CS/SS` selector | 호스트 코드·스택 세그먼트 |
| `VM_HOST_TR` selector + `VM_HOST_TR_BASE` | TSS |
| `VM_HOST_GDTR_BASE` / `VM_HOST_IDTR_BASE` | 디스크립터 테이블 |

→ 상세: [`host.md`](host.md)

### Guest (필수)

| 필드 | 이유 |
|------|------|
| `VM_GUEST_CR0/3/4` | 게스트 페이징 |
| `VM_GUEST_RSP` / `VM_GUEST_RIP` | 진입 지점 |
| `VM_GUEST_RFLAGS` | 플래그 |
| 세그먼트 selector + base/limit/access (CS~TR, GDTR/IDTR) | 보호 모드 일관성 |
| `VM_VMCS_LINK_PTR` | `0xFFFFFFFFFFFFFFFF` (섀도 없음) |

→ 상세: [`guest.md`](guest.md)

### Control (필수)

| 필드 | 이유 |
|------|------|
| `VM_CTRL_PIN_BASED_VM_EXE_CTRL` | MSR cap에 맞춘 값 |
| `VM_CTRL_PRI_PROC_BASED_EXE_CTRL` | **IO/MSR bitmap 비트 끔** |
| `VM_CTRL_SEC_PROC_BASED_EXE_CTRL` | EPT·shadow **비트 끔** |
| `VM_CTRL_VM_EXIT_CTRLS` | 64비트 호스트 등 |
| `VM_CTRL_VM_ENTRY_CTRLS` | IA-32e guest 진입 등 |
| `VM_CTRL_CR0/CR4` mask·shadow | 0 또는 최소 (hyper_box는 WP/VMXE 등) |

→ 상세: [`control-execution.md`](control-execution.md), [`control-exit.md`](control-exit.md), [`control-entry.md`](control-entry.md)

### 단계 1에서 생략 가능 (비트를 안 켰을 때)

- IO bitmap A/B 주소
- MSR bitmap 주소
- EPT pointer, VPID, VMREAD/VMWRITE bitmap
- Virtual APIC page
- Guest preemption timer (pin-based timer 비트 off)

---

## 단계 2: Hyper-box / Alcatraz에서 추가되는 것

| 추가 항목 | 문서 |
|-----------|------|
| IO / MSR / VMREAD / VMWRITE bitmap (4KB×N) | [`control-execution.md`](control-execution.md) |
| EPT pointer, VPID | [`control-execution.md`](control-execution.md) |
| `VM_VMCS_LINK_PTR` = nested/shadow PA | [`guest.md`](guest.md#vmcs-link-pointer) |
| CR0/CR4 guest-host mask (WP, VMXE…) | [`control-execution.md`](control-execution.md) |
| 런타임 `VMWRITE` (exit 핸들러, nested 에뮬) | [`../vmwrite_vmcs.md`](../vmwrite_vmcs.md) |

---

## 파일 목록

| 파일 | 내용 |
|------|------|
| [`setting.md`](setting.md) | 본 요약·단계·순서 |
| [`revision.md`](revision.md) | VMCS revision (VMWRITE 아님) |
| [`host.md`](host.md) | Host state 필드 |
| [`guest.md`](guest.md) | Guest state 필드 |
| [`control-execution.md`](control-execution.md) | Pin / proc / bitmap / mask |
| [`control-exit.md`](control-exit.md) | VM-exit control |
| [`control-entry.md`](control-entry.md) | VM-entry control |
| [`exit-information.md`](exit-information.md) | Exit 시 CPU 기록 (launch 전 미설정) |
| [`launch-adjust.md`](launch-adjust.md) | VMLAUNCH 직전 RIP/RSP 보정 |

---

## 구현 시 참고 코드

| 목적 | 경로 |
|------|------|
| hb_probe (프로브) | [`../../hb_probe.c`](../../hb_probe.c) |
| 값 수집 | `hb_setup_vm_host_register`, `hb_setup_vm_guest_register`, `hb_setup_vm_control_register` in [`hypervisor.c`](../../../Source/hyper_box/hypervisor.c) |
| VMWRITE 일괄 | `hb_setup_vmcs()` 동일 파일 |
| 필드 encoding | [`hyper_box.h`](../../../Source/hyper_box/hyper_box.h) `VM_HOST_*`, `VM_GUEST_*`, `VM_CTRL_*` |

---

## 자주 하는 질문

| 질문 | 답 |
|------|-----|
| host/guest/control 말고 또 세팅할 영역? | **없음.** exit/entry control은 control 문서에 포함. **exit information만** launch 전 제외. |
| hb_probe 지금은? | **단계 0** — link pointer만. |
| control 비트 vs 필드? | **비트를 켠 기능**에 대응하는 주소·필드만 VMWRITE (예: IO bitmap 비트 on → A/B 주소 필수). |

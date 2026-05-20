# MSR (Model Specific Register)

## 약자와 정의

**MSR** = **Model Specific Register** (모델별 레지스터)

Intel x86 CPU 내부에 있는 **CPU 전용 설정·상태 레지스터**이다. 각 MSR은 **인덱스(주소)** 로 구분하며(예: `IA32_FEATURE_CONTROL` = `0x3A`), 64비트 값을 담는다.

- **접근 명령:** `RDMSR`(읽기), `WRMSR`(쓰기)
- **일반적인 호출 경로:** 커널/하이퍼바이저에서 `rdmsr` / `wrmsr` (또는 래퍼 `read_msr` / `hb_rdmsr` 등)

MSR은 **BIOS 화면에 표시되는 항목이 아니다.** POST/UEFI 메뉴에 MSR 목록이 뜨지 않으며, CPU 하드웨어 상태로만 존재한다.

---

## 누가, 언제 MSR을 다루는가

| 주체 | 시점 | 예 |
|------|------|-----|
| **BIOS/UEFI** | 부팅 초기 | `IA32_FEATURE_CONTROL`에 VMX 허용 비트 설정 후 **Lock** |
| **OS/커널** | 부팅 이후 | 정책 MSR **읽기**, 성능·전력·일부 기능 MSR 읽기/쓰기 |
| **하이퍼바이저/VMM** | VMX 준비·동작 중 | `IA32_VMX_*` 등 VMX capability MSR, VMX 관련 설정 |

**정리:** MSR 전체가 “BIOS 전용”은 아니다. 다만 **특정 MSR**(아래 `IA32_FEATURE_CONTROL`)은 BIOS가 부팅 시 값을 박아 두고 잠그는 **플랫폼 정책** 용도로 쓰인다.

---

## CPUID vs MSR (VMX probe 관점)

VMX를 쓰기 전에 확인하는 두 단계는 **서로 다른 질문**에 답한다.

| 단계 | 방법 | 질문 | 설정 주체 |
|------|------|------|-----------|
| 1 | **CPUID** (leaf 1, ECX bit 5) | 이 **CPU 칩**이 VMX를 **구현**했는가? | CPU 하드웨어 (고정 스펙) |
| 2 | **MSR** `IA32_FEATURE_CONTROL` | 이 **머신**에서 VMXON이 **허용**되는가? | BIOS/펌웨어 (부팅 시 기록) |

- CPUID만 통과 → “칩은 VT-x 가능”
- MSR까지 통과 → “BIOS가 VMX 사용을 열어 둠” (또는 아직 lock 전이라 OS가 설정할 여지가 있을 수 있음)

프로젝트 probe 구현: [`hb_probe.c`](../hb_probe.c)

```
hypervisor_init()
  → is_vmx_supported()      // CPUID
  → is_bios_vmx_allowed()   // IA32_FEATURE_CONTROL MSR
```

Intel SDM 요약: [`vol_3c.md`](../../vol_3c.md) 25.6절(CPUID), 25.7절(`IA32_FEATURE_CONTROL` + VMXON)

---

## `IA32_FEATURE_CONTROL` (hb_probe에서 쓰는 MSR)

| 항목 | 값 |
|------|-----|
| 이름 | `IA32_FEATURE_CONTROL` (Linux 5.6+: `MSR_IA32_FEAT_CTL`) |
| 주소 | `0x3A` |
| 리셋 | 논리 프로세서 리셋 시 **0** |
| BIOS 역할 | UEFI “Intel Virtualization Technology” 등과 연동해 VMX 허용/차단 후 **Lock** 하는 경우가 일반적 |

### 관련 비트 (`hb_probe.c` 기준)

| 비트 | 이름 | 의미 |
|------|------|------|
| 0 | **Lock** | 1이면 이 MSR을 **WRMSR로 더 이상 수정 불가** (전원 리셋 전까지) |
| 2 | **VMXON outside SMX** | SMX 동작 **밖**에서 `VMXON` 허용 (일반 OS 하이퍼바이저가 쓰는 경로) |

(비트 1은 SMX 동작 안에서의 VMXON 허용. 일반 Linux VMM 경로는 비트 2가 중요하다.)

### `is_bios_vmx_allowed()` 판정 로직

```c
msr = hb_rdmsr(HB_MSR_FEATURE_CTL);

if (msr & LOCK) {
    if (!(msr & VMXON_ENABLED_OUTSIDE_SMX))
        return false;   // BIOS가 lock + VMX 차단
}
return true;
```

| MSR 상태 | probe 결과 | 해석 |
|----------|------------|------|
| Lock=1, outside SMX=0 | 실패 | BIOS에서 VT-x 꺼짐 또는 정책상 VMX 차단 후 고정 |
| Lock=1, outside SMX=1 | 통과 | BIOS가 VMX 허용 후 잠금 (일반적인 “VT-x 켬”) |
| Lock=0 | 통과 (현재 코드) | 아직 lock 안 됨 → 이론상 OS가 설정 가능 (실제 배포 환경은 대부분 lock 됨) |

**참고 (Intel SDM):** Lock이 **0**인 상태에서 `VMXON`을 실행하면 **#GP**가 날 수 있다. 즉 실제 VMXON 전에는 BIOS가 Lock + 허용 비트를 설정하는 것이 정상 경로이다. probe는 “lock 되었는데 막혀 있는지”를 가장 확실히 걸러낸다.

---

## “MSR 읽기 = BIOS 체크?”

| 말 | 맞는지 |
|----|--------|
| MSR 읽기 = BIOS API 호출 | **아님** |
| `IA32_FEATURE_CONTROL` RDMSR = BIOS가 부팅 시 남긴 **정책 스냅샷** 확인 | **맞음** |
| 모든 MSR이 BIOS 설정 | **아님** (VMX capability, 성능 카운터 등은 OS/VMM이 사용) |

함수명 `is_bios_vmx_allowed()`는 **BIOS 메뉴를 여는 것**이 아니라, **BIOS가 FEATURE_CONTROL에 기록해 둔 값을 OS가 나중에 읽는 것**을 의미한다.

---

## VMX 준비 시 MSR이 나오는 위치 (로드맵)

`hb_probe`는 아래 전체 중 **앞의 두 가지**만 검사한다.

1. **CPUID** — VMX 지원 여부
2. **`IA32_FEATURE_CONTROL`** — BIOS/플랫폼 VMX 허용
3. VMXON 영역 준비, `CR0`/`CR4` 고정 MSR 정렬, `CR4.VMXE`, `VMXON` … (본격 VMM 단계)

VMX capability 보고용 MSR (`IA32_VMX_BASIC` 등)은 **CPUID/FEATURE_CONTROL 다음** 단계에서 VMCS·VMXON 영역 크기 등을 알 때 사용한다.

---

## 참고

- Intel SDM Vol.3C 25.6–25.7: [`vol_3c.md`](../../vol_3c.md)
- Probe 소스: [`hb_probe.c`](../hb_probe.c)
- MSR 읽기 래퍼: [`asm_helper.h`](../asm_helper.h) — `hb_rdmsr()`

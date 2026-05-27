# x86 세그먼트 (Segment) — 기초 정리

> **처음이면 [쉬운 설명](#쉬운-설명-먼저-읽기)만 읽고 끝내도 됩니다.**  
> 아래 §1~§13 은 용어·코드 대응용 **참고**입니다.

**다음 단계 (VMCS):** [`guest-segments-why.md`](guest-segments-why.md) (짧음)  
**필드 표:** [`guest.md`](guest.md)

---

## 쉬운 설명 (먼저 읽기)

### 한 문장

**세그먼트 = CPU가 “지금 이 코드/스택이 몇 층(ring) 권한이냐”를 적어 두는 출입 시스템**이다.  
주소는 **페이징(CR3)** 이 담당하고, 세그먼트는 **권한·검사** 쪽에 가깝다.

---

### 비유: 도서관

```text
  CS, SS …        =  내가 들고 있는 「회원 카드 번호」(selector)
  GDT             =  서버실 「회원 DB」(번호 → 상세 정보)
  VMCS guest      =  하이퍼바이저가 「게스트용으로 복사해 둔 명부」
```

| 비유 | CPU |
|------|-----|
| 카드 번호 | **Selector** (`0x0010` 같은 값) |
| DB 한 줄 (이름·등급·유효기간) | **GDT 디스크립터** (base, limit, 권한…) |
| 게스트 명부 4칸 | VMCS: **selector + base + limit + access** |

CPU는 카드 번호만 보고 실행하지 않는다. **DB 한 줄과 명부 4칸이 서로 맞아야** `VMLAUNCH`가 통과한다.

---

### 기억할 것 3개만

1. **Selector** — `CS`, `SS` … 지금 들고 있는 **번호** (`hb_get_cs()`)
2. **GDT** — 번호로 찾는 **원본 정보** (메모리 테이블, `GDTR`이 위치를 가리킴)
3. **VMCS** — 게스트용으로 **번호 + 원본 내용을 4줄씩 복사** (`VMWRITE`)

```text
  지금 커널이 쓰는 번호·DB 내용
           ↓  그대로 복사
  Guest VMCS (VMLAUNCH 후 게스트가 “처음부터” 갖는 값)
```

**Host VMCS와 헷갈리지 말 것:**  
- **Guest** = 게스트 CPU 명부  
- **Host** = VM exit 후 하이퍼바이저가 도는 쪽 (다른 영역)

---

### 왜 “구조가 어렵게” 느껴지나

세그먼트 **레지스터가 8개**이고, VMCS마다 **4가지**를 적어서 **8 × 4 = 32개** 필드가 생긴다.

| 8개 이름 | 대략 하는 일 (쉽게) |
|----------|---------------------|
| CS | 코드, **지금 ring** |
| SS | 스택 |
| DS, ES | 데이터 (64비트에선 거의 flat) |
| FS, GS | 스레드/per-CPU 포인터 (**베이스는 MSR**) |
| LDTR | 거의 안 씀 (0) |
| TR | 커널 TSS (꼭 있음) |

**처음엔 CS·SS·TR만 “권한·스택·커널 TSS”** 로 이해해도 된다. DS/ES는 flat, LDTR은 0.

---

### hb_probe가 하는 일 (코드 한 줄 요약)

```text
① 번호 8개 읽기     (hb_get_cs … hb_get_tr)
② GDT/MSR에서 상세   (base, limit, 권한)
③ VMCS에 32필드 쓰기 (hb_probe_vmwrite_guest_state)
④ 나중에 RIP/RSP만 따로 맞춤 → VMLAUNCH
```

**우리가 직접 “설계”하는 게 아니라**, 이미 돌아가는 **Linux 커널 상태를 복사**하는 것이다.  
그래서 어려운 건 “이론 전체”가 아니라 **복사 형식이 왜 4칸인지** 정도만 알면 된다.

---

### 꼭 외울 필요 없는 것

- 32비트 far jump, LDT 설계
- limit/granularity 계산
- 세그먼트로 주소 공간 나누기 → **CR3 + EPT** 가 담당

---

### 조금만 더 알면 좋은 것 (2개)

**① GDT 찾을 때 `selector & ~3`**  
카드 번호 **맨 끝 2비트**는 “몇 층이냐” 표시라서, DB 칸 번호 계산할 땐 **떼고** 쓴다.

**② LDTR=0, TR=있음**  
- LDTR 없음 → VMCS에 “없음” 표시 (`access = 0x10000`)  
- TR 없음 → 다른 방식 (`access = 0`) — **이름만 비슷하고 규칙이 다름**

---

### 다음에 읽을 문서

| 읽을 것 | 길이 | 내용 |
|---------|------|------|
| [`guest-segments-why.md`](guest-segments-why.md) | 짧음 | VMCS에 **왜** 4칸씩 넣는지, 미러, 체크리스트 |
| 아래 §1~§13 | 참고 | 용어·비트·코드 줄 대응 |

---

## 읽는 순서 (자세한 참고)

| 순서 | 주제 | 이 문서 섹션 |
|------|------|--------------|
| 1 | 세그먼트가 뭔지 | [§1](#1-세그먼트란) |
| 2 | Selector 16비트 | [§2](#2-selector) |
| 3 | GDT · LDT · 디스크립터 | [§3](#3-gdt--ldt--디스크립터) |
| 4 | 세그먼트 레지스터 8개 | [§4](#4-세그먼트-레지스터-8개) |
| 5 | Access Rights · CPL · DPL | [§5](#5-access-rights--cpl--dpl) |
| 6 | 롱 모드 (64비트) | [§6](#6-롱-모드-64비트) |
| 7 | FS/GS · MSR base | [§7](#7-fsgss--msr-base) |
| 8 | LDTR · TR · TSS | [§8](#8-ldtr--tr--tss) |
| 9 | GDTR · IDT | [§9](#9-gdtr--idt) |
| 10 | GDT 오프셋 계산 (`& ~3`) | [§10](#10-gdt-오프셋-계산) |
| 11 | hb_probe 코드 매핑 | [§11](#11-hb_probe-코드-매핑) |

---

## 1. 세그먼트란

**CPU가 코드·스택·데이터를 “어떤 권한(ring)”으로 다룰지 정하는 x86 메커니즘**이다.

| 비유 | x86 |
|------|-----|
| 건물 층 (권한) | **CPL** — Current Privilege Level, ring 0~3 |
| 출입증 번호 | **Selector** — `CS`, `SS`, `DS` … 레지스터 값 |
| 출입증 원본 DB | **GDT** — Global Descriptor Table (`GDTR`이 가리킴) |

### 페이징과의 관계

- **주소 공간의 주인:** 페이징 (`CR3` → 페이지 테이블).
- **세그먼트:** 여전히 CPU가 검사. 특히 **CPL**, **스택 세그먼트**, **TSS**, **VM entry 검사**에 필요.
- 롱 모드에서 데이터 주소는 대부분 **flat** (base≈0, limit≈전체)이지만, **세그먼트 레지스터·GDT는 사라지지 않는다.**

### 하이퍼바이저에서 왜 배워야 하나

VT-x guest state에는 세그먼트마다 **selector + base + limit + access rights** 가 VMCS에 들어간다.  
GDT를 읽어 VMWRITE 하는 이유는 **CPU가 entry 시 이 4종 세트의 일관성을 검사**하기 때문이다.  
→ VMCS 쪽 “왜”: [`guest-segments-why.md`](guest-segments-why.md)

---

## 2. Selector

세그먼트 **selector**는 16비트. 예: Linux 커널 `CS` = `0x0010`.

```text
 15      3 2 1 0
┌─────────┬─┬───┐
│  Index  │T│RPL│
└─────────┴─┴───┘
```

| 필드 | 비트 | 이름 | 의미 |
|------|------|------|------|
| RPL | 0–1 | Requested Privilege Level | 요청 권한 (0=ring0, 3=ring3) |
| TI | 2 | Table Indicator | 0 = **GDT**, 1 = **LDT** (`LDTR`가 가리키는 테이블) |
| Index | 3–15 | Descriptor index | GDT/LDT 안 **몇 번째 항목** (바이트 오프셋 = Index × 8) |

### CPU 동작

Selector만으로 실행하지 않는다. CPU는:

1. TI로 GDT vs LDT 선택
2. Index로 디스크립터 위치 계산
3. 디스크립터에서 **base, limit, type, DPL, present** 등을 읽어 **내부 캐시**에 적재

### 코드에서 읽기

```c
gst->cs_sel = hb_get_cs();   /* 현재 CS selector */
gst->ss_sel = hb_get_ss();
/* … ES, CS, SS, DS, FS, GS, LDTR, TR */
```

---

## 3. GDT · LDT · 디스크립터

### GDT (Global Descriptor Table)

| 항목 | 설명 |
|------|------|
| **GDTR** | `{ base, limit }` — GDT가 메모리 어디에 있는지 |
| **내용** | 8바이트(또는 TSS 16바이트) **디스크립터** 배열 |
| **포함** | 커널 코드/데이터, TSS, LDT 게이트, (옛날) 게이트 등 |

### LDT (Local Descriptor Table)

| 항목 | 설명 |
|------|------|
| **LDTR** | LDT를 가리키는 selector |
| **Linux** | 보통 **LDTR = 0** (LDT 미사용) |
| **TI=1** | selector의 TI 비트가 1이면 LDT 쪽 디스크립터 참조 |

### 일반 디스크립터 (8바이트) — 개념

Linux `struct desc_struct` 와 대응. CPU·VMCS에서 쓰는 대표 필드:

| 필드 | 의미 |
|------|------|
| **Base** | 세그먼트 시작 선형 주소 |
| **Limit** | 세그먼트 크기 (granularity에 따라 ×4K 가능) |
| **Type** | 코드 / 데이터 / … |
| **S** | 시스템(0) vs 코드/데이터(1) |
| **DPL** | Descriptor Privilege Level — 이 세그먼트의 권한 |
| **P** | Present — 1이면 유효 |

### 64비트 LDT/TSS 디스크립터 (16바이트)

TSS·LDT segment gate는 **base3**, **limit** 조합이 일반 8바이트와 다르다.  
Hyper-box / hb_probe: `LDTTSS_DESC64` / `hb_ldttss_desc64`.

```c
struct hb_ldttss_desc64 {
    u16 limit0, base0;
    u8  base1, limit1, limit2, base2;
    u32 base3;
    u32 reserved;
};
```

---

## 4. 세그먼트 레지스터 8개

x86은 **8개** 세그먼트 레지스터를 VMCS guest에도 그대로 요구한다.

| 레지 | 용도 | 롱 모드에서 |
|------|------|-------------|
| **CS** | Code Segment — **CPL(현재 링)** | 필수. 코드 fetch·권한 |
| **SS** | Stack Segment | 필수. RPL은 보통 CS와 동일 |
| **DS** | Data | flat, base≈0 |
| **ES** | Extra Data | flat, base≈0 |
| **FS** | Extra | **MSR base** — thread-local 등 |
| **GS** | Extra | **MSR base** — per-CPU (`current` 등) |
| **LDTR** | LDT selector | 커널: 보통 0 |
| **TR** | Task State Segment (TSS) | ring 전환·일부 예외·컨텍스트 |

```text
CS, SS, DS, ES  —  코드·스택·데이터 (GDT)
FS, GS          —  MSR로 base (GDT selector는 있음)
LDTR            —  LDT (거의 미사용)
TR              —  TSS (커널 필수)
```

---

## 5. Access Rights · CPL · DPL

### CPL / DPL / RPL

| 용어 | 의미 |
|------|------|
| **CPL** | Current Privilege Level — **지금 실행 중인 코드의 링** (CS DPL에서 유래) |
| **DPL** | Descriptor Privilege Level — 디스크립터에 적힌 최소 권한 |
| **RPL** | Selector 하위 2비트 — 요청 시 희망 권한 |

게스트 ring0 커널을 미러하면 CS DPL=0, SS RPL=0 같은 조합이 VMCS에 들어간다.

### VMCS Guest Segment Access Rights

GDT access byte를 VMCS 형식으로 넣는다. hb_probe / Hyper-box 추출:

```c
access = (*((u32 *)gdt + 1)) >> 8;
access &= 0xF0FF;
```

### invalid / unused 인코딩 (VMCS)

| 값 | 의미 | 쓰는 곳 |
|----|------|---------|
| **`0x10000`** | bit16=1 → **unusable / invalid** | LDTR selector == 0 |
| **`0`** | TR 미사용 (Hyper-box) | TR selector == 0 |

LDTR와 TR은 “없음” 표기가 **다르다** — Intel VM entry 검사 규칙.

---

## 6. 롱 모드 (64비트)

| 항목 | 32비트 protected | Long mode |
|------|------------------|-----------|
| 데이터 주소 | base+offset, limit 검사 | **flat** — base 무시에 가깝, 64비트 주소 |
| CS | 세그먼트 base+limit | **L bit** (64-bit code), DPL 여전히 중요 |
| limit | 의미 있음 | Hyper-box/hb_probe: CS~GS **0xFFFFFFFF** 고정 관례 |
| 세그먼트 | 필수 | **사라지지 않음** — VMCS·CPL·TSS 때문에 |

**정리:** 32비트 세그mented programming 전체를 외울 필요는 없다.  
**selector → GDT → base/limit/AR**, **FS/GS MSR**, **LDTR/TR 예외** 정도면 된다.

---

## 7. FS/GS · MSR base

64비트에서 **FS/GS의 실제 베이스 주소는 MSR이 정본**이다.

| MSR | 용도 (Linux 예) |
|-----|-----------------|
| `IA32_FS_BASE` | user thread-local storage |
| `IA32_GS_BASE` | 커널 per-CPU 영역 (`current` 등) |

GDT 디스크립터의 base는 0인 경우가 많다. VMCS `VM_GUEST_FS/GS_BASE` 에 **MSR 값**을 넣어야 게스트 컨텍스트가 맞다.

```c
gst->fs_base = hb_rdmsr(MSR_FS_BASE);
gst->gs_base = hb_rdmsr(MSR_GS_BASE);
gst->fs_ar   = hb_probe_gdt_desc_access(gst->fs_sel);  /* AR은 GDT에서 */
```

---

## 8. LDTR · TR · TSS

### LDTR

- LDT segment를 가리키는 selector.
- Linux 커널: **보통 0** (LDT 없음).
- VMCS: selector=0 → base/limit=0, **AR=0x10000** (invalid).

### TR (Task Register)

- **TSS** (Task State Segment) 디스크립터를 가리킴.
- ring 전환, double fault stack, 일부 컨텍스트에 사용.
- Linux: **TR ≠ 0** (커널 TSS 사용).
- VMCS: GDT에서 **base0~base3, limit, access** 전부 파싱 (`hb_probe_ldtr_tr_fields`).

| 케이스 | base | limit | AR |
|--------|------|-------|-----|
| LDTR == 0 | 0 | 0 | **0x10000** |
| TR == 0 | 0 | 0 | **0** |
| 사용 중 | GDT `base0~3` | GDT limit | `access & 0xF0FF` |

일반 `desc_struct` 만으로 TSS **base3·limit** 을 놓치기 쉬워 전용 구조체로 파싱한다.

---

## 9. GDTR · IDT

세그먼트 selector는 GDT **안의 한 칸**만 가리킨다. **테이블 전체 위치**는 별도.

| 레지스터 | 테이블 | 역할 |
|----------|--------|------|
| **GDTR** | GDT | 모든 전역 디스크립터 |
| **IDTR** | IDT | 인터럽트·예외 핸들러 벡터 |

VMCS guest에도 `VM_GUEST_GDTR_*`, `VM_GUEST_IDTR_*` 가 있다.  
세그먼트만 맞고 GDTR/IDTR이 틀리면 게스트가 디스크립터·인터럽트를 찾지 못한다.

```c
native_store_gdt(&gdtr);
store_idt(&idtr);
gst->gdtr_base = gdtr.address;
gst->gdtr_lim  = gdtr.size;
gst->idtr_base = idtr.address;
gst->idtr_lim  = idtr.size;
```

---

## 10. GDT 오프셋 계산

Selector → GDT 디스크립터 포인터:

```c
#define HB_MASK_GDT_ACCESS  0x03ULL   /* RPL + TI */

native_store_gdt(&gdtr);
ent = (struct desc_struct *)(gdtr.address + (selector & ~HB_MASK_GDT_ACCESS));
```

**왜 `& ~3`?**

- 하위 2비트(RPL, TI)는 **테이블 내 바이트 오프셋이 아님**.
- Index만 남겨야 **올바른 8/16바이트 디스크립터**를 가리킨다.

예: `CS = 0x0010` → `0x0010 & ~3 = 0x10` → GDT base + 0x10 바이트.

---

## 11. hb_probe 코드 매핑

[`hb_probe_capture_guest_state`](../../hb_probe.c) — 세그먼트 관련만:

```text
[1] selector 8개     hb_get_cs() … hb_get_tr()
[2] CS~ES            hb_probe_gdt_desc_base / _access, limit=0xFFFFFFFF
[3] FS/GS            rdmsr(FS/GS_BASE), AR from GDT
[4] LDTR/TR          hb_probe_ldtr_tr_fields (hb_ldttss_desc64)
[5] GDTR/IDTR        native_store_gdt, store_idt
[6] VMWRITE          hb_probe_vmwrite_guest_state → VM_GUEST_*
```

전체 guest state 흐름·미러 전략·체크리스트: [`guest-segments-why.md`](guest-segments-why.md)

---

## 12. 하이퍼바이저에서 덜 알아도 되는 것

| 주제 | 이유 |
|------|------|
| Real mode 세그먼트 | VMX guest는 protected/long |
| Far jump / call 조합 | bring-up에 거의 불필요 |
| LDT 직접 설계 | Linux LDTR=0 |
| 세그먼트로 주소 공간 격리 | **EPT + CR3** 가 더 중요 |
| 16비트 limit/granularity 세부 | 롱 모드 flat + 0xFFFFFFFF 관례 |

---

## 13. 관련 문서

| 문서 | 내용 |
|------|------|
| [`guest-segments-why.md`](guest-segments-why.md) | VMCS에 넣는 **이유**, 미러, entry 검사, 체크리스트 |
| [`guest.md`](guest.md) | VM_GUEST_* 필드 목록·Hyper-box 매핑 |
| [`launch-adjust.md`](launch-adjust.md) | RIP/RSP launch 직전 재설정 |
| [`setting.md`](setting.md) | VMCS 세팅 단계 로드맵 |

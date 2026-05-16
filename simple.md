# VMX 진입·이탈 순서 (요약)

## VMXON 이전 (필수 준비)

1. **CPUID** — `CPUID.1:ECX.VMX[bit 5] = 1` 인지 확인 (25.6절)

2. **`IA32_FEATURE_CONTROL`** (MSR `3AH`)
   - **bit 0 (Lock)** = 1 이어야 함 (아니면 VMXON → **#GP**)
   - SMX 동작 **안**이면 **bit 1** = 1, SMX 동작 **밖**이면 **bit 2** = 1 (해당 경로에서 VMXON 허용)

3. **VMXON 영역** — 4KB, 4KB 경계 정렬(자연 정렬), 물리 주소 준비
   - `IA32_VMX_BASIC`(480H)의 **VMCS revision identifier** → 영역 **첫 4바이트** (bit 31 = 0)
   - 나머지 바이트는 **0**으로 초기화

4. **CR0 / CR4 고정 비트** — `IA32_VMX_CR0_FIXED0/1`, `IA32_VMX_CR4_FIXED0/1`에 맞게 정렬 (25.8절)  
   - 지원되지 않는 값이면 **VMXON 실패**
   - 초기 VMX 프로세서 기준 **반드시 1**:
     - **CR0.PE**, **CR0.NE**, **CR0.PG**
     - **CR4.VMXE**
   - → **페이지 보호 모드**(IA-32e 포함)에서만 VMX 동작; 실모드·비페이징 보호 모드는 불가  
     (후속 CPU + **unrestricted guest**면 게스트는 예외 — VMM의 VMXON 전 CR0/CR4는 여전히 고정 MSR 따름)

5. **A20M 모드 아님** — A20M 이면 VMXON 실패 (25.8절)

6. **CR4.VMXE = 1** — VMX 명령 해석 허용 (0이면 VMXON → **#UD**)

7. **VMXON** `[vmxon_region_pa]` — VMX 동작(보통 VMX root) 진입

> VMX 동작 **안**에서는 **CR4.VMXE**를 0으로 못 씀.

---

## VMXOFF 이후 (정리)

1. **VMXOFF**
2. **CR4.VMXE = 0** (VMXOFF 이후에만 가능)



# VMX Cycle 
**VMM (Virtual-machine Monitor)**은 가상 머신마다 서로 다른 VMCS를 사용할 수 있다.

## VMCS
- 첫 4바이트  
  - **비트 30:0** → VMCS 리비전 식별자  
    → 한 프로세서용으로 포맷된 VMCS를 다른 형식의 프로세서에서 잘못 쓰는 것을 방지함

  - **비트 31** → **섀도 VMCS(shadow VMCS) 여부 표시**  
    - 이 비트는 nested virtualization(네스티드/중첩 가상화)에서 **섀도 VMCS**에 사용됨  
    - CPU가 VMCS shadowing 기능을 지원하는 환경(nested guest에서 VMCS 직접 접근 허용 등)에서만 의미가 있음  
    - 일반(비-nested) VMCS는 비트 31이 0, shadow VMCS는 1로 설정  
    - (VMCS shadowing 지원 여부는 IA32_VMX_PROCBASED_CTLS2 MSR로 확인)

## VMCS / shadow VMCS

**역할 구분 (헷갈리기 쉬운 것)**

| 구분 | 무엇인가 |
|------|----------|
| **VMCS 포인터** | `VMPTRLD` / `VMPTRST` — CPU당 **현재 VMCS** 하나 |
| **VMCS 링크 포인터** | **일반 VMCS 안 필드** — **섀도 VMCS** 4KB의 **물리 주소** |
| **비트 31** | VMCS 영역 첫 4바이트 — 이 4KB가 **섀도 VMCS**인지 표시 |

> `VMPTRLD`는 비트 31과 무관하게 **그 VMCS를 current로 올린다**.  
> “0=교체, 1=기존 VMCS에 연결” **아님**. 연결은 **링크 포인터**로 한다.

---

### 관행: Intel nested + VMCS shadowing (L0/L1)

**전제:** secondary proc-based **VMCS shadowing** = 1, L1이 VMX non-root에서 VMX 명령 사용.

1. L0가 **일반 VMCS**(비트 31 = 0)를 `VMPTRLD` → **current**
2. L0가 **일반 VMCS**에 `VMWRITE` → **VMCS link pointer** = **섀도 VMCS** 물리 주소
3. **섀도 VMCS** 영역: 리비전 + **비트 31 = 1**, 나머지 0 등 초기화
4. **VM entry** 성공 시 → 링크가 가리키는 섀도 VMCS가 **활성(active)**  
   **current**는 계속 **일반 VMCS** (VM entry·`VMLAUNCH`/`VMRESUME` 대상)
5. L1(게스트 하이퍼바이저)의 `VMREAD`/`VMWRITE`는 **활성 섀도 VMCS** 쪽으로 동작
6. 섀도잉 안 쓸 때: 링크 포인터 = `FFFFFFFF_FFFFFFFFH`

**L1이 `VMPTRLD`를 실행하는 경우 (하드웨어 nested):**  
VMX non-root에서 `VMPTRLD` → **VM exit** → L0가 정책에 따라 처리(다른 VMCS로 바꾸거나, shadowing으로 링크 갱신 등).  
**CPU가 L1 오퍼랜드를 그대로 current로 올리지는 않는** 경우가 일반적(구현·설정에 따름).

---

### Alcatraz (Hyper-box): KVM VMX 명령 에뮬레이션

**배경:** Hyper-box가 **VMX root**, KVM은 **VMX non-root**(Ring 0이지만 진짜 VMX root 아님).  
KVM의 `VMPTRLD`/`VMREAD`/`VMWRITE` 등은 **VM exit**로 L0에 넘어감.

**게스트 `VMPTRLD` 시 Hyper-box가 하는 일** (하드웨어 `VMPTRLD` **실행 안 함**):

1. 게스트 메모리에서 오퍼랜드(**nested VMCS** 물리 주소) 읽기
2. 게스트 **RIP** 진행 + **CF/ZF** 성공 처리
3. nested VMCS 영역 **비트 31 = 1** (섀도 표시), `VMCLEAR` 등으로 초기화
4. **지금 current인 Hyper-box guest VMCS**에 **`VMCS link pointer`** = nested VMCS 주소 (`VMWRITE`)
5. **`g_nested_vmcs_ptr`** 등에 주소 저장

→ **CPU current = Hyper-box guest VMCS 유지**, KVM이 만지는 VMCS 내용 = **섀도 + 링크**로 연결.

**nested VM exit** 시에도 비슷하게, 나온 VMCS(`prev_vmcs`)를 섀도로 링크하는 경로 있음.

**호스트 KVM과 충돌:** Alcatraz가 **VMCS shadowing** 슬롯을 씀 → nested 쓸 때  
`modprobe kvm_intel enable_shadow_vmcs=0` (README 참고).

---

### 한 줄 비교

| | **관행 (Intel shadowing)** | **Alcatraz** |
|---|---------------------------|--------------|
| L0 current | 일반 VMCS | **guest VMCS** 고정 |
| L1 `VMPTRLD` | L0가 exit에서 처리(정책 다양) | **에뮬**: 링크만 설정, **HW `VMPTRLD` 없음** |
| L1 VMCS 조작 | **활성 섀도** | 링크된 **nested VMCS(섀도)** |
| VMCS shadowing | L0 하이퍼바이저가 사용 | **Hyper-box**가 사용, **호스트 KVM은 끔** |
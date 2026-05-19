# 27.1 VM Exit를 유발하는 명령 (Vol. 3C)

> 아래 **「기반 지식」**을 먼저 보면 본문(27.1.1~)의 INS, #GP, CPL 등이 읽힌다.

---

## 기반 지식 (용어·개념)

### 1. 가상화에서 누가 어디서 도는지

```
[ 물리 CPU ]
    VMX root     ← VMM (KVM, Hyper-V, 알카트raz…) — 하이퍼바이저
    VMX non-root ← Guest OS / 앱 — 가상 머신 안 소프트웨어
```

| 용어 | 뜻 |
|------|-----|
| **VMM** | Virtual Machine Monitor. 게스트를 돌리고 자원·장치를 가상화하는 소프트웨어 |
| **Guest** | VM 안에서 도는 OS·프로그램 |
| **VM entry** | root → non-root (게스트에게 CPU 넘김) |
| **VM exit** | non-root → root (게스트 명령/이벤트 때문에 VMM으로 복귀) |
| **VMCS** | Virtual-Machine Control Structure. CPU당 “현재 VM” 설정·상태를 담는 4KB 구조 (VMM이 채움) |

게스트는 자신이 VM 안인지 CPU만으로는 알기 어렵게 설계되어 있다 (25.3절).

---

### 2. VM exit가 뭐고, fault-like는 뭐냐

- **VM exit**: 
 게스트(non-root) 실행 중 CPU가 **VMM(root)로 제어를 넘기는 것**. 
 exit 이유·게스트 PC 등은 VMCS/핸들러에서 읽는다.

- **Fault-like VM exit**: 
 그 **명령은 실행되지 않음**.  
 그 명령 때문에 바뀌어야 할 레지스터·메모리도 **갱신 안 됨** 
 → VMM이 handler에서 **흉내 내거나** 거부한다.

- **Intercept / exiting**: 
 VMCS의 **VM-execution control**·**bitmap**을 켜서 “이 명령·이 포트·이 MSR 오면 VM exit”라고 CPU에 알리는 것.

---

### 3. Fault, 예외, #GP, #UD

x86에서 **예외(exception)** 는 실행 중 CPU가 내부적으로 처리하는 이벤트다. 번호로 부른다.

| 기호 | 이름 | 대략 언제 |
|------|------|-----------|
| **#UD** | Invalid Opcode | 없는/금지된 명령, 잘못된 조건 |
| **#GP** | General Protection | 권한 부족, 잘못된 세그먼트·디스크립터, 보호 위반 |
| **#PF** | Page Fault | 페이징·접근 권한 (본 문서 27.1에서 덜 다룸) |

- **Fault**: 예외 중 **재시작 가능**한 종류. 
 fault 발생 **지점의 명령은 아직 완료되지 않은 것**으로 간주 → 핸들러 처리 후 그 명령부터 다시 시도할 수 있음.

- **VM exit vs fault**: 둘 다 “게스트 실행이 멈춤”이지만, **#GP는 CPU 예외 핸들러**(게스트/VMM이 IDT에 등록)로, **VM exit는 VMM의 VM exit handler**로 간다. 
 **어느 쪽이 먼저인지**는 27.1.1 규칙으로 정해진다.

---

### 4. CPL (권한 링)

**CPL** (Current Privilege Level): 현재 코드의 권한. **0 = 커널(가장 높음)**, **3 = 사용자(가장 낮음)**.

- **27.1.1 규칙 (팩트)**: **권한(CPL) 때문에 나는 fault**, **#UD**, TSS **I/O permission**으로 나는 **#GP**는 **VM exit보다 우선**한다.  
  - Intel 예: **CPL=3**에서 `RDMSR` → **#GP** (VM exit 아님).  
  - 반대 예: **CPL=0**에서 **존재하지 않는 MSR** `RDMSR` → 하드웨어 #GP 대신 **VM exit** 가능 (bitmap·정책).
- **CPL=3에서도 VM exit가 날 수 있음** (위 규칙의 예외·별도 control):  
  - `PAUSE` — “PAUSE exiting”=1이면 **CPL>0**에서도 VM exit (27.1.3).  
  - `MOV DR` — “MOV-DR exiting”=1이면 **#GP·#UD보다 VM exit 우선** (권한 fault보다 먼저).  
  - `UMWAIT` / `TPAUSE` — “RDTSC exiting” + “enable user wait and pause” 둘 다 1이면 사용자 모드에서도 exit 가능.  
  - I/O bitmap 등으로 **권한 검사를 통과한** `IN`/`OUT`/`INS`/`OUTS`는 CPL과 무관하게 exit 가능.
- **정리**: “유저 모드 = 항상 #GP만” **아님**. 다만 **MSR·대부분 특권 명령을 유저가 쓰면** 27.1.1 때문에 **먼저 #GP**인 경우가 많고, **커널(CPL=0)**에서 VMM이 켠 intercept가 실제로 동작하는 경우가 많다.

---

### 5. 세그먼트 (INS/OUTS와 #GP 이해용)

**보호 모드·64비트**에서도 문자열 I/O 명령은 **세그먼트 레지스터**로 메모리 주소를 잡는다.

| 명령 | 주로 쓰는 세그먼트 | 역할 |
|------|-------------------|------|
| **INS** | **ES** (+ EDI/RDI) | 포트에서 읽은 바이트를 **메모리**에 저장 |
| **OUTS** | **DS** (+ ESI/RSI), 프리픽스로 다른 세그먼트 가능 | **메모리**에서 읽어 포트로 출력 |

CPU는 실행 전에 대략 다음을 검사한다.

- **Unusable**: 그 세그먼트로 이 접근을 할 수 없음 → **#GP**
- **Limit 초과**: 인덱스(EDI/ESI 등)가 세그먼트 한계를 넘음 → **#GP**
- **Alignment-check**: 정렬 검사 켜져 있을 때 위반 → alignment 예외

**27.1.1 포인트**: I/O exiting / I/O bitmap이 켜져 있으면, 위 #GP보다 **I/O 때문에 VM exit**가 **먼저** 날 수 있다 (명령은 여전히 실행 안 됨).

---

### 6. I/O 포트와 IN / OUT / INS / OUTS

PC 아키텍처에는 **메모리 주소**와 별도로 **I/O 포트 번호**(16비트 공간, 0~65535)가 있다.

| 명령 | 동작 |
|------|------|
| **IN** | 포트 → 레지스터 (1회) |
| **OUT** | 레지스터 → 포트 (1회) |
| **INS**, **INSB/W/D** | 포트 → **메모리 버퍼** (문자열·반복 가능, `REP INS` 등) |
| **OUTS**, **OUTSB/W/D** | **메모리 버퍼** → 포트 |

- 레거시 디스크·PIC·시리얼 등은 포트로 제어하는 경우가 많다.
- VMM은 **가상 포트**를 만들기 위해 특정 포트 접근 시 **VM exit** → handler에서 값을 에뮬레이션한다.
- VMCS: **unconditional I/O exiting**(전 포트), **use I/O bitmaps**(비트맵에 1인 포트만) — 27.1.3.

---

### 7. MSR, CR, DR (27.1.3에 자주 나옴)

| 종류 | 예 | 용도 |
|------|-----|------|
| **CR** (Control Register) | CR0, CR3, CR4 | 페이징 켜기, 페이지 테이블 주소, VMX 등 모드 |
| **DR** (Debug Register) | DR0~DR7 | 하드웨어 브레이크포인트 |
| **MSR** (Model Specific Register) | `IA32_*`, `0xC0000000` 대역 | CPU별 기능·성능·보안 설정. `RDMSR`/`WRMSR`로 접근 (ECX=번호) |

VMM은 **CR0/CR4 guest/host mask + read shadow**로 게스트가 “보는 CR”과 실제 하드웨어 CR을 다르게 할 수 있다. 게스트가 shadow와 안 맞게 쓰면 **VM exit**.

---

### 8. TSS · I/O permission bit

**TSS** (Task State Segment): 과거 멀티태스킹용 구조. 지금은 주로 **권한·스택 전환·I/O 권한 비트맵** 등에 쓰인다.

- **CPL=3**에서 I/O 포트 접근 시, TSS 안 **I/O permission bitmap**으로 허용 포트를 검사한다.
- 거부되면 **#GP** — 이 #GP는 27.1.1에서 **VM exit보다 우선**한다.

---

### 9. Operand fetch

명령은 대략 **(1) 디코드 → (2) 피연산자 읽기 → (3) 실행** 순서다.

- **(2)에서** 잘못된 메모리·권한으로 fault가 나면, **(3) 조건**만 보고 나는 VM exit보다 **fault가 우선**한다.
- 예: **LMSW** — 피연산자 읽다 #PF/#GP 나면, CR0 shadow 비교로 인한 VM exit보다 그 fault가 먼저.

---

### 10. 본문 축약 표현 정리

| 본문 표현 | 의미 |
|-----------|------|
| non-root | 게스트 실행 모드 |
| exiting = 1 | 해당 종류 VM exit 켜짐 |
| bitmap bit = 1 | 그 인덱스(포트/MSR/…) 접근 시 exit |
| mask / read shadow | 게스트 CR 비트별 “가로채기”·“게스트에게 보여줄 값” |
| descriptor-table exiting | GDT/IDT/LDT/TSS 로드·저장 명령 intercept |

---

## 27.1 본문

VMX **non-root**에서 실행되는 특정 명령은 **VM exit**를 일으킬 수 있다. 

별도 규정이 없으면 이런 VM exit는 **fault-like** — 명령은 **실행되지 않고**, 그 명령으로 인한 프로세서 상태 갱신도 **없다**. (아키텍처 상태는 29.1절)

| 절 | 내용 |
|----|------|
| **27.1.1** | fault와 VM exit의 **우선순위** |
| **27.1.2** | **무조건** VM exit (non-root에서 절대 실행 불가) |
| **27.1.3** | VM-execution control에 **따라** VM exit |

---

## 27.1.1 Fault vs VM Exit 우선순위

**VM exit가 먼저인 경우 (fault보다 우선)**

- 위에서 언급한 **예외가 아닌** 나머지 예외보다 **fault-like VM exit**가 우선  
  - 예: CPL=0에서 **존재하지 않는 MSR**에 `RDMSR` → **#GP가 아니라 VM exit**

- [`INS` / `OUTS`](#6-io-포트와-in--out--ins--outs)로 인한 VM exit(I/O exiting / I/O bitmap)는 아래 [#GP](#3-fault-예외-gp-ud)보다 **우선**:
  - 세그먼트 unusable — [INS→ES, OUTS→DS](#5-세그먼트-insouts와-gp-이해용)(프리픽스로 override 가능)
  - 세그먼트 limit 초과
  - alignment-check

**Fault / 예외가 VM exit보다 우선**

- **#UD** (invalid opcode)
- **권한(CPL) 기반 fault** — 예: CPL=3에서 `RDMSR` → **#GP**, VM exit 아님¹
- TSS **I/O permission bit** 검사에 따른 **#GP**
- **operand fetch 중** 발생한 fault → operand 내용에 조건부인 VM exit보다 우선 (예: `LMSW`, 27.1.3)

¹ **`MOV DR`는 예외** — “MOV-DR exiting”=1이면 **#GP·#UD보다 VM exit 우선** (27.1.3)

27.1.2 / 27.1.3에서 “VM exit 가능”이라 할 때, **VM exit보다 우선하는 fault가 없다**고 가정한다.

---

## 27.1.2 무조건 VM Exit (Unconditional)

VMX non-root에서 실행 시 **항상** VM exit → **non-root에서 실행 불가**:

| 일반 명령 | VMX 명령 |
|-----------|----------|
| `CPUID`, `GETSEC`², `INVD`, `XSETBV` | `INVEPT`, `INVVPID`, `VMCALL`³, `VMCLEAR`, `VMLAUNCH`, `VMPTRLD`, `VMPTRST`, `VMRESUME`, `VMXOFF`, `VMXON` |

² `GETSEC`: **CR4.SMXE=1**이면 VM exit; **SMXE=0**이면 **#UD**  
³ dual-monitor SMI/SMM 처리 시, SMM 밖 VMX root의 `VMCALL`은 SMM VM exit (33.15.2)

---

## 27.1.3 조건부 VM Exit (VM-execution controls)

control이 **0**이면 대개 **정상 실행**. **1**이거나 마스크/비트맵 조건이 맞으면 fault-like VM exit.

> **Secondary / tertiary proc-based controls**  
> primary bit 31=0 → secondary는 **전부 0**으로 간주.  
> primary bit 17=0 → tertiary는 **전부 0**으로 간주. (26.6.2)

### CR / 디스크립터 / 캐시

| 명령 | VM exit 조건 |
|------|----------------|
| **CLTS** | CR0 guest/host **mask**와 **read shadow** 모두에서 **bit 3 (CR0.TS)** 가 set |
| **LMSW** | mask의 **저위 4비트** 중 set된 비트에 대해, 쓰려는 값 ≠ read shadow. **bit 0(PE)는 LMSW로 clear 불가** — PE 관련·bit 3:1 불일치 시 exit |
| **MOV → CR0** | source가 **CR0 guest/host mask**의 각 set 비트 위치에서 **read shadow**와 다름 (mask 전부 0이면 exit 없음) |
| **MOV → CR3** | “CR3-load exiting”=1 **이고** source가 VMCS **CR3-target** 목록에 없음. count=0이면 **항상** exit |
| **MOV from CR3** | “CR3-store exiting”=1 |
| **MOV → CR4** | MOV→CR0와 동일 (CR4 mask / shadow) |
| **MOV DR** | “MOV-DR exiting”=1 (**#GP·#UD보다 우선**) |
| **MOV to/from CR8** | “CR8-load/store exiting” |
| **LGDT, LIDT, LLDT, LTR, SGDT, SIDT, SLDT, STR** | “descriptor-table exiting”=1 |
| **INVLPG** | “INVLPG exiting”=1 |
| **INVPCID** | “INVLPG exiting” **and** “enable INVPCID” 둘 다 1 |

### I/O

| 설정 | 동작 |
|------|------|
| unconditional I/O exiting=0, use I/O bitmaps=0 | **정상 실행** |
| unconditional=**1**, bitmaps=0 | **항상** VM exit |
| use I/O bitmaps=**1** | 해당 포트 비트=1이면 exit; **16비트 wrap**(FFFFH↔0000H)도 exit (이때 unconditional은 **무시**) |

대상: `IN`, `INS`/`INSB`/`INSW`/`INSD`, `OUT`, `OUTS`/… — INS/OUTS 우선순위는 27.1.1

### HLT / MONITOR / MWAIT / PAUSE 계열

| 명령 | 조건 |
|------|------|
| **HLT** | “HLT exiting”=1 |
| **MONITOR** | “MONITOR exiting”=1 |
| **MWAIT** | “MWAIT exiting”=1 (0이면 동작 변경 가능, 27.3) |
| **PAUSE** | CPL·“PAUSE exiting”·“PAUSE-loop exiting” — 아래 참고 |
| **UMWAIT, TPAUSE** | “RDTSC exiting” **and** “enable user wait and pause” 둘 다 1 |

**PAUSE (요약)**

- **CPL=0**: PAUSE exiting=1 → exit (loop exiting 무시). 둘 다 0 → 정상. exiting=0, loop=1 → PLE_Gap / PLE_Window·TSC 기준으로 loop 탐지 후 exit
- **CPL>0**: PAUSE exiting=1 → exit; loop exiting은 **무시**

### MSR / TSC / PMU / RNG

| 명령 | VM exit 조건 |
|------|----------------|
| **RDMSR / WRMSR / WRMSRNS** | “use MSR bitmaps”=**0** **or** ECX가 low/high 범위 밖 **or** 해당 read/write bitmap 비트=1 |
| **RDMSRLIST / WRMSRLIST** | MSR-list enable=1 **and** bitmaps=0 → 전체 exit; 둘 다 1이면 MSR별로 위와 동일 (한 항목 exit 시 RCX 비트 유지·메모리/MSR 미갱신) |
| **RDPMC** | “RDPMC exiting”=1 |
| **RDTSC** | “RDTSC exiting”=1 |
| **RDTSCP** | “RDTSC exiting” **and** “enable RDTSCP” |
| **RDRAND / RDSEED** | 각 exiting control=1 |

ECX 범위: `00000000H–00001FFFH`, `C0000000H–C0001FFFH` (26.6.9)

### SGX / AMX / PASID / 기타

| 명령 | 조건 |
|------|------|
| **ENCLS / ENCLV** | 각 “enable ENCLS/ENCLV exiting”=1 **and** EAX에 해당 **exiting bitmap** 비트=1 (<63: bit EAX, ≥63: bit 63) |
| **ENQCMD, ENQCMDS** | “PASID translation”=1이면 동작 변경·exit 가능 (27.5.8) |
| **PCONFIG** | “enable PCONFIG”=1 **and** bitmap 조건 (ENCLS와 유사) |
| **LOADIWKEY** | “LOADIWKEY exiting”=1 |
| **XSAVES / XRSTORS** | “enable XSAVES/XRSTORS”=1 **and** `EDX:EAX ∧ IA32_XSS ∧ XSS-exiting bitmap`에 set 비트 있음 |

### VMCS shadow / 캐시 무효화 / RSM

| 명령 | 조건 |
|------|------|
| **VMREAD / VMWRITE** | “VMCS shadowing”=0 **or** source 상위 비트(64bit: 63:15) ≠ 0 **or** VMREAD/VMWRITE **bitmap** bit=1 → exit; 아니면 **VMCS link pointer**가 가리키는 VMCS 접근 |
| **WBINVD / WBNOINVD** | “WBINVD exiting”=1 |
| **RSM** | **SMM 안**에서 실행 시 VM exit; SMM 밖은 **#UD** (VMX 여부 무관)¹ |

¹ 33.15.3: VMX root + SMM 안의 RSM도 #UD

---

## VMM 관점 요약

1. **게스트가 못 쓰는 명령** — 27.1.2 (VMX·`CPUID` 등) → 항상 intercept  
2. **정책으로 가리는 명령** — VMCS proc-based / MSR·I/O·ENCLS bitmap 등 (26.6)  
3. **에뮬레이션 시 fault vs exit** — 27.1.1: 게스트에 #GP를 줄지, VMM이 exit handler에서 처리할지 결정  
4. **CR0/CR4 shadow** — mask+shadow로 게스트가 “보는 값”과 실제 host 값 분리; 불일치 쓰기 → exit

---

## 기술 설명 (이해용)

**VMX non-root**는 게스트 OS/앱이 도는 모드다. VMM은 VMCS에 “이 명령이 오면 CPU를 빼내라”는 **VM-execution control**과 **bitmap**을 켠다.

- **Fault-like VM exit**: 명령이 **끝까지 실행되지 않음** → 게스트 레지스터/메모리는 그 명령 기준으로 **안 바뀜**. VMM exit handler가 **에뮬레이션**하거나 거부한다.
- **무조건 exit (27.1.2)**: 하이퍼바이저 전용·위험 명령 — 게스트에 절대 맡기지 않음.
- **조건부 exit (27.1.3)**: 예) `RDMSR` — MSR bitmap으로 **일부 MSR만** intercept; 나머지는 하드웨어가 그대로 처리해 성능 유지.
- **우선순위 (27.1.1)**: 잘못된 opcode·권한은 CPU가 먼저 **#UD/#GP**; “가짜 MSR”처럼 하드웨어가 GP 안 내는 경우는 **VM exit**로 VMM에 넘김. `MOV DR`·I/O string은 매뉴얼에 적힌 **예외 규칙**이 있음.

알카트raz 같은 VMM은 27.1.3 항목별로 VMCS를 어떻게 채울지(어떤 MSR·I/O·CR3 load를 intercept할지)가 **게스트 호환성 vs 성능**의 핵심이다.

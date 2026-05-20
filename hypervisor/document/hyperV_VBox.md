# Windows Hyper-V · VirtualBox · Nested VMX 정리

Alcatraz `hypervisor_b` 모듈을 VirtualBox Linux 게스트에서 실험하면서 겪은 **VMX support: 0** 문제와, Windows 호스트·Hyper-V·VirtualBox nested 관계를 한곳에 정리한 문서입니다.

---

## 1. 목표와 전제

| 항목 | 내용 |
|------|------|
| 목표 | Linux VM(L1) 안에서 **VMX**를 쓰는 하이퍼바이저 코드(`hypervisor_b`, `hyper_box`) 실험 |
| 최소 조건 | 게스트 CPUID / `/proc/cpuinfo`에 **`vmx`** 노출 |
| 호스트 | Windows 11 + VirtualBox, CPU: 12th Gen Intel i7-1260P |
| 확인 스크립트 | `hypervisor/load_test.sh` (빌드 → insmod → dmesg → rmmod) |

**성공 판정 예:**

```text
hypervisor_b: VMX support: 1 (cpuid.1.ecx=0x........)
grep -w vmx /proc/cpuinfo   # vmx 플래그 출력
```

---

## 2. L0 / L1 / L2 레벨 정의

```
[물리 PC + Windows 11]          ← Windows는 "호스트 OS". L0 아님
        │
   ┌────┴────┐
   │  L0     │  하이퍼바이저 (VT-x 루트를 잡는 쪽)
   └────┬────┘
        │
   ┌────┴────┐
   │  L1     │  VirtualBox가 만든 Linux VM (Ubuntu 등)
   └────┬────┘
        │
   ┌────┴────┐
   │  L2     │  L1 하이퍼바이저가 만든 VM (나중 단계, 여러 개 가능)
   └─────────┘
```

| 레벨 | 이 환경에서의 대상 | 역할 |
|------|-------------------|------|
| **L0** | VirtualBox **또는** Windows Hyper-V | VT-x(VMX) **루트** 하이퍼바이저 |
| **L1** | Linux VM | `hypervisor_b` / KVM 등이 VMX를 쓰는 계층 |
| **L2** | L1 안의 VM | nested가 열린 뒤에야 의미 있음 |

### 자주 하는 오해

| 오해 | 실제 |
|------|------|
| L0 = Windows | Windows는 OS. **L0는 하이퍼바이저**(Hyper-V 또는 VBox) |
| L1 = VirtualBox | **VirtualBox = L0**, **Linux VM = L1** |
| nested = VM 1개만 | **L2 VM은 여러 개** 가능. 제한은 **중첩 깊이**(보통 L2까지) |
| shadow VMCS 1개 | **vCPU·L2 VM마다** VMCS/shadow 구조가 붙음. "슬롯 1개" 아님 |

---

## 3. `hypervisor_b`가 하는 일

- CPUID leaf 1, ECX **bit 5 (VMX)** 확인 (`CPUID_1_ECX_VMX = 1 << 5`)
- `pr_info`로 `VMX support: 0/1` 출력
- **모듈 로드/언로드는 성공**해도 VMX가 0이면 **환경 문제** (코드 버그가 아님)

```c
cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
vmx_supported = !!(ecx & CPUID_1_ECX_VMX);
```

---

## 4. 관측했던 증상 (실패 상태)

### 4.1 Linux 게스트

```bash
# /proc/cpuinfo flags 에 hypervisor 는 있으나 vmx 없음
grep -w vmx /proc/cpuinfo   # (출력 없음)
```

```text
hypervisor_b: VMX support: 0 (cpuid.1.ecx=0xfeda3203)
```

- `hypervisor` 플래그 → VM 안에서 실행 중임을 의미
- **`vmx` 없음** → nested passthrough 미적용

### 4.2 Windows 호스트 (VBoxManage)

```text
Processor supports HW virtualization: yes
Processor supports nested paging: yes
Processor supports nested HW virtualization: no   ← 핵심
```

→ **일반 VM 실행은 가능**하지만, **L1에 VMX를 넘기는 nested HW virt는 불가** 상태.

### 4.3 Windows 부팅 설정 (초기)

```cmd
bcdedit /enum | findstr /i hypervisor
hypervisorlaunchtype    Auto
```

```cmd
systeminfo | findstr /i "Hyper-V"
Hyper-V 요구 사항: 하이퍼바이저가 검색되었습니다. ...
```

---

## 5. 원인 요약 (한 줄)

**Windows가 부팅 시 Hyper-V 계열 하이퍼바이저로 VT-x 루트를 잡으면, VirtualBox는 이 호스트에서 `nested HW virtualization`을 제공하지 못하고, Linux 게스트(L1)에 VMX를 CPUID/플래그로 넘기지 않는다.**

---

## 6. `hypervisorlaunchtype Auto` vs `off`

### 6.1 `Auto` (문제가 나던 상태)

```
[CPU VT-x 루트]
       │
       ▼
  L0: Windows Hyper-V (하이퍼바이저)
       │
       ├── Windows (루트 파티션)
       └── VirtualBox → Linux VM (L1)
                └── VMX passthrough ❌
```

- 부팅 시 **가능하면** Windows 하이퍼바이저가 올라감
- VirtualBox `list hostinfo`: **`nested HW virtualization: no`**
- Hyper-V **기능 UI에서 끈 것**만으로는 부족할 수 있음 (`Auto` 유지 시)

### 6.2 `off` (우회 목표)

```cmd
bcdedit /set hypervisorlaunchtype off
```

**재부팅 필수.**

```
[CPU VT-x 루트]
       │
       ▼
  L0: VirtualBox
       │
       ▼
  L1: Linux VM  (+ VM 설정: Nested VT-x/AMD-V)
       └── VMX 보이면 ✅  → hypervisor_b: 1 기대
```

**기대:**

```text
Processor supports nested HW virtualization: yes
```

### 6.3 `off`의 의미 (프로세스 vs 하이퍼바이저)

- **일반 exe가 VMX를 잡는 게 아님**
- **Windows Hyper-V 하이퍼바이저가 부팅 시 VT-x 루트를 안 잡음**
- VM 실행 시 **VirtualBox 드라이버/백엔드가 L0로 VT-x 사용**

### 6.4 `off` 후 부작용

- WSL2, Docker(Hyper-V 백엔드), Memory Integrity 등 **제한·비활성** 가능
- 다시 쓰려면: `bcdedit /set hypervisorlaunchtype auto` + 재부팅

---

## 7. VirtualBox 설정 (호스트 `yes` 이후)

**Nested Paging** ≠ **Nested VT-x/AMD-V**

| GUI / 설정 | 의미 |
|------------|------|
| Enable Nested Paging | EPT 등 (성능). **VMX 노출과 무관** |
| **Enable Nested VT-x/AMD-V** | `--nested-hw-virt on` — **VMX passthrough** |

```cmd
VBoxManage modifyvm "VM이름" --nested-hw-virt on
```

VM **완전 종료** 후 다시 시작.

---

## 8. Hyper-V nested vs VirtualBox on Windows

### 8.1 이론

- **Hyper-V도** nested virt를 지원하면 **L1(하이퍼바이저 VM)에 VMX/SVM 노출 가능**
- CPU·nested **원리상 가능**

### 8.2 “호스트에 깔린 VirtualBox”는 다른 케이스

| | Hyper-V **안의** Hyper-V/KVM VM | Windows에 설치된 **VirtualBox** |
|---|--------------------------------|--------------------------------|
| L0 | Hyper-V | Hyper-V (`Auto`일 때) |
| L1 | 하이퍼바이저 있는 **VM 하나** | VBox **앱** + 그 안 Linux VM |
| VMX passthrough | Hyper-V 설정으로 가능한 경우 | VBox: **`nested HW virtualization: no`** 흔함 |

- VirtualBox는 Hyper-V 아래 **“L1 하이퍼바이저 VM”** 으로 깔끔히 올라가지 않음
- WHPX(Windows Hypervisor Platform) 등 **다른 경로**로 VM을 돌리는 경우가 있음
- **“Hyper-V nested가 되면 VBox도 VMX 가져야 한다”** → **자동으로 해당 안 됨**

### 8.3 VirtualBox 안 구조 (오해 정리)

**“VBox 안에 L0가 두 개라서 막는다”** → **아님**

```
off + nested on 일 때:

  L0: VirtualBox (하나)
      └── L1: Linux VM
              └── (선택) L2: 여러 VM
```

막힌 이유는 **VBox 내부 이중 L0**가 아니라 **호스트에서 Hyper-V가 VT-x 루트**였기 때문.

---

## 9. 왜 이런 설계/동작인가 (객관적 자료)

“아무 이유 없는 정책”이 아니라 **아키텍처 + 제품 지원 범위 + Windows 보안**이 겹친 결과.

### 9.1 Microsoft (by design)

**문서:** [Virtualization applications can't work together with Hyper-V](https://learn.microsoft.com/en-us/troubleshoot/windows-client/application-management/virtualization-apps-not-work-with-hyper-v) (KB 3204980)

- **Cause:** *“This behavior is by design.”*
- VT-x/AMD-V는 **한 번에 하나의 소프트웨어만** 사용
- 서드파티 가상화 앱(VirtualBox, VMware)은 Hyper-V와 **하드웨어 VT-x를 공유할 수 없음**
- Memory Integrity, Credential Guard 등은 **Hyper-V 의존**

### 9.2 Oracle VirtualBox

**문서:** [Troubleshooting — VirtualBox and Hyper-V on the same host](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/Troubleshooting.html)

- 같은 호스트에서 **충돌** 가능
- VirtualBox 사용 시 **Hyper-V 끄기** 권장
- Virtual Machine Platform, Windows Hypervisor Platform도 끄고 재부팅
- Core Isolation / Memory Integrity는 Hyper-V를 **다시 켤 수 있음**

### 9.3 VirtualBox 포럼 (공식 moderator)

**토론:** [Nested virtualization with Hyper-V (t=104666)](https://forums.virtualbox.org/viewtopic.php?t=104666)

- 6.1 이전: Hyper-V를 nested hypervisor로 **지원 안 함** — 게스트에 **SLAT/EPT 미제공**
- **mpack (Site Moderator):** 공식 지원 nested는 **“VirtualBox within VirtualBox”만** (dev가 완전 통제하는 시나리오)
- *“not supported ≠ 절대 안 됨”* — 다만 Hyper-V 루트 + VBox 조합은 기대하지 말 것

### 9.4 이슈 트래커

- [VirtualBox #586 — Nested VT-x 옵션 비활성](https://github.com/VirtualBox/virtualbox/issues/586)

### 9.5 정리 표

| 층 | 내용 |
|----|------|
| 하드웨어 | VT-x 루트는 **한 주체** (Intel/AMD 아키텍처) |
| Windows | 보안·WSL2 등 위해 **Hyper-V 루트** 선호 (`Auto`) |
| VirtualBox | Hyper-V 루트 호스트에서 **nested HW virt passthrough 미지원/제한** |
| Oracle 지원 범위 | 공식 nested: **VBox-in-Vbox** 위주 |

---

## 10. 해결 체크리스트 (Windows 호스트)

순서대로 진행.

### 10.1 Windows

```cmd
bcdedit /set hypervisorlaunchtype off
```

재부팅 후:

```cmd
bcdedit /enum | findstr /i hypervisor
```

→ `hypervisorlaunchtype    Off`

필요 시 추가:

- Windows 기능: Hyper-V, Virtual Machine Platform 끄기
- 보안: **메모리 무결성**(코어 격격) 끄기
- WSL2 / Docker Desktop 중지

### 10.2 VirtualBox (호스트 CMD)

```cmd
cd "C:\Program Files\Oracle\VirtualBox"
VBoxManage list hostinfo | findstr /i "nested HW virtualization"
```

→ **`Processor supports nested HW virtualization: yes`**

```cmd
VBoxManage list vms
VBoxManage showvminfo "VM이름" | findstr /i nested
VBoxManage modifyvm "VM이름" --nested-hw-virt on
```

VM **완전 종료** → 다시 시작.

### 10.3 Linux 게스트

```bash
grep -w vmx /proc/cpuinfo | head -1
cd hypervisor && ./load_test.sh
```

---

## 11. 대안 경로

| 방법 | L0 | 비고 |
|------|-----|------|
| `hypervisorlaunchtype off` + VBox nested | VirtualBox | 지금 실험 환경에 맞음 |
| 물리 Linux / 듀얼부트 | KVM | nested 제어가 단순 |
| Hyper-V VM + 가상화 확장 | Hyper-V | VBox-on-Windows와 **다른 길** |
| VMware 등 | 제품별 | 호스트·버전 의존 |

---

## 12. 용어 빠른 참조

| 용어 | 설명 |
|------|------|
| VT-x / VMX | Intel 하드웨어 가상화 확장 |
| CPUID.1:ECX bit 5 | VMX 지원 비트 |
| nested HW virtualization | L0가 L1에 VMX/SVM passthrough |
| nested paging / EPT | 2차 주소 변환 (VMX 노출과 별개) |
| `hypervisorlaunchtype` | Windows 부팅 시 하이퍼바이저 로드 여부 |
| WHPX | Windows Hypervisor Platform (VBox 백엔드 중 하나) |
| L0 / L1 / L2 | 하이퍼바이저 / 게스트 하이퍼바이저 / 그 안의 VM |

---

## 13. 참고 링크

- Microsoft KB: https://learn.microsoft.com/en-us/troubleshoot/windows-client/application-management/virtualization-apps-not-work-with-hyper-v
- VirtualBox Troubleshooting: https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/Troubleshooting.html
- VirtualBox Forum (nested + Hyper-V): https://forums.virtualbox.org/viewtopic.php?t=104666
- VirtualBox GitHub #586: https://github.com/VirtualBox/virtualbox/issues/586
- 프로젝트: `hypervisor/hypervisor_b.c`, `hypervisor/load_test.sh`, `hypervisor/Makefile`

---

## 14. 한 페이지 요약

1. **목표:** Linux VM에서 VMX 보이게 → `hypervisor_b` VMX=1.
2. **실패 원인:** `nested HW virtualization: no` → 게스트에 `vmx` 없음.
3. **L0/L1:** VBox=L0, Linux=L1 (Windows는 OS).
4. **`Auto`:** Hyper-V가 VT-x 루트 → VBox가 L1에 VMX passthrough 못 함.
5. **`off` + 재부팅:** VBox가 L0 → nested 켜면 L1에 VMX 가능.
6. **“정책”:** MS *by design* 단일 VT-x + Oracle *VBox-in-Vbox* 공식 nested + 기술적 WHPX/루트 제약.
7. **확인:** `list hostinfo` → `yes`, `grep vmx /proc/cpuinfo`, `load_test.sh`.

---

*작성: Alcatraz hypervisor 실험 정리 (Windows 11 + VirtualBox + Ubuntu 게스트)*

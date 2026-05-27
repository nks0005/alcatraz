# VM-entry control fields (VMWRITE)

**VM entry** (VMLAUNCH / VMRESUME) 시 CPU 동작: 64비트 게스트 진입, 이벤트 주입, MSR load 등.

---

## 주 필드

| VMWRITE 필드 | MSR | Hyper-box / HyperDbg 예 |
|--------------|-----|-------------------------|
| `VM_CTRL_VM_ENTRY_CTRLS` | `IA32_VMX_TRUE_ENTRY_CTRLS` | **IA-32e mode guest**, Load debug controls, (옵션) Load CET |

```text
vm_entry_ctrl = (rdmsr(TRUE_ENTRY_CTRLS) | VM_BIT_VM_ENTRY_CTRL_IA32E_MODE_GUEST | ...) & 0xFFFFFFFF
```

64비트 게스트면 **IA-32e mode guest** 비트와 `VM_GUEST_EFER.LMA` 등이 맞아야 한다.

---

## 이벤트 주입 (초기·런타임)

| 필드 | launch 전 | 런타임 |
|------|-----------|--------|
| `VM_CTRL_VM_ENTRY_INT_INFO_FIELD` | 보통 **0** | 인터럽트/예외 주입 시 설정 |
| `VM_CTRL_VM_ENTRY_EXCEPT_ERR_CODE` | 0 | 예외 시 |
| `VM_CTRL_VM_ENTRY_INST_LENGTH` | 0 | 소프트 인터럽트 등 |

hyper_box `hb_setup_vmcs`: 위 셋 **0**  
인터럽트 주입 helper에서 `VMWRITE`로 갱신.

---

## MSR load (entry)

| 필드 | 단계 1 |
|------|--------|
| `VM_CTRL_VM_ENTRY_MSR_LOAD_COUNT` | 0 |
| `VM_CTRL_VM_ENTRY_MSR_LOAD_ADDR` | 0 |

entry control에서 “load MSR from list”를 켰을 때만 count/addr·리스트 필요.

---

## Guest state와 짝

| entry 비트 | guest/host 쪽 |
|------------|----------------|
| IA-32e mode guest | `VM_GUEST_EFER`, `VM_GUEST_CR4`, `VM_CTRL_VM_EXIT/ENTRY` 호스트 주소 크기 |
| Load debug controls | `VM_GUEST_DEBUGCTL`, host debugctl |

---

## Launch와의 관계

- **첫 진입:** `VMLAUNCH` — VMCS launch state = clear (`VMCLEAR` 직후)
- **이후:** `VMRESUME` — entry control은 보통 유지, **guest RIP/RSP** 등만 exit handler에서 수정

→ [`launch-adjust.md`](launch-adjust.md), [`../load_write_launch.md`](../load_write_launch.md)

---

## 관련

- [`guest.md`](guest.md)
- [`control-exit.md`](control-exit.md)
- [`setting.md`](setting.md)

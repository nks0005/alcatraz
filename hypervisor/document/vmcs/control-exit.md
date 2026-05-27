# VM-exit control fields (VMWRITE)

**VM exit** 시 CPU/VMM 동작 방식: 64비트 호스트, 어떤 MSRs를 save/load 할지 등.

필드: `VM_CTRL_VM_EXIT_CTRLS` (`VM_CTRL_VM_EXIT_CTRLS` in hyper_box.h)  
준비: `hb_vm_control_register->vm_exti_ctrl_field`

---

## 주 필드

| VMWRITE 필드 | MSR | Hyper-box에서 자주 켜는 비트 |
|--------------|-----|------------------------------|
| `VM_CTRL_VM_EXIT_CTRLS` | `IA32_VMX_TRUE_EXIT_CTRLS` | Host address space size (64비트 호스트), Save debug controls, Save IA32_EFER, (옵션) Save VMX preemption timer |

계산 예:

```text
vm_exit_ctrl = (rdmsr(TRUE_EXIT_CTRLS) | VM_BIT_VM_EXIT_CTRL_HOST_ADDR_SIZE | ...) & 0xFFFFFFFF
```

**켠 비트**에 맞게 host/guest 쪽 **PAT/EFER/DEBUGCTL** 등을 실제로 맞춰야 `VMLAUNCH`가 성공한다.

---

## MSR save/load (hyper_box는 count/addr = 0)

| 필드 | 단계 1 | 단계 2 |
|------|--------|--------|
| `VM_CTRL_VM_EXIT_MSR_STORE_COUNT` | 0 | MSR save list 쓸 때 |
| `VM_CTRL_VM_EXIT_MSR_LOAD_COUNT` | 0 | |
| `VM_CTRL_VM_EXIT_MSR_LOAD_ADDR` | 0 | exit 시 load할 MSR 목록 PA |

exit control에서 “load IA32_EFER on exit” 등을 켰으면, guest/host **EFER 필드**와 일관되게 둔다.

---

## 단계 1 최소

- **Host address space size** — 64비트 커널이면 보통 **켬**
- Save debug / PAT / EFER — 켠 만큼 host state에 해당 MSR 반영
- MSR store/load list — **0** (리스트 없음)

---

## 런타임 VMWRITE (exit handler)

exit **이후** 핸들러가 다시 쓰는 경우 (초기 setup과 별도):

- `VM_CTRL_VM_ENTRY_INST_LENGTH` — 재진입 전 (hyper_box: nested 경로에서 0으로 두기도 함)
- 인터럽트 주입 시 entry 관련 필드 — [`control-entry.md`](control-entry.md)

exit **이유·자격**은 execution이 아니라 **exit information** — [`exit-information.md`](exit-information.md)

---

## 관련

- [`host.md`](host.md)
- [`control-entry.md`](control-entry.md)
- [`control-execution.md`](control-execution.md)
- [`setting.md`](setting.md)

# VM-exit information fields (launch 전 VMWRITE ❌)

SDM **6번째 VMCS 영역**.  
**VM exit가 발생한 뒤** CPU가 기록하고, VMM은 **`VMREAD`** 로 읽는다.

`VMLAUNCH` **전에** `VMWRITE`로 채우지 **않는다**.

상수 예: [`hyper_box.h`](../../../Source/hyper_box/hyper_box.h) `VM_DATA_*`

---

## 대표 필드 (VMREAD)

| 필드 | 용도 |
|------|------|
| `VM_DATA_EXIT_REASON` | 왜 exit 했는지 (예: `VM_EXIT_REASON_VMWRITE` = 25) |
| `VM_DATA_EXIT_QUALIFICATION` | 추가 정보 (CR 번호, IO 포트, VMX insn 디테일) |
| `VM_DATA_VM_EXIT_INST_LENGTH` | 게스트 RIP 스킵 길이 → `hb_advance_vm_guest_rip()` |
| `VM_DATA_VM_EXIT_INST_INFO` | VMREAD/VMWRITE 피연산자 해석 |
| `VM_DATA_GUEST_LINEAR_ADDR` | 페이지 폴트 등 |
| `VM_DATA_GUEST_PHYSICAL_ADDR` | EPT violation 등 |
| `VM_DATA_INST_ERROR` | **VMX 명령 실패** (VMLAUNCH/VMWRITE 등) 시 원인 |

---

## hb_probe / setup과의 구분

| 시점 | 동작 |
|------|------|
| `hb_setup_vmcs` (launch 전) | guest/host/control 만 **VMWRITE** |
| VM exit handler | `VMREAD` → 처리 → 필요 시 guest/control **VMWRITE** → `VMRESUME` |
| `VMLAUNCH` 실패 | `VMREAD(VM_DATA_INST_ERROR)` — hyper_box `hb_vm_launch` 실패 경로 |

---

## 단계 0 (현재 probe)

exit information **해당 없음** — VM entry 없이 link pointer만 검증.

---

## 핸들러에서 자주 쓰는 패턴 (Hyper-box)

```text
VMREAD exit_reason, exit_qual
switch (reason) { ... }
VMREAD VM_GUEST_RIP, VM_DATA_VM_EXIT_INST_LENGTH
VMWRITE VM_GUEST_RIP, rip + length   // advance
VMRESUME
```

nested VMX 명령: `VM_DATA_VM_EXIT_INST_INFO` + `VM_DATA_EXIT_QUALIFICATION` — [`../vmwrite_vmcs.md`](../vmwrite_vmcs.md)

---

## 관련

- [`setting.md`](setting.md) — “6번째 영역은 세팅 목록에서 제외”
- [`../load_write_launch.md`](../load_write_launch.md) — exit 루프

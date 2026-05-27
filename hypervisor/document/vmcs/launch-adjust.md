# VMLAUNCH 직전 보정 (guest RIP/RSP)

`hb_setup_vmcs()`에서 guest/host RIP·RSP를 이미 `VMWRITE` 했어도, **launch 직전**에 asm에서 **한 번 더** 맞추는 패턴이 있다.

---

## Hyper-box: `hb_vm_launch`

[`asm_helper.asm`](../../../Source/hyper_box/asm_helper.asm)

| VMWRITE encoding | 값 | 의미 |
|------------------|-----|------|
| `0x681C` (`VM_GUEST_RSP`) | 현재 `rsp` | “seamless” 호스트 스택을 게스트 RSP로 |
| `0x681E` (`VM_GUEST_RIP`) | `.success` 레이블 | launch 성공 시 게스트가 도달할 RIP |

그 다음 `vmlaunch`. 실패 시 CF/ZF → -1 / -2, `VM_DATA_INST_ERROR` 확인.

---

## HyperDbg: `VmxSetupVmcs` 끝

| 필드 | 값 |
|------|-----|
| `VMCS_GUEST_RSP` | 인자 `GuestStack` |
| `VMCS_GUEST_RIP` | `AsmVmxRestoreState` |
| `VMCS_HOST_RSP` | 정렬된 `VmmStack` top |
| `VMCS_HOST_RIP` | `AsmVmexitHandler` |

---

## hb_probe 단계 1

1. `hb_setup_vmcs`에서 host RIP = **exit stub**, host RSP = **per-CPU 스택**  
2. guest RIP/RSP = **테스트용 게스트 코드** (또는 launch asm의 `.success`)  
3. IRQ: `local_irq_save` 후 launch — [`../it_req.md`](../it_req.md)

probe만 할 때(단계 0)는 **이 문서 전체 생략**.

---

## Host RSP도 launch 전에 다시 쓸 수 있음

exit handler 스택이 launch 시점과 다르면 `VM_HOST_RSP`만 재설정하는 경우가 있다 (hyper_box exit 경로 일부).

---

## 관련

- [`guest.md`](guest.md)
- [`host.md`](host.md)
- [`../load_write_launch.md`](../load_write_launch.md)
- [`setting.md`](setting.md)

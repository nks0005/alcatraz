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

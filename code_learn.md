# hyper_box

# entry
hypervisor.c 

## hyper_box_init

1. hb_print_hyper_box_logo();

2. Check VMX support
    cpuid_count(1, 0, &eax, &ebx, &ecx, &edx)
    ```c
    #define CPUID_1_ECX_VMX	((u64)0x01 << 5)
    #define CPUID_1_ECX_SMX ((u64)0x01 << 6)
    ```

3. Check BIOS locked feature
    MSR >> 커널 버전에 따라 `MSR_IA32_FEATURE_CONTROL`, `MSR_IA32_FEAT_CTL` - 둘 다 같은 목적의 `CPU 기능 잠금/허용 스위치`

    hb_rdmsr 
    `extern u64 hb_rdmsr(u64 msr_index);`
    ```asm
    hb_rdmsr:
        push rdx
        push rcx

        xor rdx, rdx
        xor rax, rax

        mov ecx, edi # RDI에 인자가 들어감(msr_index)
        rdmsr 

        shl rdx, 32
        or rax, rdx

        pop rcx
        pop rdx
        ret    
    ```
    
    64비트 리눅스 > 기본 규약   
        RDI, RSI, RDX, RCX, R8, R9, Stack...

    2. 에서 CPUID를 통해 CPU가 VMX 기능 지원 확인 이후
    그 기능을 실제로 켜도 되는지 검사
    
    3. 1. CONTROL_LOCKED : BIOS가 설정을 잠가서 OS/드라이버가 임의로 바꾸기 어려운 상태
        이 상태에서 VMXON_ENABLED_OUTSIDE_SMX 가 꺼져있으면, 일반 환경에서 VMX를 킬 수 없음
    
    3. 2. VMCS_SHADOWING : 중첩 가상화할 경우 VMCS 개선
    3. 3. VPID : Virtual Processor ID, CPU의 TLB(주소 변환 캐시)에 "이 캐시가 어느 VM의 것"인지 태깅하는 기능
        - VM-entry/VM-exit or context switch때 TLB를 자주 flush 해야 함 

    3. 4. get function pointers : GetProcAddress

    3. 5. XSAVES, XRSTORS
        XSAVES : 현재 CPU의 확장 프로세서 상태(extended processor state)를 메모리에 저장(save)
        XRSTORS : 메모리에 저장된 확장 상태를 CPU레지스터로 복원 

    3. 6. USE_SHUTDOWN
        - 시스템이 재부팅/종료로 들어갈 때, 정리 절차를 안전하게 타기 위해 권한 조회
        > register_reboot_notifier

    3. 7. USE_SLEEP 
        - 슬립 감지 기능
        > register_pm_notifier

    3. 8. g_nested_vmcs_Array 초기화

    3. 9. 최대 RAM 크기

    3. 10. 
        cpu_id = smp_processor_id() -> 현재 실행 중인 CPU ID
        cpu_count = num_online_cpus() -> 온라인 상태인 CPU 개수
    
    3. 11. 하이퍼바이저 vmcs 메모리 할당
        - VMCS(가상 머신 제어 구조체) 관련 메모리를 할당/준비하는 초기화 함수 호출
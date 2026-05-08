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
    


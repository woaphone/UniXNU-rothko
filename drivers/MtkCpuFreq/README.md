# MtkCpuFreq

MediaTek `mediatek,cpufreq-hw` SRAM client for non-Apple board bring-up. The firmware owns PLL and voltage changes. The component reads the firmware LUT (32 rows, low 12 bits in MHz), enables each supplied domain and submits a supported frequency or OPP index according to the firmware scaling-mode word. Device code supplies register ranges and the requested rate.

This is a fixed boot performance request, not an adaptive governor. Request readback is not a measured CPU clock. The firmware must already have populated its LUT; no firmware is loaded by this component. MT6989 resource addresses belong to the rothko kit, not this reusable source.

Build: the device's resolved `build-native-input.py` compiles this component from the prepared iPadOS 17 IOKit recipe and adds its object to the force-loaded board archive. Runtime prefix: `MTK-CPUFREQ17`. No proprietary firmware is distributed.

Reference: retained OnePlusOSS MT6989 `mediatek-cpufreq-hw_main.c`; source identity is recorded in the delivery manifest. Offline build result and hardware result are recorded separately.

中文：使用联发科现有固件频率表与请求接口，固件负责电压/PLL时序。设备提供地址和目标频率。本版是固定启动请求，不是动态调频；请求寄存器读回不等于实际频率，尚待实机。

MT6989 enables FDVFS (stock DT and Android log confirmed). When firmware magic is `0xabcd0001`, the component submits one 26 MHz vote per logical CPU in each domain mask; otherwise it uses the existing domain request ABI. It never replaces a live FDVFS vote with a domain SRAM write. CCI coupling remains firmware/default policy in this fixed boot request.

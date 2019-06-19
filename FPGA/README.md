# FPGA additions to ldd3 Training -Linux Device Drivers- 

## Introduction

kernel drivers to digital hardware on the PL side of the SoC sea.

## FPGA applications

0. FPGA: **BRAM**
    * kernel driver for mapping BRAM memory on the PL side from the Zynq SoC.
    * It "kmallocates" contiguous memory on the kernel side (useful for accessing it with DMA if a more advanced version of the driver).
    * At HW project folder, there are sample Vivado 2017.4 projects
    * Tested on ZCU102 and PYNQ boards with kernels 4.9 and 4.14.
        * TODO: control of the available amount of memory free at the kernel space.
1. FPGA: **DMA & BRAM**
    * kernel driver for mapping BRAM memory on the PL side from the Zynq SoC with DMA.
    * It "kmallocates" contiguous memory on the kernel side and transfers it with DMA.
    * Tested on ZCU102 and PYNQ boards with kernels 4.9 and 4.14.
        * TODO: PULL IT to repo after cleaning.
2. FPGA: **REGISTERS**
    * Same thing as the BRAM modules
    * Tested on ZCU102 and PYNQ boards with kernels 4.9 and 4.14.
        * TODO: PULL IT to repo after cleaning.




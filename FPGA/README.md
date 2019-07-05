# FPGA additions to ldd3 Training -Linux Device Drivers- 

## Introduction

kernel drivers for trainning how to access digital hardware at the PL side from a ZYNQ SoC.

## FPGA applications

1. FPGA: **BRAM**
    * kernel driver for mapping BRAM memory at the PL side from the Zynq SoC.
    * It "kmallocates" contiguous memory on the kernel side.
    * At HW project folder, there are sample Vivado 2017.4 projects
    * Tested on ZCU102 and PYNQ boards with kernels 4.9 and 4.14.
        * TODO: control of the available amount of memory free at the kernel space.
2. FPGA: **CDMA memory access**
    * kernel driver for mapping memory at the PL side of the Zynq SoC with DMA.
    * It uses dma_alloc_coherent() to allocate contiguous memory on the kernel side and transfers it with DMA.
    * This driver has been shown to transfer data between about 130 and 400 MB/s, depending on the frequency/parameters combination at the CDMA.
    * Tested now only in PYNQ board kernel 4.14.

## Using the driver
##### A) FPGA HW 
                
+ I) First thing needed is a place from which read/write. For example, an HLS/RTL IP with some registers with known offsets address.
  + If we have created an IP in Vivado_hls, on which a variable has an associated input/output port, in this port, the variable will have some given offset. The most common case is the control port of the ip, for which one can use an axi interface. In this case, the default given offsets use to look like:

```default

-- ------------------------Address Info-------------------
-- 0x00 : Control signals
--        bit 0  - ap_start (Read/Write/COH)
--        bit 1  - ap_done (Read/COR)
--        bit 2  - ap_idle (Read)
--        bit 3  - ap_ready (Read)
--        bit 7  - auto_restart (Read/Write)
--        others - reserved
-- 0x04 : Global Interrupt Enable Register
--        bit 0  - Global Interrupt Enable (Read/Write)
--        others - reserved
-- 0x08 : IP Interrupt Enable Register (Read/Write)
--        bit 0  - Channel 0 (ap_done)
--        bit 1  - Channel 1 (ap_ready)
--        others - reserved
-- 0x0c : IP Interrupt Status Register (Read/TOW)
--        bit 0  - Channel 0 (ap_done)
--        bit 1  - Channel 1 (ap_ready)
--        others - reserved
-- 0x10 : Data signal of total_in
--        bit 31~0 - total_in[31:0] (Read/Write)
-- 0x14 : reserved
-- (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

```
  + For each created port there is a file with the name "IPname_PortName_BusType.vhd" (example: neural_control_s_axi.vhd) where we can find the offset of our variables. This offset is important, because it must be added to the address of the port for correct accesing to the internal variables.
                
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/HLS_vhd.png)

> vhd generated files at HLS project

				
+ II) Second thing needed is a Vivado project where the IP will be instantiated.
                  
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/insert_IP_to_vivado.png)

> Inserting the IP to the Vivado project

				
  + After conecting all the clks, resets, busses, etc., one must take care of the addresses from the IP.
  
                  
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/addresses.png)

> IP addresses in the Vivado project

				
  
  + III) Once all the HW is ready, next thing is to generate the bitstream and export it as an *.bin file
    
                  
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/generate_memory_conf1.png)

> Write memory configuration file

![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/generate_memory_conf2.png)

> Exporting the bitstream as *.bin file

				
				
  + IV) Copy a operative system to a SD, insert the card to the FPGA board, power on it.
				
  + V) Send the bitstream to the FPGA. In my usual configuration, at localhost I like to do,
```console
  $ scp myHW.bin  FPGAuser@FPGAaddres:/tmp
```
				
  + VI) Connect to the FPGA, and configurate it:
			
```console
  $ ssh FPGAuser@FPGAaddres
  $ sudo su -
  # cd /lib/firmware/
  # cp /tmp/myHW.bin .
  # echo 0 > /sys/class/fpga_manager/fpga0/flags
  # echo myHW.bin > /sys/class/fpga_manager/fpga0/firmware
  # exit
  $
```
##### B) Driver preparation

  + I) I will asume that the FPGA board has the 
  + II) clone this repo:

```console
  $ git clone https://github.com/srivera1/ldd3_training.git
```
  + III) Since the changes are not (yet) at master:

```console
  $ git checkout FPGA_kernel
```
  + VI) We would like to write and read some kB to a particular address with DMA. To keep things simpler, we are reading and writing from the same address:
```console
  $  cd FPGA/CDMA/
```
  
  
  
  please, send any amount of money, correction, comment... to srivera(at)alumnos.upm.es

###End

# FPGA additions to ldd3 Training -Linux Device Drivers- 

## Introduction

kernel drivers for training how to access digital hardware at the PL side from a ZYNQ SoC.

## FPGA applications

1. FPGA: **BRAM**
    * kernel driver for mapping (as BRAM) memory at the PL side from the Zynq SoC.
    * It "kmallocates" contiguous memory on the kernel side.
    * At HW project folder, there are sample Vivado 2017.4 projects
    * Tested on ZCU102 and PYNQ boards with kernels 4.9 and 4.14.
        * TODO: control of the available amount of memory free at the kernel space.
2. FPGA: **CDMA memory access**
    * kernel driver for mapping memory at the PL side of the Zynq SoC with DMA.
    * It uses dma_alloc_coherent() to allocate contiguous memory on the kernel side and transfers it with DMA.
    * This driver has shown to transfer data between about 130 and 400 MB/s, depending on the frequency/parameters combination at the CDMA.
    * Tested now only in PYNQ board, kernel 4.14.

## Using the driver
##### A) FPGA HW 
                
+ I) First thing needed is a place from which read/write. For example, an HLS/RTL IP with some registers with known offsets address.
  + If we have created an IP in Vivado_hls, on which a variable has an associated input/output port, in this port, the variable will have some given offset. The most common case is the control port of the IP, for which one can use an axi interface. In this case, the default given offsets use to look like:

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
  + For each created port there is a file with the name "IPname_PortName_BusType.vhd" (example: neural_control_s_axi.vhd) where we can find the offset of our variables. This offset is important because it must be added to the address of the port for correct accessing to the internal variables.
                
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/HLS_vhd.png)

> vhd generated files at HLS project

				
+ II) Second thing needed is a Vivado project where the IP will be instantiated.
                  
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/insert_IP_to_vivado.png)

> Inserting the IP to the Vivado project

				
  + After connecting all the clocks, resets, busses, etc., one must take care of the addresses from the IP.
  
                  
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/addresses.png)

> IP addresses in the Vivado project

				
  
  + III) Once all the HW is ready, next thing is to generate the bitstream and export it as an *.bin file
    
                  
				
![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/generate_memory_conf1.png)

> Write memory configuration file

![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/generate_memory_conf2.png)

> Exporting the bitstream as *.bin file

				
				
  + IV) Copy an operative system to an SD, insert the card to the FPGA board, power on it.
				
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

  + I) I will assume that the FPGA board has the kernel sources, [linux-xlnx](https://github.com/Xilinx/linux-xlnx) located in its file system. You may pre-crosscompile a matching kernel version and upload it to the board. (I find to be faster while developing directly on board).

  + II) clone this repo:

```console
  $ git clone https://github.com/srivera1/ldd3_training.git
```
  + III) Since the changes are not (yet) at master:

```console
  $ git checkout FPGA_kernel
  $  cd FPGA/CDMA/
```
  + IV) On a second console, execute and keep it in sight:

```console
  $ dmesg -w
```
  + V) We would like to write and read some kB to a particular contiguous memory address with DMA. To keep things simpler, we are reading and writing from the same address. In the driver (mmap_CDMA_myHW.c) we define the HW addresses and the amount of allocated memory at kernel space:

```c
#define dataLength  (unsigned long)512000  // available memory
#define MAX_SIZE dataLength   /* max size mmaped to userspace */

...

// addresses should match your HW
#define a_BASE_ADDRESS          0xa0000000  // a address as seen from PS
#define b_BASE_ADDRESS          0xa0000000  // b address as seen from PS
#define a_CDMA_ADDRESS          0xa0000000  // a address as seen from CDMA
#define b_CDMA_ADDRESS          0xa0000000  // b address as seen from CDMA
#define CDMA_BASE_ADDRESS       0x7E200000      
```

![](https://github.com/srivera1/ldd3_training/raw/FPGA_kernel/FPGA/media/data_path.png)

> Data path when reading/writing to the device

  + VI) Also at the user space application (RW_to_HW.c):


```c
#define a_BASE_ADDRESS          0xa0000000
#define b_BASE_ADDRESS          0xa0000000
```
  
  + VII) Driver and application compilation

```console
$ make
$ sudo insmod mmap_CDMA_myHW.ko
$ gcc -Wall RW_to_HW.c -o test; sudo ./test 
```

  + VIII) check dmesg -w output:

```console
[ 4049.139134] HW_transfered(256000 bytes), read time (ns): 643446
[ 4063.611574] hwchar: Device opened
[ 4063.611588] MAX_SIZE: 512000 
[ 4063.612721] HW_transfered(256000 bytes), read time (ns): 643665
[ 4063.613118] hwchar: Device opened
[ 4063.613127] MAX_SIZE: 512000 
```

  + IX) test the driver:

```console
$ mkdir q                                                   # create a folder
$ sudo mount -t tmpfs -o size=20M tmpfs q                   # mount 20M of RAM on q
$ sudo dd if=/dev/zero of=q/zeros.bin bs=256000 count=1     # file with 256000 bytes
$ sudo dd of=/dev/hwchar if=q/zeros.bin bs=256000 count=1   # send 256000 zero bytes with DMA
$ sudo ~/tfm/drivers/gp -g 0xa0000008 -o1010                # modify any memory address (i) with GP (*)
$ sudo ~/tfm/drivers/gp -g 0xa003e7f8 -o1010                # modify any memory address (ii) with GP
$ sudo dd of=q/hwout_z.bin if=/dev/hwchar bs=256000 count=1 # read 256000 bytes with DMA (memory map)
$ xxd q/hwout_z.bin > qq                                    # binary to ascii
$ xxd q/zeros.bin > qz                                      # binary to ascii
$ diff qq qz                                                # check memory map differences
1c1
< 00000000: 0000 0000 0000 0000 f203 0000 0000 0000  ................
---
> 00000000: 0000 0000 0000 0000 0000 0000 0000 0000  ................
16000c16000
< 0003e7f0: 0000 0000 0000 0000 f203 0000 0000 0000  ................
---
> 0003e7f0: 0000 0000 0000 0000 0000 0000 0000 0000  ................

$ vim qq                                                    # check the hole memory map

00000000: 0000 0000 0000 0000 f203 0000 0000 0000  ................
00000010: 0000 0000 0000 0000 0000 0000 0000 0000  ................

...

0003e7e0: 0000 0000 0000 0000 0000 0000 0000 0000  ................
0003e7f0: 0000 0000 0000 0000 f203 0000 0000 0000  ................

```

(*) my version of [gpio-dev-mem-test.c](https://github.com/srivera1/ldd3_training/blob/master/utilities/test_gpio_userspace/gpio-dev-mem-test.c)

  please, send any amount of money, corrections, comments... to srivera(at)alumnos.upm.es


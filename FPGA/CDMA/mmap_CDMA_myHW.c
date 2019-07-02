/* 
 * Kernel space module for writing to HW with CDMA at the PL side of a zynq SoC
 * for older kernels you might have to replace:
 * raw_copy_to_user()   with copy_to_user()
 * raw_copy_from_user() with copy_from_user()
 *
 * inspired by:
 *    https://gist.github.com/laoar/4a7110dcd65dbf2aefb3231146458b39
 *      an example of kernel space to user space zero-copy via mmap
 *
 *    https://raw.githubusercontent.com/Xilinx/embeddedsw/master/XilinxProcessorIPLib/drivers/axicdma/src/xaxicdma.c
 *      The implementation of the API of Xilinx CDMA engine
 *
 * 
 * compilation:
 *  $ make clean ; make ; sudo insmod mmap_myHW.ko
 * 
 * 
 * MY HW just copy data from a to b (a and b are int arrays inside MY HW):
 *  GP0 maps MY HW for debugging purposes
 *  GP1 maps the CDMA configuration port
 *  a has addr 0x80040000 (from the PS) or 0x44A40000 from CDMA's point of view
 *  b has addr 0x80140000 (from the PS) or 0x44B40000 from CDMA's point of view
 * 
 * 
 * The vivado project:
 *  
 * +-----------------------------------------------------------------------------+
 * |                                                                             |
 * |   +----------------+           Vivado HW                                    |
 * |   |                |                                                        |
 * +---+ HP0            |                                                        |
 *     |           GP0  +-------------------------------------+                  |
 *     |  ZYNQ          |                                     |                  |
 *     |   SoC     GP1  +--+  +------------+                  | +--------------+ |
 *     |                |  |  |            |                  | |              | |  +---------+
 *     +----------------+  |  |  smart     |  +-------------+ +-+  smart       +-+  |         |
 *                         +--+  connect   |  |             |   |  connect     |    |         |
 *                            |            +--+  CDMA       +---+              +----+ MY HW   |
 *                            |            |  |             |   |              |    |         |
 *                            +------------+  +-------------+   +--------------+    |         |
 *                                                                                  +---------+
 * 
 * 
 * Data path:
 * 
 *                              +---------+       +---------+
 *                        write |         |       |         |
 *                      +------>+  *ptr   +------>-----+    |
 *                      |       |         |     a |    |    |
 *                      |       |         |       |    |    |
 * /dev/hwchar +--------+       |   RAM   |       |  MY|HW  |
 *                      ^       |         |       |    |    |
 *                      |       |         |       |    |    |
 *                      | read  |         |       |    |    |
 *                      +-------+         +<-----------<    |
 *                              |         |     b |         |
 *                              +---------+       +---------+
 * 
 * performance with the a burst size of 2 (the smallest):
 *   HW_transfered(131072 bytes), read time (ns): 986650 ->  132.845 MB/s
 *   HW_transfered(60000 bytes), write time (ns): 453879 ->  132.194 MB/s
 *   HW_transfered(60000 bytes), read  time (ns): 453958 ->  132.170 MB/s
 * 
 * 
 * 
 * Author  :   Sergio Rivera <srivera@alumnos.upm.es>
 * Date    :   May 2019
 * 
*/

#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h> 
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/dma-mapping.h>

#define dataLength  (unsigned long)1535000  // available bram byte
#define MAX_SIZE dataLength   /* max size mmaped to userspace */

#define DEVICE_NAME "hwchar"
#define  CLASS_NAME "mogu"


#define a_BASE_ADDRESS          0x80040000  // a address as seen from PS
#define b_BASE_ADDRESS          0x80140000  // b address as seen from PS
#define a_CDMA_ADDRESS          0x44A40000  // a address as seen from CDMA
#define b_CDMA_ADDRESS          0x44B40000  // b address as seen from CDMA
#define CDMA_BASE_ADDRESS       0x7E200000      
#define MYIP_BASE_ADDRESS       0x43C00000      

#define XAXICDMA_CR_OFFSET          0x00000000  /**< Control register */
#define XAXICDMA_SR_OFFSET          0x00000004  /**< Status register */
#define XAXICDMA_CDESC_OFFSET       0x00000008  /**< Current descriptor pointer */
#define XAXICDMA_TDESC_OFFSET       0x00000010  /**< Tail descriptor pointer */
#define XAXICDMA_SRCADDR_OFFSET     0x00000018  /**< Source address register */
#define XAXICDMA_DSTADDR_OFFSET     0x00000020  /**< Destination address register */
#define XAXICDMA_BTT_OFFSET         0x00000028  /**< Bytes to transfer */
#define XAXICDMA_CR_RESET_MASK      0x00000004  /**< Reset DMA engine */
#define XAXICDMA_CR_SGMODE_MASK     0x00000008  /**< Scatter gather mode */
#define XAXICDMA_XR_IRQ_IOC_MASK        0x00001000 /**< Completion interrupt */
#define XAXICDMA_XR_IRQ_DELAY_MASK      0x00002000 /**< Delay interrupt */
#define XAXICDMA_XR_IRQ_ERROR_MASK      0x00004000 /**< Error interrupt */
#define XAXICDMA_XR_IRQ_ALL_MASK        0x00007000 /**< All interrupts */
#define XAXICDMA_XR_IRQ_SIMPLE_ALL_MASK 0x00005000 /**< All interrupts for
                                                        simple only mode */
#define XAXICDMA_SR_IDLE_MASK           0x00000002  /**< DMA channel idle */
#define XAXICDMA_SR_SGINCLD_MASK        0x00000008  /**< Hybrid build */
#define XAXICDMA_SR_ERR_INTERNAL_MASK   0x00000010  /**< Datamover internal err */
#define XAXICDMA_SR_ERR_SLAVE_MASK      0x00000020  /**< Datamover slave err */
#define XAXICDMA_SR_ERR_DECODE_MASK     0x00000040  /**< Datamover decode err */
#define XAXICDMA_SR_ERR_SG_INT_MASK     0x00000100  /**< SG internal err */
#define XAXICDMA_SR_ERR_SG_SLV_MASK     0x00000200  /**< SG slave err */
#define XAXICDMA_SR_ERR_SG_DEC_MASK     0x00000400  /**< SG decode err */
#define XAXICDMA_SR_ERR_ALL_MASK        0x00000770  /**< All errors */
#define MAP_SIZE                    4096UL
#define MAP_MASK                    (MAP_SIZE - 1)
#define DDR_MAP_SIZE                0x10000000
#define DDR_MAP_MASK                (DDR_MAP_SIZE - 1)
#define DDR_WRITE_OFFSET            0x000
#define DDR_READ_OFFSET             0x000
#define BUFFER_BYTESIZE             20048 // Length of the buffers for DMA transfer

union data {
    int u;
    char c[4]; // sizeof(int)
};
union datas {
    unsigned long u[MAX_SIZE];
    char c[sizeof(unsigned)*MAX_SIZE];
};
unsigned int ResetMask;

static struct class*  class;
static struct device*  device;
static int major;
static char *sh_mem = NULL; 
static unsigned long sh_mem_phys = 0;

static DEFINE_MUTEX(hwchar_mutex);

static void print_nanos_len(int len, struct timespec diff) {
    if (diff.tv_sec > 0)
        printk ("HW_transfered(%d bytes), read time (ns): %lu\"%lu,", len, diff.tv_sec, diff.tv_nsec );
    else
        printk ("HW_transfered(%d bytes), read time (ns): %lu", len, diff.tv_nsec);
}

static void print_nanos(struct timespec diff) {
    if (diff.tv_sec > 0)
        printk ("read time (ns): %lu\"%lu,", diff.tv_sec, diff.tv_nsec );
    else
        printk ("read time (ns): %lu", diff.tv_nsec);
}
void __iomem *regs;
/* CDMA
 * (this should be minimun to maximize speed)
 */ 
static void cdma_write( int len, unsigned long SRC, unsigned long DST ) {
    volatile unsigned long RegValue = 0;
	iowrite32((unsigned long) SRC , ((volatile unsigned long *) (regs + XAXICDMA_SRCADDR_OFFSET)));
	iowrite32((unsigned long) DST , ((volatile unsigned long *) (regs + XAXICDMA_DSTADDR_OFFSET)));
	iowrite32((unsigned long) len , ((volatile unsigned long *) (regs + XAXICDMA_BTT_OFFSET    )));
    do {
		RegValue = (u32)ioread32(((volatile unsigned long *) (regs + XAXICDMA_SR_OFFSET)));
	} while (!(RegValue & XAXICDMA_SR_IDLE_MASK));
 }

/* copy from kernel space alocated memory to HW.
 *
 */
static void HW_write(int len, unsigned long INPUT_BASE_ADDRESS) {
    union data x;
    x.u=0;
    void __iomem *regs = ioremap_nocache(INPUT_BASE_ADDRESS,0x40);
    int i = 0;
    while(i < len/4) {
      x.u=0;
      x.c[0] = *( sh_mem + 4 * i + 0 );
      x.c[1] = *( sh_mem + 4 * i + 1 );
      x.c[2] = *( sh_mem + 4 * i + 2 );
      x.c[3] = *( sh_mem + 4 * i + 3 );
      iowrite32(x.u ,regs+i*4);
      i++;
    }
    iounmap(INPUT_BASE_ADDRESS);

}

/* copy from HW to kernel space alocated memory.
 *
 */
static void HW_read(int len, unsigned long INPUT_BASE_ADDRESS){
    union data x;
    x.u=0;
    void __iomem *regs = ioremap_nocache(INPUT_BASE_ADDRESS,0x4);
    int i = 0;
    while(i < len/4) {
      x.u=(u32)ioread32(regs+i*4);
      *( sh_mem + 4 * i + 0 ) = x.c[0];
      *( sh_mem + 4 * i + 1 ) = x.c[1];
      *( sh_mem + 4 * i + 2 ) = x.c[2];
      *( sh_mem + 4 * i + 3 ) = x.c[3];
      i++;
    }
    iounmap(INPUT_BASE_ADDRESS);

}


/*  executed once the device is closed or releaseed by userspace
 *  @param inodep: pointer to struct inode
 *  @param filep: pointer to struct file 
 */
static int hwchar_release(struct inode *inodep, struct file *filep){    
    mutex_unlock(&hwchar_mutex);
    //pr_info("hwchar: Device successfully closed\n");

    return 0;
}

/* executed once the device is opened.
 *
 */
static int hwchar_open(struct inode *inodep, struct file *filep){
    int ret = 0; 

    if(!mutex_trylock(&hwchar_mutex)) {
        pr_alert("hwchar: device busy!\n");
        ret = -EBUSY;
        goto out;
    }
 
    pr_info("hwchar: Device opened\n");
    printk("MAX_SIZE: %d \n", MAX_SIZE);

out:
    return ret;
}

/*  mmap handler to map kernel space to user space  
 *
 */
static int hwchar_mmap(struct file *filp, struct vm_area_struct *vma) {
    int ret = 0;
    struct page *page = NULL;
    unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);

    if (size > MAX_SIZE) {
        ret = -EINVAL;
        goto out;  
    } 
   
    page = virt_to_page((unsigned long)sh_mem + (vma->vm_pgoff << PAGE_SHIFT)); 
    ret = remap_pfn_range(vma, vma->vm_start, page_to_pfn(page), size, vma->vm_page_prot);
    if (ret != 0) {
        goto out;
    }   

out:
    return ret;
}

static ssize_t hwchar_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    int ret;
    //printk(KERN_ALERT "HW_read()....");
    struct timespec beginR, endR, diffR;
    getnstimeofday (&beginR);

    cdma_write( (int) len, (unsigned long) b_CDMA_ADDRESS, (unsigned long) (sh_mem_phys ) );
    //cdma_write( (int) len/2, (unsigned long) sh_mem_phys,             (unsigned long) a_CDMA_ADDRESS );
    //HW_read(len);          // <----------------------------

    getnstimeofday (&endR);
    //pr_info("total time read: ----------", len);
    print_nanos_len(len, timespec_sub(endR, beginR));

    if (len > MAX_SIZE) {
        printk(KERN_ALERT "read overflow!\n");
        printk(KERN_ALERT "please, increase max size mmaped to userspace!\n");
        ret = -EFAULT;
        goto out;
    }

    if (raw_copy_to_user(buffer, sh_mem , len) == 0) {
        //pr_info("hwchar: copy %u char to the user\n", len);
        ret = len;

    } else {
        ret =  -EFAULT;   
    } 

out:
    return ret;
}

static int reserve_buffer(void ) {
    pr_info("1\n");
    int id;
    size_t size;
    dma_addr_t paddr;
    pr_info("2\n");
    pr_info("3\n");
    sh_mem = (char *)dma_alloc_coherent(NULL, MAX_SIZE, &paddr, GFP_KERNEL); // FP_ATOMIC|GFP_DMA
    sh_mem_phys = virt_to_phys(sh_mem);
    pr_info("4\n");
    if (sh_mem == NULL)
    {
        printk(KERN_ERR "ERROR: Allocation failure (sh_mem %p).\n", sh_mem);
        return(-1);
    }
    pr_info("5\n");
    return(0);
}

static ssize_t hwchar_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    int ret;
    
    
    if (raw_copy_from_user(sh_mem, buffer, len)) {
        pr_err("hwchar: write fault!\n");
        ret = -EFAULT;
        goto out;
    }

    //pr_info("hwchar: copy %d char from the user\n", len);
    ret = len;

    //printk(KERN_ALERT "HW_write()....");
    struct timespec begin, end, diff;
    getnstimeofday (&begin);
    
    cdma_write( (int) len, (unsigned long) sh_mem_phys, (unsigned long) a_CDMA_ADDRESS );
    //cdma_write( (int) len/2, (unsigned long) sh_mem_phys+(int) len/2, (unsigned long) b_CDMA_ADDRESS );
            // <---------------------------- acceso a HW con DMA

    getnstimeofday (&end);
    //pr_info("total time write: ----------", len);
    print_nanos_len(len, timespec_sub(end, begin));

    //getnstimeofday (&begin);
    //HW_write(len);          // <---------------------------- acceso a HW sin DMA
    //getnstimeofday (&end);
    //print_nanos_len(len, timespec_sub(end, begin));

out:
    return ret;
}

static const struct file_operations hwchar_fops = {
    .open = hwchar_open,
    .read = hwchar_read,
    .write = hwchar_write,
    .release = hwchar_release,
    .mmap = hwchar_mmap,
    .owner = THIS_MODULE,
};

static int __init hwchar_init(void) {
    int ret = 0;    
    
    regs = ioremap_nocache(CDMA_BASE_ADDRESS,0x4);
    struct timespec begin, end, begin0;
    getnstimeofday (&begin0);
    u32 RegValue = 0;
    int TimeOut = 1;
/* init CDMA configuration  */
 //1) Reset CDMA
    do {
        ResetMask = (unsigned long) XAXICDMA_CR_RESET_MASK;
        iowrite32((unsigned long) ResetMask     , ( (regs + XAXICDMA_CR_OFFSET    )));
        /* If the reset bit is still high, then reset is not done */
        ResetMask = (u32)ioread32(((volatile unsigned long *) (regs + XAXICDMA_CR_OFFSET)));
        if (!(ResetMask & XAXICDMA_CR_RESET_MASK)) { break; }
           TimeOut -= 1;
    } while (TimeOut);
    getnstimeofday (&end);
    printk ("1 - ");print_nanos(timespec_sub(end, begin0));
    getnstimeofday (&begin);
 //2) enable Interrupt
    RegValue = (u32)ioread32(((volatile unsigned long *) (regs + XAXICDMA_CR_OFFSET)));
    RegValue = (unsigned long) (RegValue | XAXICDMA_XR_IRQ_ALL_MASK);
    iowrite32((unsigned long) RegValue          , ((volatile unsigned long *) (regs + XAXICDMA_CR_OFFSET    )));
    getnstimeofday (&end);
    printk ("2 - ");print_nanos(timespec_sub(end, begin0));
    getnstimeofday (&begin);
 //3) Checking for the Bus Idle
    RegValue = (u32)ioread32(((volatile unsigned long *) (regs + XAXICDMA_SR_OFFSET)));
    if (!(RegValue & XAXICDMA_SR_IDLE_MASK)) {
        printk("BUS IS BUSY Error Condition \n\r");
        return 1;
    }
    getnstimeofday (&end);
    printk ("3 - ");print_nanos(timespec_sub(end, begin0));
    getnstimeofday (&begin);
 //4) Check the DMA Mode and switch it to simple mode
    RegValue = (u32)ioread32(((volatile unsigned long *) (regs + XAXICDMA_CR_OFFSET)));
    if ((RegValue & XAXICDMA_CR_SGMODE_MASK)) {
        RegValue = (unsigned long) (RegValue & (~XAXICDMA_CR_SGMODE_MASK));
        printk("Reading \n \r");
        iowrite32((unsigned long) RegValue      , ((volatile unsigned long *) (regs + XAXICDMA_CR_OFFSET)));
    }
/* end CDMA configuration   */

    printk(KERN_INFO "trying to register the device /dev/hwchar \n");

    major = register_chrdev(0, DEVICE_NAME, &hwchar_fops);

    if (major < 0) {
        pr_info("hwchar: fail to register major number!");
        ret = major;
        printk(KERN_ALERT "failed to register device /dev/hwchar \n");
        goto out;
    }

    class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(class)){ 
        unregister_chrdev(major, DEVICE_NAME);
        pr_info("hwchar: failed to register device class");
        ret = PTR_ERR(class);
        goto out;
    }

    device = device_create(class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(device)) {
        class_destroy(class);
        unregister_chrdev(major, DEVICE_NAME);
        ret = PTR_ERR(device);
        printk(KERN_ALERT "failed to register device /dev/hwchar \n");
        goto out;
    }

    printk(KERN_INFO "mapping memory to device /dev/hwchar \n");

    /* init this mmap area */
    //sh_mem = dma_alloc_coherent(); // GFP_DMA -> contiguous physical memory


    printk("7.0\n");
    //sh_mem = kmalloc(MAX_SIZE, GFP_ATOMIC|GFP_DMA); // GFP_DMA -> Kmalloc contiguous physical memory
    reserve_buffer(); // dma_alloc_coherent contiguous physical memory

    printk("7\n");
    if (sh_mem == NULL) {
        ret = -ENOMEM; 
        printk("8\n");
        goto out;
    }
    printk("9\n");
    
    // printk("memory allocated: %d\n", ksize(sh_mem));
    mutex_init(&hwchar_mutex);
out: 
    return ret;
}

static void __exit hwchar_exit(void) {

    printk(KERN_INFO "trying to unregister the device /dev/hwchar \n");

    mutex_destroy(&hwchar_mutex); 
    device_destroy(class, MKDEV(major, 0));  
    class_unregister(class);
    class_destroy(class); 
    unregister_chrdev(major, DEVICE_NAME);
    iounmap(CDMA_BASE_ADDRESS);
    
    //kfree(sh_mem);
    sh_mem=NULL; // ->  NULL pointer dereference 

    pr_info("hwchar: unregistered!");
}

module_init(hwchar_init);
module_exit(hwchar_exit);
MODULE_LICENSE("GPL");

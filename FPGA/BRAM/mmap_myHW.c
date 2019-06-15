/* 
 * Kernel space module for writing to a BRAM at the PL side of a zynq SoC
 * for older kernels you might have to replace:
 * raw_copy_to_user()   with copy_to_user()
 * raw_copy_from_user() with copy_from_user()
 *
 * inspired on https://gist.github.com/laoar/4a7110dcd65dbf2aefb3231146458b39
 * an example of kernel space to user space zero-copy via mmap
 * 
 *  $ make clean ; make ; sudo insmod mmap_myHW.ko
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

#define MAX_SIZE ( PAGE_SIZE * 10 )   /* max size mmaped to userspace ;
                                         if PAGE_SIZE == 4094,
                                              1MB = PAGE_SIZE * 200 + 1 */
#define DEVICE_NAME "mchar"
#define  CLASS_NAME "mogu"


#define BRAM_BASE_ADDRESS           0x40000000

union data {
    int u;
    char c[4]; // sizeof(int)
};
union datas {
    unsigned u[MAX_SIZE];
    char c[sizeof(unsigned)*MAX_SIZE];
};

static struct class*  class;
static struct device*  device;
static int major;
static char *sh_mem = NULL; 

static DEFINE_MUTEX(mchar_mutex);

/* copy from kernel space alocated memory to BRAM.
 *
 */
static void bram_write(int len)
{
    union data x;
    x.u=0;
    void __iomem *regs = ioremap_nocache(BRAM_BASE_ADDRESS,0x40);
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
    iounmap(BRAM_BASE_ADDRESS);

}

/* copy from BRAM to kernel space alocated memory.
 *
 */
static void bram_read(int len)
{
    union data x;
    x.u=0;
    void __iomem *regs = ioremap_nocache(BRAM_BASE_ADDRESS,0x4);
    int i = 0;
    while(i < len/4) {
      x.u=(u32)ioread32(regs+i*4);
      *( sh_mem + 4 * i + 0 ) = x.c[0];
      *( sh_mem + 4 * i + 1 ) = x.c[1];
      *( sh_mem + 4 * i + 2 ) = x.c[2];
      *( sh_mem + 4 * i + 3 ) = x.c[3];
      i++;
    }
    iounmap(BRAM_BASE_ADDRESS);

}


/*  executed once the device is closed or releaseed by userspace
 *  @param inodep: pointer to struct inode
 *  @param filep: pointer to struct file 
 */
static int mchar_release(struct inode *inodep, struct file *filep)
{    
    mutex_unlock(&mchar_mutex);
    pr_info("mchar: Device successfully closed\n");

    return 0;
}

/* executed once the device is opened.
 *
 */
static int mchar_open(struct inode *inodep, struct file *filep)
{
    int ret = 0; 

    if(!mutex_trylock(&mchar_mutex)) {
        pr_alert("mchar: device busy!\n");
        ret = -EBUSY;
        goto out;
    }
 
    pr_info("mchar: Device opened\n");
    printk("MAX_SIZE: %d \n", MAX_SIZE);

out:
    return ret;
}

/*  mmap handler to map kernel space to user space  
 *
 */
static int mchar_mmap(struct file *filp, struct vm_area_struct *vma)
{
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

static ssize_t mchar_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    int ret;
    printk(KERN_ALERT "bram_read()....");
    struct timespec begin, end, diff;
    getnstimeofday (&begin);

    bram_read(len);          // <----------------------------

    getnstimeofday (&end);
    diff = timespec_sub(end, begin);
    if (diff.tv_sec > 0)
        printk ("bram_read(%d bytes), read time (ns): %lu\"%lu,", len, diff.tv_sec, diff.tv_nsec );
    else
        printk ("bram_read(%d bytes), read time (ns): %lu", len, diff.tv_nsec);

    if (len > MAX_SIZE) {
        printk(KERN_ALERT "read overflow!\n");
        printk(KERN_ALERT "please, increase max size mmaped to userspace!\n");
        ret = -EFAULT;
        goto out;
    }

    if (raw_copy_to_user(buffer, sh_mem, len) == 0) {
        pr_info("mchar: copy %u char to the user\n", len);
        ret = len;

    } else {
        ret =  -EFAULT;   
    } 

out:
    return ret;
}

static ssize_t mchar_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    int ret;
    
    
    if (raw_copy_from_user(sh_mem, buffer, len)) {
        pr_err("mchar: write fault!\n");
        ret = -EFAULT;
        goto out;
    }

    pr_info("mchar: copy %d char from the user\n", len);
    ret = len;

    printk(KERN_ALERT "bram_write()....");
    struct timespec begin, end, diff;
    getnstimeofday (&begin);

    bram_write(len);          // <----------------------------

    getnstimeofday (&end);
    diff = timespec_sub(end, begin);
    if (diff.tv_sec > 0)
        printk ("bram_write(%d bytes), write time (ns): %lu\"%lu,", len, diff.tv_sec, diff.tv_nsec );
    else
        printk ("bram_write(%d bytes), write time (ns): %lu,", len, diff.tv_nsec );


out:
    return ret;
}

static const struct file_operations mchar_fops = {
    .open = mchar_open,
    .read = mchar_read,
    .write = mchar_write,
    .release = mchar_release,
    .mmap = mchar_mmap,
    .owner = THIS_MODULE,
};

static int __init mchar_init(void)
{
    int ret = 0;    
    
    printk(KERN_INFO "trying to register the device /dev/mchar \n");

    major = register_chrdev(0, DEVICE_NAME, &mchar_fops);

    if (major < 0) {
        pr_info("mchar: fail to register major number!");
        ret = major;
        printk(KERN_ALERT "failed to register device /dev/mchar \n");
        goto out;
    }

    class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(class)){ 
        unregister_chrdev(major, DEVICE_NAME);
        pr_info("mchar: failed to register device class");
        ret = PTR_ERR(class);
        goto out;
    }

    device = device_create(class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(device)) {
        class_destroy(class);
        unregister_chrdev(major, DEVICE_NAME);
        ret = PTR_ERR(device);
        printk(KERN_ALERT "failed to register device /dev/mchar \n");
        goto out;
    }

    printk(KERN_INFO "mapping memory to device /dev/mchar \n");

    /* init this mmap area */
    sh_mem = kmalloc(MAX_SIZE, GFP_ATOMIC|GFP_DMA); // GFP_DMA -> contiguous physical memory
    if (sh_mem == NULL) {
        ret = -ENOMEM; 
        goto out;
    }
    
    printk("memory allocated: %d\n", ksize(sh_mem));
    mutex_init(&mchar_mutex);
out: 
    return ret;
}

static void __exit mchar_exit(void)
{

    printk(KERN_INFO "trying to unregister the device /dev/mchar \n");

    mutex_destroy(&mchar_mutex); 
    device_destroy(class, MKDEV(major, 0));  
    class_unregister(class);
    class_destroy(class); 
    unregister_chrdev(major, DEVICE_NAME);
    
    kfree(sh_mem);

    pr_info("mchar: unregistered!");
}

module_init(mchar_init);
module_exit(mchar_exit);
MODULE_LICENSE("GPL");

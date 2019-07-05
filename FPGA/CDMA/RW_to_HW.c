/* x86_64 + arm
 * 
 *  sudo insmod ~/tfm/modules/kalloc/module.ko
 *  gcc -Wall -o rwtohw RW_to_HW.c ; sudo ./rwtohw
*/


#include <stdio.h>     // printf
#include <string.h>    // memcpy
#include <sys/mman.h>  // mmap, munmap
#include <fcntl.h>  
#include <unistd.h>  
#include <sys/mman.h>  
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#define CDMA_BASE_ADDRESS       0x7E200000      
#define MYIP_BASE_ADDRESS       0x43C00000  
#define a_BASE_ADDRESS          0x80040000
#define b_BASE_ADDRESS          0x80140000

#define dataLength ( 3*4*4000 )// 1565900
#define vectorLength ( dataLength/3/4 )// size of a, b, c vectors

#define DEVICE_FILENAME "/dev/hwchar"  // CDMA mapped device
#define DEVICE_FILENAME2 "/dev/mem"    //

union uf {
  float f;
  unsigned u;
};

union data {
    unsigned u[ dataLength/sizeof(float) ];
    char c[ dataLength ];
};


union ifloat {
    float f;
    int i;
};

union ufloat {
    float f;
    unsigned u;
};

int fd2;
void *ptr2;
unsigned page_size2;
unsigned page_offset2;

int readMem(void *ptr2, unsigned page_offset2) {
    return *((unsigned *)(ptr2 + page_offset2));
}

int readMemInt(unsigned base_addr, unsigned offset_addr) {
    page_size2=sysconf(_SC_PAGESIZE);
    unsigned hw_addr = base_addr + offset_addr;
    unsigned page_addr = (hw_addr & (~(page_size2-1)));
    page_offset2 = hw_addr - page_addr;
    ptr2 = mmap(NULL, page_size2, PROT_READ|PROT_WRITE, MAP_SHARED, fd2, page_addr);
    return readMem(ptr2,page_offset2);
}

void writeMem(void *ptr2, unsigned page_offset2, int value) {
    *((unsigned *)(ptr2 + page_offset2)) = value;
    //return 0;
}

void writeMemFloat(unsigned base_addr, unsigned offset_addr, float value) {
    page_size2=sysconf(_SC_PAGESIZE);
    unsigned hw_addr = base_addr + offset_addr;
    unsigned page_addr = (hw_addr & (~(page_size2-1)));
    page_offset2 = hw_addr - page_addr;
    ptr2 = mmap(NULL, page_size2, PROT_READ|PROT_WRITE, MAP_SHARED, fd2, page_addr);
    union ifloat u3;
    u3.f=value; 
    writeMem(ptr2,page_offset2,u3.i);
    //return 0;
}

void writeMemInt(unsigned base_addr, unsigned offset_addr, int value) {
    page_size2=sysconf(_SC_PAGESIZE);
    unsigned hw_addr = base_addr + offset_addr;
    unsigned page_addr = (hw_addr & (~(page_size2-1)));
    page_offset2 = hw_addr - page_addr;
    ptr2 = mmap(NULL, page_size2, PROT_READ|PROT_WRITE, MAP_SHARED, fd2, page_addr);
    writeMem(ptr2,page_offset2,value);
    //return 0;
}

void sendToHW(union data data) {
  int fd;  
  int ret;
  // open device
  fd = open(DEVICE_FILENAME, O_RDWR|O_NDELAY); 
  void* mem = mmap(0, dataLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  
  ret = write(fd, data.c, dataLength);
  if (ret < 0) {
      printf("write error!\n");
      ret = errno;
  }

  close(fd); 
  munmap(mem, sizeof(data));
}

union data getFromHW(union data data) {
  int fd; 
  int ret; 
  fd = open(DEVICE_FILENAME, O_RDWR|O_NDELAY); 
  void* mem = mmap(0, dataLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); 
  // copy from kernel space
 // memcpy(data.c, mem, sizeof(data));
  ret = read(fd, data.c, dataLength );
  if (ret < 0) {
      printf("read error!\n");
      ret = errno;
  }
  close(fd); 
  // Free up allocated memory
  munmap(mem, sizeof(data.c));
  return data;
}

void getFromHW_NODMA(void) {
  int k = 0;
  while(k < vectorLength){
    printf(" a %08x", readMemInt(a_BASE_ADDRESS, 4*k                   ));
    printf(" %08x",   readMemInt(a_BASE_ADDRESS, 4*k + vectorLength*4  ));
    printf(" %08x",   readMemInt(a_BASE_ADDRESS, 4*k + 2*vectorLength*4));
    printf("  b %08x",readMemInt(b_BASE_ADDRESS, 4*k                   ));
    printf(" %08x",   readMemInt(b_BASE_ADDRESS, 4*k + vectorLength*4  ));
    printf(" %08x \n",readMemInt(b_BASE_ADDRESS, 4*k + 2*vectorLength*4));
//                      , readMemInt(b_BASE_ADDRESS, 4*k));
    k++;
  }
}

void sendToHW_NODMA(void) {
  int k = 0;
  while(k < vectorLength){
    writeMemInt(a_BASE_ADDRESS, 4*k                   , 0xb);
    writeMemInt(a_BASE_ADDRESS, 4*k +   vectorLength*4, 0xf);
    writeMemInt(a_BASE_ADDRESS, 4*k + 2*vectorLength*4, 0xc);
    k++;
  }
}

int main () {
  srand(time(NULL));
  fd2 = open ("/dev/mem", O_RDWR);
  union data x;
  union data y;
  union uf tmp;
  //x.u[0]=tmp.u;
  //memset(x.c,tmp.u,sizeof(unsigned));
  //printf("dataLength : %f\n", ((float)2)/((float)3)*(float)dataLength/sizeof(float));
  for(int i = 0; i<dataLength/sizeof(float); i++){
    if(i<((float)2)/((float)3)*(float)dataLength/sizeof(float))
      tmp.f=(12.21f*i*10.0f);
    else
      tmp.f=(0.0f*i);
    x.u[i]=rand();//tmp.u;

   // printf("x.u[i] %p \n",x.u[i]);
  }

  sendToHW(x);
  //sendToHW_NODMA(); // for testing

  writeMemInt(MYIP_BASE_ADDRESS,0x0,0x1);

  while(readMemInt(MYIP_BASE_ADDRESS,0x0)!=0x4){printf(" readMemInt(MYIP_BASE_ADDRESS,0x0):  %d\n",readMemInt(MYIP_BASE_ADDRESS,0x0) );}

/*
// .............................................

    Now MY HW is filled with our data

// .............................................
*/

  y=getFromHW(y);
  int j = 0;
  int good = 1;
  printf("\n x.u      y.u  \n");
  for(int i = 0; i<dataLength/sizeof(float); i++){   // dataLength
    union uf tmp2;
    union uf tmp3;
    tmp2.u=y.u[i];
    tmp3.u=x.u[i];
    //printf("%p %p %d %f %f\n", x.u[i], y.u[i], i , tmp3.f, tmp2.f);
    if((x.u[i] != y.u[i])) {
      printf("%p != %p ->  error at index %d\n", x.u[i], y.u[i], i );
      good = 0;
      /* if DMA fails we check with /dev/mem */
      getFromHW_NODMA();
      break;
    }
  }
  if(good == 1)
      printf("\nwe are all good!!\n");
  printf("\n");
}

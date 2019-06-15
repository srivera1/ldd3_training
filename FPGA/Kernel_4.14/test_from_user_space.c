/* x86_64 + arm
 * 
 *  1) sudo insmod module.ko 
 *  2) gcc -Wall -o test_from_user_space test_from_user_space.c ; sudo ./test_from_user_space
 * 
 * 
 * Author  :   Sergio Rivera <srivera@alumnos.upm.es>
 * Date    :   May 2019
 * 
*/


#include <stdio.h> 
#include <string.h>
#include <sys/mman.h> 
#include <fcntl.h>  
#include <unistd.h>  
#include <sys/mman.h>  
#include <errno.h>

#define dataLength ( 4096 )

#define DEVICE_FILENAME "/dev/mchar"  

union uf {
  float f;
  unsigned u;
};

union data {
	unsigned u[ dataLength/sizeof(float) ];
	char c[ dataLength ];
};

void sendToHW(union data data) {
  int fd;  
  int ret;
  // open device
  fd = open(DEVICE_FILENAME, O_RDWR|O_NDELAY); 
  void* mem = mmap(0, dataLength, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  
  // our floats array is passed as chars array
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
  // our floats array is passed as chars array 
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

int main () {
  union data x;
  union data y;
  union uf tmp;
  x.u[0]=tmp.u;

  for(int i = 0; i<dataLength/sizeof(float); i++){
    // We can pass any kind of data to our HW.
    // But since we are passing floats,
    // the total amount is limited to dataLength/sizeof(float)
    tmp.f=1.01f+10.0f*i;
    x.u[i]=tmp.u;
  }

  sendToHW(x);

/*
// .............................................

    Now the BRAM is filled with our data

// .............................................
*/

  y=getFromHW(y);

  int j = 0;
  int good = 1;
  printf("\n x.u      y.u  \n");
  for(int i = 0; i<dataLength/sizeof(float); i++){
    if((x.u[i] != y.u[i]) ) {
      printf("%p != %p ->  error at index %d\n", x.u[i], y.u[i], i );
      good = 0;
      break;
    }
  }
  if(good == 1)
      printf("\nwe are all good!!\n");
  printf("\n");
}


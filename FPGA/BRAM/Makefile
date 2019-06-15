obj-m += mmap_myHW.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules -lm

clean:
	rm -fr mmap_myHW.*o

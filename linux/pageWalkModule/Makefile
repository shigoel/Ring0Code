obj-m += pageWalk.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

run: all
	sudo insmod ./pageWalk.ko
	sudo rmmod pageWalk
	dmesg | tail

stage1: ../stage1/stage1.c
	arm-linux-gnueabi-gcc ../stage1/stage1.c -o stage1
stage2: ../stage2/stage2.c
	arm-linux-gnueabi-gcc ../stage2/stage2.c -o stage2
stage2_tec: ../stage2/stage2_tec.c
	arm-linux-gnueabi-gcc ../stage2/stage2_tec.c -o stage2_tec
stage3: ../stage3/stage3.c
	arm-linux-gnueabi-gcc -o stage3 ../stage3/stage3.c
clear:
	rm -rf stage* *~

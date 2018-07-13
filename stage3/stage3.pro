TEMPLATE = app
#CONFIG -= console
#CONFIG -= app_bundle
CONFIG -= qt
INCLUDEPATH += "/home/Igor/cross_arm_gcc-6.3.0/arm-linux-gnueabi/include"
#INCLUDEPATH += "/home/Igor/intelFPGA_lite/EDS/embedded//ip/altera/hps/altera_hps/hwlib/include"
#INCLUDEPATH += "/home/Igor/intelFPGA_lite/EDS/embedded//ip/altera/hps/altera_hps/hwlib/include/soc_cv_av"
#QMAKE_CXXFLAGS = "-I/home/Igor/cross_arm_gcc-6.3.0/arm-linux-gnueabi/include"
#QMAKE_CFLAGS = "-I/home/Igor/cross_arm_gcc-6.3.0/arm-linux-gnueabi/include"

SOURCES += stage3.c \
           ../build/Makefile \


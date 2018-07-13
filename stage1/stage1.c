#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
//#include <socal/alt_h2f.h>
////#include <cursesp.h>
//#include "hwlib.h"
//#include "socal/socal.h"
//#include "socal/hps.h"
//#include "socal/alt_gpio.h"

#define OFF_MEM_2P 0x40000

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

int main(int argc, char *argv[])
{
    void *virtual_base;
    int fd;
    size_t length = 0xC0000;
    off_t offset = 0xC0000000, \
            pa_offset;
    struct stat sb;

    fd = open("/dev/mem", (O_RDWR | O_SYNC));
    if (fd == -1) {
        printf( "ERROR: could not open \"/dev/mem\"...\n" );
        return( 1 );
    }

    if (fstat(fd, &sb) == -1)
        handle_error("fstat");
    printf("\"/dev/mem\"  size: %ld\n", sb.st_size);

    long ps = sysconf(_SC_PAGESIZE);
    printf("Page size: %ld\n", ps);
    pa_offset = offset & ~(ps-1);
    size_t delta = offset - pa_offset;

    virtual_base = mmap(NULL, length + delta, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, pa_offset);
    if(virtual_base == MAP_FAILED) {
        printf( "ERROR: mmap() failed...\n" );
        close( fd );
        return( 1 );
    }

    printf("offset: %#x\n", offset);
    printf("pa_offset: %#x\n", pa_offset);
    printf("delta: %#x\n", delta);
    size_t n = sizeof(unsigned int);
    printf("size int, bytes: %u\n", n);

    char test = '0';
    if (argc == 2) test = *argv[1];

    if (test == '1') {
        //--------------------------------------- begin and end test ------------------------------------------------
        unsigned int dataA5 = 0xA5A5A5A5;
        unsigned int dataB5 = 0xB5B5B5B5;
        unsigned int readData;

        printf("\n   In cycle:\n"
               "1. Write 0xA5A5A5A5 into 0x40000\n"
               "2. Write 0xB5B5B5B5 into 0xBFFFC\n"
               "3. Read data from 0x40000\n"
               "4. Read data from 0xBFFFC\n"
               );

        unsigned int* wroteData;
//        long i=0;
        while(1) {
//            if (i==10) {
//                sleep(1);
//                i=0;
//            }
            wroteData = (unsigned int*)memcpy(virtual_base + 0x40000 + delta, (void*)&dataA5, n);
//            printf("wrote data to  0x40000: %#x\n", *wroteData);
//            sleep(1);

            wroteData = (unsigned int*)memcpy(virtual_base + 0xBFFFC + delta, (void*)&dataB5, n);
//            printf("wrote data to  0xBFFFC: %#x\n", *wroteData);
//            sleep(1);

            memcpy((void*)&readData, virtual_base + 0x40000 + delta, n);
//            printf("read data from 0x40000: %#x\n", readData);
//            sleep(1);

            memcpy((void*)&readData, virtual_base + 0xBFFFC + delta, n);
//            printf("read data from 0xBFFFC: %#x\n", readData);
//            sleep(1);
//            i++;
        }
        //--------------------------------------- begin and end test ------------------------------------------------
    } else if (test == '2') {
        while (1) {
            //----------------------------------------------------------- mem_test -----------------------------------------------------
            printf("\n");
            unsigned int readData;
            ulong wErrors = 0;
            ulong rErrors = 0;
            //------------ address test -----------
            for (ulong i=OFF_MEM_2P; i<0x80000; i+=n) {
                memcpy(virtual_base + i + delta, (void*)&i, n);
                //printf("Write error: addr=%#x, value=%#x\n", i, *wroteData);
            }

            for (ulong i=OFF_MEM_2P; i<0x80000; i+=n) {
                memcpy((void*)&readData, virtual_base + i + delta, n);
                if (readData != i) {
                    rErrors++;
                    //printf("Read error: addr=%#x, value=%#x\n", i, readData);
                }
            }
            printf("The count of reading address errors: %u\n", rErrors);
            //------------ address test -----------

            wErrors = 0;
            rErrors = 0;
            //------------ inverse address test -----------
            ulong invi;
            for (ulong i=OFF_MEM_2P; i<0x80000; i+=n) {
                invi = ~i;
                memcpy(virtual_base + i + delta, (void*)&invi, n);
                //printf("Write error: addr=%#x, value=%#x\n", i, *wroteData);
            }

            for (ulong i=OFF_MEM_2P; i<0x80000; i+=n) {
                memcpy((void*)&readData, virtual_base + i + delta, n);
                if (readData != ~i) {
                    rErrors++;
                    //printf("Read error: addr=%#x, value=%#x\n", i, readData);
                }
            }
            printf("The count of reading inverse address errors: %u\n", rErrors);
            //------------ inverse address test -----------
            //----------------------------------------------------------- mem_test -----------------------------------------------------
        }
    } else {
        printf("Unknown test number\n");
    }

    return 0;
}

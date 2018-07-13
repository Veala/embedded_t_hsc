#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

//#define debug

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

void *virtual_base;
size_t beginAddr, endAddr, range;
char test = '0';
char *purpose;
size_t delta;
size_t n;

void regsNSK() {
    if (test == '1') {
        //--------------------------------------- begin and end test ------------------------------------------------
        unsigned int dataA5 = 0xA5A5A5A5;
        unsigned int dataB5 = 0xB5B5B5B5;
        unsigned int readData;

        printf("\n   In cycle:\n"
               "1. Write 0xA5A5A5A5 into %#x\n"
               "2. Write 0xB5B5B5B5 into %#x\n"
               "3. Read data from %#x\n"
               "4. Read data from %#x\n", beginAddr, endAddr-3, beginAddr, endAddr-3
               );

        unsigned int* wroteData;
        while(1) {
            wroteData = (unsigned int*)memcpy(virtual_base + beginAddr + delta, (void*)&dataA5, n);
            wroteData = (unsigned int*)memcpy(virtual_base + endAddr-3 + delta, (void*)&dataB5, n);
            memcpy((void*)&readData, virtual_base + beginAddr + delta, n);
            memcpy((void*)&readData, virtual_base + endAddr-3 + delta, n);
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
            for (ulong i=beginAddr; i<range; i+=n)
                memcpy(virtual_base + i + delta, (void*)&i, n);

            for (ulong i=beginAddr; i<range; i+=n) {
                memcpy((void*)&readData, virtual_base + i + delta, n);
                if (readData != i) {
                    rErrors++;
                }
            }
            printf("The count of reading address errors: %u\n", rErrors);
            //------------ address test -----------

            wErrors = 0;
            rErrors = 0;
            //------------ inverse address test -----------
            ulong invi;
            for (ulong i=beginAddr; i<range; i+=n) {
                invi = ~i;
                memcpy(virtual_base + i + delta, (void*)&invi, n);
            }

            for (ulong i=beginAddr; i<range; i+=n) {
                memcpy((void*)&readData, virtual_base + i + delta, n);
                if (readData != ~i) {
                    rErrors++;
                }
            }
            printf("The count of reading inverse address errors: %u\n", rErrors);
            //------------ inverse address test -----------
            //----------------------------------------------------------- mem_test -----------------------------------------------------
        }
    } else {
        printf("Unknown test number of 'arg1'\n");
    }
}

void regsVSK() {

}

void memNSK() {
    if (test == '1') {
        //--------------------------------------- begin and end test ------------------------------------------------
        unsigned int dataA5 = 0xA5A5;
        unsigned int dataB5 = 0xB5B5;
        unsigned int readData;

        printf("\n   In cycle:\n"
               "1. Write 0xA5A5 into %#x\n"
               "2. Write 0xB5B5 into %#x\n"
               "3. Read data from %#x\n"
               "4. Read data from %#x\n", beginAddr, endAddr-3, beginAddr, endAddr-3
               );

        unsigned int* wroteData;
        while(1) {
            wroteData = (unsigned int*)memcpy(virtual_base + beginAddr + delta, (void*)&dataA5, n);
            wroteData = (unsigned int*)memcpy(virtual_base + endAddr-3 + delta, (void*)&dataB5, n);
            memcpy((void*)&readData, virtual_base + beginAddr + delta, n);
            memcpy((void*)&readData, virtual_base + endAddr-3 + delta, n);
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
            for (ulong i=beginAddr; i<range; i+=n)
                memcpy(virtual_base + i + delta, (void*)&i, n);

            for (ulong i=beginAddr; i<range; i+=n) {
                memcpy((void*)&readData, virtual_base + i + delta, n);
                if (readData != i) {
                    rErrors++;
                }
            }
            printf("The count of reading address errors: %u\n", rErrors);
            //------------ address test -----------

            wErrors = 0;
            rErrors = 0;
            //------------ inverse address test -----------
            ulong invi;
            for (ulong i=beginAddr; i<range; i+=n) {
                invi = ~i;
                memcpy(virtual_base + i + delta, (void*)&invi, n);
            }

            for (ulong i=beginAddr; i<range; i+=n) {
                memcpy((void*)&readData, virtual_base + i + delta, n);
                if (readData != ~i) {
                    rErrors++;
                }
            }
            printf("The count of reading inverse address errors: %u\n", rErrors);
            //------------ inverse address test -----------
            //----------------------------------------------------------- mem_test -----------------------------------------------------
        }
    } else {
        printf("Unknown test number of 'arg1'\n");
    }

}

void memVSK() {
    //----------------------------------------------------------- mem_test -----------------------------------------------------
    printf("\n");
    void *data = malloc(range);
    if (data == NULL) {
        printf( "ERROR: could not malloc call...\n" );
        return;
    }
    memcpy(data, virtual_base + beginAddr + delta, range);
    int fd = open("./memVSK.txt", (O_RDWR | O_SYNC));
    if (fd == -1) {
        printf( "ERROR: could not open \"./memVSK.txt\"...\n" );
        return;
    }
    if (write(fd, data, range) == -1) {
        printf( "ERROR: could not write data to \"./memVSK.txt\"...\n" );
        return;
    }
    if (close(fd) == -1)    handle_error("close");
    printf("Data wrote to \"memVSK.txt\" file\n");
    //----------------------------------------------------------- mem_test -----------------------------------------------------
}

void regsAUX() {
    memNSK();
}

int main(int argc, char *argv[])
{
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

    long ps = sysconf(_SC_PAGESIZE);
    pa_offset = offset & ~(ps-1);
    delta = offset - pa_offset;

    virtual_base = mmap(NULL, length + delta, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, pa_offset);
    if(virtual_base == MAP_FAILED) {
        printf( "ERROR: mmap() failed...\n" );
        close( fd );
        return( 1 );
    }

    n = sizeof(unsigned int);

#ifdef debug
    printf("\"/dev/mem\"  size: %ld\n", sb.st_size);
    printf("page size: %ld\n", ps);
    printf("offset: %#x\n", offset);
    printf("pa_offset: %#x\n", pa_offset);
    printf("delta: %#x\n", delta);
    printf("size int, bytes: %u\n\n", n);
#endif

    if (argc < 3) {
        printf("Arguments is not set to \"./stage2 'arg1' 'arg2'\".\n"
               "arg1 = 1|2\n"
               "arg2 = regsNSK|regsVSK|regsAUX|memNSK|memVSK\n");
        if (munmap(virtual_base, length + delta) == -1) handle_error("munmap");
        if (close(fd) == -1)    handle_error("close");
        return 1;
    }

    test = *argv[1];
    purpose = argv[2];
    void (*func)();

    if        (strcmp(purpose, "regsNSK") == 0) {
        beginAddr = 0x00000;    endAddr = 0x0007f; func = &regsNSK;
    } else if (strcmp(purpose, "regsVSK") == 0) {
        beginAddr = 0x00080;    endAddr = 0x000ff; func = &regsVSK;
    } else if (strcmp(purpose, "regsAUX") == 0) {
        beginAddr = 0x00180;    endAddr = 0x001bf; func = &regsAUX;
    } else if (strcmp(purpose, "memNSK") == 0) {
        beginAddr = 0x04000;    endAddr = 0x07fff; func = &memNSK;
    } else if (strcmp(purpose, "memVSK") == 0) {
        beginAddr = 0x08000;    endAddr = 0x09fff; func = &memVSK;
    } else {
        printf("Unknown 'arg2'\n");
        if (munmap(virtual_base, length + delta) == -1) handle_error("munmap");
        if (close(fd) == -1)    handle_error("close");
        return 1;
    }
    range = endAddr - beginAddr + 1;

#ifdef debug
    printf("beginAddr: %#x\n", beginAddr);
    printf("endAddr: %#x\n", endAddr);
    printf("range: %#x\n", range);
#endif

    func();

    return 0;
}

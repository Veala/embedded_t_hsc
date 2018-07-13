#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

//#define debug

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

void *virtual_base;
size_t delta;
size_t n;

char cmd[20], mode[2];
char *writeC = "w", *readC = "r", *writereadC = "wr", *fileDC = "df", \
        *fileAC = "af", *helpC = "h", *exitC = "e";
uint addr, data, range, addrinc, datainc, inverse, output; long inCycle;

long it, decrement;
void cycle_cfg() {
    it=0, decrement=0;
    if (inCycle == 0) { it=-1;  decrement=-1;   }
}

void also_write() {
    cycle_cfg();
    for (; it<inCycle; it=it+1+decrement) {
        ulong final;
        for (ulong i=addr, j=data; i<range; i+=addrinc, j+=datainc) {
            if (inverse) final = ~j;    else    final = j;
            memcpy(virtual_base + i + delta, (void*)&final, n);
        }
    }
}

void also_read() {
    cycle_cfg();
    for (; it<inCycle; it=it+1+decrement) {
        for (ulong i=addr; i<range; i+=addrinc) {
            memcpy((void*)&data, virtual_base + i + delta, n);
            if (output) printf("Read: %#x\n", data);
        }
    }
}

void write_read() {
    uint readData;
    ulong same, diff;
    cycle_cfg();
    for (; it<inCycle; it=it+1+decrement) {
        same = 0, diff = 0;
        ulong final;
        for (ulong i=addr, j=data; i<range; i+=addrinc, j+=datainc) {
            if (inverse) final = ~j;    else    final = j;
            memcpy(virtual_base + i + delta, (void*)&final, n);
        }
        for (ulong i=addr, j=data; i<range; i+=addrinc, j+=datainc) {
            memcpy((void*)&readData, virtual_base + i + delta, n);
            if (inverse) final = ~j; else   final = j;
            if (output) printf("Write: %#x;    Read: %#x\n", final, readData);
            if (readData != final) diff++; else same++;
        }
        printf("Write != Read:  %u;    Write == Read:  %u\n", diff, same);
    }
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

    while (1) {
        printf("=>");
        scanf("%s", cmd);
        if (!strcmp(cmd,writeC)) {
            printf("Start address:0x");             scanf("%x", &addr);
            printf("End address:0x");               scanf("%x", &range);
            printf("Address increment:");           scanf("%i", &addrinc);
            printf("First data:0x");                scanf("%x", &data);
            printf("Data increment:");              scanf("%i", &datainc);
            printf("Inverse(0/1):");                scanf("%1u", &inverse);
            printf("Iteration(0 - in cycle):");     scanf("%u", &inCycle);
            also_write();
        } else if (!strcmp(cmd,readC)) {
            printf("Start address:0x");             scanf("%x", &addr);
            printf("End address:0x");               scanf("%x", &range);
            printf("Address increment:");           scanf("%i", &addrinc);
            printf("Iteration(0 - in cycle):");     scanf("%u", &inCycle);
            printf("Output(0/1):");                 scanf("%1u", &output);
            also_read();
        } else if (!strcmp(cmd,writereadC)) {
            printf("Start address:0x");             scanf("%x", &addr);
            printf("End address:0x");               scanf("%x", &range);
            printf("Address increment:");           scanf("%i", &addrinc);
            printf("Start data:0x");                scanf("%x", &data);
            printf("Data increment:");              scanf("%i", &datainc);
            printf("Inverse(0/1):");                scanf("%1u", &inverse);
            printf("Iteration(0 - in cycle):");     scanf("%u", &inCycle);
            printf("Output(0/1):");                 scanf("%1u", &output);
            write_read();
        } else if (!strcmp(cmd,fileDC)) {
            if (argc < 2) {
                printf("Argument is not set to \"./stage2_tec 'arg1'\".\n"
                       "'arg1' is path to file\n");
                continue;
            }
            FILE* fp = fopen(argv[1], "r");
            if (fp == NULL) {
                printf( "ERROR: could not open %s ...\n=>", argv[1]);
                continue;
            }

            errno = 0;
            int n = fscanf(fp,"Mode:%s\nInverse:%1u\nIteration:%u\nOutput:%1u",mode,&inverse,&inCycle,&output);
            if (errno!=0) {  perror("fscanf preamble"); continue; }
            if (n==0) { printf("No matching characters in preamble\n"); continue; }

            uint line = 0;
            uint alladdr[100], alldata[100];
            while (1)
            {
                n = fscanf(fp,"\n%x:%x",&addr,&data);
                if ((errno!=0) || (n==0) || (n==EOF)) break;
                alladdr[line] = addr;
                alldata[line] = data;
                line++;
            }
            if (errno!=0) {  perror("fscanf addr:data");    continue; }
            if (n==0) { printf("No matching characters in addr:data for line %u\n", line+4);    continue; }

            if (!strcmp(mode,"w")) {    //----------------------------------------------------------- also_write -----------------------------------------------------
                cycle_cfg();
                for (; it<inCycle; it=it+1+decrement) {
                    ulong final;
                    for (ulong i=0; i<line; i++) {
                        if (inverse) final = ~*(alldata+i);    else    final = *(alldata+i);
                        memcpy(virtual_base + *(alladdr+i) + delta, (void*)&final, n);
                    }
                }
            } else if (!strcmp(mode,"r")) { //----------------------------------------------------------- also_read -----------------------------------------------------
                cycle_cfg();
                for (; it<inCycle; it=it+1+decrement) {
                    for (ulong i=0; i<line; i++) {
                        memcpy((void*)(alldata+i), virtual_base + *(alladdr+i) + delta, n);
                        if (output) printf("Read: %#x\n", *(alldata+i));
                    }
                }
            } else if (!strcmp(mode,"wr")) {    //----------------------------------------------------------- write_read -----------------------------------------------------
                ulong same;
                ulong diff;
                uint readData;

                cycle_cfg();
                for (; it<inCycle; it=it+1+decrement) {
                    diff=0; same=0;
                    ulong final;
                    for (ulong i=0; i<line; i++) {
                        if (inverse) final = ~*(alldata+i);    else    final = *(alldata+i);
                        memcpy(virtual_base + *(alladdr+i) + delta, (void*)&final, n);
                    }

                    for (ulong i=0; i<line; i++) {
                        memcpy((void*)&readData, virtual_base + *(alladdr+i) + delta, n);
                        if (inverse) final = ~*(alldata+i); else   final = *(alldata+i);
                        if (output) printf("Write: %#x;    Read: %#x;\n", final, readData);
                        if (readData != final) diff++; else same++;
                    }
                    printf("Write != Read:  %u;    Write == Read:  %u;\n", diff, same);
                }
            } else {
                printf("Unknown mode\n");
            }
            //free((void*)alladdr); free((void*)alldata); //if Ctrl+C in cycle > memory leak
        } else if (!strcmp(cmd,fileAC)) {
            if (argc < 2) {
                printf("Argument is not set to \"./stage2_tec 'arg1'\".\n"
                       "'arg1' is path to file\n");
                continue;
            }
            FILE* fp = fopen(argv[1], "r");
            if (fp == NULL) {
                printf( "ERROR: could not open %s ...\n=>", argv[1]);
                continue;
            }

            errno = 0;
            int n = fscanf(fp,"Mode:%s\nStart address:%x\nEnd address:%x\nAddress increment:%i\n"
                              "Start data:%x\nData increment:%i\nInverse:%1u\nIteration:%u\nOutput:%1u\n", \
                           mode,&addr,&range,&addrinc,&data,&datainc,&inverse,&inCycle,&output);
            if (errno!=0) {  perror("fscanf preamble"); continue; }
            if (n==0) { printf("No matching characters in preamble\n"); continue; }

            if (!strcmp(mode,"w")) {
                cycle_cfg();    also_write();
            } else if (!strcmp(mode,"r")) {
                cycle_cfg();    also_read();
            } else if (!strcmp(mode,"wr")) {
                cycle_cfg();    write_read();
            } else {
                printf("Unknown mode\n");
            }
        } else if (!strcmp(cmd,helpC)) {
            printf("Alailable commands:\n"
                   "w - write data to virtual memory;\n"
                   "r - read data from virtual memory;\n"
                   "wr - write, read and compare data for one procedure;\n"
                   "af - auto run w|r|wr with settings from file(programm argument);\n"
                   "df - run w|r|wr with specific data and settings from file(programm argument);\n"
                   "h - help;\n"
                   "e - exit.\n");
        } else if (!strcmp(cmd,exitC)) {
            break;
        } else {
            printf("Unknown command\n");
        }
    }

    return 0;
}

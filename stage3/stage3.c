//#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
//#include <rpc/auth_des.h>

//#define debug

#define MAXNUMCONN 1    //max number connections
#define CMDSIZE 4       //command size in bytes
#define DSZSIZE 4       //data count in bytes
#define ADDRSIZE 4      //address size in bytes
#define DATASIZE 4      //data size in bytes
#define ECHOSIZE 1000   //echo max size in bytes
#define TYPEMANSIZE 4   //man_type
#define SOCKBUFSIZE 5000000   //socket buffer size

#define QPSK  0b00
#define QAM16 0b01
#define QAM64 0b10
#define ADDR_MEM2P_DELTA    0x200  //128words*4b=d512b=h200
#define ADDR_REG_HSC_CFG    0x90

//--- system errors ---
#define ERROR_TIME 1
#define ERROR_CONNECTION 2
#define ERROR_READ 3
#define ERROR_SELECT_CONN 4
#define ERROR_FD_ISSET_CONN 5
#define ERROR_OPENMEM 6
#define ERROR_OPENLOG 7
#define ERROR_MMAP 8
#define ERROR_ARG 9
#define ERROR_OPENSOCK 10
#define ERROR_ATON 11
#define ERROR_BIND 12
#define ERROR_LISTEN 13
#define ERROR_ACCEPT 14
#define ERROR_MUNMAP 15
#define ERROR_SELECT_READ 16
#define ERROR_FD_ISSET_READ 17
#define ERROR_CLOSE_SOCK 18
#define ERROR_WRITE 19
#define ERROR_MALLOC 20
#define ERROR_MULTIPLE 21
#define ERROR_SIGNAL 22
#define ERROR_THREAD_CLOSE 23
#define ERROR_SETSOCKOPT 24

//--- protocol errors ---
#define ERROR_CMD 100
#define ERROR_DSZ 101

FILE* lf;
char *lt = "l";
int fd;

void *virtual_base;
size_t length = 0xC0000;
off_t offset = 0xC0000000, \
        pa_offset;
size_t delta;
size_t n;

int tcp_socket, rw_socket, em_socket;
fd_set rfds;
struct timeval tv;
struct sockaddr_in my_addr, peer_addr;
uint16_t port_server = 3307;
uint16_t port_rw = 3308;
uint16_t port_em = 3303;
int n_conned = 0;     //current connections count

int cmd, dsz, addr, data;
char echoBuf[ECHOSIZE];
char *readArray = NULL, *writeArray = NULL;

pthread_t thread_bulb;
pthread_t thread_socket;

void *sockBuffer;
int bufferPointerForSocket;
int bufferPointerForReader;
int bufferFreeSize;
int reservedSize;

int futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3) {
    return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

int freeSizeToEnd;
ssize_t realRead;
ssize_t addDataToBuffer(int size)
{
    freeSizeToEnd = SOCKBUFSIZE - bufferPointerForSocket;
    if (size <= freeSizeToEnd) {
        realRead = read(rw_socket, sockBuffer+bufferPointerForSocket, size);
    } else {
        realRead = read(rw_socket, sockBuffer+bufferPointerForSocket, freeSizeToEnd);
    }
    bufferPointerForSocket+=realRead;
    if (bufferPointerForSocket == SOCKBUFSIZE) bufferPointerForSocket=0;
    return realRead;
}

pthread_mutex_t mutex_socket = PTHREAD_MUTEX_INITIALIZER;

void* start_socket_thread(void* arg) {
    //printf("start_socket_thread start\n");
    ssize_t r=0;
    int argp = 0;
    int curBufferFreeSize;
    tcp_info info;
    socklen_t l3 = (socklen_t)sizeof(tcp_info);
    bufferPointerForSocket = 0;
    bufferPointerForReader = 0;
    bufferFreeSize = SOCKBUFSIZE;

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(rw_socket, &rfds);
        int retval = select(rw_socket+1, &rfds, NULL, NULL, NULL);
        if (retval) {
            if (FD_ISSET(rw_socket,&rfds)) {
                int rcb = getsockopt(rw_socket, SOL_TCP, TCP_INFO, (void*)&info, &l3);
                if (rcb == 0) {
                    printf("tcpi_state: %d\n", info.tcpi_state);
                    //Важно для ускорения, но в функции на запись, т.к. там при полном заполнении буфера может быть возвращен 0
                } else if (rcb == -1) {
                    printf("error info, errno: %d\n", errno);
                }

                ioctl(rw_socket, FIONREAD, &argp);
                curBufferFreeSize = bufferFreeSize;
                if (curBufferFreeSize == 0) continue;
                if (curBufferFreeSize < argp) r = addDataToBuffer(curBufferFreeSize);
                else    r = addDataToBuffer(argp);
                if (r == -1) {
                    handle_error(ERROR_READ);
                    return;
                }
                if (r ==  0) {
                    handle_error(ERROR_CONNECTION);
                    return;
                }
                bufferFreeSize-=r;
            } else {
                handle_error(ERROR_FD_ISSET_READ);
                return;
            }
        } else if(retval == -1) {
            handle_error(ERROR_SELECT_READ);
            return;
        }
    }
}


#define closeAll_1  if(munmap(virtual_base, length+delta) == -1) \
                        handle_error(ERROR_MUNMAP); \
                    close(fd);\

#define closeAll_2  if(close(tcp_socket) == -1) \
                        handle_error(ERROR_CLOSE_SOCK); \
                    if(munmap(virtual_base, length+delta) == -1) \
                        handle_error(ERROR_MUNMAP); \
                    close(fd); \

void writeLog(char *msg) {
    if (strcmp(lt, "t") == 0) {
        lf = NULL;
    } else {
        lf = fopen("stage3.log", "a");
        if (lf == NULL) printf("WARNING: could not open log.txt ...\n");
    }
    time_t curTime = time(NULL);
    struct tm* ltm = localtime(&curTime);
    if (lf==NULL) {
        printf("%d.%d.%d %d:%d:%d %s", ltm->tm_mday, ltm->tm_mon, ltm->tm_year, ltm->tm_hour, ltm->tm_min, ltm->tm_sec, msg);
    } else {
        fprintf(lf, "%d.%d.%d %d:%d:%d %s", ltm->tm_mday, ltm->tm_mon, ltm->tm_year, ltm->tm_hour, ltm->tm_min, ltm->tm_sec, msg);
    }
    if (lf==NULL) return;
    fclose(lf);
}

int handle_error(int err)
{
    char msg[1000];
    int errsv = errno;
    if (err == ERROR_OPENMEM) {
        sprintf(msg, "ERROR: could not open \"/dev/mem\"...: %s\n", strerror(errsv));
        writeLog(msg);
        exit(EXIT_FAILURE);
    } else if (err == ERROR_OPENLOG) {
        printf("WARNING: could not open log.txt ...\n");
        return 0;
    } else if (err == ERROR_MMAP) {
        sprintf(msg, "ERROR: mmap() failed...: %s\n", strerror(errsv));
        writeLog(msg);
        close(fd);
        exit(EXIT_FAILURE);
    } else if (err == ERROR_ARG) {
        sprintf(msg, "ERROR: <dotted-address-server> program argument is empty\n");
        writeLog(msg); closeAll_1
        exit(EXIT_FAILURE);
    } else if (err == ERROR_OPENSOCK) {
        sprintf(msg, "ERROR: tcp_socket failed...: %s\n", strerror(errsv));
        writeLog(msg); closeAll_1
        exit(EXIT_FAILURE);
    } else if (err == ERROR_ATON) {
        sprintf(msg, "ERROR: inet_aton() failed...: %s\n", strerror(errsv));
        writeLog(msg); closeAll_2
        exit(EXIT_FAILURE);
    } else if (err == ERROR_BIND) {
        sprintf(msg, "ERROR: bind() failed...: %s\n", strerror(errsv));
        writeLog(msg); closeAll_2
        exit(EXIT_FAILURE);
    } else if (err == ERROR_LISTEN) {
        sprintf(msg, "ERROR: listen() failed...: %s\n", strerror(errsv));
        writeLog(msg); closeAll_2
        exit(EXIT_FAILURE);
    } else if (err == ERROR_ACCEPT) {
        sprintf(msg, "ERROR: accept() failed...: %s\n", strerror(errsv));
        writeLog(msg); closeAll_2
        exit(EXIT_FAILURE);
    } else if (err == ERROR_MUNMAP) {
        sprintf(msg, "ERROR: munmap() failed...: %s\n", strerror(errsv));
        writeLog(msg);
        return 0;
    } else if (err == ERROR_CLOSE_SOCK) {
        sprintf(msg, "ERROR: close(sock) failed...: %s\n", strerror(errsv));
        writeLog(msg);
        return 0;
    } else if(err == ERROR_CONNECTION) {
        sprintf(msg, "ERROR: read() failed(==0)...: %s\n", strerror(errsv));
        writeLog(msg);
        close(rw_socket);
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_READ) {
        sprintf(msg, "ERROR: read() failed(==-1)...: %s\n", strerror(errsv));
        writeLog(msg);
        close(rw_socket);
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_WRITE) {
        sprintf(msg, "ERROR: write() failed(==-1)...: %s\n", strerror(errsv));
        writeLog(msg);
        close(rw_socket);
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_TIME) {
        FD_ZERO(&rfds);
        sprintf(msg, "WARNING: No data within five seconds.\n");
        writeLog(msg);
        tv.tv_sec = 5;
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        return 0;
    } else if (err == ERROR_SELECT_CONN) {
        FD_ZERO(&rfds);
        sprintf(msg, "ERROR: select() connection failed...: %s\n", strerror(errsv));
        writeLog(msg); closeAll_2
        exit(EXIT_FAILURE);
    } else if (err == ERROR_SELECT_READ) {
        sprintf(msg, "ERROR: select() read failed...: %s\n", strerror(errsv));
        writeLog(msg);
        close(rw_socket);
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_FD_ISSET_CONN) {
        FD_ZERO(&rfds);
        sprintf(msg, "ERROR: FD_ISSET connection failed...\n");
        writeLog(msg); closeAll_2
        exit(EXIT_FAILURE);
    } else if (err == ERROR_FD_ISSET_READ) {
        sprintf(msg, "ERROR: FD_ISSET read failed...\n");
        writeLog(msg);
        close(rw_socket);
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_CMD) {
        sprintf(msg, "ERROR: unknown command code\n");
        writeLog(msg);
        close(rw_socket);
        n_conned = n_conned-1;
        return -1;
    } else if (err == ERROR_DSZ) {
        sprintf(msg, "ERROR: data size no multiple 4\n");
        writeLog(msg);
        close(rw_socket);
        n_conned = n_conned-1;
        return -1;
    } else if (err == ERROR_MALLOC) {
        sprintf(msg, "ERROR: malloc return NULL\n");
        writeLog(msg);
        close(rw_socket);
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        n_conned = n_conned-1;
        return -1;
    } else if (err == ERROR_MULTIPLE) {
        sprintf(msg, "ERROR: Not multiple cmd 4 data for read\n");
        writeLog(msg);
        close(rw_socket);
        if (readArray != NULL)  { free(readArray); readArray = NULL; }
        if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
        n_conned = n_conned-1;
        return -1;
    } else if (err == ERROR_SIGNAL) {
        sprintf(msg, "WARNING: Stop signal is not working\n");
        writeLog(msg);
        return 0;
    } else if (err == ERROR_THREAD_CLOSE) {
        sprintf(msg, "ERROR: Thread close function is not working\n");
        writeLog(msg);
        return 0;
    } else if (err == ERROR_SETSOCKOPT) {
        sprintf(msg, "ERROR: SETSOCKOPT failed: %s\n", strerror(errsv));
        writeLog(msg);
        return 0;
    }
}

int connection() {
    writeLog("listening...\n");
    socklen_t peer_addr_size = sizeof(struct sockaddr_in);
    char txt[100];

    FD_ZERO(&rfds);
    FD_SET(tcp_socket, &rfds);
    int retval = select(tcp_socket+1, &rfds, NULL, NULL, NULL);
    if (retval) {
        if (FD_ISSET(tcp_socket,&rfds)) {
            int temp_socket = accept(tcp_socket, (struct sockaddr*) &peer_addr, &peer_addr_size);
            if (temp_socket == -1)  handle_error(ERROR_ACCEPT);
#ifdef debug
            printf("Peer port: %d\n", ntohs(peer_addr.sin_port));
#endif
//            if (ntohs(peer_addr.sin_port) == port_rw) rw_socket = temp_socket;
//            else if (ntohs(peer_addr.sin_port) == port_em) em_socket = temp_socket;
            rw_socket = temp_socket;
            sprintf(txt,"Connection on port %d is available now\n", ntohs(peer_addr.sin_port));
            writeLog(txt);
            n_conned=n_conned+1;
            return 0;
        } else {
            handle_error(ERROR_FD_ISSET_CONN);
        }
    } else if (retval == -1) {
        handle_error(ERROR_SELECT_CONN);
    }
}

//int readAllData(int size, char* refArray, struct timeval* tv) {
//    //printf("readAllData start\n");
//    ssize_t r=0;
//    int n=0;
//    while (1) {
//        FD_ZERO(&rfds);
//        FD_SET(rw_socket, &rfds);
//        int retval = select(rw_socket+1, &rfds, NULL, NULL, tv);
//        if (retval) {
//            if (FD_ISSET(rw_socket,&rfds)) {
//                r = read(rw_socket, refArray+n, size - n);
//                if (r == -1) {
//                    handle_error(ERROR_READ);
//                    return -1;
//                }
//                if (r ==  0) {
//                    handle_error(ERROR_CONNECTION);
//                    return -1;
//                }
//#ifdef debug
//                printf("recv: %ld bytes\n", r);
//#endif
//            } else {
//                handle_error(ERROR_FD_ISSET_READ);
//                return -1;
//            }
//        } else if(retval == -1) {
//            handle_error(ERROR_SELECT_READ);
//            return -1;
//        } else {
//            handle_error(ERROR_TIME);
//            return -1;
//        }
//        n+=r;
//        if (n>=size) {
//            printf("//----- readAllData end\n");
//            return 0;
//        }
//    }
//}


int waitForAllData(int size, const struct timespec *timeout) {
    if (bufferFreeSize == 0) {

    } else {
        //futex();
    }

}

int writeAllData(int size, char* refArray) {
    printf("//----- writeAllData start\n");
    ssize_t r=0;
    printf("size: %d\n", size);
    int n=0;
    while (1) {
        r = write(rw_socket, refArray+n, size - n);
        if (r == -1) {
            handle_error(ERROR_WRITE);
            return -1;
        }
        if (r ==  0) {
            handle_error(ERROR_CONNECTION);
            return -1;
        }
#ifdef debug
        printf("send: %ld bytes\n", r);
#endif
        n+=r;
        printf("r: %d\n", r);
        printf("n: %d\n", n);
        if (n>=size) {
            printf("//----- writeAllData end\n");
            return 0;
        }
    }
}

int checkCmd() {
    if (!((cmd>=1) && (cmd<=7))) {
        handle_error(ERROR_CMD);
        return -1;
    }
    return 0;
}

int checkMult(int num, int who) {
    int remainder = num % 4;
    if (remainder != 0) {
        if (who == 0)
            handle_error(ERROR_DSZ);
        if (who == 1)
            handle_error(ERROR_MULTIPLE);
        return -1;
    }
    return 0;
}

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* start_bulb_thread(void* arg) {
    void *bulb_ptr = virtual_base + 0x194 + delta;
    int indicator = 0;
    while (1) {
        usleep(400000);
        pthread_mutex_lock(&mutex);
        if (indicator==1) indicator=0; else indicator=1;
        *(int*)bulb_ptr=indicator;
        pthread_mutex_unlock(&mutex);
    }
}

void de1_stop_signal(int s, siginfo_t *i, void *c) {
    char msg[1000];
    sprintf(msg, "Signal %d is obtained\n", s);
    writeLog(msg);
    if (pthread_cancel(thread_bulb) == -1) handle_error(ERROR_THREAD_CLOSE);
//    int optval = 1; socklen_t len = sizeof(optval);
//    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, (void*)&optval, len);
    if(close(tcp_socket) == -1) handle_error(ERROR_CLOSE_SOCK);
    if(close(rw_socket) == -1) handle_error(ERROR_CLOSE_SOCK);
    if(munmap(virtual_base, length+delta) == -1) handle_error(ERROR_MUNMAP);
    close(fd);
    if (readArray != NULL)  { free(readArray); readArray = NULL; }
    if (writeArray != NULL) { free(writeArray); writeArray = NULL; }
    kill(getpid(), SIGKILL);
}

int working() {
    tv.tv_sec = 5;tv.tv_usec = 0;
    char msg[1000];

    //----- read cmd ------
    printf("//----- read cmd ------\n");
    if (readAllData(CMDSIZE, (char*)&cmd, NULL) == -1) return -1;
    sprintf(msg, "Cmd: %d\n",cmd);  writeLog(msg);
    if (checkCmd() == -1) return -1;

    //----- read dsz ------
    printf("//----- read dsz ------\n");
    if (readAllData(DSZSIZE, (char*)&dsz, &tv) == -1) return -1;
    sprintf(msg, "Dsz: %ld\n",dsz); writeLog(msg);
    if (cmd >= 1 && cmd <=4 )
        if (checkMult(dsz, 0) == -1) return -1;

    readArray = (char*)malloc((size_t)dsz);
    if (readArray == NULL)
        return handle_error(ERROR_MALLOC);

    printf("//----- read array ------\n");
    if (readAllData(dsz, readArray, &tv) == -1) return -1;

    //----- pars cmd ------
    if (cmd == 1) {
        int N = dsz/(DATASIZE+ADDRSIZE);
        for (int i=0; i<N; i++) {
            addr = *(int*)(readArray+(DATASIZE+ADDRSIZE)*i);    data = *(int*)(readArray+(DATASIZE+ADDRSIZE)*i+n);
            memcpy(virtual_base + addr + delta, (void*)&data, n);
        }
        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;  //для ускорения закомментировано!!!
    } else if (cmd == 2) {
        addr = *(int*)(readArray);
        int N = (dsz-ADDRSIZE)/DATASIZE;
        for (int i=0; i<N; i++) {
            data = *(int*)(readArray+DATASIZE*(i+1));
            memcpy(virtual_base + addr + i*ADDRSIZE + delta, (void*)&data, n);
        }
        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;  //для ускорения закомментировано!!!
    } else if (cmd == 3) {
        int N = dsz/4;
        writeArray = (char*)malloc((size_t)dsz);
        if (writeArray == NULL)
            return handle_error(ERROR_MALLOC);

        pthread_mutex_lock(&mutex);
        for (int i=0; i<N; i++) {
            addr = *(int*)(readArray+ADDRSIZE*i);
            memcpy((void*)(writeArray+DATASIZE*i), virtual_base + addr + delta, n);
#ifdef debug
            printf("Read: %d\n", *(int*)(writeArray+DATASIZE*i));
#endif
        }
        pthread_mutex_unlock(&mutex);
        if (writeAllData(dsz, writeArray)) return -1;

        free(writeArray);
        writeArray = NULL;
    } else if (cmd == 4) {
        addr = *(int*)(readArray);   int count = *(int*)(readArray+DATASIZE);
        if (checkMult(count, 1) == -1) return -1;
        printf("cmd == 4, addr==%d\n", addr);
        printf("cmd == 4, count==%d\n", count);

        writeArray = (char*)malloc((size_t)count);
        if (writeArray == NULL)
            return handle_error(ERROR_MALLOC);

        pthread_mutex_lock(&mutex);
        for (int i=0; i<count; i+=DATASIZE, addr+=DATASIZE)
            memcpy((void*)(writeArray+i), virtual_base + addr + delta, n);
        pthread_mutex_unlock(&mutex);
        if (writeAllData(count, writeArray)) return -1;

        free(writeArray);
        writeArray = NULL;
    } else if (cmd == 5) {
        printf("%s\n", readArray);
        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;  //для ускорения закомментировано!!!
    } else if (cmd == 6) {  //writing path to the 2-port mem
//        addr = *(int*)(readArray);
//        *(unsigned int*)&cmdWord = *((int*)(readArray)+1);
//        int SL = symbolLength();
//        int Nsp = cmdWord.num_of_symbols;
//        int lastSL = SL - (Nsp*SL - (dsz - n)); //without nulls
//        void* curArrayPointer = readArray+n;
//        for (int i=1; i<Nsp; i++, addr+=ADDR_MEM2P_DELTA, curArrayPointer+=SL)
//            memcpy(virtual_base + addr + delta, curArrayPointer, SL);
//        memcpy(virtual_base + addr + delta, curArrayPointer, lastSL);
//        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;
    } else if (cmd == 7) {
        addr = *(int*)(readArray);
        printf("addr: %d\n", addr);
        size_t count = (size_t)*(int*)(readArray+DATASIZE);
        printf("count: %ld\n", count);
        int destAddr = *(int*)(readArray+2*DATASIZE);
        printf("destAddr: %d\n", destAddr);
        if (checkMult(count, 1) == -1) return -1;

//        writeArray = (char*)malloc((size_t)count);
//        for (int i=0; i<count; i+=DATASIZE, addr+=DATASIZE)
//            memcpy((void*)(writeArray+i), virtual_base + addr + delta, n);
//        for (int i=0; i<count; i+=DATASIZE, destAddr+=DATASIZE)
//            memcpy(virtual_base + destAddr + delta, (void*)(writeArray + i), n);

        pthread_mutex_lock(&mutex);
        for (int i=0; i<count; i+=DATASIZE, addr+=DATASIZE)
            memcpy(virtual_base + destAddr + i + delta, virtual_base + addr + delta, n);
        pthread_mutex_unlock(&mutex);

//            memcpy(virtual_base + destAddr + delta, virtual_base + addr + delta, count); //not working

        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;    //для ускорения закомментировано!!!
    } else {
        printf("ELSE");
        return 1;
    }
    free(readArray);
    readArray = NULL;
    sprintf(msg, "Cmd %d is completed\n", cmd);
    writeLog(msg);
}

int main(int argc, char* argv[])
{
    //------------------------------------------------------------ Output setting ---------------------------------------------------------
    if (argc < 2) handle_error(ERROR_ARG);
    if ((argc > 2) && (strcmp(argv[2], "t") == 0)) lt = "t";

    //------------------------------------------------------------ Virtual memory setting ---------------------------------------------------------
    fd = open("/dev/mem", (O_RDWR | O_SYNC));

    if (fd == -1)   handle_error(ERROR_OPENMEM);

    long ps = sysconf(_SC_PAGESIZE);
    pa_offset = offset & ~(ps-1);
    delta = offset - pa_offset;

    virtual_base = mmap(NULL, length + delta, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, pa_offset);
    if(virtual_base == MAP_FAILED)  handle_error(ERROR_MMAP);

    n = sizeof(unsigned int);

#ifdef debug
    //printf("\"/dev/mem\"  size: %ld\n", sb.st_size);
    printf("page size: %ld\n", ps);
    printf("offset: %#x\n", offset);
    printf("pa_offset: %#x\n", pa_offset);
    printf("delta: %#x\n", delta);
    printf("size int, bytes: %u\n\n", n);
#endif

    //------------------------------------------------------------ Socket setting ---------------------------------------------------------
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (tcp_socket == -1)   handle_error(ERROR_OPENSOCK);

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port_server);

    if (inet_aton(argv[1], &my_addr.sin_addr) == 0) handle_error(ERROR_ATON);

    if (bind(tcp_socket, (struct sockaddr *) &my_addr, sizeof(struct sockaddr_in)) == -1) handle_error(ERROR_BIND);

    if (listen(tcp_socket, MAXNUMCONN) == -1)    handle_error(ERROR_LISTEN);

    //------------------------------------------------------------ LED thread ---------------------------------------------------------
    //pthread_t* thread = (pthread_t*)malloc(sizeof(pthread_t));
    if (pthread_create(&thread_bulb, NULL, &start_bulb_thread, NULL) != 0)
        printf("Bulb thread didn't start!\n");
    else
        printf("Bulb thread started!\n");

    //------------------------------------------------------------ Signal interruption ------------------------------------------------
    struct sigaction de1_stop_action;
    de1_stop_action.sa_flags = SA_SIGINFO;
    de1_stop_action.sa_sigaction = de1_stop_signal;
    if (sigaction(40, &de1_stop_action, NULL) == -1) handle_error(ERROR_SIGNAL);
    else printf("For correct close the programm send signal 40 to the process.\n");

    //------------------------------------------------------------ Main cycle ---------------------------------------------------------
    while (1) {
        if (n_conned < MAXNUMCONN) {
            connection();
        } else {
            working();
        }
    }

    if (close(rw_socket) == -1)
        handle_error(ERROR_CLOSE_SOCK);
    closeAll_2

    return 0;
}

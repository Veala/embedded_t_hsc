#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
//#include <rpc/auth_des.h>

#define debug

#define MAXNUMCONN 1    //max number connections
#define CMDSIZE 4       //command size in bytes
#define DSZSIZE 4       //data count in bytes
#define ADDRSIZE 4      //address size in bytes
#define DATASIZE 4      //data size in bytes
#define ECHOSIZE 1000   //echo max size in bytes

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
int n_conned;     //current connections count

int cmd, dsz, addr, data;
char echoBuf[ECHOSIZE];

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
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_READ) {
        sprintf(msg, "ERROR: read() failed(==-1)...: %s\n", strerror(errsv));
        writeLog(msg);
        close(rw_socket);
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_WRITE) {
        sprintf(msg, "ERROR: write() failed(==-1)...: %s\n", strerror(errsv));
        writeLog(msg);
        close(rw_socket);
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_TIME) {
        FD_ZERO(&rfds);
        sprintf(msg, "WARNING: No data within five seconds.\n");
        writeLog(msg);
        tv.tv_sec = 5;
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

int readAllData(int size, char* refArray, struct timeval* tv) {
    ssize_t r=0;
    int n=0;
    while (1) {
        FD_ZERO(&rfds);
        FD_SET(rw_socket, &rfds);
        int retval = select(rw_socket+1, &rfds, NULL, NULL, tv);
        if (retval) {
            if (FD_ISSET(rw_socket,&rfds)) {
                r = read(rw_socket, refArray+n, size - n);
                if (r == -1) {
                    handle_error(ERROR_READ);
                    return -1;
                }
                if (r ==  0) {
                    handle_error(ERROR_CONNECTION);
                    return -1;
                }
#ifdef debug
                printf("recv: %ld bytes\n", r);
#endif
            } else {
                handle_error(ERROR_FD_ISSET_READ);
                return -1;
            }
        } else if(retval == -1) {
            handle_error(ERROR_SELECT_READ);
            return -1;
        } else {
            handle_error(ERROR_TIME);
            return -1;
        }
        n+=r;
        if (n>=size) return 0;
    }
}

int writeAllData(int size, char* refArray) {
    ssize_t r=0;
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
        if (n>=size) return 0;
    }
}

int checkCmd() {
    if (!((cmd>=1) && (cmd<=5))) {
        handle_error(ERROR_CMD);
        return -1;
    }
    return 0;
}

int checkDsz() {
    int remainder = dsz % 4;
    if (remainder != 0) {
        handle_error(ERROR_DSZ);
        return -1;
    }
    return 0;
}

int working() {
    tv.tv_sec = 5;tv.tv_usec = 0;
    char msg[1000];

    //----- read cmd ------
    if (readAllData(CMDSIZE, (char*)&cmd, NULL) == -1) return -1;
    sprintf(msg, "Cmd: %d\n",cmd);  writeLog(msg);
    if (checkCmd() == -1) return -1;

    //----- read dsz ------
    if (readAllData(DSZSIZE, (char*)&dsz, &tv) == -1) return -1;
    sprintf(msg, "Dsz: %ld\n",dsz); writeLog(msg);
    if (cmd >= 1 && cmd <=4 )
        if (checkDsz() == -1) return -1;

    char* readArray = (char*)malloc((size_t)dsz);
    if (readArray == NULL) exit(1);
    if (readAllData(dsz, readArray, &tv) == -1) return -1;

    //----- pars cmd ------
    if (cmd == 1) {
        int N = dsz/(DATASIZE+ADDRSIZE);
        for (int i=0; i<N; i++) {
            addr = *(int*)(readArray+(DATASIZE+ADDRSIZE)*i);    data = *(int*)(readArray+(DATASIZE+ADDRSIZE)*i+n);
            memcpy(virtual_base + addr + delta, (void*)&data, n);
        }
        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;

//        int N = dsz/(DATASIZE+ADDRSIZE);
//        for (int i=0; i<N; i++) {
//            if (readAllData(ADDRSIZE, (char*)&addr, &tv) == -1) return -1;
//            if (readAllData(DATASIZE, (char*)&data, &tv) == -1) return -1;
//            memcpy(virtual_base + addr + delta, (void*)&data, n);
//        }
//        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;
    } else if (cmd == 2) {
        addr = *(int*)(readArray);
        int N = (dsz-ADDRSIZE)/DATASIZE;
        for (int i=0; i<N; i++) {
            data = *(int*)(readArray+DATASIZE*(i+1));
            memcpy(virtual_base + addr + i*ADDRSIZE + delta, (void*)&data, n);
        }
        if (writeAllData(CMDSIZE, (char*)&cmd)) return -1;

//        if (readAllData(ADDRSIZE, (char*)&addr, &tv) == -1) return -1;
//        int N = (dsz-ADDRSIZE)/DATASIZE;
//        for (int i=0; i<N; i++) {
//            if (readAllData(DATASIZE, (char*)&data, &tv) == -1) return -1;
//            memcpy(virtual_base + addr + i*ADDRSIZE + delta, (void*)&data, n);
//        }
    } else if (cmd == 3) {
        int N = dsz/4;
        printf("cmd 3, dsz: %d\n", dsz);

        char* writeArray = (char*)malloc((size_t)dsz);
        if (writeArray == NULL) exit(1);
        printf("cmd 3, after malloc\n", dsz);

        for (int i=0; i<N; i++) {
            addr = *(int*)(readArray+ADDRSIZE*i);
            memcpy((void*)(writeArray+DATASIZE*i), virtual_base + addr + delta, n);
            //printf("Read: %d\n", *(int*)(writeArray+DATASIZE*i));
        }
        printf("cmd 3, after for()\n");
        if (writeAllData(dsz, writeArray)) return -1;
        printf("cmd 3, after writeAllData\n");

        free(writeArray);
    } else if (cmd == 4) {
        int count;
        if (readAllData(ADDRSIZE, (char*)&addr, &tv) == -1) return -1;
        if (readAllData(DATASIZE, (char*)&count, &tv) == -1) return -1;
        write(rw_socket, virtual_base + addr + delta, count);
    } else if (cmd == 5) {
        if (readAllData(dsz, echoBuf, &tv) == -1) return -1;
        printf("%s\n", echoBuf);
    }
    free(readArray);
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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
//#include <rpc/auth_des.h>

//#define debug

#define CMDSIZE 2
#define DSZSIZE 2

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
#define ERROR_BIND 15
#define ERROR_LISTEN 16
#define ERROR_MUNMAP 17
#define ERROR_SELECT_READ 18
#define ERROR_FD_ISSET_READ 19

//--- protocol errors ---
#define ERROR_CMD 100

FILE* lf;
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
sockaddr_in my_addr, peer_addr;
uint16_t port_server = 3307;
uint16_t port_rw = 3308;
uint16_t port_em = 3303;
int n_conned;

char cmdBuf[CMDSIZE]; int cmd;
char dszBuf[DSZSIZE]; int dsz;

#define closeAll if(munmap(virtual_base, length+delta) == -1) \
                    handle_error(ERROR_MUNMAP); \
                close(fd); logClose(); \

int handle_error(int err)
{
    char msg[1000];
    int errsv = errno;
    if (err == ERROR_OPENMEM) {
        sprintf(msg, "ERROR: could not open \"/dev/mem\"...: %s\n", strerror(errsv));
        writelog(msg); logClose();
        return -1;
    } else if (err == ERROR_OPENLOG) {
        printf("WARNING: could not open log.txt ...\n");
        return 0;
    } else if (err == ERROR_MMAP) {
        sprintf(msg, "ERROR: mmap() failed...: %s\n", strerror(errsv));
        writelog(msg);
        close(fd); logClose();
        return -1;
    } else if (err == ERROR_ARG) {
        sprintf(msg, "ERROR: <dotted-address-server> program argument is empty\n");
        writelog(msg); closeAll
        return -1;
    } else if (err == ERROR_OPENSOCK) {
        sprintf(msg, "ERROR: tcp_socket: %s\n", strerror(errsv));
        writelog(msg); closeAll
        return -1;
    } else if (err == ERROR_ATON) {
        sprintf(msg, "ERROR: inet_aton: %s\n", strerror(errsv));
        writelog(msg); close(tcp_socket); closeAll
        return -1;
    } else if (err == ERROR_BIND) {
        sprintf(msg, "ERROR: bind: %s\n", strerror(errsv));
        writelog(msg); close(tcp_socket); closeAll
        return -1;
    } else if (err == ERROR_LISTEN) {
        sprintf(msg, "ERROR: listen: %s\n", strerror(errsv));
        writelog(msg); close(tcp_socket); closeAll
        return -1;
    } else if (err == ERROR_ACCEPT) {
        sprintf(msg, "ERROR: accept: %s\n", strerror(errsv));
        writelog(msg); close(tcp_socket); closeAll
        return -1;
    } else if (err == ERROR_MUNMAP) {
        sprintf(msg, "ERROR: munmap(): %s\n", strerror(errsv));
        writelog(msg);
        return -1;
    } else if(err == ERROR_CONNECTION) {
        sprintf(msg, "ERROR: read(): %s\n", strerror(errsv));
        writelog(msg);
        close(rw_socket);
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_READ) {
        sprintf(msg, "ERROR: read(): %s\n", strerror(errsv));
        writelog(msg);
        close(rw_socket);
        n_conned = n_conned-1;
        return 0;
    } else if (err == ERROR_TIME) {
        FD_ZERO(&rfds);
        printf("No data within five seconds.\n");
        tv.tv_sec = 5; continue;
    } else if (err == ERROR_SELECT_CONN) {
        FD_ZERO(&rfds);
        sprintf(msg, "ERROR: select conn: %s\n", strerror(errsv));
        writelog(msg); close(tcp_socket); closeAll
        return -1;
    } else if (err == ERROR_SELECT_READ) {
        sprintf(msg, "ERROR: select read: %s\n", strerror(errsv));
        writelog(msg);
        return -1;
    } else if (err == ERROR_FD_ISSET_CONN) {
        FD_ZERO(&rfds);
        sprintf(msg, "ERROR: FD_ISSET(no connection)\n");
        writelog(msg);
        return 0;
    } else if (err == ERROR_FD_ISSET_READ) {
        sprintf(msg, "ERROR: FD_ISSET(no connection)\n");
        writelog(msg);
        return 0;
    } else if (err == ERROR_CMD) {
        printf("ERROR: unknown command code\n");
        continue;
    }
}

void writelog(char *msg) {
    time_t curTime = time(NULL);
    tm* ltm = localtime(&curTime);
    if (lf==NULL) {
        printf("%d.%d.%d %d:%d %s", ltm->tm_mday, ltm->tm_mon, ltm->tm_year, ltm->tm_hour, ltm->tm_min, msg);
    } else {
        fprintf(lf, "%d.%d.%d %d:%d %s", ltm->tm_mday, ltm->tm_mon, ltm->tm_year, ltm->tm_hour, ltm->tm_min, msg);
    }
}

void logClose() {
    if (lf==NULL) return;
    fclose(lf);
}

int connection() {
    writelog("listening...");
    socklen_t peer_addr_size = sizeof(sockaddr_in);
    char txt[100];
    while (1) {
        FD_ZERO(&rfds);
        FD_SET(tcp_socket, &rfds);
        int retval = select(tcp_socket+1, &rfds, NULL, NULL, NULL);
        if (retval) {
            if (FD_ISSET(tcp_socket,&rfds)) {
                int temp_socket = accept(tcp_socket, (sockaddr*) &peer_addr, &peer_addr_size);
                if (temp_socket == -1)
                    if (handle_error(ERROR_ACCEPT) == -1)
                        exit(EXIT_FAILURE);
                if (ntohs(peer_addr.sin_port) == port_rw) rw_socket = temp_socket;
                else if (ntohs(peer_addr.sin_port) == port_em) em_socket = temp_socket;
                sprintf(txt,"Connection on port %d is available now", ntohs(peer_addr.sin_port));
                writelog(txt);
            } else {
                if (handle_error(ERROR_FD_ISSET_CONN) == -1)
                    exit(EXIT_FAILURE);
            }
        } else if (retval == -1) {
            if (handle_error(ERROR_SELECT_CONN) == -1)
                exit(EXIT_FAILURE);
        }
    }
}

void checkCmd() {
    if (!((cmd>=1) && (cmd<=5))) throw ERROR_CMD;
}

int readAllData(int& size, char (&refArray), timeval* tv) {
    ssize_t r=0;
    int n=0;
    while (1) {
        FD_ZERO(&rfds);
        FD_SET(rw_socket, &rfds);
        int retval = select(rw_socket+1, &rfds, NULL, NULL, tv);
        if (retval) {
            if (FD_ISSET(rw_socket,&rfds)) {
                r = read(rw_socket, (&refArray)+n, size - n);
                if (r == -1)
                    if (handle_error(ERROR_READ) == -1) {

                    }
                if (r ==  0)
                    if (handle_error(ERROR_CONNECTION) == -1)
                        exit(EXIT_FAILURE);
                #ifdef debug
                printf("recv: %ld bytes\n", r);
                #endif
            } else {
                if (handle_error(ERROR_FD_ISSET_READ) == -1)
                    exit(EXIT_FAILURE);
            }
        } else if(retval == -1) {
            throw ERROR_SELECT_READ;
        } else {
            throw ERROR_TIME;
        }
        n+=r;
        if (n>=size) return;
    }
}

int working() {

}

int main(int argc, char* argv[])
{
    lf = fopen("log.txt", "a");
    if (lf == NULL) {
        if (handle_error(ERROR_OPENLOG) == -1)
            exit(EXIT_FAILURE);
    }

    //------------------------------------------------------------ Virtual memory setting ---------------------------------------------------------
    fd = open("/dev/mem", (O_RDWR | O_SYNC));
    if (fd == -1) {
        if (handle_error(ERROR_OPENMEM) == -1)
            exit(EXIT_FAILURE);
    }

    long ps = sysconf(_SC_PAGESIZE);
    pa_offset = offset & ~(ps-1);
    delta = offset - pa_offset;

    virtual_base = mmap(NULL, length + delta, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, pa_offset);
    if(virtual_base == MAP_FAILED) {
        if (handle_error(ERROR_MMAP) == -1)
            exit(EXIT_FAILURE);
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

    //------------------------------------------------------------ Connection setting ---------------------------------------------------------
    if (argc != 2) {
        if (handle_error(ERROR_ARG) == -1)
            exit(EXIT_FAILURE);
    }

    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (tcp_socket == -1) {
        if (handle_error(ERROR_OPENSOCK) == -1)
            exit(EXIT_FAILURE);
    }

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(3307);

    if (inet_aton(argv[1], &my_addr.sin_addr) == 0) {
        if (handle_error(ERROR_ATON) == -1)
            exit(EXIT_FAILURE);
    }

    if (bind(tcp_socket, (sockaddr *) &my_addr, sizeof(sockaddr_in)) == -1) {
        if (handle_error(ERROR_BIND) == -1)
            exit(EXIT_FAILURE);
    }

    if (listen(tcp_socket, 2) == -1) {
        if (handle_error(ERROR_LISTEN) == -1)
            exit(EXIT_FAILURE);
    }

    connection();

    //------------------------------------------------------------ Working ---------------------------------------------------------
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    while (1) {
        int cmdsize = CMDSIZE;
        int dszsize = DSZSIZE;
        readAllData(cmdsize, cmdBuf[0], NULL);
        cmd = atoi(cmdBuf);
        checkCmd();
        readAllData(dszsize, dszBuf[0], &tv);
        dsz = atoi(dszBuf);

        switch (cmd) {
        case 1:

            break;
        case 2:

            break;
        default:
            break;
        }
    }

    if (close(tcp_socket) == -1)
        handle_error("close tcp socket");

    return 0;
}

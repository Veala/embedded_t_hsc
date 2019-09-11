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
#include <ifaddrs.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <linux/if_link.h>


//#include <rpc/auth_des.h>

//#define debug

#define MAXNUMCONN 2    //max number connections
#define CMDSIZE 4       //command size in bytes
#define DSZSIZE 4       //data count in bytes
#define ADDRSIZE 4      //address size in bytes
#define DATASIZE 4      //data size in bytes
#define ECHOSIZE 1000   //echo max size in bytes
#define TYPEMANSIZE 4   //man_type

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
#define ERROR_INTERFACE 25
#define ERROR_CON_THREAD_CREATE 26

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

typedef struct {
    int rw_socket;
    struct sockaddr_in peer_addr;
    fd_set rfds;
    pthread_t thread;
    char *readArray, *writeArray;

} Connection;

Connection *Connections[MAXNUMCONN];
int getI() {
    for (int i = 0; i < MAXNUMCONN; ++i)
        if (Connections[i] == NULL)
            return i;
}

void delCon(Connection *con);
void *working(void* arg);
//void delAllCon();
void freeConnection(Connection *con);

int tcp_socket;
fd_set tcp_rfds;
struct sockaddr_in my_addr;
uint16_t port_server = 3307;
int n_conned=0;     //current connections count

pthread_t thread_bulb;

#define closeAll_1  if(munmap(virtual_base, length+delta) == -1) \
                        handle_error(ERROR_MUNMAP, NULL); \
                    close(fd);\

#define closeAll_2  if(close(tcp_socket) == -1) \
                        handle_error(ERROR_CLOSE_SOCK, NULL); \
                    if(munmap(virtual_base, length+delta) == -1) \
                        handle_error(ERROR_MUNMAP, NULL); \
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

int handle_error(int err, Connection* con)
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
        sprintf(msg, "ERROR: two program arguments must be, ./stage3 <network_interfce> <t|l>\n");
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
    } else if (err == ERROR_INTERFACE) {
        sprintf(msg, "ERROR: getifaddrs() failed or not found eth0 interface: %s\n", strerror(errsv));
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
        writeLog(msg);
        freeConnection(con);
    } else if (err == ERROR_CON_THREAD_CREATE) {
        sprintf(msg, "ERROR: Connection thread didn't start: %s\n", strerror(errsv));
        writeLog(msg);
        freeConnection(con);
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
        delCon(con);
        return 0;
    } else if (err == ERROR_READ) {
        sprintf(msg, "ERROR: read() failed(==-1)...: %s\n", strerror(errsv));
        writeLog(msg);
        delCon(con);
        return 0;
    } else if (err == ERROR_WRITE) {
        sprintf(msg, "ERROR: write() failed(==-1)...: %s\n", strerror(errsv));
        writeLog(msg);
        delCon(con);
        return 0;
    } else if (err == ERROR_TIME) {
        sprintf(msg, "WARNING: No data within five seconds.\n");
        writeLog(msg);
        delCon(con);
        return 0;
    } else if (err == ERROR_SELECT_CONN) {
        sprintf(msg, "ERROR: select() connection failed...: %s\n", strerror(errsv));
        writeLog(msg);

//        closeAll_2
//        exit(EXIT_FAILURE);
    } else if (err == ERROR_SELECT_READ) {
        sprintf(msg, "ERROR: select() read failed...: %s\n", strerror(errsv));
        writeLog(msg);
        delCon(con);
        return 0;
    } else if (err == ERROR_FD_ISSET_CONN) {
        sprintf(msg, "ERROR: FD_ISSET connection failed...\n");
        writeLog(msg);

//        closeAll_2
//        exit(EXIT_FAILURE);
    } else if (err == ERROR_FD_ISSET_READ) {
        sprintf(msg, "ERROR: FD_ISSET read failed...\n");
        writeLog(msg);
        delCon(con);
        return 0;
    } else if (err == ERROR_CMD) {
        sprintf(msg, "ERROR: unknown command code\n");
        writeLog(msg);
        delCon(con);
        return -1;
    } else if (err == ERROR_DSZ) {
        sprintf(msg, "ERROR: data size no multiple 4\n");
        writeLog(msg);
        delCon(con);
        return -1;
    } else if (err == ERROR_MALLOC) {
        sprintf(msg, "ERROR: malloc return NULL\n");
        writeLog(msg);
        delCon(con);
        return -1;
    } else if (err == ERROR_MULTIPLE) {
        sprintf(msg, "ERROR: Not multiple cmd 4 data for read\n");
        writeLog(msg);
        delCon(con);
        return -1;
    } else if (err == ERROR_SIGNAL) {
        sprintf(msg, "WARNING: Stop signal is not working\n");
        writeLog(msg);
        return 0;
    } else if (err == ERROR_THREAD_CLOSE) {
        if (con==NULL)
            sprintf(msg, "ERROR: Thread close function is not working on %s\n", "Bulb");
        if (con!=NULL)
            sprintf(msg, "ERROR: Thread close function is not working on %s\n", " Connection");
        writeLog(msg);
        return 0;
    } else if (err == ERROR_SETSOCKOPT) {
        sprintf(msg, "ERROR: SETSOCKOPT failed: %s\n", strerror(errsv));
        writeLog(msg);
        return 0;
    }
}

void freeConnection(Connection *con) {
    int i = 0;
    for (; (i < MAXNUMCONN) && (Connections[i] != con); ++i)  continue;
    free((void*)con);                   //?????? pthread_t
    Connections[i] = NULL;
}

void delCon(Connection *con) {
    if (con->rw_socket != -1) {
//        printf("Go to if(con->rw_socket !=-1)\n");
        if (close(con->rw_socket) == -1)
            handle_error(ERROR_CLOSE_SOCK, NULL);
    }
    if (con->readArray != NULL) {
//        printf("free(con->readArray)\n");
        free(con->readArray);
    }
    if (con->writeArray != NULL) {
//        printf("free(con->writeArray)\n");
        free(con->writeArray);
    }
    freeConnection(con);
    n_conned-=1;
    printf("One connection is deleted, active connection count: %d\n", n_conned);
}

void delAllCon() {
    for (int i = 0; i < MAXNUMCONN; ++i) {
//        printf("%d\n", i);
        if (Connections[i] == NULL) continue;
//        printf("%d\n", i);
        //if (pthread_cancel(Connections[i]->thread) != 0) handle_error(ERROR_THREAD_CLOSE, Connections[i]);
        if (Connections[i]->rw_socket !=-1) {
//            printf("Go to if(Connections[i]->rw_socket !=-1)\n");
            if (close(Connections[i]->rw_socket) == -1)
                handle_error(ERROR_CLOSE_SOCK, NULL);
        }
//        printf("%d\n", i);
        if (Connections[i]->readArray != NULL) {
//            printf("free(Connections[i]->readArray)\n");
            free(Connections[i]->readArray);
        }
        if (Connections[i]->writeArray != NULL) {
//            printf("free(Connections[i]->writeArray)\n");
            free(Connections[i]->writeArray);
        }
//        printf("%d\n", i);
        if (pthread_cancel(Connections[i]->thread) != 0) handle_error(ERROR_THREAD_CLOSE, Connections[i]);
        free((void*)Connections[i]);
//        printf("%d\n", i);
        Connections[i] = NULL;
        n_conned-=1;
    }
    printf("All threads deleted, active connection count: %d\n", n_conned);
}

int connection() {
    socklen_t peer_addr_size = sizeof(struct sockaddr_in);
    char txt[100];
    for (int i = 0; i < MAXNUMCONN; ++i)
        Connections[i] = NULL;

    while(1) {
        writeLog("listening...\n");
        FD_ZERO(&tcp_rfds);
        FD_SET(tcp_socket, &tcp_rfds);
        int retval = select(tcp_socket+1, &tcp_rfds, NULL, NULL, NULL);
        if (retval) {
            if (FD_ISSET(tcp_socket,&tcp_rfds)) {
                if (n_conned == MAXNUMCONN) {
                    printf("n_conned == MAXNUMCONN\n");     //??????
                    continue;
                }
                int i = getI();
                Connections[i] = (Connection*)malloc(sizeof(Connection));
                Connections[i]->readArray = NULL; Connections[i]->writeArray = NULL;
                Connections[i]->rw_socket = accept(tcp_socket, (struct sockaddr*) &(Connections[i]->peer_addr), &peer_addr_size);
                if (Connections[i]->rw_socket == -1) {
                    handle_error(ERROR_ACCEPT, Connections[i]);
                    continue;
                }
#ifdef debug
                printf("Peer port: %d\n", ntohs(Connections[i]->peer_addr.sin_port));
#endif
                if (pthread_create(&(Connections[i]->thread), NULL, &working, Connections[i]) != 0) {
                    handle_error(ERROR_CON_THREAD_CREATE, Connections[i]);
                } else {
                    sprintf(txt,"Connection on port %d is available now\n", ntohs(Connections[i]->peer_addr.sin_port));
                    writeLog(txt);
                    n_conned=n_conned+1;
                }
            } else {
                handle_error(ERROR_FD_ISSET_CONN, NULL);
            }
        } else if (retval == -1) {
            handle_error(ERROR_SELECT_CONN, NULL);
        }
    }
}

int readAllData(int size, char* refArray, struct timeval* tv, Connection* con) {
    //printf("readAllData start\n");
    ssize_t r=0;
    int n=0;
    while (1) {
        FD_ZERO(&con->rfds);
        FD_SET(con->rw_socket, &con->rfds);
        int retval = select(con->rw_socket+1, &con->rfds, NULL, NULL, tv);
        if (retval) {
            if (FD_ISSET(con->rw_socket,&con->rfds)) {
                r = read(con->rw_socket, refArray+n, size - n);
                if (r == -1) {
                    handle_error(ERROR_READ, con);
                    return -1;
                }
                if (r ==  0) {
                    handle_error(ERROR_CONNECTION, con);
                    return -1;
                }
            } else {
                handle_error(ERROR_FD_ISSET_READ, con);
                return -1;
            }
        } else if(retval == -1) {
            handle_error(ERROR_SELECT_READ, con);
            return -1;
        } else {
            handle_error(ERROR_TIME, con);
            return -1;
        }
        n+=r;
        if (n>=size) {
            return 0;
        }
    }
}

int writeAllData(int size, char* refArray, Connection* con) {
    ssize_t r=0;
    int n=0;
    while (1) {
        r = write(con->rw_socket, refArray+n, size - n);
        if (r == -1) {
            handle_error(ERROR_WRITE, con);
            return -1;
        }
        if (r ==  0) {
            handle_error(ERROR_CONNECTION, con);
            return -1;
        }
        n+=r;
        if (n>=size) {
            return 0;
        }
    }
}

int checkCmd(int cmd, Connection* con) {
    if (!((cmd>=1) && (cmd<=7))) {
        handle_error(ERROR_CMD, con);
        return -1;
    }
    return 0;
}

int checkMult(int num, int who, Connection* con) {
    int remainder = num % 4;
    if (remainder != 0) {
        if (who == 0)
            handle_error(ERROR_DSZ, con);
        if (who == 1)
            handle_error(ERROR_MULTIPLE, con);
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
    if (pthread_cancel(thread_bulb) != 0) handle_error(ERROR_THREAD_CLOSE, NULL);
//    int optval = 1; socklen_t len = sizeof(optval);
//    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, (void*)&optval, len);

    delAllCon();
    if(close(tcp_socket) == -1) handle_error(ERROR_CLOSE_SOCK, NULL);
    if(munmap(virtual_base, length+delta) == -1) handle_error(ERROR_MUNMAP, NULL);
    close(fd);
    kill(getpid(), SIGKILL);
}

void *working(void* arg) {
    char echoBuf[ECHOSIZE];
    Connection *con = (Connection *)arg;
    int cmd, dsz, addr, data;
    struct timeval tv;
    char *readArray, *writeArray;
    while (1) {
        tv.tv_sec = 5; tv.tv_usec = 0;
        if (readAllData(CMDSIZE, (char*)&cmd, NULL, con) == -1) return NULL;
        if (checkCmd(cmd, con) == -1) return NULL;

        //----- read dsz ------
        if (readAllData(DSZSIZE, (char*)&dsz, &tv, con) == -1) return NULL;
        if (cmd >= 1 && cmd <=4 )
            if (checkMult(dsz, 0, con) == -1) return NULL;

        con->readArray = (char*)malloc((size_t)dsz);
        readArray = con->readArray;
        if (readArray == NULL) {
            handle_error(ERROR_MALLOC, con);
            return NULL;
        }

        if (readAllData(dsz, readArray, &tv, con) == -1) return NULL;

        //----- pars cmd ------
        if (cmd == 1) {
            int N = dsz/(DATASIZE+ADDRSIZE);
            pthread_mutex_lock(&mutex);
            for (int i=0; i<N; i++) {
                addr = *(int*)(readArray+(DATASIZE+ADDRSIZE)*i);    data = *(int*)(readArray+(DATASIZE+ADDRSIZE)*i+n);
                memcpy(virtual_base + addr + delta, (void*)&data, n);
            }
            pthread_mutex_unlock(&mutex);
            if (writeAllData(CMDSIZE, (char*)&cmd, con) == -1) return NULL;
        } else if (cmd == 2) {
            addr = *(int*)(readArray);
            int N = (dsz-ADDRSIZE)/DATASIZE;
            pthread_mutex_lock(&mutex);
            for (int i=0; i<N; i++) {
                data = *(int*)(readArray+DATASIZE*(i+1));
                memcpy(virtual_base + addr + i*ADDRSIZE + delta, (void*)&data, n);
            }
            pthread_mutex_unlock(&mutex);
            if (writeAllData(CMDSIZE, (char*)&cmd, con) == -1) return NULL;
        } else if (cmd == 3) {
            int N = dsz/4;
            con->writeArray = (char*)malloc((size_t)dsz);
            writeArray = con->writeArray;
            if (writeArray == NULL) {
                handle_error(ERROR_MALLOC, con);
                return NULL;
            }
            pthread_mutex_lock(&mutex);
            for (int i=0; i<N; i++) {
                addr = *(int*)(readArray+ADDRSIZE*i);
                memcpy((void*)(writeArray+DATASIZE*i), virtual_base + addr + delta, n);
            }
            pthread_mutex_unlock(&mutex);
            if (writeAllData(dsz, writeArray, con) == -1) return NULL;

            free(con->writeArray);
            writeArray = NULL;  con->writeArray = NULL;
        } else if (cmd == 4) {
            addr = *(int*)(readArray);   int count = *(int*)(readArray+DATASIZE);
            if (checkMult(count, 1, con) == -1) return NULL;
            con->writeArray = (char*)malloc((size_t)count);
            writeArray = con->writeArray;
            if (writeArray == NULL) {
                handle_error(ERROR_MALLOC, con);
                return NULL;
            }

            pthread_mutex_lock(&mutex);
            for (int i=0; i<count; i+=DATASIZE, addr+=DATASIZE)
                memcpy((void*)(writeArray+i), virtual_base + addr + delta, n);
            pthread_mutex_unlock(&mutex);
            if (writeAllData(count, writeArray, con) == -1) return NULL;

            free(con->writeArray);
            writeArray = NULL;  con->writeArray = NULL;
        } else if (cmd == 5) {
            printf("%s\n", readArray);
            if (writeAllData(CMDSIZE, (char*)&cmd, con) == -1) return NULL;
        } else if (cmd == 6) {

        } else if (cmd == 7) {
            addr = *(int*)(readArray);
            size_t count = (size_t)*(int*)(readArray+DATASIZE);
            int destAddr = *(int*)(readArray+2*DATASIZE);
            if (checkMult(count, 1, con) == -1) return NULL;

            pthread_mutex_lock(&mutex);
            for (int i=0; i<count; i+=DATASIZE, addr+=DATASIZE)
                memcpy(virtual_base + destAddr + i + delta, virtual_base + addr + delta, n);
            pthread_mutex_unlock(&mutex);

            if (writeAllData(CMDSIZE, (char*)&cmd, con) == -1) return NULL;
        } else {
            return NULL;
        }
        free(con->readArray);
        readArray = NULL;   con->readArray = NULL;
    }
}

int main(int argc, char* argv[])
{
    //------------------------------------------------------------ Output setting ---------------------------------------------------------
    if (argc < 2) handle_error(ERROR_ARG, NULL);
    if ((argc > 2) && (strcmp(argv[2], "t") == 0)) lt = "t";

    //------------------------------------------------------------ Virtual memory setting ---------------------------------------------------------
    fd = open("/dev/mem", (O_RDWR | O_SYNC));

    if (fd == -1)   handle_error(ERROR_OPENMEM, NULL);

    long ps = sysconf(_SC_PAGESIZE);
    pa_offset = offset & ~(ps-1);
    delta = offset - pa_offset;

    virtual_base = mmap(NULL, length + delta, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, pa_offset);
    if(virtual_base == MAP_FAILED)  handle_error(ERROR_MMAP, NULL);

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

    if (tcp_socket == -1)   handle_error(ERROR_OPENSOCK, NULL);

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port_server);

    //if (inet_aton(argv[1], &my_addr.sin_addr) == 0) handle_error(ERROR_ATON); //старый вариант с параметром ip вместо интерфейса у программы
    my_addr.sin_addr.s_addr = 0;
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) handle_error(ERROR_INTERFACE, NULL);
    for (ifa=ifaddr; ifa!=NULL; ifa = ifa->ifa_next) {
        if (strcmp(argv[1], ifa->ifa_name) != 0) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        my_addr.sin_addr.s_addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
        break;
    }
    freeifaddrs(ifaddr);
    if (my_addr.sin_addr.s_addr == 0) handle_error(ERROR_INTERFACE, NULL);

    if (bind(tcp_socket, (struct sockaddr *) &my_addr, sizeof(struct sockaddr_in)) == -1) handle_error(ERROR_BIND, NULL);

    if (listen(tcp_socket, MAXNUMCONN) == -1)    handle_error(ERROR_LISTEN, NULL);

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
    if (sigaction(40, &de1_stop_action, NULL) == -1) handle_error(ERROR_SIGNAL, NULL);
    else printf("For correct close the programm send signal 40 to the process.\n");

    //------------------------------------------------------------ Main cycle ---------------------------------------------------------
    connection();

    if (pthread_cancel(thread_bulb) != 0) handle_error(ERROR_THREAD_CLOSE, NULL);
    delAllCon();
    char msg[100];
    sprintf(msg, "Main cycle is finished\n");
    writeLog(msg);
    closeAll_2

    return 0;
}

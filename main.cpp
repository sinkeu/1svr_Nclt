#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>  
#include <sys/un.h>  
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

void SigintHandler(int);
int32_t SigintSetup(void);

struct StreamInfo {
    uint32_t uMatPhyAddr[FDMA_IX_END];
    volatile uint32_t todoCnt[FDMA_IX_END];
    pthread_mutex_t mtx; //mutex for todoCnt
};
struct StreamInfo mSInfo;

#define CONN_MAX 32
static int popon = 0;
static int connStore[CONN_MAX];
static int connsCnt;
static pthread_mutex_t connMtx = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t capp = PTHREAD_COND_INITIALIZER; //condition for app process done
static pthread_mutex_t cappMtx = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t cpop = PTHREAD_COND_INITIALIZER; //condition for pop done
static pthread_mutex_t cpopMtx = PTHREAD_MUTEX_INITIALIZER;

bool IsAllAppsProcessDone(void)
{
    int i = 0;

    pthread_mutex_lock(&mSInfo.mtx);
    for(; i < FDMA_IX_END; i++) {
        if(mSInfo.todoCnt[i] > 0)
            break;
    }
    pthread_mutex_unlock(&mSInfo.mtx);

    if(i == FDMA_IX_END)
        return true;

    return false;
}

bool IsAllConnsBreak(void)
{
    int i = 0;
    for(; i < CONN_MAX; i++) {
        if(connStore[i] != 0)
            break;
    }

    if(i == CONN_MAX)
        return true;

    return false;
}

void TrimConnStore(int fd)
{
    pthread_mutex_lock(&connMtx);
    //1. clear fd
    for(int i = 0; i < CONN_MAX; i++) {
        if(connStore[i] == fd) {
            connStore[i] = 0;
            break;
        }
    }

    //2. reorganize connStore
    for(int i = 0; i+1 < CONN_MAX; i++) {
        if(connStore[i] == 0 && connStore[i+1] != 0) {
            connStore[i] = connStore[i+1];
            connStore[i+1] = 0;
        }
    }
    pthread_mutex_unlock(&connMtx);
}

void *SocketAgent(void *arg)
{
    int fdmaIdx = -1;
    int fd = *(int *)arg;
    int ret = 0;

    //which fdma index app use
    read(fd, &fdmaIdx, sizeof(int)); 
    printf("recv fdma index %d\n", fdmaIdx);

    if(fdmaIdx < 0 || fdmaIdx >= FDMA_IX_END) {
        printf("unknown fdma index.\n");
        return 0;
    }

    //store connection fd
    pthread_mutex_lock(&connMtx);
    connStore[connsCnt++] = fd;
    pthread_mutex_unlock(&connMtx);

    while(1) {
            
#if DEBUG_SOCK
        printf("wait for buffer pop...\n");
#endif
        pthread_mutex_lock(&cpopMtx);
        pthread_cond_wait(&cpop, &cpopMtx);
        pthread_mutex_unlock(&cpopMtx);

        if(!mSInfo.uMatPhyAddr[fdmaIdx]) {
            printf("no buffers for use.\n"); 
            continue;
        }

        //increase todo count
        pthread_mutex_lock(&mSInfo.mtx);
        mSInfo.todoCnt[fdmaIdx]++; 
        pthread_mutex_unlock(&mSInfo.mtx);

        //tell client physical address of buffer
        write(fd, &mSInfo.uMatPhyAddr[fdmaIdx], sizeof(mSInfo.uMatPhyAddr[fdmaIdx]));

        //waiting for app processing done...
        ret = read(fd, &fdmaIdx, sizeof(fdmaIdx));

        //app client may done, decrease todo count
        pthread_mutex_lock(&mSInfo.mtx);
        mSInfo.todoCnt[fdmaIdx]--; 
        pthread_mutex_unlock(&mSInfo.mtx);

        //app client stop or goes wrong
        if(ret == 0) {
            printf("client number [%d] stop or goes wrong, connect break.\n", fd);
            break;
        }

        //all apps are done, wakeup main process to go on popping
        pthread_mutex_lock(&cappMtx);
        if(IsAllAppsProcessDone())
            pthread_cond_signal(&capp);
#if DEBUG_SOCK
        printf("signal main process go on popping...\n");
#endif
        pthread_mutex_unlock(&cappMtx);
    }

    //connection break, decrease connection count
    connsCnt--;

    //reorganize ConnStore
    TrimConnStore(fd);

    //all connections break, stop buffer pop
    if(IsAllConnsBreak()) {
        //stop pop
        popon = 0;
        printf("stop pop.\n");

    }

    //wakeup main process if main is waiting
    pthread_mutex_lock(&cappMtx);
    pthread_cond_signal(&capp);
    pthread_mutex_unlock(&cappMtx);

    close(fd);
    pthread_exit(NULL);
}

void *SocketSvr(void *arg)
{
    int listen_fd, connfd;
    int ret, len;
    pthread_t pid;
    pid_t childpid;
    socklen_t clt_addr_len;
    struct sockaddr_in servaddr, cliaddr;

    printf("sock svr start...\n");
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0)
    {
        printf("cannot create communication socket\n");
        return 0;
    }
      
    //set server addr param
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(61080);

    //bind sockfd & addr
    ret = bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if(ret == -1)
    {
        printf("cannot bind server socket\n");
        close(listen_fd);
        return 0;
    }

    //listen sockfd   
    ret = listen(listen_fd, CONN_MAX);
    if(ret == -1)
    {
        printf("cannot listen the client connect request\n");
        close(listen_fd);
        return 0;
    }

    while(1) {
        //accept client connect
        len = sizeof(cliaddr);
        connfd = accept(listen_fd, (struct sockaddr*)&cliaddr, (socklen_t *)&len);
        if(connfd < 0)
        {
            printf("cannot accept client connect request\n");
            close(listen_fd);
            return 0;
        }
        printf("connect accept.\n");

        pthread_create(&pid, NULL, SocketAgent, &connfd);

        //give enough time to SocketAgent thread to schedule before main process send broadcast signal.
        //this is special for the first connection and first SocketAgent thread.
        //also, it have no influence on SocketAgent threads created later.
        usleep(50*1000);

        //start buffer pop once client connect
        popon = 1;
    }

    close(listen_fd);

    return 0;
}

int main(int argc, char *argv[])
{
  int i = 0;
  SDI_Frame lf[FDMA_IX_END];
  pthread_t sockPid;

  pthread_mutex_init(&mSInfo.mtx, NULL);

  pthread_create(&sockPid, NULL, SocketSvr, NULL);

  // *** grabbing/processing loop ***
  for(;;)
  {
    if(sStop) break;

    if(!popon) {
        usleep(1000*1000); //1s
        continue;
    }

    for(i = 0; i < FDMA_IX_END; i++) {
        lf[i] = lpGrabber->FramePop(i);
        if(lf[i].mUMat.empty())
        {
                printf("Failed to grab image number\n");
                sStop = true;
                break;
        } //if pop failed

        //get mUMat buffer physical address
        mSInfo.uMatPhyAddr[i] = ((uint32_t)(uint64_t)lf[i].mUMat.u->handle) + lf[i].mUMat.offset;
    }

#if DEBUG_SOCK
    printf("buffer poped.\n");
#endif

    //wakeup agent, send address to app
    pthread_mutex_lock(&cpopMtx);
    pthread_cond_broadcast(&cpop);
    pthread_mutex_unlock(&cpopMtx);

    //wait for all apps process done
    pthread_mutex_lock(&cappMtx);
    pthread_cond_wait(&capp, &cappMtx);
    pthread_mutex_unlock(&cappMtx);

    for(i--; i >= 0; i--) {
        mSInfo.uMatPhyAddr[i] = 0;
        lpGrabber->FramePush(lf[i]);
    }
  } // for ever
  
  return lRet;
}

void SigintHandler(int)
{
  sStop = true;
} // SigintHandler()

int32_t SigintSetup()
{
  int32_t lRet = SEQ_LIB_SUCCESS;
  
  // prepare internal signal handler
  struct sigaction lSa;
  memset(&lSa, 0, sizeof(lSa));
  lSa.sa_handler = SigintHandler;
  
  if( sigaction(SIGINT, &lSa, NULL) != 0)
  {
    VDB_LOG_ERROR("Failed to register signal handler.\n");
    lRet = SEQ_LIB_FAILURE;
  } // if signal not registered
  
  return lRet;
} // SigintSetup()

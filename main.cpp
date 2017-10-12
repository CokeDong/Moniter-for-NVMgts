#include <iostream>
#include <sys/shm.h> // symphony header
#include <sys/socket.h> //socket header
#include <sys/sem.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "const.hpp"
#include <string>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>


using namespace std;

typedef struct SMDATA
{
    double dataDou[4];
    int type;
    char stringData[10000];
    bool flag[2];
    int dataInt[4];
} SMDATA;

union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static int creat_sem(key_t key);
static int set_semvalue(int semid);
static int sem_p(int semid);
static int sem_v(int semid);
static int del_sem(int semid);

int main()
{
    cout << "Hello world!" << endl;
    void* shm[SMTYPE_NUM] = {NULL};
    SMDATA *shared[SMTYPE_NUM] = {NULL};
    int shmid[SMTYPE_NUM];
    int semid[SMTYPE_NUM];
    //-------------------------------------------------
    // initial shm and semaphore
    for(int i=0; i<SMTYPE_NUM; i++)
    {
        shmid[i] = shmget((key_t)i,sizeof(SMDATA),0666|IPC_CREAT);
        if(shmid[i] == -1)
        {
            // fail
            printf("failed to create shared memory :%d",shmid[i]);
        }
        shm[i] = shmat(shmid[i],0,0);
        if(shm[i]==(void*)-1)
        {
            printf("shmat err\n");
        }
        shared[i] = (SMDATA*)shm[i];
        // initial semaphore
        semid[i] = creat_sem((key_t)i);
        set_semvalue(semid[i]);
        shared[i]->flag[0] = false;
        shared[i]->flag[1] = false;
    }
    //------------------------------------------------------------
    // initial socket
    int sock_fd, client_fd;
    struct sockaddr_in ser_addr;
    struct sockaddr_in cli_addr;
    char buffer[8 * 1024 * 1024]; // buffer
    int ser_sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); // non-block mode
    if(ser_sockfd<0)
    {
        // fail
        printf("fail to create socket.\n");
    }
    socklen_t addrlen = sizeof(struct sockaddr_in);
    bzero(&ser_addr, addrlen);
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ser_addr.sin_port = htonl(SERVER_PORT);
    if(bind(ser_sockfd, (struct sockaddr*)&ser_addr,sizeof(struct sockaddr_in))<0)
    {
        printf("bind error.\n");
    }
    if(listen(ser_sockfd,5)<0)
    {
        printf("listen fail.\n");
        close(ser_sockfd);
    }
    int closeMe = 0;
    int numTrajs = 0,numPoints = 0;

    client_fd = accept(ser_sockfd, (struct sockaddr*)&cli_addr,&addrlen);
    if(client_fd<=0)
        printf("Accept error.\n");
    else
    {
        // start service.
        char sendMsg[4*1024*1024];
        while(!closeMe)
        {
            // shm 0
            //--------------------------------------------------------
            // load data
            bool haveFinisedLoad, haveStartedLoad;
            if(sem_p(semid[0]))
            {
                printf("sem_p fail.\n");
            }
            haveStartedLoad = shared[0]->flag[1];
            if(haveStartedLoad)
            {
                // check if finish
                haveFinisedLoad = shared[0]->flag[0];
                if(haveFinisedLoad)
                {
                    numTrajs = shared[0]->dataInt[0];
                    numPoints = shared[0]->dataInt[1];
                    double startLoadTime,endLoadTime;
                    startLoadTime = shared[0]->dataDou[0];
                    endLoadTime = shared[0]->dataDou[1];
                    // send info about num through socket

                    char tempStr[1024*64];
                    // int strLen = 0+2+2;
                    memset(sendMsg,0,4*1024*1024);
                    strcat(sendMsg, "0;");
                    sprintf(tempStr, "%d:", numTrajs);
                    strcat(sendMsg,tempStr);
                    sprintf(tempStr, "%d:",numPoints);
                    strcat(sendMsg,tempStr);
                    sprintf(tempStr, "%llf:", (endLoadTime - startLoadTime))/1000000;
                    strcat(sendMsg,tempStr);
                    strcat(sendMsg, ";");
                    strcat(sendMsg, "1;");
                    send(client_fd, sendMsg, strlen(sendMsg) +1, 0); // send message

                    // after finish loading, set flag to false
                    shared[0]->flag[0] = false;
                    shared[0]->flag[1] = false;
                }
            }
            else
            {
                //check if a socket saying start loading
                memset(buffer,0, sizeof(buffer));
                int len = recv(client_fd, buffer, sizeof(buffer), 0);
                // for LOAD, format of msg is 0;fileName;finishFlag;
                if(len > 0)
                {
                    char* temp = NULL;
                    temp = strtok(buffer,";");
                    if(atoi(temp) == 0)
                    {
                        // start loading
                        temp = strtok(NULL, ";"); // file Name
                        char cmd[100];
                        strcat(cmd , "./test l ");
                        strcat(cmd, temp);
                        system(cmd); // !!!! need GTS support this api and send data through share mem
                        shared[0]->flag[1] = true;
                    }
                }
            }
            if(sem_v(semid[0]))
            {
                printf("sem_v fail.\n");
            }
            // shm 1
            //-----------------------------------------------------
            // batch from gts to client
            if(sem_p(semid[1]))
            {
                printf("sem_p fail.\n");
            }
            bool allFinish,finishOne;
            allFinish = shared[1]->flag[0];
            finishOne = shared[1]->flag[1];
            if(finishOne)
            {
                // only if this is true, batch query need to be check
                int finishNum = shared[1]->dataInt[0];
                double runningTimeOne = shared[1]->dataDou[3] - shared[1]->dataDou[2];
                printf("Task %d finished using %llf s.\n",finishNum,runningTimeOne/1000000);
                // send this message through socket
                //.......
                // maybe not need to return through socket because of the high latency of internet
                shared[1]->flag[1] = false;
                if(allFinish)
                {
                    double runningTimeBatch = shared[1]->dataDou[1] - shared[1]->dataDou[0];
                    printf("All queries are finished using %llf s.\n",finishNum,runningTimeOne/1000000);
                    // send this message through socket
                    // for SENDBatch, format of msg is 1;time:;finishFlag=1;
                    char tempStr[1024*64];
                    // int strLen = 0+2+2;
                    memset(sendMsg,0,4*1024*1024);
                    strcat(sendMsg, "1;");
                    sprintf(tempStr, "%llf:", runningTimeBatch/1000000);
                    strcat(sendMsg,tempStr);
                    strcat(sendMsg, ";");
                    strcat(sendMsg, "1;");
                    send(client_fd, sendMsg, strlen(sendMsg) +1, 0); // send message
                    //........
                    shared[1]->flag[0] = false;
                }

            }
            if(sem_v(semid[1]))
            {
                printf("sem_v fail.\n");
            }
            // shm 2
            //-----------------------------------------------------
            // batch from client to gts

            // look for socket
            // if there is a call
            // .........

            // shm 3
            //-----------------------------------------------------
            // single from gts to client
            if(sem_p(semid[3]))
            {
                printf("sem_p fail.\n");
            }
            allFinish = shared[3]->flag[0];
            if(allFinish)
            {
                double runningTime = shared[3]->dataDou[1] - shared[3]->dataDou[0];
                printf("This query is finished using %llf s.\n", runningTime/1000000);
                // send this msg through socket
                //......
                shared[3]->flag[0] = false;
            }
            if(sem_v(semid[3]))
            {
                printf("sem_v fail.\n");
            }
            // shm 4
            //-----------------------------------------------------
            // single from client to gts

            // look for socket
            // if ......

            // shm 5
            //-----------------------------------------------------
            // get status of system

            // shm 6
            //-----------------------------------------------------
            // clean data

            //---------------------------------------------------------
            // need demo? get from socket.


            // --------------------------------------------------------
            // need close?
            // if socket say close, set closeMe=1



        }
    }

    // clean database and kill it
    // close socket.....
    // close shm,semaphore....
    for(int i=0; i<SMTYPE_NUM; i++)
    {
        if(shmdt(shm[i])==-1)
        {
            printf("fail shmdt.\n");
        }
        if(shmctl(shmid[i],IPC_RMID,0)==-1)
        {
            printf("fail shm_rmID.\n");
        }
        if(del_sem(semid[i]))
            printf("delete sem fail.\n");
    }


    return 0;

}



int creat_sem(key_t key)
{
    int semid = 0;

    semid = semget(key, 1, IPC_CREAT|0666);
    if(semid == -1)
    {
        printf("%s : semid = -1!\n",__func__);
        return -1;
    }

    return semid;

}

int set_semvalue(int semid)
{
    union semun sem_arg;
    sem_arg.val = 1;

    if(semctl(semid, 0, SETVAL, sem_arg) == -1)
    {
        printf("%s : can't set value for sem!\n",__func__);
        return -1;
    }
    return 0;
}

int sem_p(int semid)
{
    struct sembuf sem_arg;
    sem_arg.sem_num = 0;
    sem_arg.sem_op = -1;
    sem_arg.sem_flg = SEM_UNDO;

    if(semop(semid, & sem_arg, 1) == -1)
    {
        printf("%s : can't do the sem_p!\n",__func__);
        return -1;
    }
    return 0;
}

int sem_v(int semid)
{
    struct sembuf sem_arg;
    sem_arg.sem_num = 0;
    sem_arg.sem_op = 1;
    sem_arg.sem_flg = SEM_UNDO;

    if(semop(semid, & sem_arg, 1) == -1)
    {
        printf("%s : can't do the sem_v!\n",__func__);
        return -1;
    }
    return 0;
}

int del_sem(int semid)
{
    if(semctl(semid, 0, IPC_RMID) == -1)
    {
        printf("%s : can't rm the sem!\n",__func__);
        return -1;
    }
    return 0;
}

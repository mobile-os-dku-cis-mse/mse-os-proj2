#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>

#define MAX_PROC 10
#define PFN 0x100000	//# of page 1M

int tick = 10000;
int time_q = 10;
int exec;
int end_exec;	//0:not yet  /  1:end
int curr;
void time_tick(int signo);
void CPU();
void MMU();
pid_t parent;

struct PCB{
	int remain_exec;
	int remain_tq;
};

int PM[PFN/4] = {0,};	//physical memory
int FreeFrame[PFN/4]={0,};	//free page frame list
struct PageTableEntry{
	int pfn;
	int valid;	//0:invlid 1:valid
};
struct PageTable{
	pid_t pid;
	struct PageTableEntry pte[PFN/4];
	struct PCB pcb;
};
struct PageTable pt[MAX_PROC];


//msg
int msgq;
int ret;
int key = 0x19273;
struct MemAccessRequest{
	pid_t pid;
	int acc_rq[10];
	int new_exec;
};
struct MemAccessRequest msg;




int main(int argc, char *arg[]){


	struct sigaction old_sa, new_sa;
	memset(&new_sa, 0, sizeof(new_sa));
	new_sa.sa_handler = &time_tick;
	sigaction(SIGALRM, &new_sa, &old_sa);

	//sigusr1
	struct sigaction old_sa_sigusr, new_sa_sigusr;
	memset(&new_sa_sigusr, 0, sizeof(new_sa_sigusr));
	new_sa_sigusr.sa_handler = CPU;
	sigaction(SIGUSR1, &new_sa_sigusr, &old_sa_sigusr);

	//sigusr2
	struct sigaction old_sa_sigusr2, new_sa_sigusr2;
	memset(&new_sa_sigusr2, 0, sizeof(new_sa_sigusr2));
	new_sa_sigusr2.sa_handler = MMU;
	sigaction(SIGUSR2, &new_sa_sigusr2, &old_sa_sigusr2);

	struct itimerval new_itimer, old_itimer;
	memset(&new_itimer, 0, sizeof(new_itimer));
	new_itimer.it_interval.tv_sec = 1;
	new_itimer.it_interval.tv_usec = 0;
	new_itimer.it_value.tv_sec = 1;
	new_itimer.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &new_itimer, &old_itimer);




	srand(time(NULL));
	exec = rand()%100;

	parent = getpid();
	curr = 0;
	
	for(int i=0; i<MAX_PROC; i++){
		pid_t pid = fork();

		if(pid<0){
			perror("fork error");
			break;
		}
		else if(pid == 0){
			//child
			while(1);
		}
		else{
			//parent
			pt[i].pid = pid;
			pt[i].pcb.remain_tq = time_q;
			pt[i].pcb.remain_exec = exec;
			
			printf("pt[%d].pid:%d, exec:%d\n",i,pid,exec);

			srand(exec);
			exec = rand()%100;

		}

	}
	
	while(1);

	return 0;
}





void time_tick(int signo){

	if(tick==0){
		for(int i=0;i<MAX_PROC;i++){
			printf("----kill process %d----\n",pt[i].pid);
			kill(pt[i].pid,SIGKILL);
		}
		msgctl(msgq,IPC_RMID,NULL);
		printf("----all processes are terminated\n");
		kill(parent,SIGKILL);
	}

	if(pt[curr].pcb.remain_tq == 0){
		pt[curr].pcb.remain_tq = time_q;
		if(curr!=9){
			curr++;
		}
		else{
			curr = 0;
		}
	}

	tick--;
	
	pt[curr].pcb.remain_exec--;
	pt[curr].pcb.remain_tq--;

	if(pt[curr].pcb.remain_exec == 0){
		end_exec = 1;
		pt[curr].pcb.remain_tq = time_q;
	}


	
	printf("\n----tick %d----\n",tick);
	for(int i=0; i<10; i++){
		printf("[%d] exec:%d tq:%d\n",pt[i].pid,pt[i].pcb.remain_exec,pt[i].pcb.remain_tq);
	}

	kill(pt[curr].pid,SIGUSR1);
}







void CPU(){

	exec--;
	
	if(exec==0){
		end_exec = 1;
	}

	printf("------[%d] exec:%d\n",getpid(),exec);
		
	//msgsnd
	//random VA msgq에 전달.
	msgq = msgget(key,IPC_CREAT | 0666);
	memset(&msg, 0, sizeof(msg));

	msg.pid = getpid();
	srand(time(NULL));
	msg.acc_rq[0]=rand()%0xffffffff;
	for(int i=0;i<9;i++){
		srand(msg.acc_rq[i]);
		msg.acc_rq[i+1] = rand()%0xffffffff;
	}
	if(end_exec == 1){
		srand(time(NULL));
		exec = rand()%100;
		msg.new_exec = exec;
		end_exec = 0;
	}
	ret = msgsnd(msgq,&msg, sizeof(msg), NULL);
	//printf("msgsnd ret:%d\n",ret);

/*
	for(int i=0; i<10; i++){
		printf("VA[%d]:0x%08x\n",i,msg.acc_rq[i]);
	}
*/

	kill(parent,SIGUSR2);
}







void MMU(){
	int pageN;
	int offset;
	int frameN;
	int pa;


	//msgrcv	
	//msgq로va 받기
	msgq = msgget(key,IPC_CREAT | 0666);
	memset(&msg, 0, sizeof(msg));
	ret = msgrcv(msgq, &msg, sizeof(msg), 0, NULL);
	//printf("msgrcv ret:%d\n",ret);

/*
	for(int i=0; i<10; i++){
		printf("recived VA[%d]:0x%08x\n",i,msg.acc_rq[i]);
	}
*/
	

	//va --> pa addr trans
	for(int i=0; i<10; i++){
		pageN = msg.acc_rq[i]>>12;
		offset = msg.acc_rq[i]&0xfff;

		//page table check
		if(pt[curr].pte[pageN/4].valid == 0){
			//invalid
			//page fault!

			//random mapping
			srand(time(NULL));
			frameN = rand()%0x100000;
			while(1){
				if(FreeFrame[frameN/4] == 1){
					//already mapped frame
					//printf("already mapped frame!\n");
					srand(frameN);
					frameN = rand()%0x100000;
					continue;
				}
				else{
					FreeFrame[frameN/4] = 1;
					break;
				}
			}

			printf("request[%d] page fault! free frame:%d\n",i,frameN);

			pt[curr].pte[pageN/4].pfn = frameN;
			pt[curr].pte[pageN/4].valid = 1;

			//write
		}
		else{
			//valid

			//calculate PA
			frameN = pt[curr].pte[pageN/4].pfn;
			pa = (frameN<<12)+offset;
			printf("request[%d] access! frameN:%d\n",i,frameN);

			//PA access
			//read
		}
	}





	if(end_exec == 1){
		pt[curr].pcb.remain_exec  = msg.new_exec;
		if(curr!=9){
			curr++;
		}
		else{
			curr = 0;
		}
		end_exec = 0;
	}
}

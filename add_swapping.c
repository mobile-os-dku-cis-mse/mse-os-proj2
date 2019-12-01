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


int tick = 1000;
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

//int PM[PFN/4] = {0,};	//physical memory
//int FreeFrame[PFN]={0,};	//free page frame list
int FreeFrame[0x100]={0,};

struct PageTableEntry{
	int pfn;
	int valid;	//0:invlid 1:valid
};
struct PageTable{
	pid_t pid;
	struct PageTableEntry pte[PFN];
	struct PCB pcb;
};
struct PageTable pt[MAX_PROC];

//LRU algorithm

struct LRU_algorithm{
	int value[PFN];//pageN
	int age[PFN];//mmu_time
};

struct LRU_algorithm LRU[MAX_PROC];


//int DISK[0x100000];
struct DISK{
	int pageN;
	int pid;
	int data;
};

struct DISK disk[0x100000]= {0,};
int disk_fill;

//page flag bit
int p_flag[0x100000];//0: VM 1: disk

int mmu_time;

//msg
int msgq;
int ret;
int key = 0x10000;
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
	new_itimer.it_interval.tv_sec = 0;
	new_itimer.it_interval.tv_usec = 100000;
	new_itimer.it_value.tv_sec = 0;
	new_itimer.it_value.tv_usec = 100000;
	setitimer(ITIMER_REAL, &new_itimer, &old_itimer);




	srand(time(NULL));
	while(1){
		exec = rand()%100;
		if(exec > 0)
			break;
	}
	parent = getpid();
	curr = 0;
	
	for(int i = 0; i<MAX_PROC; i++){
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
			while(1){
				exec = rand()%100;
				if(exec > 0)
					break;
			}

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
	/*
	for(int i=0; i<10; i++){
		printf("[%d] exec:%d tq:%d\n",pt[i].pid,pt[i].pcb.remain_exec,pt[i].pcb.remain_tq);
	}
	*/
	kill(pt[curr].pid,SIGUSR1);
}




void CPU(){

	exec--;
	
	if(exec == 0){
		end_exec = 1;
	}

//	printf("------[%d] exec:%d\n",getpid(),exec);
		
	//msgsnd
	//random VA msgq에 전달.
	msgq = msgget(key,IPC_CREAT | 0666);
	memset(&msg, 0, sizeof(msg));

	msg.pid = getpid();
	srand(tick + exec*9);
	
	msg.acc_rq[0]=rand()%0xffffffff;
	for(int i=0;i<9;i++){
		srand(msg.acc_rq[i]+i*7+exec*6);
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
	int need_swap;
	int swapped_out_page;

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
		printf("i: %d, pageN: %d, offset: %d\n", i, pageN, offset);
		
		//page table check
		if(pt[curr].pte[pageN].valid == 0){
			//invalid
			//page fault!
			//printf("page fault\n");

			//checking swapping need + swap out
			int j = 0;
			while(1){
				if(FreeFrame[j] == 0 && j < 0x100){
					//There is FreeFrame
					//no swapping
					printf("no swapping\n");
					need_swap = 0;
					break;
				}
				else if(j == 0x100){
					//There is no FreeFrame
					//need swapping
					printf("need swapping\n");
					
					//first check disk
					int check_disk = 0;
					for(int m = 0; m < disk_fill; m++){
						if(p_flag[pageN] == 1 && getpid() == disk[m].pid){
							printf("data is in disk\n");
	
							//where swapped_in
							int age_min = LRU[curr].age[0];
							int age_min_index= 0;
							for(int n = 1; n < PFN; n++){
								if(LRU[curr].age[n] < age_min){
								age_min = LRU[curr].age[n];
								age_min_index = n;
								}
							}
			
							int temp = disk[m].pageN;
							disk[m].pageN = LRU[curr].value[age_min_index];
							disk[m].pid = getpid();
							pt[curr].pte[temp].pfn = pt[curr].pte[disk[m].pageN].pfn;
							

							pt[curr].pte[disk[m].pageN].valid = 0;
							pt[curr].pte[temp].valid = 1;
							p_flag[disk[m].pageN] = 1;//page is in disk
							p_flag[temp] = 0;//page is not in disk
							check_disk = 1;
							break;
							}
						else{
							check_disk = 0;
							//data is not in DISK
						}
					}//for
				
					if(check_disk == 1){
						need_swap = 2;
						break;
					}
					
					printf("data is not in disk, need swapping\n");
					//decide swapped_out_page
					int age_min = LRU[curr].age[0];
					int age_min_index = 0;
					for(int k = 0; k < PFN; k++){
						if(LRU[curr].age[k] < age_min){
							age_min = LRU[curr].age[k];
							age_min_index = k;
						}
					}
			
					swapped_out_page = LRU[curr].value[age_min_index];
					//printf("swapped_out_page: %d\n", swapped_out_page);

					//add DISK in front
					int k = 0;
					while(1){
						if(disk[k].pageN == 0 && disk[k].pid == 0){
							disk[k].pageN = swapped_out_page;
							disk[k].pid = getpid();
							break;
						}	
						k++;
					}

					//put new data
					pt[curr].pte[swapped_out_page].pfn = pt[curr].pte[disk[k].pageN].pfn;
					

					pt[curr].pte[disk[k].pageN].valid = 0;
					pt[curr].pte[pageN].valid = 1;
					disk_fill++;
					p_flag[pageN] = 1;//pageN is in disk
					need_swap = 1;
					break;
				}
				else{

				}
				j++;
			}//while
		
			//when swapping, tell empty frameN

			//no swap
			if(need_swap == 0){
				//random mapping

				srand(tick + pageN * 9 + i);
				//frameN = rand() % 0x100000;
				//frameN = rand() % 0x100;
				frameN = j;
				while(1){
					if(FreeFrame[frameN] == 1){
						//already mapped frame
						//printf("already mapped frame!\n");
						srand(frameN*7 + i*3);
						//frameN = rand()%0x100000;
						frameN = rand() % 0x100;
						continue;
					}
					else{
						FreeFrame[frameN] = 1;
						break;
					}
				}
			
				pt[curr].pte[pageN].pfn = frameN;
				pt[curr].pte[pageN].valid = 1;

			}//if
			//need_swap

			//printf("request[%d] page fault! free frame:%d\n",i,frameN);
			//printf("pt[%d].pte[%d].pfn = %d\n", curr, pageN, frameN);
			//write
			
		}//if

		else{
			//valid

			//calculate PA
			printf("already mapped page\n");
			frameN = pt[curr].pte[pageN].pfn;
			pa = (frameN<<12) + offset;
			//printf("request[%d] access! frameN:%d\n",i,frameN);

			//PA access
			//read
			
		}//else
		
		//LRU
		int min;
		int min_index;
		for(int j = 0; j < PFN; j++){
			if(LRU[curr].value[j] == 0 && LRU[curr].age[j] == 0){
				LRU[curr].value[j] = pageN;
				LRU[curr].age[j] = mmu_time;
				break;
			}//there is empty index
			else if(LRU[curr].value[j] != 0  && j == PFN){
				for(int k = 0; k < PFN; k++){
					if(LRU[curr].value[k] == pageN){
						//printf("same page is in LRU\n");
						LRU[curr].age[k] = mmu_time;
						break;
					}	
				}//for
			}
			else{

			}//else
		}//for
	
	mmu_time++;
	}//for

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

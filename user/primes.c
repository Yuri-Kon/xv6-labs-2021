#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define MAX_NUM 35

int create_pipe(int *pipefd){
	if(pipe(pipefd) < 0){
		fprintf(2, "pipe failed\n");
		return -1;
	}
	return 0;
}
//筛选素数
void sieve(int read_fd, int write_fd){
	int num;
	while(read(read_fd, &num, 4) > 0){
		//输出当前素数
		printf("prime %d\n", num);
		//为下一个进程筛选倍数
		int next_pipe[2];
		if(create_pipe(next_pipe) < 0){
			exit(1);
		}
		//fork子进程处理筛选
		if(fork() == 0){
			close(next_pipe[0]);
			//过滤掉当前素数的倍数，剩余数写入新管道
			int x;
			while(read(read_fd, &x, 4) >0){
				if(x % num != 0){
					write(next_pipe[1], &x, 4);
				}
			}
			close(next_pipe[1]);
			exit(0);
		}

		close(next_pipe[1]);
		read_fd = next_pipe[0];//更新读端的引用，传给下一个进程
				       //这个管道里应该只有当前素数的非倍数
	}
}

int main(int argc, char const *argv[]){
	int pipefd[2];
	if(create_pipe(pipefd) < 0){
		exit(1);
	}

	if(fork() == 0){
		close(pipefd[0]);
		for(int i = 2; i < MAX_NUM; i++){
			write(pipefd[1], &i, 4);
		}
		close(pipefd[1]);
		exit(0);
	}

	close(pipefd[1]);
	sieve(pipefd[0], pipefd[1]);
	close(pipefd[0]);

	wait(0);
	exit(0);
}



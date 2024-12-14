#include"kernel/types.h"
#include"kernel/stat.h"
#include"user/user.h"
//测试子进程继承父进程的fd，包括管道fd
int main(){
	int parent_to_child[2];
	int child_to_parent[2];
	if(pipe(parent_to_child) < 0 || pipe(child_to_parent) < 0){
		fprintf(2, "pipe failed\n");
		exit(1);
	}

	if(fork() == 0){
		close(parent_to_child[1]);
		close(child_to_parent[0]);

		int num;
		while(read(parent_to_child[0], &num, 4) > 0){
			fprintf(1, "Child received: %d\n");
			num *= 2;
			write(child_to_parent[1], &num, 4);
		}

		close(parent_to_child[0]);
		close(child_to_parent[1]);
		exit(0);

	}else{
		close(parent_to_child[0]);
		close(child_to_parent[1]);
		
		for(int i = 1; i< 5; i++){
			fprintf(1, "Parent sending: %d\n", i);
			write(parent_to_child[1], &i, 4);
			int response;
			read(child_to_parent[0], &response, 4);
			fprintf(1, "Parent Received: %d\n", response);
		}
		close(parent_to_child[1]);
		close(child_to_parent[0]);
		wait(0);
		exit(0);
	}
}


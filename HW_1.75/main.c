#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#define MEMORY_KEY 42
#define SEMAPHORES_KEY 44
#define SIZE_OF_BUF 10
#define NUM_OF_SEMS 7


enum Semaphore{

    mutex_prod = 0,
    full = 1,
    empty = 2,
    prod_id = 3,
    cons_id = 4,
    mutex_cons = 5,
    has_been_inited = 6
};

struct Buf_data{

    int data_size;
    char data[SIZE_OF_BUF];
};

void init_op(unsigned short num, short op, short flag, struct sembuf* sem_op){

    sem_op->sem_num = num;
    sem_op->sem_op =  op;
    sem_op->sem_flg = flag;
}

int create_semaphores(){

    int sem_id = semget(SEMAPHORES_KEY, NUM_OF_SEMS, IPC_CREAT | 0777);

    struct sembuf sem_op[4];
    init_op(has_been_inited, 0, IPC_NOWAIT, &sem_op[0]);
    init_op(has_been_inited, 1, 0, &sem_op[1]);
    init_op(mutex_prod, 1, 0, &sem_op[2]);
    init_op(mutex_cons, 1, 0, &sem_op[3]);

    semop(sem_id, sem_op, 4);

    return sem_id;
}

void* create_shared_m(){

    int shm_id = shmget(MEMORY_KEY, sizeof(struct Buf_data), IPC_CREAT | 0777);

    if (shm_id == -1){

        printf("Can't create shared memory error[%i]\n", errno);
        exit(0);
    }

    void* shm_ptr = shmat(shm_id, 0, 0);

    if (shm_ptr == NULL){

        printf("Can't access shared memory in read error[%i]\n", errno);
        exit(0);
    }

    return shm_ptr;
}

void producer(int fd){

    struct Buf_data data_from_file;
    int my_consumer_id = 0;
    int sem_id = create_semaphores();
    void* shm_ptr = create_shared_m();
    struct sembuf sem_op[4];

    init_op(mutex_prod, -1, SEM_UNDO, &sem_op[0]);
    semop(sem_id, sem_op, 1);
//-----------
    init_op(prod_id, getpid() % 10000, SEM_UNDO, &sem_op[0]);    
    semop(sem_id, sem_op, 1);

    init_op(cons_id, -1, 0, &sem_op[0]);
    init_op(cons_id, 1, 0, &sem_op[0]);
    semop(sem_id, sem_op, 2);
    
    do{
        data_from_file.data_size = read(fd, data_from_file.data, SIZE_OF_BUF);
      
        init_op(cons_id, -my_consumer_id, IPC_NOWAIT, &sem_op[0]); // добавить проверку на 0
        init_op(empty, -1, 0, &sem_op[1]);  
        init_op(cons_id, 0, IPC_NOWAIT, &sem_op[2]);
        init_op(cons_id, my_consumer_id, 0, &sem_op[3]);

        if (semop(sem_id, sem_op, 4) == -1){

            printf("Other consumer now\n");
            exit(0);
        }

        memcpy(shm_ptr, &data_from_file, sizeof(struct Buf_data));
        
        init_op(cons_id, -my_consumer_id, IPC_NOWAIT, &sem_op[0]);
        init_op(full, 1, SEM_UNDO, &sem_op[1]);  
        init_op(cons_id, 0, IPC_NOWAIT, &sem_op[2]);
        init_op(cons_id, my_consumer_id, 0, &sem_op[3]);  

        if (semop(sem_id, sem_op, 4) == -1){

            printf("Other consumer now\n");
            exit(0);
        }

    } while (data_from_file.data_size != 0);

    while (my_consumer_id == semctl(sem_id, cons_id, GETVAL, NULL)){}

    init_op(cons_id, -my_consumer_id, IPC_NOWAIT, &sem_op[0]); //тут херня
    init_op(cons_id, 0, 0, &sem_op[1]);
    init_op(cons_id, my_consumer_id, 0, &sem_op[2]);

    if (semop(sem_id, sem_op, 3) == -1){

        printf("Other consumer now\n");
        exit(0);
    }


    init_op(prod_id, -(getpid() % 10000), SEM_UNDO, &sem_op[0]);    
    semop(sem_id, sem_op, 1);
//-----------   
    init_op(mutex_prod, 1, SEM_UNDO, &sem_op[0]);
    semop(sem_id, sem_op, 1);
}


void consumer(){

    struct Buf_data data_from_shm;
    data_from_shm.data_size = -1;
    int my_prod_id = 0, op_ret = 0;
    int sem_id = create_semaphores();
    void* shm_ptr = create_shared_m();
    struct sembuf sem_op[4];

    init_op(mutex_cons, -1, SEM_UNDO, &sem_op[0]); //тут встают процессы кроме одного
    semop(sem_id, sem_op, 1);
//-----------  
    init_op(cons_id, getpid() % 10000, SEM_UNDO, &sem_op[0]);    
    semop(sem_id, sem_op, 1);
    
    if (semctl(sem_id, empty, SETVAL, 1) == -1){

        printf("Can't set empty sem\n");
        exit(0);
    }

    init_op(prod_id, -1, 0, &sem_op[0]);
    init_op(prod_id, 1, 0, &sem_op[0]);
    semop(sem_id, sem_op, 2);

    while (data_from_shm.data_size != 0){
        
        init_op(prod_id, -my_prod_id, IPC_NOWAIT, &sem_op[0]);
        init_op(full, -1, 0, &sem_op[1]); 
        init_op(prod_id, 0, IPC_NOWAIT, &sem_op[2]);
        init_op(prod_id, my_prod_id, 0, &sem_op[3]); 

        if (semop(sem_id, sem_op, 4) == -1){

            printf("Other producer now\n");
            exit(0);
        }
        
        memcpy(&data_from_shm, shm_ptr, sizeof(struct Buf_data));

        write(STDOUT_FILENO, data_from_shm.data, data_from_shm.data_size);
         
        init_op(prod_id, -my_prod_id, IPC_NOWAIT, &sem_op[0]);
        init_op(empty, 1, SEM_UNDO, &sem_op[1]);  
        init_op(prod_id, 0, IPC_NOWAIT, &sem_op[2]);
        init_op(prod_id, my_prod_id, 0, &sem_op[3]);  

        if (semop(sem_id, sem_op, 4) == -1){

            printf("Other producer now\n");
            exit(0);
        }
    }
    init_op(cons_id, -(getpid() % 10000), SEM_UNDO, &sem_op[0]);    
    semop(sem_id, sem_op, 1);
//-----------  
    init_op(mutex_cons, 1, SEM_UNDO, &sem_op[0]); 
    semop(sem_id, sem_op, 1);
}

int open_file(char* file){

    int tmp = open(file, O_RDONLY);

    if (tmp == -1){

        printf("Can't open file\n");
        exit(0);
    }

    return tmp;
}
                                 //ipcs
int main(int argc, char* argv[]){//ipcrm sem <IPC идентификатор>

    if (argc == 1){

        consumer();
    } else{

        producer(open_file(argv[1]));
    }
}
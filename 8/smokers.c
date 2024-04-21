#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <fcntl.h>


#define SHM_NAME "/shm_table"

struct sembuf sem_release = {0, -1, SEM_UNDO};
struct sembuf sem_stop = {0, 1, SEM_UNDO};

// cтруктура отвечающая за стол общий стол, который делят курильщики.
// smoke_item_id принимает значения 0, 1, 2 в зависимости от того, какой компонент для какого курильщика лежит на столе
// если на столе нет никакого компонента, то он принимает значение -1.
typedef struct {
    int smoke_item_id;
} shared_table;

shared_table *table;
int table_sem;


// функция что удаления старой shared памяти и ресурсов семафора
// необходима для работы на MacOS, без нее семафора не может создаться
// также позволяет корректно обрабатывать прерывания программы
void deleteSemAndShm(int signal_id) {
    munmap(table, sizeof(table));
    shm_unlink(SHM_NAME);

    semctl(table_sem, 0, IPC_RMID);
}


int main() {
    // нужно для корректной обработки сигналов выхода по типу ctrl + C
    signal(SIGINT, deleteSemAndShm);
    signal(SIGTERM, deleteSemAndShm);

    srand(time(NULL));

    table_sem = semget(1, 1, IPC_CREAT | 0666);
    if (table_sem == -1) {
        perror("Got error on creating sem");
        return 1;
    }

    if (semctl(table_sem, 0, SETVAL, 1) == -1) {
        perror("Got error on sem init");
        return 1;
    }

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Get error while trying to open shm");
        return 1;
    }

    table = mmap(NULL, sizeof(shared_table), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (table == MAP_FAILED) {
        perror("Got error while trying to mmap");
        return 1;
    }

    // создаю процессы курильщиков, каждый обозначется id компонента, который нужно выкурить
    for (int i = 0; i < 3; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("Got error while creating process");
            return 1;
        }
        else if (pid == 0) {
            while (1) {
                int time_to_sleep = 0;

                // если на столе лежит компонент, который нужен курильщику - началь курить
                if (table->smoke_item_id == i) {
                    semop(table_sem, &sem_stop, 1);
                    printf("%d smoker start smoking\n", i);
                    time_to_sleep = 2 + (rand() % 4);
                    table->smoke_item_id = -1;
                    semop(table_sem, &sem_release, 1);
                }

                if (time_to_sleep > 0) {
                    printf("Smoker %d went to sleep for %d seconds\n", i, time_to_sleep);
                    sleep(time_to_sleep);
                    time_to_sleep = 0;
                }
            }
            exit(0);
        }
    }

    deleteSemAndShm(0);
    return 0;
}
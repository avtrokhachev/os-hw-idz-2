#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <fcntl.h>


#define SEM_NAME "/sem_table"
#define SHM_NAME "/shm_table"


// cтруктура отвечающая за стол общий стол, который делят курильщики.
// smoke_item_id принимает значения 0, 1, 2 в зависимости от того, какой компонент для какого курильщика лежит на столе
// если на столе нет никакого компонента, то он принимает значение -1.
typedef struct {
    int smoke_item_id;
} shared_table;

shared_table *table;
sem_t *table_sem;


// функция что удаления старой shared памяти и ресурсов семафора
// необходима для работы на MacOS, без нее семафора не может создаться
// также позволяет корректно обрабатывать прерывания программы
void deleteSemAndShm(int signal_id) {
    munmap(table, sizeof(table));
    shm_unlink(SHM_NAME);

    sem_close(table_sem);
    sem_unlink(SEM_NAME);
}


// отвечает за поставку компонентов для сигарет на стол.
// кладет случайный компонент на стол для курильщика 0, 1 или 2
// затем уходит в сон
// если компонент не забрали, ждет пока его заберут и кладет новый
void* worker(void *data) {
    shared_table *table_worker;

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Got error while trying to open shm");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(shared_table)) == -1) {
        perror("Got error while trying to ftruncate");
        return 1;
    }

    table_worker = mmap(NULL, sizeof(shared_table), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (table_worker == MAP_FAILED) {
        perror("Got error while trying to mmap table_worker");
        return 1;
    }

    table_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (table_sem == SEM_FAILED) {
        perror("Got error while trying to open sem");
        return 1;
    }

    // изначально на столе нет никакого предмета
    table_worker->smoke_item_id = -1;

    while (1) {
        int time_to_sleep = 0;

        sem_wait(table_sem);
        // если стол пустой - на него нужно положить предмет
        if (table_worker->smoke_item_id == -1) {
            int smoke_item = rand() % 3;
            table_worker->smoke_item_id = smoke_item;
            printf("Worker put smoke_item id=%d on the table\n", smoke_item);
            time_to_sleep = (rand() % 4) + 2;
        }
        sem_post(table_sem);

        if (time_to_sleep > 0) {
            printf("Worker went to sleep for %d seconds\n", time_to_sleep);
            sleep(time_to_sleep);
            time_to_sleep = 0;
        }
    }
}


int main() {
    // нужно для корректной обработки сигналов выхода по типу ctrl + C
    signal(SIGINT, deleteSemAndShm);
    signal(SIGTERM, deleteSemAndShm);

    srand(time(NULL));

    // создаю работника, который кладет компоненты для сигарет на стол
    pthread_t worker_thread;
    pthread_create(&worker_thread, NULL, worker, NULL);

    table_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (table_sem == SEM_FAILED) {
        perror("Get error while trying to open sem");
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
                    sem_wait(table_sem);
                    printf("%d smoker start smoking\n", i);
                    time_to_sleep = 2 + (rand() % 4);
                    table->smoke_item_id = -1;
                    sem_post(table_sem);
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

    pthread_join(worker_thread, NULL);

    deleteSemAndShm(0);
    return 0;
}
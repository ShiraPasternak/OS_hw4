//
// Created by shira on 01/01/2023.
//

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <limits.h> //for PATH_MAX
#include <threads.h> //for threads
#include <unistd.h>

mtx_t queue_mutex;

// Queue struct taken from https://gist.github.com/ArnonEilat/4471278
// changed needed methods to be threat safe

typedef struct Node_t {
    char* dirName;
    struct Node_t *prev;
} Node;

/* the HEAD of the Queue, hold the amount of node's that are in the queue*/
typedef struct Queue {
    Node *head;
    Node *tail;
    int size;
    int limit;
} Queue;

Queue *constructQueue(/*int limit*/);
void destructQueue(Queue *queue);
bool enqueue(Queue *pQueue, Node *item);
Node *dequeue(Queue *pQueue);
bool isEmpty(Queue* pQueue);

Queue *constructQueue(/*int limit*/) {
    Queue *queue = (Queue*) malloc(sizeof (Queue));
    if (queue == NULL) {
        return NULL;
    }
    /*if (limit <= 0) {
        limit = 65535;
    } */
    queue->limit = 65535;
    queue->size = 0;
    queue->head = NULL;
    queue->tail = NULL;

    return queue;
}

void destructQueue(Queue *queue) {
    Node *pN;
    while (!isEmpty(queue)) {
        pN = dequeue(queue);
        free(pN-> dirName);
        free(pN);
    }
    free(queue);
}

bool enqueue(Queue *pQueue, Node *item) {
    mtx_lock(&queue_mutex);
    if ((pQueue == NULL) || (item == NULL)) {
        return false;
    }
    // if(pQueue->limit != 0)
    if (pQueue->size >= pQueue->limit) {
        return false;
    }
    /*the queue is empty*/
    item->prev = NULL;
    if (pQueue->size == 0) {
        pQueue->head = item;
        pQueue->tail = item;

    } else {
        /*adding item to the end of the queue*/
        pQueue->tail->prev = item;
        pQueue->tail = item;
    }
    pQueue->size++;
    mtx_unlock(&queue_mutex);
    return true;
}

Node * dequeue(Queue *pQueue) {
    /*the queue is empty or bad param*/
    Node *item;
    mtx_lock(&queue_mutex);
    if (isEmpty(pQueue))
        return NULL;
    item = pQueue->head;
    pQueue->head = (pQueue->head)->prev;
    pQueue->size--;
    mtx_unlock(&queue_mutex);
    return item;
}

bool isEmpty(Queue* pQueue) {
    if (pQueue == NULL) {
        return false;
    }
    if (pQueue->size == 0) {
        return true;
    } else {
        return false;
    }
}

bool rootDirIsSearchable(char *dir);
void handleDirIsNotSearchable(char *dir);
void createThreads(int n);
int handleSearchByThreads(void *t);

atomic_int thread_failed = 0, success_counter = 0;
atomic_int t_sleeping = 0;
int numOfThreads;
thrd_t thread_ids[numOfThreads];
Queue *searchQueue;
cnd_t start_cv;
mtx_t start_mutex;

void handleDirIsNotSearchable(char *dir) {
    printf("Directory %s: Permission denied.\n", dir);
}

bool rootDirIsSearchable(char *dir) {
    if (access(dir, R_OK && X_OK) == 0) {
        return true;
    }
    return false;
}

void createThreads(int n) {
    for (size_t i = 0; i < n; i++) {
        int rc = thrd_create(&thread_ids[i], handleSearchByThreads, (void *)i);
        if (rc != thrd_success) {
            fprintf(stderr, "Failed creating thread\n");
        }
    }
}

int handleSearchByThreads(void *t) {
    long my_id = (long)t;
    Node *dirNode;

    printf("handleSearchByThreads(): thread %ld\n", my_id);
    mtx_lock(&queue_mutex);
    if (cnd_wait(&start_cv, &start_mutex)) {
        // handle error todo
    }
    mtx_unlock(&queue_mutex);

    printf("handleSearchByThreads(): thread %ld Condition signal received.\n", my_id);

    if (isEmpty(searchQueue)) {
        t_sleeping++;
        cnd_wait();
    }
    else {
        if (t_sleeping!=0)
            t_sleeping--;
        dirNode = dequeue(searchQueue);
        handleDir(dirNode);
        if (t_sleeping = )
    }

}

int main(int argc, char **argv) {
    char *rootDir, *searchTerm;
    Node *dirNode;

    if (argc != 4) {
        fprintf(stderr,"%s","incorrect number of inputs\n");
        exit(1);
    } else if(!rootDirIsSearchable(argv[1])){
        handleDirIsNotSearchable(argv[1]);
        exit(1);
    } else {
        rootDir = argv[1];
        searchTerm = argv[2];
        sscanf(argv[3], "%d", &numOfThreads);
    }
    // init search queue
    searchQueue = constructQueue();
    dirNode = (Node*)malloc(sizeof(Node));
    strcpy(dirNode ->dirName, rootDir);
    if (!enqueue(searchQueue, dirNode)){
        //handle error in queue todo
    }
    cnd_init(&start_cv);
    createThreads(numOfThreads);
    cndForThreads = createCnds(numOfThreads);

    // --- Wait for threads to finish --- not sure if needed
    for (long t = 0; t < NUM_THREADS; ++t) {
        rc = thrd_join(thread[t], &status);
        if (rc != thrd_success) {
            printf("ERROR in thrd_join()\n");
            exit(-1);
        }
        printf("Main: completed join with thread %ld "
               "having a status of %d\n",
               t, status);
    }

    if (cnd_broadcast(start_cv) == thrd_error) {
        // handle error todo
    }

    printf("Done searching, found %d files\n", successfulSearchesCounter);
    destructQueue(searchQueue);
    if(thread_failed)
        exit(1);
    else
        exit(0);
}


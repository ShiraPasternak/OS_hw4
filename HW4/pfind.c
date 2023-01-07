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
#include <dirent.h>
#include <sys/stat.h>

#define SELF "."
#define PARENT ".."

// Queue struct taken from https://gist.github.com/ArnonEilat/4471278
// changed needed methods to be threat safe

// task Queue struct
typedef struct Node_task { //add path of dir todo
    char* dirName;
    cnd_t threadCV;
    struct Node_task *prev;
} NodeTask;

/* the HEAD of the Queue, hold the amount of node's that are in the queue*/
typedef struct Queue_task {
    NodeTask *head;
    NodeTask *tail;
    int size;
    int limit;
} TaskQueue;

// threads Queue struct
typedef struct Node_thread {
    long threadId;
    cnd_t threadCV;
    struct Node_thread *prev;
} NodeThread;

/* the HEAD of the Queue, hold the amount of node's that are in the queue*/
typedef struct Queue_thread {
    NodeThread *head;
    NodeThread *tail;
    int size;
    int limit;
} ThreadQueue;

mtx_t task_queue_mutex;
mtx_t threads_queue_mutex;

atomic_int thread_failed = 0, success_counter = 0;
atomic_int t_sleeping = 0;
int numOfThreads;
thrd_t thread_ids[numOfThreads];
cnd_t cnd_for_search_threads[numOfThreads];
TaskQueue *searchQueue;
ThreadQueue *threadQueue;
cnd_t start_cv;
cnd_t created_all_threads;
mtx_t start_mutex;
char *searchTerm;

TaskQueue *constructTaskQueue(/*int limit*/);
void destructTaskQueue(TaskQueue *taskQueue);
bool enqueue(TaskQueue *pTaskQueue, NodeTask *item);
NodeTask *dequeue(TaskQueue *pTaskQueue);
bool isEmpty(TaskQueue* pTaskQueue);

ThreadQueue *constructThreadQueue(int limit);
void destructThreadQueue(ThreadQueue *threadQueue);
bool enqueueThread(ThreadQueue *pThreadQueue, NodeThread *item);
NodeThread *dequeueThread(ThreadQueue *pThreadQueue);
bool isThreadQueueEmpty(ThreadQueue* pThreadQueue);

bool rootDirIsSearchable(char *dir, NodeTask *pTask);
void createThreads(int n);
void createConditionVariablesForEachThread(int n);
int handleSearchByThreads(void *t);
cnd_t getCDbyThreadId(long tId);
void assignCVToTask(TaskQueue *tq);
void wakeUpThreadByFifoOrder();
void addRelevantSubDirsToQueue(char *dirName, NodeTask *pTask);
NodeTask *dequeueTaskByCv(cnd_t cv, TaskQueue *tq);
bool rootEqualsDefaultDirs(char *dirName);
bool isDirectory(char *name);
const char *getFilePath(char *name, NodeTask *pTask); //todo
bool isFile(char *name);
bool stopSearching();

// task Queue methods
TaskQueue *constructTaskQueue(/*int limit*/) {
    TaskQueue *taskQueue = (TaskQueue*) malloc(sizeof (TaskQueue));
    if (taskQueue == NULL) {
        return NULL;
    }
    /*if (limit <= 0) {
        limit = 65535;
    } */
    taskQueue->limit = 65535;
    taskQueue->size = 0;
    taskQueue->head = NULL;
    taskQueue->tail = NULL;

    return taskQueue;
}

void destructTaskQueue(TaskQueue *taskQueue) {
    NodeTask *pN;
    while (!isEmpty(taskQueue)) {
        pN = dequeue(taskQueue);
        free(pN-> dirName);
        free(pN);
    }
    free(taskQueue);
}

bool enqueue(TaskQueue *pTaskQueue, NodeTask *item) {
    mtx_lock(&task_queue_mutex);
    if ((pTaskQueue == NULL) || (item == NULL)) {
        return false;
    }
    // if(pQueue->limit != 0)
    if (pTaskQueue->size >= pTaskQueue->limit) {
        return false;
    }
    item->prev = NULL;
    /*the TaskQueue is empty*/
    if (pTaskQueue->size == 0) {
        pTaskQueue->head = item;
        pTaskQueue->tail = item;

    } else {
        /*adding item to the end of the queue*/
        pTaskQueue->tail->prev = item;
        pTaskQueue->tail = item;
    }
    pTaskQueue->size++;

    if (pTaskQueue->size < numOfThreads) {
        assignCVToTask(pTaskQueue);
        wakeUpThreadByFifoOrder();
    }
    mtx_unlock(&task_queue_mutex);
    return true;
}

NodeTask * dequeue(TaskQueue *pTaskQueue) {
    NodeTask *item;
    NodeThread *nodeThread;
    mtx_lock(&task_queue_mutex);
    cnd_t cv = getCDbyThreadId(thrd_current());
    nodeThread ->threadId = thrd_current();
    nodeThread ->threadCV = cv;
    if (cv == NULL) {
        fprintf(stderr, "%s%ld\n", "Failed to find cv for thread id", thrd_current());
        return NULL;
    }
    while (isEmpty(pTaskQueue)) {
        t_sleeping++;
        enqueueThread(threadQueue, nodeThread);
        cnd_wait(&cv, &task_queue_mutex);
        if (stopSearching()) {
            thrd_exit(0);
        }
    }
    cnd_t assigned_cv = pTaskQueue->head->threadCV;
    if (assigned_cv == NULL/* || assigned_cv == cv*/){
        item = pTaskQueue->head;
        pTaskQueue->head = (pTaskQueue->head)->prev;
        pTaskQueue->size--;
    }
    else {
        item = dequeueTaskByCv(cv, pTaskQueue);
    }
    mtx_unlock(&task_queue_mutex);
    return item;
}

NodeTask *dequeueTaskByCv(cnd_t cv, TaskQueue *tq) {
    NodeTask *nodeTask, *nextNodeTask;
    nextNodeTask = tq->head;
    nodeTask = nextNodeTask->prev;
    while(nodeTask != NULL) {
        if (nodeTask->threadCV == cv) {
            nextNodeTask->prev = nodeTask->prev;
            if (nodeTask->prev == NULL)
                tq->tail = nextNodeTask;
            tq->size--;
        }
        nextNodeTask = nodeTask;
        nodeTask = nodeTask->prev;
    }
}

bool isEmpty(TaskQueue* pTaskQueue) {
    if (pTaskQueue == NULL) {
        return false;
    }
    if (pTaskQueue->size == 0) {
        return true;
    } else {
        return false;
    }
}

// threads Queue methods
ThreadQueue *constructThreadQueue(int limit) {
    ThreadQueue *threadQueue = (ThreadQueue*) malloc(sizeof (ThreadQueue));
    if (threadQueue == NULL) {
        return NULL;
    }
    if (limit <= 0) {
        limit = 65535;
    }
    threadQueue->limit = limit;
    threadQueue->size = 0;
    threadQueue->head = NULL;
    threadQueue->tail = NULL;

    return threadQueue;
}

void destructThreadQueue(ThreadQueue *ThreadQueue) {
    NodeTask *pN;
    while (!isEmpty(ThreadQueue)) {
        pN = dequeue(ThreadQueue);
        free(pN);
    }
    free(ThreadQueue);
}

bool enqueueThread(ThreadQueue *pThreadQueue, NodeThread *item) {
    mtx_lock(&threads_queue_mutex);
    if ((pThreadQueue == NULL) || (item == NULL)) {
        return false;
    }
    // if(pQueue->limit != 0)
    if (pThreadQueue->size >= pThreadQueue->limit) {
        return false;
    }
    /*the ThreadQueue is empty*/
    item->prev = NULL;
    if (pThreadQueue->size == 0) {
        pThreadQueue->head = item;
        pThreadQueue->tail = item;

    } else {
        /*adding item to the end of the queue*/
        pThreadQueue->tail->prev = item;
        pThreadQueue->tail = item;
    }
    pThreadQueue->size++;
    mtx_unlock(&threads_queue_mutex);
    return true;
}

NodeThread * dequeueThread(ThreadQueue *pThreadQueue) {
    NodeThread *item;
    mtx_lock(&threads_queue_mutex);
    item = pThreadQueue->head;
    pThreadQueue->head = (pThreadQueue->head)->prev;
    pThreadQueue->size--;
    mtx_unlock(&threads_queue_mutex);
    return item;
}

bool isThreadQueueEmpty(ThreadQueue* pThreadQueue) {
    if (pThreadQueue == NULL) {
        return false;
    }
    if (pThreadQueue->size == 0) {
        return true;
    } else {
        return false;
    }
}

bool rootDirIsSearchable(char *dir, NodeTask *pTask) {
    if (access(dir, R_OK | X_OK) == 0) {
        return true;
    }
    printf("Directory %s: Permission denied.\n", getFilePath(dir, pTask));
    return false;
}

void createThreads(int n) {
    for (size_t i = 0; i < n; i++) {
        int rc = thrd_create(&thread_ids[i], handleSearchByThreads, (void *)i);
        if (rc != thrd_success) {
            fprintf(stderr,"%s\n", "Failed creating thread");
        }
    }
}

void createConditionVariablesForEachThread(int n) {
    for (size_t i = 0; i < n; i++) {
        int rc = cnd_init(&cnd_for_search_threads[i]);
        if (rc != thrd_success) {
            fprintf(stderr, "%s\n", "Failed creating cv for thread");
        }
    }
}

int handleSearchByThreads(void *t) {
    long thread_id = (long)t;
    NodeTask *dirNode;

    printf("handleSearchByThreads(): thread %ld\n", thread_id);
    mtx_lock(&start_mutex);
    int t_created = 0;
    t_created++;
    if (t_created == numOfThreads) {
        cnd_broadcast(&created_all_threads);
    }
    cnd_wait(&start_cv, &start_mutex)) // handle error todo
    mtx_unlock(&start_mutex);

    printf("handleSearchByThreads(): thread %ld Condition signal received.\n", my_id);

    while(1) { //consider if its the best solution todo
        dirNode = dequeue(searchQueue);
        addRelevantSubDirsToQueue(dirNode->dirName, dirNode);
        if (stopSearching()) {

        }
    }

}

bool stopSearching() {
    return t_sleeping == numOfThreads-1 && isEmpty(searchQueue);
}

void addRelevantSubDirsToQueue(char *dirName, NodeTask *pTask) { // iterating throw directories from https://www.ibm.com/docs/en/zos/2.4.0?topic=functions-readdir-read-entry-from-directory
    DIR *dir;
    struct dirent *entry;
    char *subDirName;

    if ((dir = opendir(dirName)) == NULL)
        fprintf(stderr, "%s %d\n", "opendir() error in thread", thrd_current());
    else {
        while ((entry = readdir(dir)) != NULL) {
            //printf("  %s\n", entry->d_name);
            subDirName = entry->d_name;
            if (rootEqualsDefaultDirs(subDirName))
                continue;
            if (isFile(dirName) && strstr(subDirName, searchTerm)) {
                success_counter++;
                printf(getFilePath(subDirName, pTask));
                break;
            }
            else if (isDirectory(subDirName) && rootDirIsSearchable(subDirName, pTask)) {
                NodeTask *subDirNode = (NodeTask *) malloc(sizeof(NodeTask));
                strcpy(subDirNode->dirName, subDirName);
                if (!enqueue(searchQueue, subDirNode))
                    fprintf(stderr, "%s\n", "Failed to add root directory to queue");
            }
        }
        closedir(dir);
    }
}

bool isFile(char *name) {
    struct stat *st;
    if (stat(name, st) == -2) {
        fprintf(stderr, "%s\n", "A component of pathname does not exist or is a dangling symbolic link");
    }
    else if (stat(name, st) < 0) {
        fprintf(stderr, "%s %s\n", "Failed to to get stat on directory:", name);
    }
    else if (S_ISREG(st->st_mode)) {
        return true;
    }
    return false;
}

const char *getFilePath(char *name, NodeTask *pTask) {
    if (pTask == NULL) {
        return name;
    }
    else {
        //todo
    }
    return NULL;
}

bool isDirectory(char *name) {
    struct stat *st;
    if (stat(name, st) == -2) {
        fprintf(stderr, "%s\n", "A component of pathname does not exist or is a dangling symbolic link");
    }
    else if (stat(name, st) < 0) {
        fprintf(stderr, "%s %s\n", "Failed to to get stat on directory:", name);
    }
    else if (S_ISLNK(st->st_mode) || S_ISDIR(st->st_mode)) {
        return true;
    }
    return false;
}

bool rootEqualsDefaultDirs(char *dirName) {
        return strcmp(dirName, SELF) || strcmp(dirName, PARENT);
}

cnd_t getCDbyThreadId(long tId) {
    for (int i = 0; i < numOfThreads; i++) {
        if (thread_ids[i] == tId) {
            return cnd_for_search_threads[i];
        }
    }
    return NULL;
}

void assignCVToTask(TaskQueue *tq) {
    NodeTask *nodeTask = tq->head;
    NodeThread *nodeThread = threadQueue->head;
    while (nodeTask != NULL && nodeThread != NULL) {
        if (nodeTask->threadCV == NULL) {
            nodeTask->threadCV = nodeThread->threadCV;
            nodeThread = nodeThread->prev;
        }
        nodeTask = nodeTask->prev;
    }
}

void wakeUpThreadByFifoOrder() {
    NodeThread *nodeThread;
    for (int i = 0; i < threadQueue->size; ++i) {
        nodeThread = dequeueThread(threadQueue);
        cnd_signal(&nodeThread->threadCV);
    }
}

int main(int argc, char **argv) {
    char *rootDir;
    NodeTask *dirNode;

    if (argc != 4) {
        fprintf(stderr, "%s\n", "incorrect number of inputs");
        exit(1);
    } else if (!rootDirIsSearchable(argv[1], NULL)) {
        exit(1);
    } else {
        rootDir = argv[1];
        searchTerm = argv[2];
        sscanf(argv[3], "%d", &numOfThreads);
    }
    // init queues
    searchQueue = constructQueue();
    threadQueue = constructThreadQueue(numOfThreads);

    //init locks and cv
    cnd_init(&start_cv);
    cnd_init(&created_all_threads);
    createConditionVariablesForEachThread(numOfThreads);
    mtx_init(start_mutex);
    mtx_init(task_queue_mutex);
    mtx_init(threads_queue_mutex);

    // init search process
    dirNode = (NodeTask *) malloc(sizeof(NodeTask));
    strcpy(dirNode->dirName, rootDir);
    if (!enqueue(searchQueue, dirNode)) { // adding root directory to queue
        fprintf(stderr, "%s\n", "Failed to add root directory to queue");
        exit(1);
        //handle error in queue todo
    }

    mtx_lock(&start_mutex);
    createThreads(numOfThreads);
    cnd_wait(&created_all_threads, &start_mutex);
    cnd_broadcast(&start_cv);
    mtx_unlock(&start_mutex);


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

    printf("Done searching, found %d files\n", successfulSearchesCounter);
    //destroy all
    destructQueue(searchQueue);
    destructQueue(threadQueue);
    cnd_destroy(&start_cv);
    cnd_destroy(&created_all_threads);
    mtx_destroy(&start_mutex);
    mtx_destroy(&task_queue_mutex);
    mtx_destroy(&threads_queue_mutex);
    if (thread_failed)
        exit(1);
    else
        exit(0);
}


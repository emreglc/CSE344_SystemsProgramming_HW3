#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <limits.h>     // for INT_MIN

#define TIMEOUT 5
#define WORK_TIME 9
#define NUM_SATELLITES 5
#define NUM_ENGINEERS 3

// --- Priority queue data structures ---
typedef struct Satellite {
    int             id;
    int             priority;
    sem_t* reply;    // per‑satellite semaphore (NULL for shutdown)
    struct Satellite* next;
} Satellite;

typedef struct {
    Satellite* head;
    pthread_mutex_t lock;
} PriorityQueue;

PriorityQueue queue = { NULL, PTHREAD_MUTEX_INITIALIZER };

// --- Synchronization primitives ---
sem_t            newRequest;
pthread_mutex_t  requestMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t  engineerMutex = PTHREAD_MUTEX_INITIALIZER;

// Remove a request by its ID from the queue (call with queue.lock held)
void remove_request_by_id(int id) {
    Satellite* cur = queue.head;
    Satellite* prev = NULL;
    while (cur) {
        if (cur->id == id) {
            if (prev) prev->next = cur->next;
            else      queue.head = cur->next;
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

// Enqueue (normal) in descending priority order
void enqueue_with_sem(int id, int priority, sem_t* reply) {
    pthread_mutex_lock(&queue.lock);
    Satellite* s = malloc(sizeof * s);
    s->id = id;
    s->priority = priority;
    s->reply = reply;
    s->next = NULL;
    if (!queue.head || queue.head->priority < priority) {
        s->next = queue.head;
        queue.head = s;
    }
    else {
        Satellite* cur = queue.head;
        while (cur->next && cur->next->priority >= priority)
            cur = cur->next;
        s->next = cur->next;
        cur->next = s;
    }
    pthread_mutex_unlock(&queue.lock);
}

// Dequeue highest‑priority request
Satellite* dequeue() {
    pthread_mutex_lock(&queue.lock);
    Satellite* s = queue.head;
    if (s) queue.head = s->next;
    pthread_mutex_unlock(&queue.lock);
    return s;
}

// --- Satellite thread ---
void* satellite(void* arg) {
    int* params = arg;
    int  id = params[0],
        priority = params[1];
    free(params);

    // 1) per‑satellite semaphore
    sem_t satSem;
    sem_init(&satSem, 0, 0);

    printf("[SATELLITE] Satellite %d is waiting for an engineer (priority %d)\n",
        id, priority);

    // 2) enqueue + signal
    pthread_mutex_lock(&requestMutex);
    pthread_mutex_lock(&engineerMutex);
    enqueue_with_sem(id, priority, &satSem);
    pthread_mutex_unlock(&engineerMutex);
    sem_post(&newRequest);
    pthread_mutex_unlock(&requestMutex);

    // 3) wait (with timeout)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += TIMEOUT;

    if (sem_timedwait(&satSem, &ts) == -1) {
        if (errno == ETIMEDOUT) {
            printf("[TIMEOUT] Satellite %d timed out (no engineer in %d seconds)\n",
                id, TIMEOUT);
            // remove stale request
            pthread_mutex_lock(&queue.lock);
            remove_request_by_id(id);
            pthread_mutex_unlock(&queue.lock);
        }
        else {
            perror("sem_timedwait");
        }
    }

    sem_destroy(&satSem);
    return NULL;
}

// --- Engineer thread ---
void* engineer(void* arg) {
    long eid = (long)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (1) {
        sem_wait(&newRequest);

        pthread_mutex_lock(&engineerMutex);
        Satellite* sat = dequeue();
        pthread_mutex_unlock(&engineerMutex);

        if (!sat) continue;

        // shutdown pill?
        if (sat->id < 0) {
            free(sat);
            break;
        }

        printf("[ENGINEER %ld] Handling satellite %d (priority: %d)\n",
            eid, sat->id, sat->priority);

        // signal *that* satellite
        sem_post(sat->reply);

        // simulate work
        sleep(WORK_TIME);

        printf("[ENGINEER %ld] Finished Satellite %d\n", eid, sat->id);
        free(sat);
    }

    sleep(1);
    printf("[ENGINEER %ld] Shutting down...\n", eid);

    return NULL;
}

int main(void) {
    srand(time(NULL));
    sem_init(&newRequest, 0, 0);

    pthread_t sats[NUM_SATELLITES];
    pthread_t engs[NUM_ENGINEERS];

    for (long i = 0; i < NUM_ENGINEERS; i++) {
        pthread_create(&engs[i], NULL, engineer, (void*)i);
    }

    for (int i = 0; i < NUM_SATELLITES; i++) {
        int* args = malloc(2 * sizeof(int));
        args[0] = i;
        args[1] = rand() % 5 + 1;
        pthread_create(&sats[i], NULL, satellite, args);
        sleep(1);
    }

    for (int i = 0; i < NUM_SATELLITES; i++) {
        pthread_join(sats[i], NULL);
    }

    for (int i = 0; i < NUM_ENGINEERS; i++) {
        pthread_mutex_lock(&engineerMutex);
        enqueue_with_sem(-1, INT_MIN, NULL);
        pthread_mutex_unlock(&engineerMutex);
        sem_post(&newRequest);
    }

    for (int i = 0; i < NUM_ENGINEERS; i++) {
        pthread_join(engs[i], NULL);
    }

    // cleanup
    sem_destroy(&newRequest);
    pthread_mutex_destroy(&engineerMutex);
    pthread_mutex_destroy(&requestMutex);
    pthread_mutex_destroy(&queue.lock);

    return 0;
}

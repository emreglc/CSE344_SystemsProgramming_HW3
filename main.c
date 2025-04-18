#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define TIMEOUT 5
#define NUM_SATELLITES 5
#define NUM_ENGINEERS 3

// --- Priority queue data structures ---
typedef struct Satellite {
    int id;
    int priority;
    struct Satellite* next;
} Satellite;

typedef struct {
    Satellite* head;
    pthread_mutex_t lock;
} PriorityQueue;

PriorityQueue queue = { NULL, PTHREAD_MUTEX_INITIALIZER };

// --- Synchronization primitives ---
sem_t newRequest;
sem_t requestHandled;
pthread_mutex_t requestMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t engineerMutex = PTHREAD_MUTEX_INITIALIZER;

int availableEngineers = NUM_ENGINEERS;

// Enqueue in descending priority order
void enqueue(int id, int priority) {
    pthread_mutex_lock(&queue.lock);
    Satellite* s = malloc(sizeof(Satellite));
    s->id = id;
    s->priority = priority;
    s->next = NULL;

    if (!queue.head || queue.head->priority < priority) {
        s->next = queue.head;
        queue.head = s;
    } else {
        Satellite* cur = queue.head;
        while (cur->next && cur->next->priority >= priority)
            cur = cur->next;
        s->next = cur->next;
        cur->next = s;
    }
    pthread_mutex_unlock(&queue.lock);
}

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
    int id = params[0], priority = params[1];
    free(params);

    printf("[SATELLITE] Satellite %d is waiting for an engineer (priority %d)\n", id, priority);

    // Only one satellite at a time
    pthread_mutex_lock(&requestMutex);

    // Enqueue request
    pthread_mutex_lock(&engineerMutex);
    enqueue(id, priority);
    pthread_mutex_unlock(&engineerMutex);

    // Notify engineers
    sem_post(&newRequest);

    // Wait for an engineer (with timeout)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += TIMEOUT;

    if (sem_timedwait(&requestHandled, &ts) == -1) {
        if (errno == ETIMEDOUT) {
            printf("[TIMEOUT] Satellite %d timed out (no engineer in %d seconds)\n", id, TIMEOUT);
        } else {
            perror("sem_timedwait");
        }
    }

    pthread_mutex_unlock(&requestMutex);
    return NULL;
}

// --- Engineer thread ---
void* engineer(void* arg) {
    long eid = (long)arg;
    // Allow cancellation at sem_wait
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (1) {
        // Wait for a new request
        sem_wait(&newRequest);

        // Pick highest-priority satellite
        pthread_mutex_lock(&engineerMutex);
        Satellite* sat = dequeue();
        if (!sat) {
            pthread_mutex_unlock(&engineerMutex);
            continue;
        }
        availableEngineers--;
        printf("[ENGINEER %ld] Handling satellite %d (priority: %d)\n",
               eid, sat->id, sat->priority);
        pthread_mutex_unlock(&engineerMutex);

        // Signal the satellite that we're handling it
        sem_post(&requestHandled);

        // Simulate update work
        sleep(rand() % 3 + 1);

        // Release engineer
        pthread_mutex_lock(&engineerMutex);
        availableEngineers++;
        printf("[ENGINEER %ld] Finished Satellite %d\n", eid, sat->id);
        pthread_mutex_unlock(&engineerMutex);

        free(sat);
    }
    return NULL;
}

int main(void) {
    srand(time(NULL));

    // Initialize semaphores
    sem_init(&newRequest, 0, 0);
    sem_init(&requestHandled, 0, 0);

    pthread_t sats[NUM_SATELLITES];
    pthread_t engs[NUM_ENGINEERS];

    // Launch engineer threads
    for (long i = 0; i < NUM_ENGINEERS; i++) {
        pthread_create(&engs[i], NULL, engineer, (void*)i);
    }

    // Launch satellite threads (with random priority)
    for (int i = 0; i < NUM_SATELLITES; i++) {
        int* args = malloc(2 * sizeof(int));
        args[0] = i;
        args[1] = rand() % 5 + 1;
        pthread_create(&sats[i], NULL, satellite, args);
        sleep(rand() % 2);  // simulate random arrivals
    }

    // Wait for all satellites to finish
    for (int i = 0; i < NUM_SATELLITES; i++) {
        pthread_join(sats[i], NULL);
    }

    // Cancel and join engineers (they may be blocked on sem_wait)
    for (int i = 0; i < NUM_ENGINEERS; i++) {
        pthread_cancel(engs[i]);
        pthread_join(engs[i], NULL);
    }

    // Clean up
    sem_destroy(&newRequest);
    sem_destroy(&requestHandled);
    pthread_mutex_destroy(&engineerMutex);
    pthread_mutex_destroy(&requestMutex);
    pthread_mutex_destroy(&queue.lock);

    return 0;
}
#include "shared.h"
#include <cstdlib>

void q_init(Queue* q) {
    q->head = nullptr;
    q->tail = nullptr;
    q->size = 0;
}

void q_push_fcfs(Queue* q, int id, std::uint64_t request_ts, std::uint64_t seq) {
    Node* n = (Node*)malloc(sizeof(Node));
    n->flight_id = id;
    n->request_ts = request_ts;
    n->seq = seq;
    n->next = nullptr;

    if (q->head == nullptr) {
        q->head = q->tail = n;
        q->size = 1;
        return;
    }

    // insert at head if earlier than current head
    if (request_ts < q->head->request_ts ||
        (request_ts == q->head->request_ts && seq < q->head->seq)) {
        n->next = q->head;
        q->head = n;
        q->size++;
        return;
        }

    Node* cur = q->head;
    while (cur->next != nullptr) {
        Node* nxt = cur->next;

        bool goes_before =
            (request_ts < nxt->request_ts) ||
            (request_ts == nxt->request_ts && seq < nxt->seq);

        if (goes_before) break;
        cur = cur->next;
    }

    n->next = cur->next;
    cur->next = n;

    if (n->next == nullptr) q->tail = n;
    q->size++;
}



bool q_pop(Queue* q, int* id) {
    if (q->head == nullptr)
        return false;

    Node* n = q->head;
    *id = n->flight_id;
    q->head = n->next;

    if (q->head == nullptr)
        q->tail = nullptr;

    free(n);
    q->size--;
    return true;
}

void* scheduler_thread(void* arg) {
    SharedState* s = (SharedState*)arg;

    while (true) {
        pthread_mutex_lock(&s->mutex);

        while (!s->shutdown && s->queue.size == 0) {
            pthread_cond_wait(&s->cv_scheduler, &s->mutex);
        }

        if (s->shutdown) {
            pthread_mutex_unlock(&s->mutex);
            break;
        }

        int fid;
        if (!q_pop(&s->queue, &fid)) {
            pthread_mutex_unlock(&s->mutex);
            continue;
        }

        s->flights[fid].granted = true;
        pthread_cond_signal(&s->cv_flight[fid]);

        pthread_mutex_unlock(&s->mutex);
    }

    return nullptr;
}

#include "shared.h"
#include <cstdio>
#include <cstdint>

void* flight_thread(void* arg) {
    FlightArg* a = (FlightArg*)arg;
    SharedState* s = a->state;
    int id = a->id;

    Flight* f = &s->flights[id];

    // 1) arrival
    sleep_ms(f->arrival_ms);

    // 2) request runway (enqueue) => t_requested
    pthread_mutex_lock(&s->mutex);

    f->granted = false;
    f->t_requested = now_ms();

    std::uint64_t req_ts = f->t_requested;
    std::uint64_t my_seq = s->next_seq++;

    q_push_fcfs(&s->queue, id, req_ts, my_seq);
    pthread_cond_signal(&s->cv_scheduler);

    // wait until scheduler grants THIS flight
    while (!f->granted) {
        pthread_cond_wait(&s->cv_flight[id], &s->mutex);
    }

    // 3) scheduler granted => t_granted
    f->t_granted = now_ms();

    pthread_mutex_unlock(&s->mutex);

    // 4) wait for runway (semaphore) => t_started
    sem_wait(s->runway_sem);
    // semaphore acquired => now we REALLY have a runway
    f->t_started = now_ms();

    pthread_mutex_lock(&s->mutex);
    s->active_on_runway++;
    if (s->active_on_runway > s->runways) {
        printf("BUG: active_on_runway=%d > runways=%d (flight %d)\n",
               s->active_on_runway, s->runways, f->id);
    }
    if (s->active_on_runway > s->max_active_seen) {
        s->max_active_seen = s->active_on_runway;
    }
    pthread_mutex_unlock(&s->mutex);


    // 5) simulate runway usage
    sleep_ms(f->service_ms);

    // 6) finished service => t_finished
    f->t_finished = now_ms();

    // leaving runway
    pthread_mutex_lock(&s->mutex);
    s->active_on_runway--;
    if (s->active_on_runway < 0) {
        printf("BUG: active_on_runway=%d < 0 (flight %d)\n",
               s->active_on_runway, f->id);
    }
    pthread_mutex_unlock(&s->mutex);

    sem_post(s->runway_sem);


    // 7) mark completion
    pthread_mutex_lock(&s->mutex);

    s->completed++;
    if (s->completed == s->num_flights) {
        s->shutdown = true;
        pthread_cond_signal(&s->cv_scheduler);
    }

    pthread_mutex_unlock(&s->mutex);

    // metrics
    std::uint64_t wait_time   = f->t_started  - f->t_requested;
    std::uint64_t sched_delay = f->t_granted  - f->t_requested;
    std::uint64_t runway_time = f->t_finished - f->t_started;

    std::printf("Flight %d metrics: wait=%llums sched=%llums runway=%llums\n",
                f->id,
                (unsigned long long)wait_time,
                (unsigned long long)sched_delay,
                (unsigned long long)runway_time);

    return nullptr;
}

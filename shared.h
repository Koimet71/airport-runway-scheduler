#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <semaphore.h>
#include <vector>
#include <cstdint>

enum class FlightType { LAND, TAKEOFF };
enum class FlightPriority { NORMAL, EMERGENCY };


struct Flight {
    int id{};
    FlightType type{FlightType::TAKEOFF};
    FlightPriority priority{FlightPriority::NORMAL};
    int arrival_ms{};
    double vruntime{0.0};
    int weight{1024}; // baseline weight

    int service_ms{};
    bool granted{false};
    std::uint64_t t_requested{0};
    std::uint64_t t_granted{0};
    std::uint64_t t_started{0};
    std::uint64_t t_finished{0};

};

// IMPORTANT: Node/Queue must appear BEFORE SharedState uses them
struct Node {
    int flight_id;
    std::uint64_t request_ts;
    std::uint64_t seq;
    Node* next;
};



struct Queue {
    Node* head{nullptr};
    Node* tail{nullptr};
    int size{0};
};

struct SharedState {
    int num_flights{0};
    int completed{0};
    int runways{0};
    int active_on_runway{0};
    int max_active_seen{0};
    std::uint64_t next_seq{0};


    pthread_mutex_t mutex{};
    pthread_cond_t cv_scheduler{};
    std::vector<pthread_cond_t> cv_flight;

    sem_t* runway_sem{nullptr};


    std::vector<Flight> flights;
    Queue queue;

    bool shutdown{false};
};

struct FlightArg {
    SharedState* state;
    int id;
};

void q_init(Queue* q);
void q_push_fcfs(Queue* q, int id, std::uint64_t request_ts, std::uint64_t seq);

bool q_pop(Queue* q, int* id);

void* scheduler_thread(void* arg);
void* flight_thread(void* arg);

void sleep_ms(int ms);
std::uint64_t now_ms();


#endif

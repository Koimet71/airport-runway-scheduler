#include <string>
#include <fstream>
#include <algorithm>
#include "shared.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <time.h>
#include <fcntl.h>   // O_CREAT, O_EXCL
#include <errno.h>
#include <unistd.h>


void sleep_ms(int ms) {
    timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}

int rrange(int a, int b) {
    return a + rand() % (b - a + 1);
}
std::uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (std::uint64_t)ts.tv_sec * 1000ULL + (std::uint64_t)ts.tv_nsec / 1000000ULL;
}
static void write_csv_and_summary(const SharedState& s, const std::string& csvName) {
    std::ofstream out(csvName);
    out << "FlightID,Type,Priority,ArrivalMs,ServiceMs,"
           "TRequested,TGranted,TStarted,TFinished,"
           "WaitMs,SchedulerDelayMs,RunwayTimeMs\n";

    double sumWait = 0.0;
    std::uint64_t maxWait = 0;
    double sumRunway = 0.0;

    std::uint64_t minReq = UINT64_MAX;
    std::uint64_t maxFin = 0;

    for (const auto& f : s.flights) {
        std::uint64_t waitMs = (f.t_started >= f.t_requested) ? (f.t_started - f.t_requested) : 0;
        std::uint64_t schedMs = (f.t_granted >= f.t_requested) ? (f.t_granted - f.t_requested) : 0;
        std::uint64_t runwayMs = (f.t_finished >= f.t_started) ? (f.t_finished - f.t_started) : 0;

        sumWait += (double)waitMs;
        maxWait = std::max(maxWait, waitMs);
        sumRunway += (double)runwayMs;

        minReq = std::min(minReq, f.t_requested);
        maxFin = std::max(maxFin, f.t_finished);

        out << f.id << ","
            << (f.type == FlightType::LAND ? "LAND" : "TAKEOFF") << ","
            << (f.priority == FlightPriority::EMERGENCY ? "EMERGENCY" : "NORMAL") << ","
            << f.arrival_ms << ","
            << f.service_ms << ","
            << f.t_requested << ","
            << f.t_granted << ","
            << f.t_started << ","
            << f.t_finished << ","
            << waitMs << ","
            << schedMs << ","
            << runwayMs << "\n";
    }
    out.close();

    double avgWait = (s.num_flights > 0) ? (sumWait / (double)s.num_flights) : 0.0;
    double makespanMs = (maxFin > minReq) ? (double)(maxFin - minReq) : 1.0;
    double throughput = (double)s.num_flights / (makespanMs / 1000.0);
    double utilization = (s.runways > 0) ? (sumRunway / ((double)s.runways * makespanMs)) : 0.0;

    printf("\n=== EVALUATION SUMMARY ===\n");
    printf("CSV saved: %s\n", csvName.c_str());
    printf("Average wait time: %.2f ms\n", avgWait);
    printf("Maximum delay: %llu ms\n", (unsigned long long)maxWait);
    printf("Throughput: %.3f flights/sec\n", throughput);
    printf("Runway utilization: %.2f %%\n", utilization * 100.0);
}


int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));

    int flights = 20;
    int runways = 2;

    if (argc > 1) flights = atoi(argv[1]);
    if (argc > 2) runways = atoi(argv[2]);

    SharedState s{};
    s.num_flights = flights;
    s.completed = 0;
    s.runways = runways;
    s.shutdown = false;

    pthread_mutex_init(&s.mutex, nullptr);
    pthread_cond_init(&s.cv_scheduler, nullptr);

    s.flights.resize(flights);
    s.cv_flight.resize(flights);
    for (int i = 0; i < flights; i++)
        pthread_cond_init(&s.cv_flight[i], nullptr);

    q_init(&s.queue);
    // macOS: use named semaphore (sem_open) instead of sem_init
    const char* SEM_NAME = "/runway_sem_multicore";

    sem_unlink(SEM_NAME); // cleanup from previous runs (ignore errors)

    s.runway_sem = sem_open(SEM_NAME, O_CREAT, 0644, runways);
    if (s.runway_sem == SEM_FAILED) {
        perror("sem_open failed");
        exit(1);
    }


    for (int i = 0; i < flights; i++) {
        s.flights[i].id = i;
        s.flights[i].type = (rand() % 2 == 0) ? FlightType::LAND : FlightType::TAKEOFF;
        s.flights[i].priority = (rand() % 10 == 0) ? FlightPriority::EMERGENCY : FlightPriority::NORMAL;
        s.flights[i].arrival_ms = rrange(0, 500);
        s.flights[i].service_ms = rrange(200, 800);
        s.flights[i].granted = false;
    }

    pthread_t sched;
    pthread_create(&sched, nullptr, scheduler_thread, &s);

    std::vector<pthread_t> threads(flights);
    std::vector<FlightArg> args(flights);

    for (int i = 0; i < flights; i++) {
        args[i].state = &s;
        args[i].id = i;
        pthread_create(&threads[i], nullptr, flight_thread, &args[i]);
    }

    for (int i = 0; i < flights; i++)
        pthread_join(threads[i], nullptr);

    pthread_join(sched, nullptr);
    // cleanup semaphore
    sem_close(s.runway_sem);
    sem_unlink("/runway_sem_multicore");


    printf("Done. Flights=%d Runways=%d\n", flights, runways);
    printf("Max active on runway seen = %d\n", s.max_active_seen);
    write_csv_and_summary(s, "performance_data.csv");
    return 0;
   
}



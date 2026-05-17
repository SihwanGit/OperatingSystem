#include <stdio.h>          // printf, perror 사용
#include <stdlib.h>         // exit 사용
#include <pthread.h>        // pthread_create, pthread_join 등 pthread 함수 사용
#include <unistd.h>         // usleep 사용
#include <sys/time.h>       // 시간 측정 관련 헤더
#include <sys/resource.h>   // getrusage를 이용한 CPU 사용 시간 측정
#include <time.h>           // clock_gettime을 이용한 실제 수행 시간 측정

// 각 스레드가 shared_counter를 증가시키는 반복 횟수
#define ITERATIONS 1000000

// Two-phase lock의 First phase에서 spin을 시도하는 횟수
// 과제 조건에 따라 10회로 설정한다.
#define FIRST_PHASE_TRY_COUNT 10

// Two-phase lock의 Second phase에서 sleep하는 시간
// 단위는 마이크로초이며, 여기서는 1us 동안 대기한다.
#define SLEEP_TIME_US 1

// 여러 스레드가 공유하는 카운터 변수
// 모든 스레드가 이 값을 1씩 증가시킨다.
long long shared_counter = 0;

// Two-phase lock에서 사용하는 락 변수
// 0이면 락이 풀린 상태, 1이면 락이 걸린 상태를 의미한다.
// 여러 스레드가 동시에 접근하므로 volatile로 선언한다.
volatile int two_phase_lock_var = 0;

// 각 스레드에 전달할 정보를 저장하는 구조체
typedef struct {
    int thread_id;              // 스레드 번호
    int iterations;             // 해당 스레드가 반복할 횟수
    long long lock_count;       // 해당 스레드가 lock을 획득한 횟수
    long long unlock_count;     // 해당 스레드가 unlock을 수행한 횟수
    long long sleep_count;      // 해당 스레드가 sleep을 수행한 횟수
    double lock_wait_time;      // 해당 스레드가 lock 획득을 위해 기다린 총 시간
} ThreadArg;

// 현재 실제 시간을 초 단위로 반환하는 함수
// 전체 수행 시간과 lock 대기 시간을 측정하는 데 사용한다.
double get_time_sec() {
    struct timespec ts;

    // CLOCK_MONOTONIC은 시스템 시간 변경의 영향을 받지 않는 시간 기준이다.
    // 프로그램 실행 시간 측정에 적합하다.
    clock_gettime(CLOCK_MONOTONIC, &ts);

    // 초 단위 시간과 나노초 단위 시간을 더해 초 단위 실수로 반환한다.
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// 현재 프로세스가 사용한 CPU 시간을 초 단위로 반환하는 함수
// CPU 사용률 계산에 사용한다.
double get_cpu_time_sec() {
    struct rusage usage;

    // 현재 프로세스의 자원 사용량을 가져온다.
    getrusage(RUSAGE_SELF, &usage);

    // 사용자 모드에서 사용한 CPU 시간 계산
    double user_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;

    // 커널 모드에서 사용한 CPU 시간 계산
    double system_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;

    // 전체 CPU 사용 시간 = 사용자 모드 시간 + 커널 모드 시간
    return user_time + system_time;
}

// Two-phase 방식으로 lock을 획득하는 함수
void two_phase_lock(ThreadArg* arg) {
    // lock 획득을 시도하기 직전 시간 기록
    double start = get_time_sec();

    // 락을 획득할 때까지 반복한다.
    while (1) {
        // First phase:
        // 먼저 일정 횟수만큼 CAS 기반 spin을 시도한다.
        // 즉, 바로 sleep하지 않고 짧은 시간 동안 busy waiting으로 락 획득을 시도한다.
        for (int i = 0; i < FIRST_PHASE_TRY_COUNT; i++) {
            // __sync_bool_compare_and_swap(&two_phase_lock_var, 0, 1)
            //
            // two_phase_lock_var의 현재 값이 0이면:
            // 값을 1로 바꾸고 true를 반환한다.
            // 즉, 락 획득에 성공한 것이다.
            //
            // two_phase_lock_var의 현재 값이 1이면:
            // 이미 다른 스레드가 락을 가지고 있으므로 false를 반환한다.
            if (__sync_bool_compare_and_swap(&two_phase_lock_var, 0, 1)) {
                // lock 획득에 성공한 직후 시간 기록
                double end = get_time_sec();

                // lock 획득 횟수 증가
                arg->lock_count++;

                // lock을 얻기 위해 기다린 시간을 누적
                arg->lock_wait_time += (end - start);

                // lock 획득에 성공했으므로 함수 종료
                return;
            }
        }

        // Second phase:
        // First phase에서 정해진 횟수만큼 시도했지만 락 획득에 실패한 경우 실행된다.
        // 계속 CPU를 점유하며 busy waiting하지 않고 잠시 sleep한다.
        arg->sleep_count++;

        // SLEEP_TIME_US 마이크로초 동안 대기한다.
        // 이를 통해 불필요한 CPU 사용을 줄일 수 있다.
        usleep(SLEEP_TIME_US);
    }
}

// Two-phase 방식으로 lock을 해제하는 함수
void two_phase_unlock(ThreadArg* arg) {
    // two_phase_lock_var의 현재 값이 1이면 0으로 바꾼다.
    // 즉, 락을 점유 중인 상태에서 락을 해제한다.
    __sync_bool_compare_and_swap(&two_phase_lock_var, 1, 0);

    // unlock 수행 횟수 증가
    arg->unlock_count++;
}

// 각 스레드가 실행할 함수
// 공유 변수 shared_counter를 iterations 횟수만큼 1씩 증가시킨다.
void* increase_counter(void* arg) {
    // void* 타입으로 전달된 인자를 ThreadArg* 타입으로 변환
    ThreadArg* thread_arg = (ThreadArg*)arg;

    // 각 스레드는 지정된 반복 횟수만큼 카운터를 증가시킨다.
    for (int i = 0; i < thread_arg->iterations; i++) {
        // Critical Section에 진입하기 전에 Two-phase lock 획득
        two_phase_lock(thread_arg);

        // Critical Section 시작
        // shared_counter는 모든 스레드가 공유하는 자원이다.
        // race condition을 막기 위해 lock으로 보호된 상태에서만 접근한다.
        shared_counter++;
        // Critical Section 종료

        // Critical Section 작업이 끝났으므로 lock 해제
        two_phase_unlock(thread_arg);
    }

    // 스레드 함수 종료
    return NULL;
}

// 특정 스레드 개수에 대해 Two-phase lock 실험을 수행하는 함수
void run_test(int num_threads) {
    // 스레드 객체 배열
    pthread_t threads[num_threads];

    // 각 스레드에 전달할 인자 배열
    ThreadArg thread_args[num_threads];

    // 실험 시작 전 공유 카운터 초기화
    shared_counter = 0;

    // Two-phase lock 변수 초기화
    // 0은 lock이 풀린 상태를 의미한다.
    two_phase_lock_var = 0;

    // 전체 수행 시간 측정을 위한 시작 시간 기록
    double wall_start = get_time_sec();

    // CPU 사용 시간 측정을 위한 시작 CPU 시간 기록
    double cpu_start = get_cpu_time_sec();

    // num_threads 개수만큼 스레드 생성
    for (int i = 0; i < num_threads; i++) {
        // 각 스레드 번호 설정
        thread_args[i].thread_id = i;

        // 각 스레드가 수행할 반복 횟수 설정
        thread_args[i].iterations = ITERATIONS;

        // lock 횟수 초기화
        thread_args[i].lock_count = 0;

        // unlock 횟수 초기화
        thread_args[i].unlock_count = 0;

        // sleep 횟수 초기화
        thread_args[i].sleep_count = 0;

        // lock 대기 시간 초기화
        thread_args[i].lock_wait_time = 0.0;

        // 스레드 생성
        // 생성된 스레드는 increase_counter 함수를 실행한다.
        // 각 스레드에는 thread_args[i]의 주소가 전달된다.
        if (pthread_create(&threads[i], NULL, increase_counter, &thread_args[i]) != 0) {
            perror("Failed to create thread");
            exit(1);
        }
    }

    // 모든 스레드가 종료될 때까지 대기
    // pthread_join을 사용해야 모든 스레드의 작업 완료 후 결과를 계산할 수 있다.
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // CPU 사용 시간 측정을 위한 종료 CPU 시간 기록
    double cpu_end = get_cpu_time_sec();

    // 전체 수행 시간 측정을 위한 종료 시간 기록
    double wall_end = get_time_sec();

    // 실제 전체 수행 시간 계산
    double total_time = wall_end - wall_start;

    // 프로세스가 사용한 CPU 시간 계산
    double cpu_time = cpu_end - cpu_start;

    // CPU 사용률 계산
    // 멀티스레드 환경에서는 여러 CPU 코어를 사용할 수 있으므로 100%를 초과할 수 있다.
    double cpu_usage = (cpu_time / total_time) * 100.0;

    // 전체 스레드의 lock 횟수 합
    long long total_lock_count = 0;

    // 전체 스레드의 unlock 횟수 합
    long long total_unlock_count = 0;

    // 전체 스레드의 sleep 횟수 합
    // Two-phase lock에서만 의미 있는 측정값이다.
    long long total_sleep_count = 0;

    // 전체 스레드의 lock 대기 시간 합
    double total_wait_time = 0.0;

    // 각 스레드별 측정값을 모두 합산
    for (int i = 0; i < num_threads; i++) {
        total_lock_count += thread_args[i].lock_count;
        total_unlock_count += thread_args[i].unlock_count;
        total_sleep_count += thread_args[i].sleep_count;
        total_wait_time += thread_args[i].lock_wait_time;
    }

    // 실험 결과를 CSV 형식으로 출력
    // LockType, NUM_THREADS, FinalCounter, ExpectedValue,
    // TotalTime, CPUUsage, LockCount, UnlockCount, LockWaitTime, SleepCount 순서로 출력한다.
    printf("TWO,%d,%lld,%lld,%.6f,%.2f,%lld,%lld,%.6f,%lld\n",
        num_threads,
        shared_counter,
        (long long)num_threads * ITERATIONS,
        total_time,
        cpu_usage,
        total_lock_count,
        total_unlock_count,
        total_wait_time,
        total_sleep_count);
}

// 프로그램 시작 지점
int main() {
    // 과제 조건에서 요구한 스레드 개수 목록
    int thread_counts[] = { 1, 2, 4, 8, 16, 32 };

    // 실험할 스레드 개수의 총 개수 계산
    int test_count = sizeof(thread_counts) / sizeof(thread_counts[0]);

    // CSV 형식의 결과 헤더 출력
    printf("LockType,NUM_THREADS,FinalCounter,ExpectedValue,TotalTime,CPUUsage,LockCount,UnlockCount,LockWaitTime,SleepCount\n");

    // 1, 2, 4, 8, 16, 32개의 스레드에 대해 각각 실험 수행
    for (int i = 0; i < test_count; i++) {
        run_test(thread_counts[i]);
    }

    // 프로그램 정상 종료
    return 0;
}
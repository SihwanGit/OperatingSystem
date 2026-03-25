#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>

#define MAX_PROCESSES 100
#define Q1_QUANTUM 10
#define Q2_QUANTUM 10
#define Q3_QUANTUM 20
#define BOOST_TIME 50

// Process 구조체 확장
typedef struct Process {
    int pid;
    int arrival_time;
    int burst_time; //run time
    int remaining_burst_time;

    // I/O 정보 추가
    int io_start_time;       // CPU 누적 실행시간 기준 I/O 시작 시점
    int io_burst_time;       // I/O 수행 시간 (종료시간 아님 주의)
    int io_done;             // I/O를 이미 수행했는지 여부
    int blocked_until;       // I/O 종료 시각

    // 스케줄링/통계용 정보 추가
    int completion_time;
    int turnaround_time;
    int response_time;
    int first_start_time;    // 최초 CPU 할당 시각
    int cpu_executed;        // 지금까지 실제로 사용한 CPU 시간
    int queue_level;         // 1, 2, 3
    int quantum_used;        // 현재 queue level에서 사용한 time slice
    int entered_queue_time;  // q2/q3에 들어간 시각(boost 계산용)

    // 상태 플래그
    int admitted;            // ready queue에 처음 들어왔는지
    int finished;            // 종료 여부
    int blocked;             // I/O 중인지

    struct Process* next;
} Process;

// Queue 구조체
typedef struct Queue {
    Process* head, * tail;
    int time_quantum;
} Queue;

// 새 프로세스 생성 함수 수정
Process* createProcess(int pid, int arrival, int burst, int io_start, int io_burst) {
    Process* p = (Process*)malloc(sizeof(Process));
    if (p == NULL) {
        printf("Memory allocation failed\n");
        exit(1);
    }

    p->pid = pid;
    p->arrival_time = arrival;
    p->burst_time = burst;
    p->remaining_burst_time = burst;

    p->io_start_time = io_start;
    p->io_burst_time = io_burst;
    p->io_done = (io_burst == 0) ? 1 : 0;  // I/O가 없으면 이미 끝난 것으로 처리
    p->blocked_until = -1;

    p->completion_time = 0;
    p->turnaround_time = 0;
    p->response_time = -1;
    p->first_start_time = -1;
    p->cpu_executed = 0;
    p->queue_level = 1;
    p->quantum_used = 0;
    p->entered_queue_time = 0;

    p->admitted = 0;
    p->finished = 0;
    p->blocked = 0;

    p->next = NULL;
    return p;
}

// 새 큐 생성
Queue* createQueue(int quantum) {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    if (q == NULL) {
        printf("Memory allocation failed\n"); //예외 처리 코드만 추가
        exit(1);
    }
    q->head = q->tail = NULL;
    q->time_quantum = quantum;
    return q;
}

// 큐 뒤에 삽입
void enqueue(Queue* q, Process* p) {
    p->next = NULL;
    if (q->tail == NULL) {
        q->head = q->tail = p;
        return;
    }
    q->tail->next = p;
    q->tail = p;
}

// 큐 앞에서 제거
Process* dequeue(Queue* q) {
    if (q->head == NULL) return NULL;

    Process* p = q->head;
    q->head = q->head->next;
    if (q->head == NULL) q->tail = NULL;
    p->next = NULL;
    return p;
}

// 특정 queue level에 맞는 큐에 삽입
void enqueueByLevel(Queue* q1, Queue* q2, Queue* q3, Process* p, int current_time) {
    p->next = NULL;

    //우선순위에 따라 q1, q2, q3 중 한 곳에 삽입
    if (p->queue_level == 1) {
        p->entered_queue_time = current_time;
        enqueue(q1, p);
    }
    else if (p->queue_level == 2) {
        p->entered_queue_time = current_time;
        enqueue(q2, p);
    }
    else {
        p->entered_queue_time = current_time;
        enqueue(q3, p);
    }
}

// 현재 ready queue 중 가장 높은 우선순위 반환
int highestReadyQueueLevel(Queue* q1, Queue* q2, Queue* q3) {
    if (q1->head != NULL) return 1;
    if (q2->head != NULL) return 2;
    if (q3->head != NULL) return 3;
    return 0;
}

// 현재 가장 높은 우선순위 큐에서 프로세스 선택
Process* selectNextProcess(Queue* q1, Queue* q2, Queue* q3) {
    if (q1->head != NULL) return dequeue(q1);
    if (q2->head != NULL) return dequeue(q2);
    if (q3->head != NULL) return dequeue(q3);
    //q1이 비면 q2를, q2가 비면 q3로 가면서 queue의 front를 dequeue
    return NULL;
}

// q2, q3의 boost 처리
void boostProcesses(Queue* from, Queue* q1, int current_time) {
    Process* prev = NULL;
    Process* curr = from->head;

    while (curr != NULL) {
        Process* next = curr->next;

        // q2/q3에서 50 이상 기다렸으면 q1으로 boost
        if (current_time - curr->entered_queue_time >= BOOST_TIME) {
            if (prev == NULL) {
                from->head = next;
            }
            else {
                prev->next = next;
            }

            if (curr == from->tail) {
                from->tail = prev;
            }

            curr->queue_level = 1;
            curr->quantum_used = 0;
            curr->next = NULL;
            curr->entered_queue_time = current_time;
            enqueue(q1, curr);
        }
        else {
            prev = curr;
        }

        curr = next;
    }
}

// 도착한 프로세스 ready queue에 넣기
void admitArrivals(Process* processes[], int n, Queue* q1, int current_time) {
    int i;
    for (i = 0; i < n; i++) {
        if (!processes[i]->admitted && processes[i]->arrival_time <= current_time) { //admitted하지 않은 상태(도착X)이고, 도착시간이 현재시간보다 작거나 같으면(새롭게 도착하면)
            processes[i]->admitted = 1; //도착 상태 활성화
            processes[i]->queue_level = 1; //레벨은 일단 1로 삽입
            processes[i]->quantum_used = 0; //사용한 시간은 0으로 설정
            processes[i]->entered_queue_time = current_time;
            enqueue(q1, processes[i]);
        }
    }
}

// I/O 끝난 프로세스 ready queue로 복귀
void unblockProcesses(Process* processes[], int n, Queue* q1, Queue* q2, Queue* q3, int current_time) {
    int i;
    for (i = 0; i < n; i++) {
        if (processes[i]->blocked && processes[i]->blocked_until <= current_time) {
            processes[i]->blocked = 0;
            processes[i]->quantum_used = 0; // 자발적 양보 후 복귀 시 time slice 새로 시작
            enqueueByLevel(q1, q2, q3, processes[i], current_time);
        }
    }
}

// 파일에서 trace 읽기
int loadProcessesFromFile(const char* filename, Process* processes[]) {
    FILE* fp = fopen(filename, "r");
    int pid, arrival, burst, io_start, io_burst;
    int count = 0;

    if (fp == NULL) {
        printf("Failed to open file: %s\n", filename);
        return -1;
    }

    while (fscanf(fp, "%d %d %d %d %d", &pid, &arrival, &burst, &io_start, &io_burst) == 5) {
        if (count >= MAX_PROCESSES) {
            printf("Maximum number of processes exceeded (max=%d)\n", MAX_PROCESSES);
            fclose(fp);
            return -1;
        }
        processes[count++] = createProcess(pid, arrival, burst, io_start, io_burst);
    }

    fclose(fp);
    return count;
}

// 결과 출력
void printResults(Process* processes[], int n, int final_time) {
    int i;

    printf("PID\tTurnaround Time\tResponse Time\n");
    for (i = 0; i < n; i++) {
        printf("%d\t%d\t\t%d\n",
            processes[i]->pid,
            processes[i]->turnaround_time,
            processes[i]->response_time);
    }
    printf("Final Completion Time: %d\n", final_time);
}

// MLFQ 시뮬레이션
void mlfq_scheduling(Process* processes[], int n) {
    // 문제 조건에 맞게 quantum 수정
    Queue* q1 = createQueue(Q1_QUANTUM);
    Queue* q2 = createQueue(Q2_QUANTUM);
    Queue* q3 = createQueue(Q3_QUANTUM);

    int current_time = 0;
    int completed_processes = 0;

    Process* current_process = NULL;

    while (completed_processes < n) {
        // 현재 시각에 도착한 프로세스 반영
        admitArrivals(processes, n, q1, current_time);

        // 현재 시각에 I/O가 끝난 프로세스 반영
        unblockProcesses(processes, n, q1, q2, q3, current_time);

        // q2, q3 boost 처리
        boostProcesses(q2, q1, current_time);
        boostProcesses(q3, q1, current_time);

        // 실행 중인 프로세스가 있고 더 높은 우선순위 ready queue가 있으면 선점
        if (current_process != NULL) {
            int highest_ready = highestReadyQueueLevel(q1, q2, q3);
            if (highest_ready != 0 && highest_ready < current_process->queue_level) {
                enqueueByLevel(q1, q2, q3, current_process, current_time);
                current_process = NULL;
            }
        }

        // CPU가 비어 있으면 다음 프로세스 선택
        if (current_process == NULL) {
            current_process = selectNextProcess(q1, q2, q3);

            if (current_process != NULL && current_process->first_start_time == -1) {
                current_process->first_start_time = current_time;
                current_process->response_time = current_time - current_process->arrival_time;
            }
        }

        // 실행할 프로세스가 없으면 time만 증가
        if (current_process == NULL) {
            current_time++;
            continue;
        }

        // I/O 시작 시점이면 CPU를 자발적으로 양보하고 block
        if (!current_process->io_done &&
            current_process->io_burst_time > 0 &&
            current_process->cpu_executed == current_process->io_start_time) {

            current_process->blocked = 1;
            current_process->blocked_until = current_time + current_process->io_burst_time;
            current_process->io_done = 1;
            current_process->quantum_used = 0; // 자발적 양보이므로 quantum 초기화
            current_process = NULL;
            continue;
        }

        // time unit = 1 이므로 1만큼 실행
        current_process->remaining_burst_time--;
        current_process->cpu_executed++;
        current_process->quantum_used++;
        current_time++;

        // 실행 후 종료 여부 확인
        if (current_process->remaining_burst_time == 0) {
            current_process->finished = 1;
            current_process->completion_time = current_time;
            current_process->turnaround_time = current_process->completion_time - current_process->arrival_time;
            completed_processes++;
            current_process = NULL;
            continue;
        }

        // 실행 직후 I/O 시작 시점 도달 여부 확인
        if (!current_process->io_done &&
            current_process->io_burst_time > 0 &&
            current_process->cpu_executed == current_process->io_start_time) {

            current_process->blocked = 1;
            current_process->blocked_until = current_time + current_process->io_burst_time;
            current_process->io_done = 1;
            current_process->quantum_used = 0; // 자발적 양보
            current_process = NULL;
            continue;
        }

        // time quantum 소진 시 queue 이동
        if (current_process->queue_level == 1 && current_process->quantum_used >= Q1_QUANTUM) { //현재 프로세스가 q1에 있고, 퀀텀을 사용한 시간이 Q1의 타임퀀텀(10)보다 크면 레벨을 내림.
            current_process->queue_level = 2;
            current_process->quantum_used = 0;
            enqueueByLevel(q1, q2, q3, current_process, current_time);
            current_process = NULL;
        }
        else if (current_process->queue_level == 2 && current_process->quantum_used >= Q2_QUANTUM) {
            current_process->queue_level = 3;
            current_process->quantum_used = 0;
            enqueueByLevel(q1, q2, q3, current_process, current_time);
            current_process = NULL;
        }
        else if (current_process->queue_level == 3 && current_process->quantum_used >= Q3_QUANTUM) { //Q3는 더이상 내려가지 않고, 시간만 초기화 후 큐의 뒤쪽으로 이동(라운드 로빈)
            // q3도 quantum 20으로 RR처럼 순환
            current_process->queue_level = 3;
            current_process->quantum_used = 0;
            enqueueByLevel(q1, q2, q3, current_process, current_time);
            current_process = NULL;
        }
    }

    // response/turnaround 최종 출력
    printResults(processes, n, current_time);

    // 동적 메모리 해제
    free(q1);
    free(q2);
    free(q3);
}

int main(int argc, char* argv[]) {
    Process* processes[MAX_PROCESSES];
    int n, i;

    // 기본 파일명 trace1.txt 사용, 인자가 있으면 그 파일 사용
    const char* filename = "trace1.txt";
    if (argc >= 2) {
        filename = argv[1];
    }

    // main에서 직접 프로세스를 생성하던 부분을 파일 입력 방식으로 수정
    n = loadProcessesFromFile(filename, processes);
    if (n <= 0) {
        printf("No process loaded.\n");
        return 1;
    }
    //프로세스의 배열을 100까지 만들어 두고, 파일에서 프로세스 개수 n을 구한 뒤 n개의 프로세스들만 이용.

    mlfq_scheduling(processes, n);

    // 프로세스 메모리 해제
    for (i = 0; i < n; i++) {
        free(processes[i]);
    }

    return 0;
}
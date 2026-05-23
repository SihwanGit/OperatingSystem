#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct {
    int op1;
    int op2;
    pthread_mutex_t *lock;
} Args;

void *add_thread(void *arg) {
    Args *args = (Args *)arg;

    int result = args->op1 + args->op2;

    pthread_mutex_lock(args->lock);
    printf("Add: %d\n", result);
    pthread_mutex_unlock(args->lock);

    return NULL;
}

void *sub_thread(void *arg) {
    Args *args = (Args *)arg;

    int result = args->op1 - args->op2;

    pthread_mutex_lock(args->lock);
    printf("Sub: %d\n", result);
    pthread_mutex_unlock(args->lock);

    return NULL;
}

void *mul_thread(void *arg) {
    Args *args = (Args *)arg;

    int result = args->op1 * args->op2;

    pthread_mutex_lock(args->lock);
    printf("Mul: %d\n", result);
    pthread_mutex_unlock(args->lock);

    return NULL;
}

void *div_thread(void *arg) {
    Args *args = (Args *)arg;

    pthread_mutex_lock(args->lock);

    if (args->op2 == 0) {
        printf("Div: division by zero error\n");
    } else {
        double result = (double)args->op1 / args->op2;
        printf("Div: %lf\n", result);
    }

    pthread_mutex_unlock(args->lock);

    return NULL;
}

int main(int argc, char *argv[]) {
    int op1 = 5;
    int op2 = 3;

    pthread_t p1, p2, p3, p4;
    pthread_mutex_t lock;

    pthread_mutex_init(&lock, NULL);

    Args args;
    args.op1 = op1;
    args.op2 = op2;
    args.lock = &lock;

    printf("op1: %d, op2: %d\n", op1, op2);

    pthread_create(&p1, NULL, add_thread, (void *)&args);
    pthread_create(&p2, NULL, sub_thread, (void *)&args);
    pthread_create(&p3, NULL, mul_thread, (void *)&args);
    pthread_create(&p4, NULL, div_thread, (void *)&args);

    pthread_join(p1, NULL);
    pthread_join(p2, NULL);
    pthread_join(p3, NULL);
    pthread_join(p4, NULL);

    pthread_mutex_destroy(&lock);

    return 0;
}
#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<assert.h>
// pthread랑 assert는 리눅스 기반 OS에서 지원하는 헤더파일로, 윈도우에선 실행 X
// WSL2나 Virtual Box 켜서 vim으로 코드 만들어서 실행시켜보샘


// 1. pthread_create()의 매개변수와 사용법
// 2. pthread_join()의 매개변수와 사용법
// 3. 매개변수가 void*가 아닌 경우
// 4. start routine의 반환값이 void*가 아닌 경우


// pthread 함수의 4번째 매개변수는 쓰레드가 실행하고자 하는 함수의 매개변수를 전달한다.
// 이때 매개변수는 무조건 한개만 전달할 수 있기 때문에, 여러개 필요한 경우 구조체로 타입을 만들어야함.
// 또한 매개변수는 무조건 (void *) 보이드 포인터 타입으로 받아야하며, 만약 다른 타입을 받고 싶다면.
// 일단 void *로 arg를 받은 뒤, 함수 내부에서 해당 타입의 포인터로 변환하면 된다.
typedef struct op_t {
	int a;
	int b;
} op_t;


// 함수 내부에서 형변환 필요
void* add_thread(void* arg) {
	op_t* op = (op_t*)arg;
	printf("ADD : %d\n", op->a + op->b);
	int* r = malloc(sizeof(int));
	*r = op->a + op->b;
	return (void*)r;
}
//만약 다른값을 리턴하고 싶다면 동적할당을 한 뒤 void*로 바꿔서 리턴하면 된다.
//이떄 정적할당을 하게 되면 에러가 난다. 지역변수는 Stack에 저장되기 때문에 Thread가 끝나면 해제된다.

void* sub_thread(void* arg) {
	op_t* op = (op_t*)arg;
	printf("SUB : %d\n", op->a - op->b);
	int* r = malloc(sizeof(int));
	*r = op->a - op->b;
	return (void*)r;
}

void* mul_thread(void* arg) {
	op_t* op = (op_t*)arg;
	printf("MUL : %d\n", op->a * op->b);
	int* r = malloc(sizeof(int));
	*r = op->a * op->b;
	return (void*)r;
}

void* div_thread(void* arg) {
	op_t* op = (op_t*)arg;
	printf("DIV : %d\n", op->a / op->b);
	int* r = malloc(sizeof(int));
	*r = op->a / op->b;
	return (void*)r;
}

int main() {
	op_t arg;
	arg.a = 5;
	arg.b = 3;
	pthread_t p1, p2, p3, p4;

	//pthread_create 함수는 다음 4가지를 매개변수로 받는다.
	// 1. pthread_t의 포인터
	// 2. void *로 정의된 해당 스레드의 속성 attr
	// 3. void *를 반환하는 함수 (쓰레드가 수행할 함수)
	// 4. void *로 선언된 매개변수 (3번의 함수의 매개변수)
	// 이떄 매개변수는 무조건 void*여야하고, 1개밖에 못넣으니 구조체와 형변환을 쓴다고 위해서 설명했음.
	// 만약 함수가 void *가 아닌 다른 값을 리턴하고 싶다면, 함수 내부에서 반환하고자 하는 타입을 동적 할당으로 선언하고,
	// (void *)ret_t 로 void*로 형변환해서 반환하면 됨.
	pthread_create(&p1, NULL, add_thread, (void*)&arg);
	pthread_create(&p2, NULL, sub_thread, (void*)&arg);
	pthread_create(&p3, NULL, mul_thread, (void*)&arg);
	pthread_create(&p4, NULL, div_thread, (void*)&arg);

	int* r1, *r2, *r3, *r4;
	pthread_join(p1, (void**)&r1);
	pthread_join(p2, (void**)&r2);
	pthread_join(p3, (void**)&r3);
	pthread_join(p4, (void**)&r4);
	//pthread_join은 특정 스레드가 끝날 때까지 기다리는 동기화 용으로도 사용하고, 해당 스레드의 반환값을 받을 떄도 사용한다.
	//첫번째 매개변수는 pthread_t다. 기다리고자 하는 스레드를 의미한다. 중요한건 포인터가 아니라 일반 변수라는 점이다.
	//두번쨰 매개변수는 void**로 해당 스레드의 반환주소를 받기 위한 이중 포인터다. 이중포인터기 때문에 (void**)&r로 int *인 r에 &를 붙였다.

	//참고로 지금은 단순한 사칙연산이라 문제 없지만 이렇게만 작성하면 사칙연산 간 Critical Section이 보장되지 않아 Race Condition 문제가 발생할수 있다.

	return 0;
}
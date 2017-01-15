#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/errno.h>
#include "mastermind.h"
#include "test_utilities.h"

//************************************************************

#define PASS(x) (sprintf(pass, "%d", x))

static bool check_result(char* guess, char* code, char* result) {
	char res[5];
	res[4] = 0;
	generateFeedback(res, guess, code);
	return !strcmp(res, result);
}

//************************************************************

char pass[5];

void* start_game(void* arg) {
	int fd = open("/dev/maker", O_RDWR);
	ASSERT_EQ(true, fd > 0);

	PASS(2021);
	ASSERT_EQ(write(fd, pass, 5), 1);

	sleep(2);

	ASSERT_EQ(ioctl(fd, ROUND_START, 4) > 0 , true);
	ASSERT_EQ(ioctl(fd, GET_MY_SCORE), 0);

	char res[5];
	int i, r;
	for (i = 0; i < 3; ++i) {
		ASSERT_EQ(read(fd, res, 5), 1);

		do {
			r = write(fd, res, 5);
		} while (r == -1 && errno == EBUSY);

		ASSERT_EQ(r, 1);
	}

	sleep(4);
	close(fd);
	return NULL;
}

//************************************************************

//printf("\nguess: %s\tresult: %s\n", guess, res);
//printf("\nerrno = %d\n", errno);

//************************************************************

void* game1(void* arg) {
	int fd = open("/dev/breaker", O_RDWR);
	ASSERT_EQ(true, fd > 0);
	sleep(3);
	char *guess = "0013";

	ASSERT_EQ(write(fd, guess, 5), 1);
	char res[5];
	ASSERT_EQ(read(fd, res, 5), 1);

	ASSERT_EQ(check_result(guess, pass, res), true);

	close(fd);
	return NULL;
}

//************************************************************

void* game2(void* arg) {
	int fd = open("/dev/breaker", O_RDWR);
	ASSERT_EQ(true, fd > 0);
	sleep(3);
	char *guess = "2021";

	ASSERT_EQ(write(fd, guess, 5), 1);
	char res[5];
	ASSERT_EQ(read(fd, res, 5), 1);

	ASSERT_EQ(check_result(guess, pass, res), true);
	ASSERT_EQ(ioctl(fd, GET_MY_SCORE), 1);

	close(fd);
	return NULL;
}

//************************************************************

void* game3(void* arg) {
	int fd = open("/dev/breaker", O_RDWR);
	ASSERT_EQ(true, fd > 0);
	sleep(3);
	char *guess = "2222";

	ASSERT_EQ(write(fd, guess, 5), 1);
	char res[5];
	ASSERT_EQ(read(fd, res, 5), 1);

	ASSERT_EQ(check_result(guess, pass, res), true);

	close(fd);
	return NULL;
}

//************************************************************

bool test1() {
	pthread_t maker;
	pthread_create(&maker, NULL, start_game, NULL);


	pthread_t breaker1;
	pthread_create(&breaker1, NULL, game1, NULL);

	pthread_t breaker2;
	pthread_create(&breaker2, NULL, game2, NULL);

	pthread_t breaker3;
	pthread_create(&breaker3, NULL, game3, NULL);

	pthread_join(maker, NULL);
	pthread_join(breaker1, NULL);
	pthread_join(breaker2, NULL);
	pthread_join(breaker3, NULL);

	return true;
}

int main(void) {

	RUN_TEST(test1);

	return 0;
}

/*
 * ExampleTest.c
 *
 *  Created on: Oct 24, 2016
 *      Author: itay887
 */

#include "cutest.h"
#include "test_utils.hpp"
#include <unistd.h>
#include <sched.h>
#include "mastermind.h"
#include <errno.h>
#define INPUT_FILE "settings.txt"
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <vector>
#include <signal.h>
using std::string;
//#define ONE_SEC_ITER 157000000*2
//#define THREE_SEC_ITER ONE_SEC_ITER*3
//#define TWO_SEC_ITER ONE_SEC_ITER*2

#ifdef HW4_DEBUG
#define ROUND_STOP			_IOW(MASTERMIND_MAGIC, 2, int)
#define GET_TURNS			_IOR(MASTERMIND_MAGIC, 3, int)
#define IS_ROUND_STARTED	_IOR(MASTERMIND_MAGIC, 4, int)
#endif
using namespace std;
typedef struct timespec TimeSpec;
#define MS_SLEEP(amount) nanosleep(TimeSpec{amount/1000,amount%1000},NULL)
//Setting test environemt with parameters
static TestUtils utils("settings-hw4.txt");
void SetParams();
void (*init_func)(void)=SetParams;
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t CondVar;

void WaitMs(unsigned int amount){
	TimeSpec ts;
	ts.tv_sec = amount/1000;
	ts.tv_nsec = amount * 1000000;
	nanosleep(&ts,NULL);
}

typedef struct breaker_data{
	int fd,id;
	int expected_score;
} breaker_data_t;

typedef struct maker_data{
	int fd;
	int expected_score;
	vector<string> passwords;
} maker_data_t;

void SetParams()
{
	utils.Init();
	//utils.PrintParameters("BasicTest");
}
//End of setting environment
//Tests here

void BasicMasterBringUp()
{
	int maker_fd=open(utils.ParamToString("maker_path").c_str(),O_RDWR);
	TEST_CHECK_(maker_fd != -1,"Opened a maker.Expected to succeed. got maker_fd=%d",maker_fd);
	int maker_fd2=open(utils.ParamToString("maker_path").c_str(),O_RDWR);
	TEST_CHECK_(maker_fd2 == -1 && errno == EPERM,"Opened a maker.Expected to succeed. got maker_fd=%d\nExpected errno=%d got %d",maker_fd2,EPERM,errno);
	
	int breaker1_fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
	TEST_CHECK_(breaker1_fd != -1,"Opened a breaker.Expected to succeed. got maker_fd=%d errno=%d",breaker1_fd,errno);
	int res;
	//attempting to write when the round hasn't begun
	char breaker_buffer[5]={0};
	char maker_buffer[5]={0};
	char guess_buffer[5]={0};
	
	res = write(breaker1_fd,"2232",4);
	TEST_CHECK_(res == -1 && errno == EIO,"Breaker writes before round begun.got res=%d errno=%d\nExpected res=%d errno=%d",res,errno,-1,EIO);
	strcpy(maker_buffer,"3432");
	res = write(maker_fd,maker_buffer,4);
	TEST_CHECK_(res == 1,"Maker tries to set password and should succeed.Got res=%d  errno=%d",res,errno);
	res = ioctl(breaker1_fd,ROUND_START,5);
	TEST_CHECK_(res == -1 && errno == EPERM,"Breaker tries to start a game and should fail.Got res=%d errno=%d\nExpected res=%d errno=%d",res,errno,-1,EPERM);
	res = ioctl(maker_fd,ROUND_START,5);
	TEST_CHECK_(res == 1,"Maker started a round - expected to succeed.Got res=%d errno=%d",res,errno);
	/*res = close(maker_fd);
	TEST_CHECK_(res == -1 && errno == EBUSY,"Maker attempts to close descriptor and should fail.Got res=%d",res);*/
	//guessing begins
	res = write(breaker1_fd,"0000",4);
	TEST_CHECK_(res == 1,"Breaker wrote a wrong guess - expected to succeed.Got res=%d errno=%d",res,errno);
	res = read(maker_fd,guess_buffer,4);
	int cmpres=strcmp(guess_buffer,"0000");
	TEST_CHECK_(res == 1 && cmpres == 0,"Maker read a guess.Expected to read the inputed guess.Got res=%d,cmpres=%d  errno=%d guess_buffer=%s"
					,res,cmpres,errno,guess_buffer);
	res = write(maker_fd,guess_buffer,4);
	TEST_CHECK_(res == 1,"Maker writes a response - should succeed.Got res=%d",res);
	res = read(breaker1_fd,breaker_buffer,4);
	cmpres = strcmp(breaker_buffer,"0000");
	TEST_CHECK_(res == 1 && cmpres == 0,"Breaker reads feedback.got res=%d and cmpres=%d errno=%d\nExpected res=%d",res,cmpres,errno,1);
	res = ioctl(breaker1_fd,GET_MY_SCORE,0);
	TEST_CHECK_(res == 0,"Breaker didn't guess right so his points shouldn't have incremented.Got res=%d",res);
	//trying to write an invalid guess
	res = write(breaker1_fd,"abcdjk",6);
	TEST_CHECK_(res == -1 && errno == EINVAL,"Breaker writes an guess.Got res=%d errno=%d\nExpected res=%d errno=%d",res,errno,-1,EINVAL);
	#ifdef HW4_DEBUG
	res = ioctl(breaker1_fd,GET_TURNS,0);
	TEST_CHECK_(res == 9,"Breaker used one turn - expected to have 9 turns.Got %d",res);
	res = ioctl(maker_fd,GET_TURNS,0);
	TEST_CHECK_(res == 9,"Breaker used one turn - expected maker to have 9 turns remaining.Got %d",res);
	#endif
	//another guess that should end the game
	res = write(breaker1_fd,"3432",4);
	TEST_CHECK_(res == 1,"Breaker wrote a wrong guess - expected to succeed.Got res=%d",res);
	res = read(maker_fd,guess_buffer,4);
	cmpres=strcmp(guess_buffer,"2222");
	TEST_CHECK_(res == 1 && cmpres == 0,"Maker read a guess.Expected to read the inputed guess.Got res=%d,cmpres=%d errno=%d guess_buffer=%s",
	            res,cmpres,errno,guess_buffer);
	res = write(maker_fd,guess_buffer,4);
	TEST_CHECK_(res == 1,"Maker writes a response - should succeed.Got res=%d",res);
	res = read(breaker1_fd,breaker_buffer,4);
	cmpres = strcmp(breaker_buffer,"2222");
	TEST_CHECK_(res == 1 && cmpres == 0,"Breaker reads feedback.got res=%d and cmpres=%d errno=%d breaker_buffer=%s",res,cmpres,errno,breaker_buffer);
	res = ioctl(breaker1_fd,GET_MY_SCORE,0);
	TEST_CHECK_(res == 1,"Breaker guessed right so his points should have incremented.Got res=%d",res);
	//closing
	#ifdef HW4_DEBUG
	res = ioctl(maker_fd,ROUND_STOP,0);
	#endif
	
	
	res = close(breaker1_fd);
	res=close(maker_fd);
	
}

void SimpleGameTest()
{
	int maker_fd=open(utils.ParamToString("maker_path").c_str(),O_RDWR);
	TEST_CHECK_(maker_fd != -1,"Opened a maker.Expected to succeed. got maker_fd=%d",maker_fd);
	int breaker1_fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
	TEST_CHECK_(breaker1_fd != -1,"Opened a breaker.Expected to succeed. got breaker1_fd=%d errno=%d",breaker1_fd,errno);
	int res;
	//attempting to write when the round hasn't begun
	char breaker_buffer[5]={0};
	char maker_buffer[5]={0};
	char guess_buffer[5]={0};
	
	res = write(maker_fd,"7654",4);
	TEST_CHECK_(res != -1,"Maker sets password and should succeed.Got res=%d errno=%d",res,errno);
	
	res = ioctl(maker_fd,ROUND_START,8);
	TEST_CHECK_(res == 1,"Maker starts round. Got res=%d errno=%d",res,errno);
	
	int breaker2_fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
	TEST_CHECK_(breaker1_fd != -1,"Opened another breaker.Expected to succeed. got breaker2_fd=%d errno=%d",breaker2_fd,errno);
	#ifdef HW4_DEBUG
	res = ioctl(maker_fd,GET_TURNS,0);
	TEST_CHECK_(res == 20,"Two breakers are participating so both turns should get counted.got %d",res);
	res = ioctl(breaker1_fd,GET_TURNS,0);
	TEST_CHECK_(res == 10,"Breaker1 is participating so should have default turns.got %d",res);
	res = ioctl(breaker2_fd,GET_TURNS,0);
	TEST_CHECK_(res == 10,"Breaker2 is participating so should have default turns.got %d",res);
	#endif
	int breaker1guesses=10,breaker2guesses=10,total=20;
	//both start guessing
	for(int i=0;i<10;i++)
	{
		//breaker 1
		res = write(breaker1_fd,"0000",4);
	TEST_CHECK_(res == 1,"Breaker wrote a wrong guess - expected to succeed.Got res=%d errno=%d",res,errno);
	res = read(maker_fd,guess_buffer,4);
	int cmpres=strcmp(guess_buffer,"0000");
	TEST_CHECK_(res == 1 && cmpres == 0,"Maker read a guess.Expected to read the inputed guess.Got res=%d,cmpres=%d  errno=%d guess_buffer=%s"
					,res,cmpres,errno,guess_buffer);
	res = write(maker_fd,guess_buffer,4);
	TEST_CHECK_(res == 1,"Maker writes a response - should succeed.Got res=%d",res);
	res = read(breaker1_fd,breaker_buffer,4);
	cmpres = strcmp(breaker_buffer,"0000");
	TEST_CHECK_(res == 1 && cmpres == 0,"Breaker reads feedback.got res=%d and cmpres=%d errno=%d",res,cmpres,errno);
	#ifdef HW4_DEBUG
	breaker1guesses--;	
	total--;
	
	res = ioctl(maker_fd,GET_TURNS,0);
	TEST_CHECK_(res == total,"Total guesses should decrement.Got %d expected %d",res,total);
	res = ioctl(breaker1_fd,GET_TURNS,0);
	TEST_CHECK_(res == breaker1guesses,"Breaker1 guesses should decrement.got %d expected %d",res,breaker1guesses);
#endif		
	}
	//trying to guess after
	res = write(breaker1_fd,"3030",4);
	TEST_CHECK_(res == -1 && errno == EPERM,"Breaker 1 tries to guess without any more round remaining.Got res=%d errno=%d\nExpected res=%d errno=%d"
	,res,errno,-1,EPERM);
	int breaker_3fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
	TEST_CHECK_(breaker_3fd != -1,"Opened another breaker. breaker_3fd=%d  errno=%d",breaker_3fd,errno);
	total+=10;
	#ifdef HW4_DEBUG
	res = ioctl(maker_fd,GET_TURNS,0);
	TEST_CHECK_(res == total,"Total guesses should increment.Got %d expected %d",res,total);
	res = ioctl(breaker_3fd,GET_TURNS,0);
	TEST_CHECK_(res == 10,"Breaker3 guesses should be default.got %d expected %d",res,breaker1guesses);
	#endif
	//breaker 2
	for(int i=0;i<10;i++){
	
	
			res = write(breaker2_fd,"0000",4);
	TEST_CHECK_(res == 1,"Breaker wrote a wrong guess - expected to succeed.Got res=%d errno=%d",res,errno);
	res = read(maker_fd,guess_buffer,4);
	int cmpres=strcmp(guess_buffer,"0000");
	TEST_CHECK_(res == 1 && cmpres == 0,"Maker read a guess.Expected to read the inputed guess.Got res=%d,cmpres=%d  errno=%d guess_buffer=%s"
					,res,cmpres,errno,guess_buffer);
	res = write(maker_fd,guess_buffer,4);
	TEST_CHECK_(res == 1,"Maker writes a response - should succeed.Got res=%d",res);
	res = read(breaker2_fd,breaker_buffer,4);
	cmpres = strcmp(breaker_buffer,"0000");
	TEST_CHECK_(res == 1 && cmpres == 0,"Breaker reads feedback.got res=%d and cmpres=%d errno=%d",res,cmpres,errno);
	#ifdef HW4_DEBUG
	breaker2guesses--;
	total--;
	res = ioctl(maker_fd,GET_TURNS,0);
	TEST_CHECK_(res == total,"Total guesses should decrement.Got %d expected %d",res,total);
	res = ioctl(breaker2_fd,GET_TURNS,0);
	TEST_CHECK_(res == breaker2guesses,"Breaker1 guesses should decrement.got %d expected %d",res,breaker2guesses);
	#endif
	}
	res = ioctl(maker_fd,GET_MY_SCORE,0);
	TEST_CHECK_(res == 0,"Maker hasn't won yet. Got res=%d",res);
	close(breaker_3fd);
	// both breaker should fail on EIO because round is over!
	res = write(breaker1_fd,"3030",4);
	TEST_CHECK_(res == -1 && errno == EPERM,"Breaker 1 writes after breaker 3 left - round is over.Got res=%d errno=%d\nExpected res=%d errno=%d"
	,res,errno,-1,EPERM);
	res = write(breaker2_fd,"3030",4);
	TEST_CHECK_(res == -1 && errno == EPERM,"Breaker 1 writes after breaker 3 left - round is over.Got res=%d errno=%d\nExpected res=%d errno=%d"
	,res,errno,-1,EPERM);
	//maker's score shouldve incremented
	res = ioctl(maker_fd,GET_MY_SCORE,0);
	TEST_CHECK_(res == 1,"Maker has won and deserves a point. Got res=%d",res);
	
	close(maker_fd);
	close(breaker1_fd);
	close(breaker1_fd);
	
}

void BreakerWriteEIOTest()
{
	int maker_fd,breaker1_fd,breaker2_fd,res,msg;
	int my_pipe[2];
	pipe(my_pipe);
	char buffer[5]={0};
	breaker1_fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
	TEST_CHECK_(breaker1_fd != -1,"Opened a breaker.Expected to succeed. got breaker1_fd=%d errno=%d",breaker1_fd,errno);
	breaker2_fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
	maker_fd=open(utils.ParamToString("maker_path").c_str(),O_RDWR);
	TEST_CHECK_(maker_fd != -1,"Opened a maker.Expected to succeed. got maker_fd=%d",maker_fd);
	
	res = write(maker_fd,"7654",4);
	TEST_CHECK_(res != -1,"Maker sets password and should succeed.Got res=%d errno=%d",res,errno);
	
	res = ioctl(maker_fd,ROUND_START,8);
	TEST_CHECK_(res == 1,"Maker starts round. Got res=%d errno=%d",res,errno);
	
	int son = fork();
	if(!son){
		read(my_pipe[0],&msg,4);//makes the son wait in case he starts first
		res = write(breaker2_fd,"3343",4);//should cause son to wait
		TEST_CHECK_(res == -1 && errno == EIO,"Son tries to write after round ends.\n Expected res=%d errno=%d\nGot res=%d errno=%d",-1,EIO,res,errno);
		exit(0);
	}
	res = write(breaker1_fd,"7654",4);
	TEST_CHECK_(res == 1,"Breaker 1 writes his guess and should succeed.Expected res=%d\nGot res=%d errno=%d",1,res,errno);
	write(my_pipe[1],&breaker1_fd,4);
	WaitMs(50);//let the son try to write.
	res = read(maker_fd,buffer,4);
	int cmpres=strcmp(buffer,"2222");
	TEST_CHECK_(res == 1 && cmpres == 0,"Maker read a guess.Expected to read the inputed guess.Got res=%d,cmpres=%d  errno=%d guess_buffer=%s"
					,res,cmpres,errno,buffer);
	res = write(maker_fd,buffer,4);
	TEST_CHECK_(res == 1,"Maker writes a response - should succeed.Got res=%d",res);
	res = read(breaker1_fd,buffer,4);
	cmpres = strcmp(buffer,"2222");
	TEST_CHECK_(res == 1 && cmpres == 0,"Breaker reads feedback.got res=%d and cmpres=%d errno=%d",res,cmpres,errno);
	wait(&msg);
	
	
}

void test_sigint_handler(int signum)
{
	std::cout<<"Received SIGINT...\n";
	
}

void SignalInterruptTest()
{
	int maker_fd,breaker1_fd,res,msg;
	int my_pipe[2];
	pipe(my_pipe);
	char buffer[5]={0};
	breaker1_fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
	TEST_CHECK_(breaker1_fd != -1,"Opened a breaker.Expected to succeed. got breaker1_fd=%d errno=%d",breaker1_fd,errno);
	maker_fd=open(utils.ParamToString("maker_path").c_str(),O_RDWR);
	TEST_CHECK_(maker_fd != -1,"Opened a maker.Expected to succeed. got maker_fd=%d",maker_fd);
	signal(SIGINT,test_sigint_handler);
	res = write(maker_fd,"7654",4);
	TEST_CHECK_(res != -1,"Maker sets password and should succeed.Got res=%d errno=%d",res,errno);
	
	res = ioctl(maker_fd,ROUND_START,8);
	TEST_CHECK_(res == 1,"Maker starts round. Got res=%d errno=%d",res,errno);
	
	int son = fork();
	if(!son){
		std::cout<<"Son-maker waiting..\n";
		res = read(maker_fd,buffer,4);//should cause the maker to wait
		TEST_CHECK_(res == -1 && errno == EINTR,"Maker shouldv'e been interrupted.Expected res=%d errno=%d\nGot res=%d errno=%d",-1,EINTR,res,errno);
		std::cout<<"Son-breaker waiting..\n";
		res = read(breaker1_fd,buffer,4);
		TEST_CHECK_(res == -1 && errno == EINTR,"Breaker shouldv'e been interrupted.Expected res=%d errno=%d\nGot res=%d errno=%d",-1,EINTR,res,errno);
		res = write(breaker1_fd,"3333",4);
		TEST_CHECK_(res == 1,"Breaker writes a guess and should succeed. Got res = %d",res);
		std::cout<<"Son-breaker waiting..\n";
		res = write(breaker1_fd,"3333",4);//should cause him to wait.
		TEST_CHECK_(res == -1 && errno == EINTR,"Breaker shouldv'e been interrupted.Expected res=%d errno=%d\nGot res=%d errno=%d",-1,EINTR,res,errno);
		exit(0);
	}
	WaitMs(150);//let son run.
	kill(son,SIGINT);
	WaitMs(150);
	kill(son,SIGINT);
	WaitMs(150);
	kill(son,SIGINT);
	
	close(breaker1_fd);
	close(maker_fd);
	
	
	
}



/**
ConcurrentGameTest
*/
static short NumOfRound;
Mutex global_mut;
CondVar breaker_cond,maker_cond;



static int remaining_guesses=0,breakers_ready=0,g_num_of_breakers=0;
static short is_breaker_win=0;


void* MakerRoutine(void* maker_data)
{
	maker_data_t* data=(maker_data_t*)maker_data;
	char buffer[5]={0};
	//NumOfRound = 0;
	int res,maker_fd=data->fd,round=1,num_of_iterations=0;
	for(vector<string>::iterator itr = data->passwords.begin();
			itr != data->passwords.end();++itr){
	//Before writing passwords we wait for breakers to be ready..			
	pthread_mutex_lock(&global_mut);
	while(breakers_ready != g_num_of_breakers){
		cout<<"Maker:not enough breakers ready:"<<breakers_ready<<" out of "<<g_num_of_breakers<<" are.Waiting...\n";
		pthread_cond_wait(&maker_cond,&global_mut);
	}
	res = write(maker_fd,(*itr).c_str(),4);
	TEST_CHECK_(res == 1,"Maker writes password %s and should succeed. Got res=%d errno=%d",(*itr).c_str(),res,errno);
	res = ioctl(maker_fd,ROUND_START,10);
	TEST_CHECK_(res == 1,"Maker starts round %d and should succeed. Got res=%d errno=%d",round,res,errno);
	cout<<"Maker has started round "<<round<<"\n";
	is_breaker_win = 0;
	NumOfRound++;
	pthread_cond_broadcast(&breaker_cond);
	pthread_mutex_unlock(&global_mut);
	num_of_iterations = 0;
	while(1){
		res = read(maker_fd,buffer,4);
		TEST_CHECK_(res == 1,"Maker read a guess.Expected to read the inputed guess.Got res=%d  errno=%d guess_buffer=%s"
					,res,errno,buffer);
		errno = 0;
		res = write(maker_fd,buffer,4);
		TEST_CHECK_(res == 1,"Maker writes the response.Got %d errno=%d",res,errno);
		WaitMs(25);
		if(!strcmp(buffer,"2222")){
			break;
		}
		pthread_mutex_lock(&global_mut);
		if(++num_of_iterations == 10 * g_num_of_breakers){//read and wrote all possible feedbacks for that round
			data->expected_score++;
			is_breaker_win = -1;
			pthread_mutex_unlock(&global_mut);
			break;
		}
		pthread_mutex_unlock(&global_mut);
		
	}
	round++;
		
	}
	return 0;
	
}
static int num_of_rounds;
void* BreakerRoutine(void* breaker_data)
{
	breaker_data_t* data=(breaker_data_t*)breaker_data;
	char buffer[5]={0};
	int res,breaker_fd=data->fd,id=data->id;
	vector<string> guesses;
	stringstream ss;
	for(int i = 1;i < num_of_rounds + 1;i++){
		ss<<"breaker"<<id<<"round"<<i;
		utils.LoadToStringVec(ss.str().c_str(),guesses);
		ss.str("");
		std::cout<<"Entering Round "<<i<<" Breaker "<<id<<"\n";
		pthread_mutex_lock(&global_mut);
		breakers_ready++;
		if(breakers_ready == g_num_of_breakers)
			pthread_cond_signal(&maker_cond);
		if(NumOfRound != i)//means we aren't on the right round yet
			pthread_cond_wait(&breaker_cond,&global_mut);
		breakers_ready--;
		pthread_mutex_unlock(&global_mut);
		for(vector<string>::iterator itr = guesses.begin();itr != guesses.end();++itr){
			
			errno = 0;
			res = write(breaker_fd,(*itr).c_str(),4);
			pthread_mutex_lock(&global_mut);
			if(is_breaker_win){//round ended
				pthread_mutex_unlock(&global_mut);
				break;
			}
			pthread_mutex_unlock(&global_mut);
			TEST_CHECK_(res == 1,"Breaker id %d writes his guess=%s. Got res=%d errno=%d",id,(*itr).c_str(),res,errno);
			res = read(breaker_fd,buffer,4);
			TEST_CHECK_(res == 1,"Breaker id %d reads his feedback. Got res=%d errno=%d",id,res,errno);
			if(!strcmp(buffer,"2222")){
				data->expected_score++;
				pthread_mutex_lock(&global_mut);
				is_breaker_win = 1;
				pthread_mutex_unlock(&global_mut);
				break;
			}
			WaitMs(40);
			
			
		}
		
		std::cout<<"Round "<<i<<" Ended. Breaker "<<id<<"\n";
		guesses.clear();
	}
	return 0;
}

void ConcurrentGameTest()
{
	pthread_mutex_init(&global_mut,0);
	pthread_cond_init(&breaker_cond,0);
	pthread_cond_init(&maker_cond,0);
	int num_of_breakers = utils.ParamToInt("num_of_breakers");
	g_num_of_breakers = num_of_breakers;
	breaker_data_t bkData[num_of_breakers];//starting breakers...
	for(int i = 0;i < num_of_breakers;i++){
		bkData[i].id = i;
		bkData[i].fd = open(utils.ParamToString("breaker_path").c_str(),O_RDWR);
		bkData[i].expected_score = 0;
		TEST_CHECK_(bkData[i].fd != -1,"Opening a breaker got %d",bkData[i].fd);
	}
	maker_data_t mkData;
	utils.LoadToStringVec("passwords",mkData.passwords);
	num_of_rounds = mkData.passwords.size();
	mkData.fd = open(utils.ParamToString("maker_path").c_str(),O_RDWR);
	TEST_CHECK_(mkData.fd != -1,"Opened a maker.Got res=%d errno=%d",mkData.fd,errno);
	mkData.expected_score = 0;
	
	pthread_t threads[num_of_breakers+1];
	//creating maker
	pthread_create(&threads[0],0,MakerRoutine,&mkData);
	WaitMs(100);
	for(int i = 1;i < num_of_breakers+1;i++){
		pthread_create(&threads[i],0,BreakerRoutine,&bkData[i-1]);
	}
	
	for(int i=num_of_breakers;i>=0;--i){
		pthread_join(threads[i],0);
	}
	
	int res;
	for(int i = 0;i < num_of_breakers;i++){
		res = ioctl(bkData[i].fd,GET_MY_SCORE,0);
		TEST_CHECK_(res == bkData[i].expected_score,"Breaker id %d points dont match.Expected %d got %d",bkData[i].id,bkData[i].expected_score,res);
	}
	res = ioctl(mkData.fd,GET_MY_SCORE,0);
	TEST_CHECK_(res == mkData.expected_score,"Maker points dont match.Expected %d got %d",mkData.expected_score,res);
	
	pthread_mutex_destroy(&global_mut);
	
}
TEST_LIST ={
		{"BasicMasterBringUp",BasicMasterBringUp},
			{"SimpleGameTest",SimpleGameTest},
			{"BreakerWriteEIOTest",BreakerWriteEIOTest},
			{"SignalInterruptTest",SignalInterruptTest},
			{"ConcurrentGameTest",ConcurrentGameTest},
		{0}
};

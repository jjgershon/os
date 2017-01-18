#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include "mastermind.h"

#include <linux/ctype.h>
#include <linux/config.h>
#include <linux/kernel.h>  	
#include <linux/errno.h>  
#include <linux/types.h> 
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define MASTERMIND_MAJOR 110

DECLARE_WAIT_QUEUE_HEAD(mastermind_waitqueue_r_codebreaker);
DECLARE_WAIT_QUEUE_HEAD(mastermind_waitqueue_r_codemaker);
DECLARE_WAIT_QUEUE_HEAD(mastermind_waitqueue_w_codebreaker);


struct semaphore mastermind_wq_r_codebreaker_sem;
struct semaphore mastermind_wq_r_codemaker_sem;
struct semaphore mastermind_wq_w_codebreaker_sem;



// globals  or semaphore
int num_codebreakers;
int codemaker_exists;
int round_in_progress; 
int codemaker_score;
unsigned long arg_color;
unsigned long num_rounds;
unsigned long num_total_turns;

char password_buffer[4];
char feedback_buffer[4];
char guess_buffer[4];

//char guess_buffer[4];

int feedback_buffer_empty;
int feedback_buffer_full;

int guess_buffer_empty;
int guess_buffer_full;

struct semaphore mastermind_sem;

struct semaphore mastermind_password_buffer_sem;
struct semaphore mastermind_feedback_buffer_sem;
struct semaphore mastermind_guess_buffer_sem;


struct file_operations fops_codemaker;
struct file_operations fops_codebreaker;

int my_release_codemaker(struct inode* inode, struct file* filp);

typedef struct
{
	//int private_key;
	int minor;
	//int score;
	unsigned long rounds;
} my_private_data_codemaker;

typedef struct
{
	//int private_key;
	int minor;
	int turns_number;
	int score;
	unsigned long rounds;

} my_private_data_codebreaker;

/*
This function opens the module for reading/writing 
(you should always open the module with the O_RDWR flag to support both operations).

If the module in question is that of the Codemaker you should make sure that no other Codemaker exists, 
otherwise this function should close the module and return -EPERM.
Codebreakers should get 10 turns (each) by default when opening the module.

*/
int my_open(struct inode* inode, struct file* filp){
	//int minor= MINOR(inode->i_rdev);


	//fix flags to read and write
	filp->f_flags = O_RDWR;
	
	int minor=MINOR( filp->f_dentry->d_inode->i_rdev);

	// codemaker
	
	if (minor==0){
		//if there is already a codemaker 
		down( &mastermind_sem);
		if(codemaker_exists == 1){
			//close the module
			up(&mastermind_sem);
			return -EPERM;
		}
		//there is a codemaker in the game
		codemaker_exists = 1 ;
		up(&mastermind_sem);


        filp->f_op = &fops_codemaker;
        filp->private_data=kmalloc(sizeof(my_private_data_codemaker), GFP_KERNEL);
		if(! filp->private_data){
			return -ENOMEM;
		} 
		my_private_data_codemaker* data = (my_private_data_codemaker*)(filp->private_data);
		data->minor=0;
		//data->score=0;
		data->rounds = num_rounds; //a verifier


	}
	// codebreaker
	if (minor==1){
        filp->f_op = &fops_codebreaker;
	   
	   	// update number of codebreaker
        down( &mastermind_sem );
		num_codebreakers ++ ;
		num_total_turns += 10 ;
		up(&mastermind_sem);

		filp->private_data=kmalloc(sizeof(my_private_data_codebreaker), GFP_KERNEL);
		if(!filp->private_data){
			return -ENOMEM;
		} 
		my_private_data_codebreaker* data = (my_private_data_codebreaker*)(filp->private_data);
		data->minor =  1 ; 
		data->score = 0 ;
		data->turns_number = 10 ;
		data->rounds = num_rounds; 

	}

	return 0;
}

/*
	Attempts to read the contents of the guess buffer.
	If the buffer is empty and no Codebreakers with available turns 
		exist this function should return EOF.
	If the buffer is empty but any Codebreaker exist that can still play
		(has turns available) then the Codemaker should wait until something is written into it.
	If the buffer is full then this function copies its contents into buf (make sure buf is of adequate size).
	Upon success this function return 1.
	Note: This operation should NOT empty the guess buffer.


*/
int win_codebreaker(char* buff){
	int i=0;
	down( &mastermind_sem);
	while(i<4){
		if(buff[i]!='2'){
			up( &mastermind_sem);
			return 0;
		}
		i++;
	}
	up( &mastermind_sem);
	return 1;

}


ssize_t my_read_codemaker(struct file *filp, char *buf, size_t count, loff_t *f_pos){
		down( &mastermind_sem );
		if (round_in_progress == 0){ 
			up(&mastermind_sem);	
			return -EIO;
		}
		up(&mastermind_sem);

		down(&mastermind_sem);
		if(guess_buffer_empty){
			if(( num_codebreakers == 0)||( num_total_turns == 0 )) {
				up(&mastermind_sem);
				return 0; //EOF
			}
			else {
				up(&mastermind_sem);
				down( &mastermind_wq_r_codemaker_sem );
				int wait_check = wait_event_interruptible(mastermind_waitqueue_r_codemaker, guess_buffer_full == 1);
				if ( wait_check == -ERESTARTSYS){
					up( &mastermind_wq_r_codemaker_sem );
					return -EINTR;
				}
				up( &mastermind_wq_r_codemaker_sem );
				down(&mastermind_sem);
			}
		}
		up( &mastermind_sem);
		// check again if round is in progress since we woke up a process
		down( &mastermind_sem );
		if (round_in_progress == 0) { 
			up( &mastermind_sem );
			return -EIO;
		}
		up( &mastermind_sem );

		char resultBuf[4]= {0};
		down(&mastermind_guess_buffer_sem); /// WHY down
		generateFeedback(resultBuf, guess_buffer, password_buffer);
		int res = copy_to_user(buf, resultBuf , 4) ;
		if( res != 0 ) {
			up(&mastermind_guess_buffer_sem);
			return -ENOMEM;
		} 
		up(&mastermind_guess_buffer_sem);
		
		return 1;																				//mettre à jour le round_in_process
}

/*
	Attempts to read the feedback buffer.
	if the Codebreaker has no more turns available 
		this function should immediately return -EPERM.
	If the buffer is empty and no Codemaker exists 
		this function should return EOF.
	If the buffer is empty but a Codemaker is present 
		then the Codebreaker should wait until the buffer is filled by the Codemaker.
	If the buffer is full then this function copies its contents into buf. 
	Additionally if the feedback buffer’s value is “2222” then that means the Codebreaker’s guess was correct, 
		in which case he should earn a point and the round needs to end.
	This operation should return 1 upon success, and empty both the guess buffer and the feedback buffer.

*/

ssize_t my_read_codebreaker(struct file *filp, char *buf, size_t count, loff_t *f_pos){
	down(&mastermind_sem);
	if (round_in_progress == 0) { 
		up(&mastermind_sem);
		return -EIO;
	}	
	up(&mastermind_sem);

	//the feedbakc buffer is empty
	down(&mastermind_sem);
	if(feedback_buffer_empty){
		//codemaker
		if(!codemaker_exists){
			up(&mastermind_sem);
			return 0; //EOF
		}
		//wait until the codemaker gives a feedback
		else{
			up(&mastermind_sem);
			down( &mastermind_wq_r_codebreaker_sem );
			int wait_check = wait_event_interruptible(mastermind_waitqueue_r_codebreaker , feedback_buffer_full == 1);
			if ( wait_check ==  -ERESTARTSYS){
				up( &mastermind_wq_r_codebreaker_sem );
				return -EINTR;
			}
			up(&mastermind_wq_r_codebreaker_sem );
			down(&mastermind_sem);
		}
	}
	up(&mastermind_sem);
	
	// check again if round is in progress since we woke up a process
	down(&mastermind_sem);
	if (round_in_progress == 0) { 
		up(&mastermind_sem);
		return -EIO;
	}
	up(&mastermind_sem);

	//the feedback is full
	down( &mastermind_feedback_buffer_sem);
	int res = copy_to_user(buf, feedback_buffer, 4);
	if( res != 0 ) {
		up(&mastermind_feedback_buffer_sem);
		return -ENOMEM;
	} 

	up(&mastermind_feedback_buffer_sem);

	my_private_data_codebreaker* data = (my_private_data_codebreaker*)(filp->private_data);


	//a verifier
	down(&mastermind_sem);
	data->turns_number -- ;
	num_total_turns -- ;
	up(&mastermind_sem);

	if(win_codebreaker(feedback_buffer)){ //a verifier pas de lock
		down( &mastermind_sem);	
		round_in_progress = 0;
		up(&mastermind_sem);
		data->score++;
	}
	if( (data -> turns_number == 0) && ( num_total_turns == 0 ) && (! win_codebreaker(feedback_buffer)) ){
		//codemaker win
		down( &mastermind_sem);
		round_in_progress = 0;
		codemaker_score ++ ; 
		up(&mastermind_sem);
	}
	
	//say that the guess buffer and the feedback buffer are empty now
	down( &mastermind_sem);
	guess_buffer_empty = 1 ;
	guess_buffer_full = 0 ;
	wake_up_interruptible ( &mastermind_waitqueue_w_codebreaker);
	feedback_buffer_empty = 1; 
	feedback_buffer_full = 0;
	up(&mastermind_sem);	

	return 1;
}


/*
If the round hasn't started - this function writes the contents of buf into the password buffer.
While a round is in progress - attempts to write the contents of buf into the feedback buffer.
The contents of buf should be generated using the generateFeedback function prior to writing.
If the feedback buffer is full then the function should immediately return -EBUSY.
Returns 1 upon success.

*/
ssize_t my_write_codemaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos){

if(count != 4){
 	return -EINVAL;
}

//the round hasn't started
down( &mastermind_sem);
if (round_in_progress == 0) { 
	up( &mastermind_sem);
	down( &mastermind_password_buffer_sem);
	int res = copy_from_user (password_buffer, buf, count);
	if ( res != 0) {
		up(&mastermind_password_buffer_sem);
		return -ENOMEM;
	}
	up(&mastermind_password_buffer_sem);
	return 1;

}
up( &mastermind_sem);

down( &mastermind_sem);
if( feedback_buffer_full == 1){
	up( &mastermind_sem);
	return -EBUSY;
}
up( &mastermind_sem);

	//copy buff to feedback buffer
down( &mastermind_feedback_buffer_sem);
int res = copy_from_user(feedback_buffer, buf, count);
 if( res !=0){
 	up(&mastermind_feedback_buffer_sem);
 	return -ENOMEM;
 } //  ou utiliser generate
 up(&mastermind_feedback_buffer_sem);


 down( &mastermind_sem);
 feedback_buffer_full = 1;
 feedback_buffer_empty = 0;
 wake_up_interruptible( &mastermind_waitqueue_r_codebreaker);
 up( &mastermind_sem);

return 1;
}
	

/*

Attempts to write the contents of buf into the guess buffer.
If buf contains an illegal character (one which exceeds the specified range of colors) then this function should return -EINVAL.
If the guess buffer is full and no Codemaker exists then this function should return EOF. 
If the guess buffer is full but a Codemaker exists then the Codebreaker should wait until it is emptied 
	(you may assume that the Codebreaker who filled the buffer will eventually empty it). ??? WHAT
Returns 1 upon success.


*/

ssize_t my_write_codebreaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos){
	down(&mastermind_sem);
	if (round_in_progress == 0) { 
		up(&mastermind_sem);
		return -EIO;
	}	
	up(&mastermind_sem);

	if(count != 4){
	 	return -EINVAL;
	}

	my_private_data_codebreaker* data = (my_private_data_codebreaker*)(filp->private_data);

	down(&mastermind_sem);
	if(num_rounds > data->rounds){
		data->rounds = num_rounds;
		// 	update number of turns
		num_total_turns -= data->turns_number;
		data->turns_number = 10;
		num_total_turns += 10;	
	}
	up( &mastermind_sem);
	
	down(&mastermind_sem);
	if(data->turns_number <= 0){
		up( &mastermind_sem);
		return -EPERM;
	}
	up( &mastermind_sem);

	int i=0;
	down(&mastermind_sem);
	for (i=0; i<4 ; i ++){
		if((buf[i] - '0' >= arg_color) || buf[i] - '0' < 0){
			up( &mastermind_sem);
			return -EINVAL;
		}
	}
	up( &mastermind_sem);

	down(&mastermind_sem);
	if ((guess_buffer_full == 1) && ( codemaker_exists == 0)){
		up( &mastermind_sem);
		return 0; //EOF
	}
	up( &mastermind_sem);

	down(&mastermind_sem);
	if((guess_buffer_full == 1) && ( codemaker_exists == 1)){
			up( &mastermind_sem);
			down( &mastermind_wq_w_codebreaker_sem );
			int wait_check = wait_event_interruptible ( mastermind_waitqueue_w_codebreaker , guess_buffer_empty == 1 );
			if( wait_check ==  -ERESTARTSYS){
				up( &mastermind_wq_w_codebreaker_sem );
				return -EINTR;
			}
			up( &mastermind_wq_w_codebreaker_sem );
			down(&mastermind_sem);
	}
	up( &mastermind_sem);
	// check again if round is in progress since we woke up a process
	
	down(&mastermind_sem);
	if (round_in_progress == 0) { 
		up(&mastermind_sem);
		return -EIO;
	}	
	up(&mastermind_sem);

	down(&mastermind_sem);
	if(num_rounds > data->rounds){
		data->rounds = num_rounds;
		// 	update number of turns
		num_total_turns -= data->turns_number;
		data->turns_number = 10;
		num_total_turns += 10;	
	}
	up( &mastermind_sem);
	
	down( &mastermind_guess_buffer_sem);
	int res = copy_from_user (guess_buffer, buf, count) ;
	if( res != 0){
		up (&mastermind_guess_buffer_sem);
		return -ENOMEM;
	}
	up (&mastermind_guess_buffer_sem);
	
	down(&mastermind_sem);
	guess_buffer_full = 1;
	guess_buffer_empty = 0;
	wake_up_interruptible ( &mastermind_waitqueue_r_codemaker); 
	up(&mastermind_sem);
	
	return 1;

}

/*

This function is not needed in this exercise,
 but to prevent the OS from generating a default implementation 
 always return -ENOSYS when invoked.

*/
loff_t my_llseek(struct file *filp, loff_t a, int num){

	return -ENOSYS;
}

/*
The Device Driver should support the following commands, as defined in mastermind.h:
â ROUND_START -
Starts a new round, with a colour-range specified in arg.
If 4>arg or 10<arg then this function should return -EINVAL.
If a round is already in progress this should return -EBUSY.
This command can only be initiated by the Codemaker, if a Codebreaker attempts to use it this function should return -EPERM
â GET_MY_SCORE -
Returns the score of the invoking process.
In case of any other command code this function should return -ENOTTY.
*/

int my_ioctl_codemaker(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg) {

	switch(cmd) {
		case ROUND_START:
          	
			if( arg < 4 || arg > 10){
				return -EINVAL;
			}

			down(&mastermind_sem);
			arg_color = arg;
			up(&mastermind_sem);

			down(&mastermind_sem);
			if(round_in_progress == 1){
				up(&mastermind_sem);
				return -EBUSY;
			}
			up(&mastermind_sem);

			down(&mastermind_sem);
			if( num_codebreakers <= 0){
				up(&mastermind_sem);
				return -EPERM;
			}
			up(&mastermind_sem);

			down( &mastermind_sem);
			round_in_progress = 1;
			num_rounds++;
			up(&mastermind_sem);
		
			break;

		case GET_MY_SCORE:
			return codemaker_score;
			break;

		default: return -ENOTTY;
	}
	return 1;  //WHAT TO RETURN ??????
}


int my_ioctl_codebreaker(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg) {

	my_private_data_codebreaker* data = (my_private_data_codebreaker*)(filp->private_data);
	switch(cmd) {
		case ROUND_START:
           return -EPERM;
           break;

		case GET_MY_SCORE:

			return data->score;
			break;

		default: return -ENOTTY;
	}
	return 0;
}

/*
 	This function should close the module for the invoker,
 	freeing used data and updating global variables accordingly.
	The Codemaker may only release the module when the round is over, 
	if the round is in progress this function should return -EBUSY to an invoking Codemaker.
	You may assume that Codebreakers only invoke this function after playing a full turn 
	(I.E. - writing to the guess buffer AND reading from the feedback buffer)
*/
int my_release_codemaker(struct inode* inode, struct file* filp){
	
	// the round is in progress
	down( &mastermind_sem);
	if(round_in_progress==1){
		up( &mastermind_sem);
		return -EBUSY;
	}

	//updating global variables
	
	codemaker_exists = 0 ;
	codemaker_score = 0 ;
   	//round_in_progress = 0 ;
	up( &mastermind_sem);

	kfree(filp->private_data);
	return 0;
}

int my_release_codebreaker(struct inode* inode, struct file* filp){

	//updating global variables
	my_private_data_codebreaker* data = (my_private_data_codebreaker*)(filp->private_data);
	down( &mastermind_sem);
	num_total_turns -= data->turns_number;
	num_codebreakers -- ;
	if ( num_total_turns == 0) {
		codemaker_score ++ ;  // a verifier
		guess_buffer_empty = 1;
		guess_buffer_full = 0;
		feedback_buffer_empty = 1 ;
		feedback_buffer_full = 0 ;
		round_in_progress = 0 ; 
	}
	up( &mastermind_sem);
	kfree(filp->private_data);
	return 0;
}



struct file_operations fops_codemaker = {
	owner: THIS_MODULE,
	open: my_open,
	release: my_release_codemaker,
	read: my_read_codemaker,
	write: my_write_codemaker,
	llseek: my_llseek,
	ioctl: my_ioctl_codemaker,
};

struct file_operations fops_codebreaker = {
	owner: THIS_MODULE,
	open: my_open,
	release: my_release_codebreaker,
	read: my_read_codebreaker,
	write: my_write_codebreaker,
	llseek: my_llseek,
	ioctl: my_ioctl_codebreaker,
};

//for the init module
MODULE_LICENSE("GPL");

int init_module(void){

	int my_major;
	my_major = register_chrdev (MASTERMIND_MAJOR, "mastermind", &fops_codemaker);
	if (my_major < 0)
		return my_major;


	// semaphore initialization
	init_MUTEX( &mastermind_sem);
	init_MUTEX( &mastermind_password_buffer_sem);
	init_MUTEX( &mastermind_feedback_buffer_sem);
	init_MUTEX( &mastermind_guess_buffer_sem);

 	init_MUTEX( &mastermind_wq_r_codebreaker_sem );
	init_MUTEX( &mastermind_wq_r_codemaker_sem );
	init_MUTEX( &mastermind_wq_w_codebreaker_sem );


	// global variables nitialization
	num_codebreakers = 0 ;
	codemaker_exists = 0 ;
	round_in_progress= 0 ;
	arg_color=0;
	codemaker_score = 0;

	feedback_buffer_empty = 1 ;
	feedback_buffer_full = 0 ; 

	guess_buffer_empty = 1 ;
	guess_buffer_full = 0 ;

	password_buffer[0]='0' ;
	password_buffer[1]='0';
	password_buffer[2]='0';
	password_buffer[3]='0';

	num_rounds=0;
	num_total_turns = 0 ;

	return 0 ;
}

void cleanup_module(void){
	
	unregister_chrdev (MASTERMIND_MAJOR, "mastermind");
}

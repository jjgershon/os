#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/ctype.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include "mastermind.h"
#define MODULE_NAME "MASTERMIND"
#define BUF_SIZE 4

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonathan&Ohad");

int my_open (struct inode *inode, struct file *filp);
ssize_t my_read_maker(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t my_write_maker(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
ssize_t my_read_breaker(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t my_write_breaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
loff_t my_llseek(struct file *filp, loff_t a, int num);
int my_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
int my_release(struct inode *inode, struct file *filp);

// Globals:
static int my_major = 0;
int game_curr_round = 0;
int num_of_players = 0;
int maker_exists = 0;
int guess_buffer_is_full = 0;
int round_started = 0;
int result_buffer_is_full = 0;


// locks:
spinlock_t lock_codeBuf;
spinlock_t lock_num_of_players;
spinlock_t lock_resultBuf;
spinlock_t lock_game_curr_round;
spinlock_t lock_round_started;
spinlock_t lock_guess_buffer_is_full;
spinlock_t lock_result_buffer_is_full;

struct semaphore lock_breakers_guessBuf;

wait_queue_head_t maker_guess_queue;

wait_queue_head_t breaker_result_queue;

// parameters to the module
char* codeBuf;
char* guessBuf;
char* resultBuf; // feedback buffer

MODULE_PARM(codeBuf,"s");
MODULE_PARM(guessBuf,"s");
MODULE_PARM(resultBuf,"s");


struct maker_private_data_t {
    int points;
};

typedef struct maker_private_data_t maker_private_data;

struct breaker_private_data_t {
    int points;
    int guesses;
    int curr_round;
    int i_write;
};

typedef struct breaker_private_data_t breaker_private_data;


struct file_operations my_fops_maker = {
    .open=      my_open,
    .release=   my_release,
    .read=      my_read_maker,
    .write=     my_write_maker,
    .llseek=    my_llseek,
    .ioctl=     my_ioctl,
    .owner=     THIS_MODULE
};

struct file_operations my_fops_breaker = {
    .open=      my_open,
    .release=   my_release,
    .read=      my_read_breaker,
    .write=     my_write_breaker,
    .llseek=    my_llseek,
    .ioctl=     my_ioctl,
    .owner=     THIS_MODULE
};


int init_module(void)
{
    printk("\ninitializing module\n");

    my_major = register_chrdev(my_major, MODULE_NAME, &my_fops_maker);

    // can't get dynamic major
    if (my_major < 0) {
        return my_major;
    }

    // allocate buffers
    codeBuf = kmalloc(sizeof(char)*BUF_SIZE, GFP_KERNEL);
    if (!codeBuf) {
        return -ENOMEM;
    }

    guessBuf = kmalloc(sizeof(char)*BUF_SIZE, GFP_KERNEL);
    if (!guessBuf) {
        kfree(codeBuf);
        return -ENOMEM;
    }

    resultBuf = kmalloc(sizeof(char)*BUF_SIZE, GFP_KERNEL);
    if (!resultBuf) {
        kfree(guessBuf);
        kfree(codeBuf);
        return -ENOMEM;
    }

    game_curr_round = 0;
    num_of_players = 0;
    maker_exists = 0;
    guess_buffer_is_full = 0;
    round_started = 0;
    result_buffer_is_full = 0;

    spin_lock_init(&lock_num_of_players);
    spin_lock_init(&lock_guess_buffer_is_full);
    spin_lock_init(&lock_resultBuf);
    spin_lock_init(&lock_round_started);
    spin_lock_init(&game_curr_round);
    spin_lock_init(&lock_result_buffer_is_full);

    init_MUTEX(&lock_breakers_guessBuf);
    init_waitqueue_head(&maker_guess_queue);
    init_waitqueue_head(&breaker_result_queue);

    printk("\nmodule loaded successfuly\n");

    return 0;
}


void cleanup_module(void)
{
    unregister_chrdev(my_major, MODULE_NAME);

    // deallocate all the buffers
    kfree(codeBuf);
    kfree(guessBuf);
    kfree(resultBuf);
    return;
}


int my_open (struct inode *inode, struct file *filp)
{
    // if (!inode || !filp) {

    // }

    printk("\nDEBUG: entered my_open\n");

    // check flag
    if (!filp->f_flags & O_RDWR){
    	printk("in function my_open: doesn't have O_RDWR permissions.\n");
        return -EPERM;
    }

    // minor = 0, the inode is a codemaker
    if (MINOR(inode->i_rdev == 0)) {
        maker_private_data* maker_data = filp->private_data;
        printk("\nDEBUG: trying to open maker\n");
    	if (maker_exists == 1) {
            printk("in function my_open: maker already exists.\n");
            return -EPERM;
    	}

        filp->f_op = &my_fops_maker;
        filp->private_data = (maker_private_data*)kmalloc(sizeof(struct maker_private_data_t), GFP_KERNEL);
        if (!filp->private_data) {
        	printk("in function my_open: allocating maker's filp->private_data failed.\n");
            return -ENOMEM;
        }
        maker_data->points = 0;
        maker_exists = 1;
    }

    // minor = 1, the inode is a codemaker
    if (MINOR(inode->i_rdev == 1)) {
        breaker_private_data* breaker_data = filp->private_data;

        filp->f_op = &my_fops_breaker;
        filp->private_data = (breaker_private_data*)kmalloc(sizeof(struct breaker_private_data_t), GFP_KERNEL);
        if (!filp->private_data) {
        	printk("in function my_open: allocating breaker's filp->private_data failed.\n");
            return -ENOMEM;
        }
        breaker_data->points = 0;
        breaker_data->guesses = 10;
        breaker_data->curr_round = game_curr_round;
        spin_lock(&lock_num_of_players);
        num_of_players++;
        spin_unlock(&lock_num_of_players);
    }
    printk("\nopen finished successfuly\n");
    return 0;
}


ssize_t my_read_maker(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    // guessBuf(kernel mode) ---> buf(user mode)
    // maker reads from guess buffer and writes it into buf


    // round hasn't started yet
    spin_lock(&lock_round_started);
    if (!round_started) {
    	printk("in function my_read_maker: round hasn't started yet.\n");
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    // Don't have reading permissions
    if ((filp->f_mode & FMODE_READ) == 0) {
    	printk("in function my_read_maker: Don't have reading permissions.\n");
        return -EACCES; // make sure that this is the correct error
    }

    // guess buffer is empty
    if (!guess_buffer_is_full) {
        // there isn't any breaker that can play
        if (!num_of_players) {
        	printk("in function my_read_maker: guessBuf is empty and there are no breakers.\n");
            return 0; //EOF
        // there is a breaker that can play
        } else {
            // wait until breaker finished writing
            wait_event_interruptible(maker_guess_queue, guess_buffer_is_full == 1);
        }
    }

    // copy count from kernel buffer to user space buffer.
    // remember that the buff argument is a user-space pointer,
    // thus we need to use copy_to_user.
    // copy_to_user returns 0 on success
    if (copy_to_user(buf, &guessBuf, count) != 0) {
    	printk("in function my_read_maker: copy_to_user failed.\n");
        return -EFAULT;
    }

    return 1;
}


ssize_t my_write_maker(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    printk("my_write_maker\n");
    // reads from buf and writes it into result

    // If the round hasn’t started - write the contents of buf into the
    // password buffer:
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_lock(&lock_codeBuf);
        if (copy_from_user(&codeBuf, buf, count) != 0) {
            spin_unlock(&lock_round_started);
            spin_unlock(&lock_write_codeBuf);
            printk("staring round-> copy_from_user failed \n");
            return -EFAULT;
        }
        spin_unlock(&lock_codeBuf);
        spin_unlock(&lock_round_started);
        printk("staring round \n");
        return 1;
    }
    spin_unlock(&lock_round_started);

    // round started
    spin_lock(&lock_result_buffer_is_full);
    if (result_buffer_is_full == 1) {
        spin_unlock(&lock_result_buffer_is_full);
        printk("round started -> buffer is full\n");
        return -EBUSY;
    }
    spin_unlock(&lock_result_buffer_is_full);

    // generateFeedback returns 1 if guessBuf and codeBuff are identical
    // and returns 0 otherwise.

    // insert feedback into resultBuf
    // generateFeedback(resultBuf, guessBuf, codeBuf);

    if (copy_from_user(&resultBuf, buf, count) != 0) {
        printk("write result to buff -> copy_to_user failed\n");
        return -EFAULT;
    }

    spin_lock(&lock_result_buffer_is_full);
    result_buffer_is_full = 1;
    spin_unlock(&lock_result_buffer_is_full);

    wake_up_interruptible(&breaker_result_queue);

    printk("success- feedback was written to buf\n");
    return 1;
}



ssize_t my_read_breaker(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    // resultBuf(kernel mode) ---> buf(user mode)

    breaker_private_data* breaker_data = filp->private_data;

    // make sure that the current breaker is the one that wrote to guess buffer last
    if (!breaker_data->i_write) {
    	printk("in function my_read_breaker: wrong breaker- i_write = 0.\n");
        return -EPERM; // make sure that this is the correct error
    }

    if ((filp->f_mode & FMODE_READ) == 0) {
        printk("in function my_read_breaker: Does not have reading permissions\n");
        return -EACCES;
    }

    // round hasn't started yet
    spin_lock(&lock_round_started);
    if (!round_started) {
    	printk("in function my_read_breaker: round hasn't started yet.\n");
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    if (breaker_data->guesses <= 0) {
    	printk("in function my_read_breaker: breaker has 0 guesses left.\n");
        return -EPERM;
    }

    if (!result_buffer_is_full) {
        // maker does not exists
        if (!maker_exists) {
            printk("in function my_read_breaker: maker does not exist. return EOF.\n");
            return 0; //EOF
        // a maker exists
        } else {
            // wait until maker finishes writing the feedback
            wait_event_interruptible(breaker_result_queue, result_buffer_is_full == 1);
        }
    }

    //spin_lock(&lock_resultBuf);

    // copying from resultBuf to buf
    if (copy_to_user(buf, &resultBuf, count) != 0) {
    	printk("in function my_read_breaker: copy_to_user failed.\n");
    	//spin_unlock(&lock_resultBuf);
        return -EFAULT;
    }

    int i;
    int guess_is_correct = 1;
    for (i = 0; i < 4; i++) {
        if (resultBuf[i] != '2') {
            guess_is_correct = 0;
        }
    }

    if (guess_is_correct == 1) {
        breaker_data->points++;
        spin_lock(&lock_round_started);
        round_started = 0;
        spin_unlock(&lock_round_started);
        printk("The guess was correct!\n");
        while (&lock_breakers_guessBuf.wait) {
            up(&lock_breakers_guessBuf);
        }
    }

    // empty guessBuf and resultBuf
    spin_lock(&lock_guess_buffer_is_full);
    guess_buffer_is_full = 0;
    spin_unlock(&lock_guess_buffer_is_full);

    spin_lock(&lock_result_buffer_is_full);
    result_buffer_is_full = 0;
    spin_unlock(&lock_result_buffer_is_full);

    //spin_unlock(&lock_resultBuf);

    // unlock
    up(&lock_breakers_guessBuf);
    return 1;
}


ssize_t my_write_breaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    // buf(user mode) ---> guessBuf(kernel mode)

    breaker_private_data* breaker_data = filp->private_data;

    // round hasn't started yet
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_unlock(&lock_round_started);
        printk("in function my_write_breaker: round hasn't started yet\n");
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    if ((filp->f_mode & FMODE_WRITE) == 0) {
        printk("in function my_write_breaker: Does not have writing permissions\n");
        return -EACCES;
    }

    // check if buf contains illegal characters
    // int i;
    // for (i = 0; i < 4; i++) {
    //     if ( (*(buf + i)) < '4' || (*(buf + i)) > '10') {
    //     	printk("in function my_write_breaker: buf contains illegal characters\n");
    //         return -EINVAL;
    //     }
    // }

    //spin_lock(&lock_guess_buffer_is_full);
    // guess buffer is full


    if (guess_buffer_is_full == 1) {
        if (!maker_exists) {
        	printk("in function my_write_breaker: maker does not exist.\n");
            //spin_unlock(&lock_guess_buffer_is_full);
            return 0; //EOF
        }
        // } else {
        //     // guessBuf is full, maker exists
        //     // wait until guess buffer is empty

        // }
    }

    if (down_interruptible(&lock_breakers_guessBuf)) {
        return -ERESTARTSYS;
    }

    // breaker might catch the lock after the round ended,
    // in this case, exit the function.
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_unlock(&lock_round_started);
        printk("in function my_write_breaker: round hasn't started yet\n");
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    if (copy_from_user(&guessBuf, buf, count) != 0) {
        up(&lock_breakers_guessBuf);
        printk("in function my_write_breaker: copy_from_user failed.\n");
        return -EFAULT;
    }

    // lowering the number of guesses
    --breaker_data->guesses;

    spin_lock(&lock_guess_buffer_is_full);
    guess_buffer_is_full = 1;
    breaker_data->i_write = 1;
    // wake up maker to read guess
    wake_up_interruptible(&maker_guess_queue);
    spin_unlock(&lock_guess_buffer_is_full);

    return 1;
}

loff_t my_llseek(struct file *filp, loff_t a, int num)
{
    return -ENOSYS;
}


int my_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
        case ROUND_START:
            if (arg < 4 || arg > 10) {
                return -EINVAL;
            }
            spin_lock(&lock_round_started);
            if (round_started == 1) {
                spin_unlock(&lock_round_started);
                return -EBUSY;
            }
            round_started = 1;
            spin_unlock(&lock_round_started);
            // breaker attempts to start a round
            if (MINOR(inode->i_rdev) == 1) {
                return -EPERM;
            }
            spin_lock(&game_curr_round);
            game_curr_round++;
            spin_unlock(&game_curr_round);

        case GET_MY_SCORE:
            if (MINOR(inode->i_rdev) == 0) {
                maker_private_data* maker_data = filp->private_data;
                return maker_data->points;
            }

            if (MINOR(inode->i_rdev) == 1) {
                breaker_private_data* breaker_data = filp->private_data;
                return breaker_data->points;
            }
        default:
            return -ENOTTY;
    }
    return 0;
}


int my_release(struct inode *inode, struct file *filp)
{
    // minor = 0: codemaker
    if (MINOR(inode->i_rdev)==0) {
        spin_lock(&lock_round_started);
        if (round_started == 1) {
            spin_unlock(&lock_round_started);
            return -EBUSY;
        }
        maker_exists = 0;
        kfree(filp->private_data);
        spin_unlock(&lock_round_started);
    }
    // minor = 1: codebreaker
    if (MINOR(inode->i_rdev)==1) {
        breaker_private_data* breaker_data = filp->private_data;
        if (breaker_data->guesses) {
            return -EPERM; // correct????
        }
        kfree(filp->private_data);
        spin_lock(&lock_num_of_players);
        num_of_players--;
        spin_unlock(&lock_num_of_players);
    }
    return 0;
}






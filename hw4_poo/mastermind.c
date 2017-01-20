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
#define BUF_SIZE 5

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
unsigned long range = 0;
int maker_points = 0;
int total_num_of_players = 0;


// locks:
spinlock_t lock_codeBuf;
spinlock_t lock_num_of_players;
spinlock_t lock_resultBuf;
//spinlock_t lock_game_curr_round;
spinlock_t lock_round_started;
spinlock_t lock_guess_buffer_is_full;
spinlock_t lock_result_buffer_is_full;
spinlock_t lock_maker_exists;

spinlock_t lock_maker_points;

spinlock_t lock_maker_wait_queue;

struct semaphore lock_breakers_guessBuf;

wait_queue_head_t maker_guess_queue;

wait_queue_head_t breaker_result_queue;

// parameters to the module
char codeBuf[4] = {0};
char guessBuf[4] = {0};
char resultBuf[4] = {0}; // feedback buffer

//MODULE_PARM(codeBuf,"s");
//MODULE_PARM(guessBuf,"s");
//MODULE_PARM(resultBuf,"s");


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
    my_major = register_chrdev(my_major, MODULE_NAME, &my_fops_maker);

    // can't get dynamic major
    if (my_major < 0) {
        return my_major;
    }

    game_curr_round = -1;
    num_of_players = 0;
    maker_exists = 0;
    guess_buffer_is_full = 0;
    round_started = 0;
    result_buffer_is_full = 0;
    maker_points = 0;
    total_num_of_players = 0;

    spin_lock_init(&lock_num_of_players);
    spin_lock_init(&lock_guess_buffer_is_full);
    spin_lock_init(&lock_resultBuf);
    spin_lock_init(&lock_round_started);
    //spin_lock_init(&game_curr_round);
    spin_lock_init(&lock_result_buffer_is_full);
    spin_lock_init(&lock_maker_exists);

    spin_lock_init(&lock_maker_points);

    spin_lock_init(&lock_maker_wait_queue);

    init_MUTEX(&lock_breakers_guessBuf);
    init_waitqueue_head(&maker_guess_queue);
    init_waitqueue_head(&breaker_result_queue);

    return 0;
}


void cleanup_module(void)
{
    unregister_chrdev(my_major, MODULE_NAME);

    return;
}


int my_open (struct inode *inode, struct file *filp)
{
    // check flag
    if (!filp->f_flags & O_RDWR){
        return -EPERM;
    }

    // minor = 0, the inode is a codemaker
    if (MINOR(inode->i_rdev) == 0) {
        spin_lock(&lock_maker_exists);
        if (maker_exists == 1) {
            spin_unlock(&lock_maker_exists);
            return -EPERM;
    	}

        filp->f_op = &my_fops_maker;
        filp->private_data = (maker_private_data*)kmalloc(sizeof(struct maker_private_data_t), GFP_KERNEL);
        if (!filp->private_data) {
            spin_unlock(&lock_maker_exists);
            return -ENOMEM;
        }
        maker_private_data* maker_data = filp->private_data;

        spin_lock(&lock_maker_points);
        maker_points = 0;
        spin_unlock(&lock_maker_points);
        maker_data->points = 0;
        maker_exists = 1;
        spin_unlock(&lock_maker_exists);
    }

    // minor = 1, the inode is a codebreaker
    if (MINOR(inode->i_rdev) == 1) {
        filp->f_op = &my_fops_breaker;
        filp->private_data = (breaker_private_data*)kmalloc(sizeof(struct breaker_private_data_t), GFP_KERNEL);
        if (!filp->private_data) {
            return -ENOMEM;
        }
        breaker_private_data* breaker_data = filp->private_data;
        breaker_data->points = 0;
        breaker_data->guesses = 10;

        //spin_lock(&lock_game_curr_round);
        spin_lock(&round_started);
        spin_lock(&lock_num_of_players);
        if (round_started == 1 || game_curr_round == -1) {
            num_of_players++;
        }
        spin_unlock(&lock_num_of_players);
        spin_unlock(&round_started);
        if (game_curr_round > -1) {
            breaker_data->curr_round = game_curr_round;
        }
        //spin_unlock(&lock_game_curr_round);
        total_num_of_players++;
    }
    return 0;
}


ssize_t my_read_maker(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    // guessBuf(kernel mode) ---> buf(user mode)
    // maker reads from guess buffer and writes it into buf

    if (!buf) {
        return -EINVAL;
    }

    // round hasn't started yet
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    // Don't have reading permissions
    if ((filp->f_mode & FMODE_READ) == 0) {
        return -EACCES; // make sure that this is the correct error
    }

    //maker_private_data* maker_data = filp->private_data;

    // guess buffer is empty
    if (!guess_buffer_is_full) {
        // there isn't any breaker that can play
        spin_lock(&lock_num_of_players);
        if (!num_of_players) {

            spin_unlock(&lock_num_of_players);
            return 0; //EOF
        // there is a breaker that can play
        } else {
            spin_unlock(&lock_num_of_players);

            //spin_lock(&lock_maker_wait_queue);
            int wait_res = wait_event_interruptible(maker_guess_queue, guess_buffer_is_full == 1);
            if (wait_res ==  -ERESTARTSYS) {
                //spin_unlock(&lock_maker_wait_queue);
                return -EINTR;
            }
            //spin_unlock(&lock_maker_wait_queue);
        }
    }

    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    generateFeedback(resultBuf, guessBuf, codeBuf);

    int res = copy_to_user(buf, resultBuf, sizeof(resultBuf));
    if (res != 0) {
        return res;
    }

    return 1;
}


ssize_t my_write_maker(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    // reads from buf and writes it into result

    if (!buf) {
        return -EINVAL;
    }

    // If the round hasn’t started - write the contents of buf into the
    // password buffer:
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_lock(&lock_codeBuf);

        if (count != sizeof(codeBuf)) {
            spin_unlock(&lock_codeBuf);
            spin_unlock(&lock_round_started);
            return -EINVAL;
        }

        int res = copy_from_user(codeBuf, buf, count);
        if (res != 0) {
            spin_unlock(&lock_codeBuf);
            spin_unlock(&lock_round_started);
            return res;
        }
        spin_unlock(&lock_codeBuf);
        spin_unlock(&lock_round_started);
        return 1;
    }
    spin_unlock(&lock_round_started);

    // round started
    spin_lock(&lock_result_buffer_is_full);
    if (result_buffer_is_full == 1) {
        spin_unlock(&lock_result_buffer_is_full);
        return -EBUSY;
    }
    spin_unlock(&lock_result_buffer_is_full);

    // generateFeedback returns 1 if guessBuf and codeBuff are identical
    // and returns 0 otherwise.

    // insert feedback into resultBuf

    if (count != sizeof(resultBuf)) {
        return -EINVAL;
    }

    int res = copy_from_user(resultBuf, buf, count);

    if (res != 0) {
        return res;
    }

    spin_lock(&lock_result_buffer_is_full);
    result_buffer_is_full = 1;
    spin_unlock(&lock_result_buffer_is_full);

    wake_up_interruptible(&breaker_result_queue);

    return 1;
}



ssize_t my_read_breaker(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    // resultBuf(kernel mode) ---> buf(user mode)

    if (!buf) {
        return -EINVAL;
    }

    // round hasn't started yet
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    breaker_private_data* breaker_data = filp->private_data;

    // make sure that the current breaker is the one that wrote to guess buffer last
    if (!breaker_data->i_write) {
        return -EPERM; // make sure that this is the correct error
    }

    if ((filp->f_mode & FMODE_READ) == 0) {
        return -EACCES;
    }

    if (!result_buffer_is_full) {
        // maker does not exists
        spin_lock(&lock_maker_exists);
        if (!maker_exists) {
            spin_unlock(&lock_maker_exists);
            return 0; //EOF
        // a maker exists
        } else {
            // wait until maker finishes writing the feedback
            spin_unlock(&lock_maker_exists);
            int wait_res = wait_event_interruptible(breaker_result_queue, result_buffer_is_full == 1);
            if (wait_res ==  -ERESTARTSYS) {
                return -EINTR;
            }
        }
    }

    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    // copying from resultBuf to buf
    int res = copy_to_user(buf, resultBuf, sizeof(resultBuf));

    if (res != 0) {
        return res;
    }

    int i;
    int guess_is_correct = 1;
    for (i = 0; i < 4; i++) {
        if (buf[i] != '2') {
            guess_is_correct = 0;
        }
    }

    // breaker guessed correctly
    if (guess_is_correct == 1) {
        breaker_data->points++;
        spin_lock(&lock_round_started);
        round_started = 0;
        spin_unlock(&lock_round_started);
        up(&lock_breakers_guessBuf);
    }

    //
    spin_lock(&lock_num_of_players);
    if (!num_of_players && guess_is_correct == 0) {
        spin_lock(&lock_round_started);
        round_started = 0;
        spin_unlock(&lock_round_started);

        spin_lock(&lock_maker_points);
        maker_points++;
        spin_unlock(&lock_maker_points);
    }
    spin_unlock(&lock_num_of_players);

    // empty guessBuf and resultBuf
    spin_lock(&lock_guess_buffer_is_full);
    guess_buffer_is_full = 0;
    spin_unlock(&lock_guess_buffer_is_full);

    spin_lock(&lock_result_buffer_is_full);
    result_buffer_is_full = 0;
    spin_unlock(&lock_result_buffer_is_full);

    // unlock
    up(&lock_breakers_guessBuf);
    return 1;
}


ssize_t my_write_breaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    // buf(user mode) ---> guessBuf(kernel mode)

    breaker_private_data* breaker_data = filp->private_data;

    if (!buf) {
        return -EINVAL;
    }

    // if (breaker_data->i_write == 1) {
    //     printk("in function my_read_breaker: wrong breaker- i_write = 0.\n");
    //     return -EPERM; // make sure that this is the correct error
    // }

    // round hasn't started yet
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    if (breaker_data->curr_round < game_curr_round) {
        breaker_data->curr_round = game_curr_round;
        spin_lock(&lock_num_of_players);
        num_of_players++;
        spin_unlock(&lock_num_of_players);
        breaker_data->guesses = 10;
        breaker_data->i_write = 0;
    }

    if ((filp->f_mode & FMODE_WRITE) == 0) {
        return -EACCES;
    }

    if (breaker_data->guesses <= 0) {
        return -EPERM;
    }

    // check if buf contains illegal characters
    int i;
    for (i = 0; i < 4; i++) {
        if (buf[i] < '0' || buf[i] > ('0' + range - 1)) {
            return -EINVAL;
        }
    }

    if (guess_buffer_is_full == 1) {
        spin_lock(&lock_maker_exists);
        if (!maker_exists) {
            spin_unlock(&lock_maker_exists);
            return 0; //EOF
        }
        spin_unlock(&lock_maker_exists);
    }

    if (down_interruptible(&lock_breakers_guessBuf)) {
        return -ERESTARTSYS;
    }

    // breaker might catch the lock after the round ended,
    // in this case, exit the function.
    spin_lock(&lock_round_started);
    if (!round_started) {
        up(&lock_breakers_guessBuf);
        spin_unlock(&lock_round_started);
        return -EIO;
    }
    spin_unlock(&lock_round_started);

    if (count != sizeof(guessBuf)) {
        up(&lock_breakers_guessBuf);
        return -EINVAL;
    }

    int res = copy_from_user(guessBuf, buf, count);

    if (res != 0) {
        up(&lock_breakers_guessBuf);
        return res;
    }

    // lowering the number of guesses
    --breaker_data->guesses;

    spin_lock(&lock_num_of_players);
    if (!breaker_data->guesses) {
        num_of_players--;
    }
    spin_unlock(&lock_num_of_players);

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
            // maker:
            if (MINOR(inode->i_rdev) == 0) {

                if (arg < 4 || arg > 10) {
                    return -EINVAL;
                }

                if (total_num_of_players == 0) {
                    return -EPERM;
                }

                range = arg;

                spin_lock(&lock_num_of_players);
                if (game_curr_round > -1) {
                    num_of_players = 0;
                }

                spin_unlock(&lock_num_of_players);

                init_MUTEX(&lock_breakers_guessBuf);
                init_waitqueue_head(&maker_guess_queue);
                init_waitqueue_head(&breaker_result_queue);

                spin_lock(&lock_round_started);
                if (round_started == 1) {
                    spin_unlock(&lock_round_started);
                    return -EBUSY;
                }

                // breaker attempts to start a round
                if (MINOR(inode->i_rdev) == 1) {
                    spin_unlock(&lock_round_started);
                    return -EPERM;
                }

                // maker_exists = 0;
                spin_lock(&lock_guess_buffer_is_full);
                guess_buffer_is_full = 0;
                spin_unlock(&lock_guess_buffer_is_full);

                spin_lock(&lock_result_buffer_is_full);
                result_buffer_is_full = 0;
                spin_unlock(&lock_result_buffer_is_full);

                game_curr_round++;

                round_started = 1;
                spin_unlock(&lock_round_started);
            }
            // breaker:
            if (MINOR(inode->i_rdev) == 1) {
                return -EPERM;
            }

            break;

        case GET_MY_SCORE:
            if (MINOR(inode->i_rdev) == 0) {
                return maker_points;
            }

            if (MINOR(inode->i_rdev) == 1) {
                breaker_private_data* breaker_data = filp->private_data;
                return breaker_data->points;
            }
            break;
        default:
            return -ENOTTY;
    }
    return 1;
}


int my_release(struct inode *inode, struct file *filp)
{
    // minor = 0: codemaker
    if (MINOR(inode->i_rdev) == 0) {
        spin_lock(&lock_round_started);
        if (round_started == 1) {
            spin_unlock(&lock_round_started);
            return -EBUSY;
        }
        round_started = 0;
        spin_lock(&lock_num_of_players);
        num_of_players = 0;
        spin_unlock(&lock_num_of_players);
        game_curr_round = -1;

        guess_buffer_is_full = 0;
        result_buffer_is_full = 0;

        spin_lock(&lock_maker_points);
        maker_points = 0;
        spin_unlock(&lock_maker_points);

        spin_lock(&lock_maker_exists);
        maker_exists = 0;
        spin_unlock(&lock_maker_exists);
        kfree(filp->private_data);
        spin_unlock(&lock_round_started);
    }
    // minor = 1: codebreaker
    if (MINOR(inode->i_rdev) == 1) {
        total_num_of_players--;
        spin_lock(&lock_num_of_players);
        breaker_private_data* breaker_data = filp->private_data;
        if (breaker_data->guesses > 0) {
            num_of_players--;
        }

        if (num_of_players == 0 && breaker_data->guesses > 0) {
            maker_points++;
        }

        spin_lock(&lock_num_of_players);
        if (!num_of_players) {

            init_MUTEX(&lock_breakers_guessBuf);
            init_waitqueue_head(&maker_guess_queue);
            init_waitqueue_head(&breaker_result_queue);

            spin_lock(&lock_round_started);
            round_started = 0;
            spin_unlock(&lock_round_started);
            guess_buffer_is_full = 0;
            result_buffer_is_full = 0;
        }
        spin_unlock(&lock_num_of_players);
        spin_unlock(&lock_num_of_players);
        kfree(filp->private_data);
    }
    return 0;
}






#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/ctype.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm-i386/semaphore.h>

#include "mastermind.h"

#define MODULE_NAME "MASTERMIND"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonathan&Ohad");


// Globals:
static int my_major = 0;
int game_curr_round = 0;
int num_of_players = 0;
int maker_exists = 0;
int guess_buffer_is_full = 0;
int round_started = 0;
int result_buffer_is_full = 0;

// locks:
spinlock_t lock_num_of_players;
spinlock_t lock_guessBuf;
spinlock_t lock_resultBuf;

struct semaphore lock_guess_buffer_is_full;
struct semaphore lock_result_buffer_is_full;


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

typedef struct private_data_t maker_private_data;

struct breaker_private_data_t {
    int points;
    int guesses;
    int curr_round;
    int i_write;
};

typedef struct private_data_t breaker_private_data;


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

    // allocate buffers
    codeBuf = kmalloc(sizeof(char)*CODELENGTH, GFP_KERNEL);
    if(!codeBuf){
        return -ENOMEM;
    }

    guessBuf = kmalloc(sizeof(char)*CODELENGTH, GFP_KERNEL);
    if(!guessBuf){
        kfree(codeBuf);
        return -ENOMEM;
    }

    resultBuf = kmalloc(sizeof(char)*CODELENGTH), GFP_KERNEL;
    if(!resultBuf){
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
    spin_lock_init(&lock_guessBuf);
    spin_lock_init(&lock_resultBuf);
    init_MUTEX(&lock_guess_buffer_is_full);
    init_MUTEX(&lock_result_buffer_is_full);
    
    // I think I exaturated a little bit with all the locks

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

    // check flag
    if (!filp->f_flags & O_RDWR){ // maybe check FMODE_READ and FMODE_WRITE?
        return -EPERM;
    }

    // minor = 0, the inode is a codemaker
    // todo: need to check if a maker already exists
    if (MINOR(inode->i_rdev == 0)) {
        filp->f_op = &my_fops_maker;
        filp->privte_data = (maker_private_data*)kmalloc(sizeof(maker_private_data), GFP_KERNEL);
        if (!filp->private_data) {
            return -ENOMEM;
        }
        filp->privte_data->points = 0;
        maker_exists = 1;
    }

    // minor = 1, the inode is a codemaker
    if (MINOR(inode->i_rdev == 1)) {
        filp->f_op = &my_fops_breaker;
        filp->privte_data = (breaker_private_data*)kmalloc(sizeof(breaker_private_data), GFP_KERNEL);
        if (!filp->private_data) {
            return -ENOMEM;
        }
        filp->privte_data->points = 0;
        filp->privte_data->guesses = 10;
        filp->privte_data->curr_round = game_curr_round;
        spin_lock(&lock_num_of_players);
        lock_num_of_players++;
        spin_unlock(&lock_num_of_players);
    }

    return 0;
}


ssize_t my_read_maker(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    // guessBuf(kernel mode) ---> buf(user mode)
    // maker reads from guess buffer and writes it into buf

    // round hasn't started yet
    // spin_lock(&lock_round_started);
    if (!round_started) {
    	printk("in function my_read_maker: round hasn't started yet.\n");
        // spin_unlock(&lock_round_started);
        return -EIO;
    }
    // spin_unlock(&lock_round_started);

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
            return EOF;
        // there is a breaker that can play
        } else {
            // wait until breaker finished writing
            while (!guess_buffer_is_full) {}
        }
    }

    spin_lock(&lock_guessBuf);

    // copy count from kernel buffer to user space buffer.
    // remember that the buff argument is a user-space pointer,
    // thus we need to use copy_to_user.
    // copy_to_user returns 0 on success
    if (copy_to_user(buf, &guessBuf, count) != 0) {
    	printk("in function my_read_maker: copy_to_user failed.\n");
        return -EFAULT;
    }

    spin_unlock(&lock_guessBuf);
    return 1;
}


ssize_t my_write_maker(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    // reads from buf and writes it into result

    // If the round hasn’t started - write the contents of buf into the
    // password buffer:
    spin_lock(&lock_round_started);
    if (!round_started) {
        spin_lock(&lock_write_codeBuf);
        if (copy_from_user(&codeBuf, buf, count) != 0) {
            spin_unlock(&lock_round_started);
            spin_unlock(&lock_write_codeBuf);
            return -EFAULT;
        }
        spin_unlock(&lock_write_codeBuf);
    }
    spin_unlock(&lock_round_started);

    if (result_buffer_is_full == 1) {
        return -EBUSY;
    }

    // generateFeedback returns 1 if guessBuf and codeBuff are identical
    // and returns 0 otherwise.

    generateFeedback(resultBuf, guessBuf, codeBuf);

    if (copy_to_user(buf, &resultBuf, count) != 0) {
        return -EFAULT;
    }

    return 1;
}



ssize_t my_read_breaker(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    // resultBuf(kernel mode) ---> buf(user mode)

    // make sure that the current breaker is the one that wrote to guess buffer last
    if (!filp->private_data->i_write) {
    	printk("in function my_read_breaker: wrong breaker- i_write = 0.\n");
        return -EPERM; // make sure that this is the correct error
    }

    // round hasn't started yet
    //spin_lock(&lock_round_started);
    if (!round_started) {
    	printk("in function my_read_breaker: round hasn't started yet.\n");
        //spin_unlock(&lock_round_started);
        return -EOF;
    }
    //spin_unlock(&lock_round_started);

    if (filp->private_data->guesses <= 0) {
    	printk("in function my_read_breaker: breaker has 0 guesses left.\n");
        return -EPERM;
    }

    if (!result_buffer_is_full) {
        // maker does not exists
        if (!maker_exists) {
            printk("in function my_read_breaker: maker does not exist\n");
            return EOF;
        // a maker exists
        } else {
            // wait until maker finishes writing the feedback
            while (!result_buffer_is_full) {}
        }
    }

    spin_lock(&lock_resultBuf);

    // copying from resultBuf to buf
    if (copy_to_user(buf, &resultBuf, count) != 0) {
    	printk("in function my_read_breaker: copy_to_user failed.\n");
    	spin_unlock(&lock_resultBuf);
        return -EFAULT;
    }

    int guess_is_correct = 1;
    for (int i = 0; i < 4; i++) {
        if (resultBuf[i] != '2') {
            guess_is_correct = 0;
        }
    }

    //spin_lock(&lock_round_started);
    if (guess_is_correct == 1) {
        filp->private_data->points++;
        round_started = 0;
        printk("The guess was correct!\n");
    }
    //spin_unlock(&lock_round_started);

    // empty guessBuf and resultBuf
    down(&lock_guess_buffer_is_full);
    guess_buffer_is_full = 0;
    up(&lock_guess_buffer_is_full);

    down(&lock_result_buffer_is_full);
    result_buffer_is_full = 0;
    up(&lock_result_buffer_is_full);

    spin_unlock(&lock_resultBuf);
    result 1;
}


ssize_t my_write_breaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    // buf(user mode) ---> guessBuf(kernel mode)

    // round hasn't started yet
    //spin_lock(&lock_round_started);
    if (!round_started) {
        //spin_unlock(&lock_round_started);
        return -EOF;
    }
    //spin_unlock(&lock_round_started);

    if ((filp->f_mode & FMODE_WRITE) == 0) {
        printk("my_write_breaker Does not have writing permissions\n");
        return -EACCES;
    }

    // check if buf contains illegal characters
    for (int i = 0; i < 4; i++) {
        if (buf[i] < '4' || buf[i] > '10') {
            return -EINVAL;
        }
    }

    //down(&lock_guess_buffer_is_full);
    // guess buffer is full
    if (guess_buffer_is_full == 1) {
        if (!maker_exists) {
            //up(&lock_guess_buffer_is_full);
            return EOF;
        } else {
            //up(&lock_guess_buffer_is_full);
            // wait until guess buffer is empty
            while (guess_buffer_is_full == 1) {}
        }
    }

    // maker exists, guess buffer is empty

    // locking the read lock because we don't want the maker to from
    // the guessBuf while a breaker is writing to it.
    spin_lock(&lock_read_guessBuf);

    // locking the write lock because we don't want any other
    // breaker to write into guessBuf.
    down(&lock_write_guessBuf);

    if (copy_from_user(&guessBuf, buf, count) != 0) {
        spin_unlock(&lock_read_guessBuf);
        up(&lock_write_guessBuf);
        return -EFAULT;
    }

    // lowering the number of guesses
    --filp->breaker_private_data->guesses;

    down(&lock_guess_buffer_is_full);
    guess_buffer_is_full = 1;
    up(&lock_guess_buffer_is_full);

    spin_unlock(&lock_read_guessBuf);
    up(&lock_write_guessBuf);
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
            if(MINOR(inode->i_rdev) == 1){
                return -EPERM;
            }
            spin_lock(&game_curr_round);
            game_curr_round++;
            spin_unlock(&game_curr_round);

        case GET_MY_SCORE:
            return filp->private_data->points;
        default:
            return -ENOTTY;
    }
    return 0;
}


int my_release(struct inode *inode, struct file *filp){
    // minor = 0: codemaker
    if (MINOR(inode->i_rdev)==0) {
        spin_lock(&lock_round_started);
        if (round_started == 1) {
            spin_unlock(&lock_round_started);
            return -EBUSY;
        }
        maker_exists = 0;
        kfree(filp->privte_data);
        spin_unlock(&lock_round_started);
    }
    // minor = 1: codebreaker
    if (MINOR(inode->i_rdev)==1) {
        if (flip->private_data->guesses) {
            return -EPERM; // correct????
        }
        kfree(flip->private_data);
        spin_lock(&lock_num_of_players);
        num_of_players--;
        spin_unlock(&lock_num_of_players);
    }
    return 0;
}






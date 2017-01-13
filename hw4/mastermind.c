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
MODULE_AUTHOR("Jonathan&Ohad");


// Globals:
int my_major = 0;
int game_curr_round = 0;
int num_of_players = 0;
spinlock_t lock_num_of_players; // spinlock or semaphore??

int game_started = 0; // Do we even need it??

// parameters to the module
char* codeBuf;
char* guessBuf;
char* resultBuf; // feedback buffer

MODULE_PARM(codeBuf,"s");
MODULE_PARM(guessBuf,"s");
MODULE_PARM(resultBuf,"s");

spinlock_t lock_guessBuf;
spinlock_t lock_resultBuf;

struct maker_private_data_t {
    int points;
};

typedef struct private_data_t maker_private_data;

struct breaker_private_data_t {
    int points;
    int guesses;
    int curr_round;
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
    codeBuf = kmalloc(sizeof(char)*CODELENGTH,GFP_KERNEL);
    if(!codeBuf){
        return -ENOMEM;
    }

    guessBuf = kmalloc(sizeof(char)*CODELENGTH,GFP_KERNEL);
    if(!guessBuf){
        kfree(codeBuf);
        return -ENOMEM;
    }

    resultBuf = kmalloc(sizeof(char)*CODELENGTH),GFP_KERNEL;
    if(!resultBuf){
        kfree(guessBuf);
        kfree(codeBuf);
        return -ENOMEM;
    }

    my_major = 0;
    curr_round = 0;
    num_of_players = 0;
    game_started = 1;

    spin_lock_init(&lock_num_of_players);
    spin_lock_init(&lock_guessBuf);
    spin_lock_init(&lock_resultBuf);

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
    if (MINOR(inode->i_rdev == 0)) {
        filp->f_op = &my_fops_maker;
        filp->privte_data = (maker_private_data*)kmalloc(sizeof(maker_private_data), GFP_KERNEL;
        if (!filp->private_data) {
            return -ENOMEM;
        }
        filp->privte_data->points = 0;
    }

    // minor = 1, the inode is a codemaker
    if (MINOR(inode->i_rdev == 1)) {
        filp->f_op = &my_fops_breaker;
        filp->privte_data = (breaker_private_data*)kmalloc(sizeof(breaker_private_data), GFP_KERNEL;
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
    // Don't have reading permissions
    if ((filp->f_mode & FMODE_READ) == 0) {
        return -EACCES;
    }

    // "filp" is the file pointer and "count" is the size of the requested data to transfer.
    // The "buff" argument points to the user buffer holding the data to be written or
    // the empty buffer where the newly read data should be placed.
    // Finally, "f_pos" is a pointer to a "long offset type" object that indicates the
    // file position the user is accessing. The return value is a "signed size type".

    int pos;
    pos = *f_pos;

    // attempts to read the contents of the guess buffer
    spin_lock(&lock_guessBuf);

    // buf is empty
    if (count == 0) {
        // there isn't any breaker that can play
        if (!num_of_players) {
            return EOF;
        // there is a breaker that can play
        } else {
            // release lock let one of the breakers a chance to write into the buffer
            spin_unlock(&lock_guessBuf);

            //wait, maybe use some sort of kernel condition variable?


        }
    }


    spin_lock(&lock_guessBuf);
    // copy count from kernel buffer to user space buffer.
    // remember that the buff argument is a user-space pointer,
    // thus we need to use copy_to_user.
    // copy_to_user returns 0 on success
    if (copy_to_user(buf, &guessBuf, count) != 0) {
        return -EFAULT; // what error should be returned?
    }

    pos += count;
    filp->f_pos = pos;

    spin_unlock(&lock_guessBuf);
    return count;
}


ssize_t my_write_maker(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{

}

int my_release( struct inode *inode, struct file *filp );

ssize_t my_read_breaker(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t my_write_breaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
loff_t my_llseek(struct file *filp, loff_t a, int num);
int my_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);


switch (cmd) {
    case
}






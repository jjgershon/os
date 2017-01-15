#ifndef _MASTERMIND_H_
#define _MASTERMIND_H_

#include <linux/ioctl.h>

#define MASTERMIND_MAGIC 	'4'
#define ROUND_START			_IOW(MASTERMIND_MAGIC, 0, int)
#define GET_MY_SCORE		_IOW(MASTERMIND_MAGIC, 1, int)

int generateFeedback(char* resultBuf, const char* guessBuf, const char* codeBuf){
	int resultBufIndex=0;
	int guessBufPaired[4];
	int codeBufPaired[4];
	for (int i=0 ; i<4 ; i++) {
		guessBufPaired[i] = 0;
		codeBufPaired[i] = 0;
	}
	for (int i=0 ; i<4 ; i++) {
		if (guessBuf[i] == codeBuf[i]) {
			resultBuf[resultBufIndex] = '2';
			resultBufIndex++;
			guessBufPaired[i] = 1;
			codeBufPaired[i] = 1;
		}
	}
	if (resultBufIndex == 4) {
		return 1;
	}
	for (int i=0 ; i<4 ; i++) {
		if (!guessBufPaired[i]) {
			for (int j=0 ; j<4 ; j++) {
				if (!codeBufPaired[j] && guessBuf[i]==codeBuf[j]) {
					resultBuf[resultBufIndex] = '1';
					resultBufIndex++;
					guessBufPaired[i] = 1;
					codeBufPaired[j] = 1;
				}
			}
		}
	}
	for ( ; resultBufIndex<4 ; resultBufIndex++) {
		resultBuf[resultBufIndex] = '0';
	}
	return 0;
}

int my_open (struct inode *inode, struct file *filp);
ssize_t my_read_maker(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t my_write_maker(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
ssize_t my_read_breaker(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t my_write_breaker(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
loff_t my_llseek(struct file *filp, loff_t a, int num);
int my_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
int my_release(struct inode *inode, struct file *filp);



#endif

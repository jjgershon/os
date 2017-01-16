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
	int i=0,j=0;
	for (i=0 ; i<4 ; i++) {
		guessBufPaired[i] = 0;
		codeBufPaired[i] = 0;
	}
	for (i=0 ; i<4 ; i++) {
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
	for (i=0 ; i<4 ; i++) {
		if (!guessBufPaired[i]) {
			for (j=0 ; j<4 ; j++) {
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

#endif

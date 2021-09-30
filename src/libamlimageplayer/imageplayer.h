#ifndef _LIB_AML_IMAGE_PLAYER_H_
#define _LIB_AML_IMAGE_PLAYER_H_

#include "netConnectionUtil.h"

int initEnv(int framewidth,int frameheight);
int setPictureAttr(int width,int height,int format);
void showBuf(int size,void* pixelbuf);
void release();

#endif // _LIB_AML_IMAGE_PLAYER_H_


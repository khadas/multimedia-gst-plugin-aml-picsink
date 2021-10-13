//netConnectionUtil.h
/**************************************************************************
Copyright:amlogic
Author: Ting.li
Date:2020.11.11
Description:Provide basic functions  to connect westeros
 **************************************************************************/
#ifndef  NET_CONNECTION_UTIL_H
#define NET_CONNECTION_UTIL_H

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <meson_drm.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#include <linux/videodev2.h>
#include <drm/drm_fourcc.h>

#define CONNECTION_WESTEROS_NAME "video"
typedef struct _ScreenSize{
  int frame_w;
  int frame_h;
}ScreenSize;
typedef enum{
  RGB,
  NV12,
}DecodeFormat;
typedef struct _PixelBuf{
  int size;
  void* buf;
}PixelBuf;
typedef struct _DecodedImageInfo{
  int width;
  int height;
  DecodeFormat format;
  PixelBuf buf;
}DecodedImageInfo;

typedef struct _VideoClientConnection{
  const char *name;
  struct sockaddr_un addr;
  int socketFd;
  int serverRefreshRate;
}VideoClientConnection;


#define PLANES_NUM 2
typedef struct _GemBuffer
{
  unsigned int handle[PLANES_NUM];
  unsigned int stride[PLANES_NUM];
  unsigned int offset[PLANES_NUM];
  uint32_t size[PLANES_NUM];
  int fd[PLANES_NUM];
} GemBuffer;

typedef struct _ImagePlayer{
  int drmFd;
  VideoClientConnection* conn;
  DecodedImageInfo *info;
  GemBuffer gemBuf;
  ScreenSize screensize;
}ImagePlayer;

int putS64( unsigned char *p,  int64_t n );
int putU32( unsigned char *p, unsigned n );
VideoClientConnection* setupConnection();
void destroyVideoClientConnection( VideoClientConnection *conn );
int SendFrameVideoClientConnection(ImagePlayer *mplayer);
void sendPauseVideoClientConnection( VideoClientConnection *conn, int pause );
#endif //NET_CONNECTION_UTIL_H

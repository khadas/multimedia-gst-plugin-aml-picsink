#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

#include "imageplayer.h"
#include "meson_drm.h"
#include "drm/drm_fourcc.h"

extern int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);

static ImagePlayer* mplayer;
static pthread_t playtid = 0;
void *y = NULL;
void *uv = NULL;
//#define DEFAULT_DRM_NAME "/dev/dri/renderD128"
#define DEFAULT_DRM_NAME "/dev/dri/card0"
//static int putS64( unsigned char *p,  int64_t n );
//static int putU32( unsigned char *p, unsigned n );
//void destroyVideoClientConnection( VideoClientConnection *conn );
int createGemBuffer(GemBuffer *gemBuf,ScreenSize* screensize );
void Log(const char* name) {
  fprintf(stderr,"%s", name);
}
typedef struct funparameter
{
  int bufsize;
  uint8_t * buffer;
}funparameter;

int checkParam() {
  if (mplayer == NULL || mplayer->info == NULL) {
    fprintf(stderr,"not initial\n");
    return -1;
  }
  if (mplayer->info->format > NV12) {
    fprintf(stderr,"not support this decode buffer\n");
    return -1;
  }
  if (mplayer->info->width > mplayer->screensize.frame_w|| mplayer->info->height > mplayer->screensize.frame_h) {
    fprintf(stderr,"picture is too big to display\n");
    return -1;
  }
  return 0;
}
int prepareDrmFd(ScreenSize* screensize,ImagePlayer* player) {
  const char *drmName;
  drmName= getenv("WESTEROS_SINK_DRM_NAME");
  if ( !drmName )
  {
    drmName= DEFAULT_DRM_NAME;
  }

  int drmFd= open( drmName, O_RDWR );
  if ( drmFd < 0 )
  {
    fprintf(stderr,"Failed to open drm render node");
    return -1;
  }
  player->drmFd = drmFd;
  if ( createGemBuffer( &(player->gemBuf),screensize) < 0 )
  {
    fprintf(stderr,"Failed to allocate gem buffer\n");
    return -1;
  }
  return drmFd;
}
int createGemBuffer(GemBuffer *gemBuf,ScreenSize* info) {
  int rc, i;
  struct drm_meson_gem_create gc;
  for ( i= 0; i < PLANES_NUM; ++i ) {
    gemBuf->fd[i]= -1;
    memset( &gc, 0, sizeof(gc) );
    gc.flags= MESON_USE_VIDEO_PLANE;
    if ( i == 0 )
    {
      gc.size= info->frame_w*info->frame_h;
    }
    else
    {
      //gc.size= info->frame_w*info->frame_h/2;
      gc.size = (info->frame_w+1)*(info->frame_h+1)/2;
    }
    gemBuf->stride[i]= info->frame_w;
    gemBuf->size[i]= gc.size;
    fprintf(stderr,"drmFd is %d",mplayer->drmFd);
    rc= ioctl( mplayer->drmFd, DRM_IOCTL_MESON_GEM_CREATE, &gc );
    if ( rc < 0 )
    {
      fprintf(stderr,"Failed to create gem buffer: plane %d DRM_IOCTL_MESON_GEM_CREATE rc %d %d\n", i, rc,errno);
      return -1;
    }

    gemBuf->handle[i]= gc.handle;
    gemBuf->offset[i]= 0;

    rc= drmPrimeHandleToFD( mplayer->drmFd, gemBuf->handle[i], DRM_CLOEXEC | DRM_RDWR, &gemBuf->fd[i] );
    if ( rc < 0 )
    {
      fprintf(stderr,"Failed to get fd for gem buf plane %d: handle %d drmPrimeHandleToFD rc %d\n",
          i, gemBuf->handle[i], rc );
      return -1;
    }

    fprintf(stderr,"create gem buf plane %d size (%dx%d) %u bytes stride %d offset %d handle %d fd %d\n",
        i, info->frame_w, info->frame_h, gemBuf->size[i],
        gemBuf->stride[i], gemBuf->offset[i], gemBuf->handle[i], gemBuf->fd[i] );
  }
  return 0;
}
void destroyGemBuffer( GemBuffer *gemBuf )
{
  int rc, i;
  struct drm_gem_close gclose;

  for ( i= 0; i <PLANES_NUM; ++i ) {
    if ( gemBuf->fd[i] >= 0 )
    {
      close( gemBuf->fd[i] );
      gemBuf->fd[i]= -1;
    }
    if ( gemBuf->handle[i] > 0 )
    {
      memset( &gclose, 0, sizeof(gclose) );
      gclose.handle= gemBuf->handle[i];
      rc= ioctl( mplayer->drmFd, DRM_IOCTL_GEM_CLOSE, &gclose );
      if ( rc < 0 )
      {
        fprintf(stderr,"Failed to release gem buffer handle %d: DRM_IOCTL_MODE_DESTROY_DUMB rc %d errno %d\n",
            gemBuf->handle[i], rc, errno );
      }
    }
  }
}

void * _showBuf(void *arg)
{
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  funparameter* para;
  para = (funparameter*) arg;
  uint8_t * addr = para->buffer;
  //fprintf(stderr,"on pthread %p thread push param %p and buffer size is%d\n",para,addr,para->bufsize);
  int buffer_size = para->bufsize;
  free(para);
  if (checkParam())
    return NULL;
  int width  = mplayer->info->width;
  int height = mplayer->info->height;
  int frame_w = mplayer->screensize.frame_w;
  int frame_h = mplayer->screensize.frame_h;
  uint8_t yuv[buffer_size];

  fprintf(stderr,"showimage  %p %d %d [%d x%d]\n",addr,frame_w,frame_h,width,height);
  if (mplayer->info->format == RGB)
    buffer_size = height*width+ ceill(1.0*height*width/2);

  if (mplayer->info->format == RGB) {
    int i,j;
    int  r,g,b, Y, U, V;
    int index = 0;
    int yindex = 0;
    int uvindex = width*height;
    fprintf(stderr,"dumpbuf size %d\n",(height*width*3+1)/2);
    for (j=0;j<height;j++) {
      for (i=0;i<width;i++) {
        r = (*((uint8_t*)addr+index)) & 0xff;
        g = (*((uint8_t*)addr+index+1)) & 0xff;
        b = (*((uint8_t*)addr+index+2))&0xff;

        Y = (int) (0.299*r +0.587*g +0.114*b);
        V = (int) (-0.1687*r -0.3313*g +0.5*b +128);
        U = (int) (0.5*r -0.4187*g -0.0813*b +128);
        yuv[yindex++]= (uint8_t) ((Y < 0) ? 0 : ((Y > 255) ? 255 : Y));
        if (j % 2 == 0 && i % 2 == 0) {
          yuv[uvindex++] = (uint8_t) ((V < 0) ? 0 : ((V > 255) ? 255 : V));
          yuv[uvindex++] = (uint8_t) ((U < 0) ? 0 : ((U > 255) ? 255 : U));
        }
        index+=3;
      }
    }
  }else {
    memcpy(yuv,addr,buffer_size);
  }

  size_t len1 = frame_w*frame_h;
  size_t len2 = (frame_w+1)*(frame_h+1)/2;
  fprintf(stderr,"before mmap error %d\n",errno);
  y = (void*)mmap(0,len1, PROT_READ|PROT_WRITE,MAP_SHARED, mplayer->gemBuf.fd[0], 0);

  uv = (void*)mmap(0,len2, PROT_READ|PROT_WRITE,MAP_SHARED, mplayer->gemBuf.fd[1], 0);

  if (uv == MAP_FAILED || y == MAP_FAILED) {
    fprintf(stderr,"mmap error %d %p %p\n",errno,y,uv);
    return NULL;
  }
  fprintf(stderr,"mmap ok %p %p \n",uv,y);
  memset(uv,0x80,len2);
  memset(y,0x0,len1);

  int wbgsize = (frame_w-width)/2;
  int hbgsize = (frame_h-height)/2;
  int bgsize_y = wbgsize+hbgsize*frame_w;
  int bgsize_uv = (wbgsize+1)/2*2+(hbgsize+1)/2*frame_w;
  for (int i=0;i<height;i++) {
    memcpy(y+i*frame_w+bgsize_y,yuv+i*width,width);
  }
  for (int i=0;i<(height+1)/2;i++) {
    memcpy(uv+i*frame_w+bgsize_uv,yuv+width*height+i*width,width);
  }
  msync((void *)y, len1, MS_SYNC);
  msync((void *)uv, len2, MS_SYNC);

  sendPauseVideoClientConnection(mplayer->conn,1);
  int step = 0;
  while (1 && step < 4) {
    step++;
    SendFrameVideoClientConnection(mplayer);
    usleep(1000000L/5);
  }

  return NULL;
}

void showBuf(int size,void* addr) {
  int ret = 0;
  fprintf(stderr,"create pthread thread push param %p\n",addr);
  funparameter * para = (funparameter *)malloc(sizeof(funparameter));
  para->bufsize = size;
  para->buffer = (uint8_t*)addr;
  //fprintf(stderr,"create pthread set param %d %p %p\n",para->bufsize,para->buffer,para);
  ret = pthread_create(&playtid,NULL,_showBuf,para);
  if (ret < 0) {
    fprintf(stderr,"pthread create err %d\n",errno);
    //return -1;
  }
}

int initEnv(int framewidth,int frameheight) {
  release();
  ScreenSize size={framewidth,frameheight};
  mplayer = (ImagePlayer*)calloc( 1, sizeof(ImagePlayer));
  if (mplayer == NULL) {
    fprintf(stderr,"malloc fail, errno %d\n",errno);
    return -1;
  }
  mplayer->screensize = size;
  for (int j= 0; j < PLANES_NUM; ++j )
  {
    mplayer->gemBuf.fd[j]= -1;
  }
  mplayer->conn = setupConnection();
  if (mplayer->conn == NULL) {
    release();
    return -1;
  }
  mplayer->drmFd = prepareDrmFd(&size,mplayer);
  if (mplayer->drmFd < 0)
  {
    release();
    return -1;
  }
  return 0;
}
int setPictureAttr(int width,int height,int format)
{
  fprintf(stderr,"setPictureAttr %dx %d\n",width,height);
  if (format > NV12) {
    fprintf(stderr,"not support this decode buffer\n");
    return -1;
  }
  DecodedImageInfo *info = (DecodedImageInfo*)calloc( 1, sizeof(DecodedImageInfo));
  if (info == NULL) {
    release();
    fprintf(stderr,"malloc fail\n");
    return -1;
  }
  mplayer->info = info;
  info->width= width;
  info->height = height;
  info->format = format;
  return 0;
}

void release()
{
  if (playtid>0) {
    pthread_cancel(playtid);
    pthread_join(playtid, NULL);
    playtid = 0;
  }
  if (mplayer != NULL) {
    if (y != NULL)
      munmap((void *)y, mplayer->screensize.frame_w*mplayer->screensize.frame_h);
    if (uv != NULL)
      munmap((void *)uv, (mplayer->screensize.frame_w+1)*(mplayer->screensize.frame_h+1)/2);
    if (mplayer->info != NULL) {
      free (mplayer->info);
      mplayer->info = NULL;
    }
    if (mplayer->conn != NULL) {
      destroyVideoClientConnection(mplayer->conn);
      mplayer->conn = NULL;
    }
    destroyGemBuffer(&mplayer->gemBuf);
    if (mplayer->drmFd > 0) {
      //fprintf(stderr,"closed drmFd is %d\n",mplayer->drmFd);
      close(mplayer->drmFd);
      mplayer->drmFd = 0;
    }
    free( mplayer);
    mplayer = NULL;
  }
}



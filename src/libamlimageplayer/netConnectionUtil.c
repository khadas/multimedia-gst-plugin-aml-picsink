#include <stdlib.h>

#include "netConnectionUtil.h"

int putS64( unsigned char *p,  int64_t n )
{
  p[0]= (((uint64_t)n)>>56);
  p[1]= (((uint64_t)n)>>48);
  p[2]= (((uint64_t)n)>>40);
  p[3]= (((uint64_t)n)>>32);
  p[4]= (((uint64_t)n)>>24);
  p[5]= (((uint64_t)n)>>16);
  p[6]= (((uint64_t)n)>>8);
  p[7]= (((uint64_t)n)&0xFF);

  return 8;
}

int putU32( unsigned char *p, unsigned n )
{
  p[0]= (n>>24);
  p[1]= (n>>16);
  p[2]= (n>>8);
  p[3]= (n&0xFF);

  return 4;
}
VideoClientConnection *setupConnection()
{
  VideoClientConnection *conn= NULL;
  int rc;
  int error= 0;
  const char *workingDir;
  int pathNameLen, addressSize;

  conn= (VideoClientConnection*)calloc( 1, sizeof(VideoClientConnection));
  if ( conn )
  {
    conn->socketFd= -1;
    conn->name= CONNECTION_WESTEROS_NAME;

    workingDir= getenv("XDG_RUNTIME_DIR");
    if ( !workingDir )
    {
      error = -1;
      fprintf(stderr,"CreateVideoClientConnection: XDG_RUNTIME_DIR is not set");
      goto exit;
    }

    pathNameLen= strlen(workingDir)+strlen("/")+strlen(conn->name)+1;
    if ( pathNameLen > (int)sizeof(conn->addr.sun_path) )
    {
      error = -1;
      fprintf(stderr,"CreateVideoClientConnection: name for server unix domain socket is too long: %d versus max %d",
          pathNameLen, (int)sizeof(conn->addr.sun_path) );
      goto exit;
    }

    conn->addr.sun_family= AF_LOCAL;
    strcpy( conn->addr.sun_path, workingDir );
    strcat( conn->addr.sun_path, "/" );
    strcat( conn->addr.sun_path, conn->name );

    conn->socketFd= socket( PF_LOCAL, SOCK_STREAM|SOCK_CLOEXEC, 0 );
    if ( conn->socketFd < 0 )
    {
      error = -1;
      fprintf(stderr,"CreateVideoClientConnection: unable to open socket");
      goto exit;
    }

    addressSize= pathNameLen + offsetof(struct sockaddr_un, sun_path);
    rc= connect(conn->socketFd, (struct sockaddr *)&conn->addr, addressSize );
    if ( rc < 0 )
    {
      error= -1;
      fprintf(stderr,"CreateVideoClientConnection: connect failed for socket");
      goto exit;
    }

  }
exit:

  if ( error )
  {
    fprintf(stderr,"connect fail\n");
    destroyVideoClientConnection( conn );
    conn= 0;
  }

  return conn;
}
void destroyVideoClientConnection( VideoClientConnection *conn )
{
  if ( conn )
  {
    conn->addr.sun_path[0]= '\0';

    if ( conn->socketFd >= 0 )
    {
      close( conn->socketFd );
      conn->socketFd= -1;
    }

    free( conn );
  }
}


void sendPauseVideoClientConnection( VideoClientConnection *conn, int pause )
{
  if ( conn )
  {
    struct msghdr msg;
    struct iovec iov[1];
    unsigned char mbody[7];
    int len;
    int sentLen;

    msg.msg_name= NULL;
    msg.msg_namelen= 0;
    msg.msg_iov= iov;
    msg.msg_iovlen= 1;
    msg.msg_control= 0;
    msg.msg_controllen= 0;
    msg.msg_flags= 0;

    len= 0;
    mbody[len++]= 'V';
    mbody[len++]= 'S';
    mbody[len++]= 2;
    mbody[len++]= 'P';
    mbody[len++]= pause;//(pause ? 1 : 0);

    iov[0].iov_base= (char*)mbody;
    iov[0].iov_len= len;

    do
    {
      sentLen= sendmsg( conn->socketFd, &msg, MSG_NOSIGNAL );
    }
    while ( (sentLen < 0) && (errno == EINTR));

    if ( sentLen == len )
    {
      fprintf(stderr,"sent pause %d to video server", pause);
    }
  }
}
int SendFrameVideoClientConnection( ImagePlayer *mplayer){
  int result= 0;
  int sentLen;
  if (mplayer == NULL) return -1;
  VideoClientConnection *conn = mplayer->conn;
  DecodedImageInfo *info = mplayer->info;
  if (info ==NULL) return -1;
  if ( conn  )
  {
    struct msghdr msg;
    struct cmsghdr *cmsg;
    struct iovec iov[1];
    unsigned char mbody[4+64];
    char cmbody[CMSG_SPACE(2*sizeof(int))];
    int i;
    int *fd;
    int numFdToSend = 1;
    int frameFd0= -1, frameFd1= -1;
    int fdToSend0= -1, fdToSend1= -1;
    int offset0, offset1, offset2;
    int stride0, stride1, stride2;
    int vx, vy, vw, vh;

    offset0= offset1= 0;
    stride0= stride1=mplayer->screensize.frame_w;
    frameFd0 = mplayer->gemBuf.fd[0];
    frameFd1 = mplayer->gemBuf.fd[1];

    if ( frameFd1 < 0 )
    {
      offset1= mplayer->screensize.frame_w*mplayer->screensize.frame_h;
      stride1= stride0;
    }
    offset2= offset1+(mplayer->screensize.frame_w*mplayer->screensize.frame_h)/2;
    stride2 =stride0;
    fdToSend0= fcntl( frameFd0, F_DUPFD_CLOEXEC, 0 );
    if ( fdToSend0 < 0 )
    {
      fprintf(stderr,"wstSendFrameVideoClientConnection: failed to dup fd0");
      goto exit;
    }
    if ( frameFd1 >= 0 )
    {
      fdToSend1= fcntl( frameFd1, F_DUPFD_CLOEXEC, 0 );
      if ( fdToSend1 < 0 )
      {
        fprintf(stderr,"wstSendFrameVideoClientConnection: failed to dup fd1");
        goto exit;
      }
      ++numFdToSend;
    }
    //test here.need adjust
    vx = 0;//mplayer->screensize.frame_w-info->width)/2;
    vy = 0;//mplayer->screensize.frame_h-info->height)/2;
    vw=mplayer->screensize.frame_w;
    vh=mplayer->screensize.frame_h;

    i= 0;
    mbody[i++]= 'V';
    mbody[i++]= 'S';
    mbody[i++]= 65;
    mbody[i++]= 'F';
    i += putU32( &mbody[i], mplayer->screensize.frame_w );
    i += putU32( &mbody[i], mplayer->screensize.frame_h );
    i += putU32( &mbody[i], V4L2_PIX_FMT_NV12 );
    i += putU32( &mbody[i], vx );
    i += putU32( &mbody[i], vy );
    i += putU32( &mbody[i], vw );
    i += putU32( &mbody[i], vh );
    i += putU32( &mbody[i], offset0 );
    i += putU32( &mbody[i], stride0 );
    i += putU32( &mbody[i], offset1 );
    i += putU32( &mbody[i], stride1 );
    i += putU32( &mbody[i], offset2 );
    i += putU32( &mbody[i], stride2 );
    i += putU32( &mbody[i], mplayer->gemBuf.fd[0] );
    i += putS64( &mbody[i], clock()*1000 );

    iov[0].iov_base= (char*)mbody;
    iov[0].iov_len= i;

    cmsg= (struct cmsghdr*)cmbody;
    cmsg->cmsg_len= CMSG_LEN(numFdToSend*sizeof(int));
    cmsg->cmsg_level= SOL_SOCKET;
    cmsg->cmsg_type= SCM_RIGHTS;

    msg.msg_name= NULL;
    msg.msg_namelen= 0;
    msg.msg_iov= iov;
    msg.msg_iovlen= 1;
    msg.msg_control= cmsg;
    msg.msg_controllen= cmsg->cmsg_len;
    msg.msg_flags= 0;

    fd= (int*)CMSG_DATA(cmsg);
    fd[0]= fdToSend0;
    if ( fdToSend1 >= 0 )
    {
      fd[1]= fdToSend1;
    }
    do
    {
      sentLen= sendmsg( conn->socketFd, &msg, 0 );
    }
    while ( (sentLen < 0) && (errno == EINTR));

    if ( sentLen == iov[0].iov_len )
    {
      result= 0;
    }
    else
    {
      result = -1;
    }

exit:
    if ( fdToSend0 >= 0 )
    {
      close( fdToSend0 );
    }
    if ( fdToSend1 >= 0 )
    {
      close( fdToSend1 );
    }
  }
  return result;
}

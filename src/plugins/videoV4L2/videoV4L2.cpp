////////////////////////////////////////////////////////
//
// GEM - Graphics Environment for Multimedia
//
// zmoelnig@iem.kug.ac.at
//
// Implementation file
//
//    Copyright (c) 1997-1998 Mark Danks.
//    Copyright (c) G�nther Geiger.
//    Copyright (c) 2001-2002 IOhannes m zmoelnig. forum::f�r::uml�ute. IEM
//    For information on usage and redistribution, and for a DISCLAIMER OF ALL
//    WARRANTIES, see the file, "GEM.LICENSE.TERMS" in this distribution.
//
/////////////////////////////////////////////////////////
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "videoV4L2.h"
using namespace gem;

#include "Gem/RTE.h"

#ifdef HAVE_LIBV4L2
REGISTER_VIDEOFACTORY("v4l2", videoV4L2);
#endif

#ifndef HAVE_LIBV4L2
# define v4l2_open open
# define v4l2_close close
# define v4l2_dup dup
# define v4l2_ioctl ioctl
# define v4l2_read read
# define v4l2_mmap mmap
# define v4l2_munmap munmap
#endif /* libv4l-2 */

#if 0
# define debugPost ::post
# define debugThread ::post
#else
# define debugPost
# define debugThread
#endif


/*
  V4L2_PIX_FMT_BGR24	--> BGR
  V4L2_PIX_FMT_RGB24	--> RGB
  V4L2_PIX_FMT_BGR32	--> BGRA
  V4L2_PIX_FMT_RGB32  --> ARGB (!!)
*/

/////////////////////////////////////////////////////////
//
// videoV4L2
//
/////////////////////////////////////////////////////////
// Constructor
//
/////////////////////////////////////////////////////////
videoV4L2 :: videoV4L2() : video()
#ifdef HAVE_VIDEO4LINUX2
                                   , m_gotFormat(0), m_colorConvert(0),
                                     m_tvfd(-1),
                                     m_buffers(NULL), m_nbuffers(0), 
                                     m_currentBuffer(NULL),
                                     m_frame(0), m_last_frame(0),
                                     m_maxwidth(844), m_minwidth(32),
                                     m_maxheight(650), m_minheight(32),
                                     m_rendering(false),
                                     m_stopTransfer(false),
				     m_newfilm(false)
{
  if (!m_width)m_width=320;
  if (!m_height)m_height=240;
  m_capturing=false;
  m_devicenum=V4L2_DEVICENO;

  provide("video4linux2");
  provide("v4l2");
#else
{
#endif /* HAVE_VIDEO4LINUX2 */
}
  
////////////////////////////////////////////////////////
// Destructor
//
////////////////////////////////////////////////////////
videoV4L2 :: ~videoV4L2()
{
#ifdef HAVE_VIDEO4LINUX2
  if (m_haveVideo)stopTransfer();
#endif /* HAVE_VIDEO4LINUX2 */
}

#ifdef HAVE_VIDEO4LINUX2
static int xioctl(int                    fd,
                  int                    request,
                  void *                 arg)
{
  int r;
     
  do {
    r = v4l2_ioctl (fd, request, arg);
    debugThread("V4L2: xioctl %d->%d\n", r, errno);
  }
  while (-1 == r && EINTR == errno);

  debugThread("V4L2: xioctl done %d\n", r);
   
  return r;
}
 
int videoV4L2::init_mmap (void)
{
  struct v4l2_requestbuffers req;
  const char*devname=((m_devicename)?m_devicename:"device");

  memset (&(req), 0, sizeof (req));

  req.count               = V4L2_NBUF;
  req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory              = V4L2_MEMORY_MMAP;

  if (-1 == xioctl (m_tvfd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      error("%s does not support memory mapping", devname);
      return 0;
    } else {
      perror("v4l2: VIDIOC_REQBUFS");
      return 0;
    }
  }

  if (req.count < V4L2_NBUF) {
    //error("Insufficient buffer memory on %s: %d", devname, req.count);
    //return(0);
  }

  m_buffers = (t_v4l2_buffer*)calloc (req.count, sizeof (*m_buffers));

  if (!m_buffers) {
    perror("v4l2: out of memory");
    return(0);
  }

  for (m_nbuffers = 0; m_nbuffers < req.count; ++m_nbuffers) {
    struct v4l2_buffer buf;

    memset (&(buf), 0, sizeof (buf));

    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = m_nbuffers;
    debugPost("v4l2: buf.index==%d", buf.index);

    if (-1 == xioctl (m_tvfd, VIDIOC_QUERYBUF, &buf)){
      perror("v4l2: VIDIOC_QUERYBUF");
      return(0);
    }

    m_buffers[m_nbuffers].length = buf.length;
    m_buffers[m_nbuffers].start =
      v4l2_mmap (NULL /* start anywhere */,
            buf.length,
            PROT_READ | PROT_WRITE /* required */,
            MAP_SHARED /* recommended */,
            m_tvfd, buf.m.offset);

    if (MAP_FAILED == m_buffers[m_nbuffers].start){
      perror("v4l2: mmap");
      return 0;
    }
  }
  return 1;
}

bool videoV4L2 :: openDevice() {
  struct stat st; 

  char buf[256];
  const char*dev_name=m_devicename;
  struct v4l2_capability cap;

  if(!dev_name){
    if (m_devicenum<0){
      sprintf(buf, "/dev/video");
    } else {
      sprintf(buf, "/dev/video%d", m_devicenum);
    }
    dev_name=buf;
  }

  // try to open the device
  debugPost("v4l2: device: %s", dev_name);

  m_tvfd = v4l2_open (dev_name, O_RDWR /* required */, 0);


  if (-1 == m_tvfd) {
    error("Cannot open '%s': %d, %s", dev_name, errno, strerror (errno));
    goto closit;
  }

  if (-1 == fstat (m_tvfd, &st)) {
    error("Cannot identify '%s': %d, %s", dev_name, errno, strerror (errno));
    goto closit;
  }

  if (!S_ISCHR (st.st_mode)) {
    error("%s is no device", dev_name);
    goto closit;
  }

  // by now, we have an open file-descriptor
  // check whether this is really a v4l2-device
  if (-1 == xioctl (m_tvfd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      error("%s is no V4L2 device",  dev_name);
      goto closit;
    } else {
      perror("v4l2: VIDIOC_QUERYCAP");//exit
      goto closit;
    }
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    error("%s is no video capture device", dev_name);
    goto closit;
  }

  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    error("%s does not support streaming i/o", dev_name);
    goto closit;
  }

  m_haveVideo=true;
  return true;

 closit:
  closeDevice();
  return false;
}

void videoV4L2 :: closeDevice() {
  stopTransfer();
  if (m_tvfd>0) v4l2_close(m_tvfd);
  m_tvfd=-1;

  m_haveVideo=false;
}

bool videoV4L2 :: grabFrame(void) {
  bool captureerror=false;
  fd_set fds;
  struct timeval tv;
  debugThread("grabFrame");
  struct v4l2_buffer buf;
  memset(&(buf), 0, sizeof (buf));
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  FD_ZERO (&fds);
  FD_SET (m_tvfd, &fds);

  m_frame++;
  m_frame%=m_nbuffers;

   /* Timeout. */
  tv.tv_sec = 0;
  tv.tv_usec = 100;
  int r = select(0,0,0,0,&tv);

  if (-1 == xioctl (m_tvfd, VIDIOC_DQBUF, &buf)) {
    switch (errno) {
    case EAGAIN:
      perror("v4l2: VIDIOC_DQBUF: stopping capture thread!");
      return false;
    case EIO:
      /* Could ignore EIO, see spec. */
      /* fall through */
    default:
      captureerror=true;
      perror("v4l2: VIDIOC_DQBUF");
    }
  }

  m_currentBuffer=m_buffers[buf.index].start;

  if (-1 == xioctl (m_tvfd, VIDIOC_QBUF, &buf)){
    perror("v4l2: VIDIOC_QBUF");
    captureerror=true;
  }
 
  if(captureerror) {
    m_errorcount++;
    if(m_errorcount>1000) {
      error("v4L2: %d capture errors in a row... I think I better stop now...", m_errorcount);
      return false;
    }
  } else {
    m_errorcount=0;
  }

  unsigned char*data=(unsigned char*)m_buffers[buf.index].start;

  lock();
  if (m_colorConvert){
    m_image.image.notowned = false;
    switch(m_gotFormat){
    case V4L2_PIX_FMT_RGB24: m_image.image.fromRGB   (data);break;
#warning implement fromBGRA
    case V4L2_PIX_FMT_RGB32: m_image.image.fromRGBA  (data); break;
    case V4L2_PIX_FMT_GREY : m_image.image.fromGray  (data); break;
    case V4L2_PIX_FMT_UYVY : m_image.image.fromYUV422(data); break;
    case V4L2_PIX_FMT_YUV420:m_image.image.fromYU12  (data); break;
    default: // ? what should we do ?
      m_image.image.data=data;
      m_image.image.notowned = true;
    }
  } else {
    m_image.image.data=data;
    m_image.image.notowned = true;
  }
  m_image.image.upsidedown=true;
  m_image.newimage = 1;
  unlock();

  return true;

}

/////////////////////////////////////////////////////////
// startTransfer
//
/////////////////////////////////////////////////////////
bool videoV4L2 :: startTransfer()
{
  if(m_tvfd<0)return false;

  unsigned int i;

  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;

  struct v4l2_format fmt;
  unsigned int min;
  enum v4l2_buf_type type;


  m_errorcount=0;
  m_frame = 0;
  m_last_frame = 0;

  /* Select video input, video standard and tune here. */

  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (-1 == xioctl (m_tvfd, VIDIOC_CROPCAP, &cropcap)) {
    /* Errors ignored. */
  }

  memset(&(cropcap), 0, sizeof (cropcap));
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (0 == xioctl (m_tvfd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */

    if (-1 == xioctl (m_tvfd, VIDIOC_S_CROP, &crop)) {
      perror("v4l2: vidioc_s_crop");
      switch (errno) {
      case EINVAL:
        /* Cropping not supported. */
        break;
      default:
        /* Errors ignored. */
        break;
      }
    }
  }

  if (-1 == xioctl (m_tvfd, VIDIOC_S_INPUT, &m_channel)) {
    perror("v4l2: VIDIOC_S_INPUT"); /* exit */
  }

  memset (&(fmt), 0, sizeof (fmt));

  // query back what we have set
  if (-1 == xioctl (m_tvfd, VIDIOC_G_FMT, &fmt)){
    perror("v4l2: VIDIOC_G_FMT");//exit
  }

  fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  switch(m_reqFormat){
  case GL_YCBCR_422_GEM: 
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY; 
    break;
  case GL_LUMINANCE: 
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY; 
    break;
  case GL_RGB: 
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24; 
    break;
  default: 
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32; 
    m_reqFormat=GL_RGBA;
    break;
  }
  fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
  
  verbose(1, "v4l2: want %d == '%c%c%c%c' ", m_reqFormat, 
	    static_cast<char>(fmt.fmt.pix.pixelformat),
	    static_cast<char>(fmt.fmt.pix.pixelformat>>8),
	    static_cast<char>(fmt.fmt.pix.pixelformat>>16),
	    static_cast<char>(fmt.fmt.pix.pixelformat>>24));

  if (-1 == xioctl (m_tvfd, VIDIOC_S_FMT, &fmt)){
    perror("v4l2: VIDIOC_S_FMT");//exit
  }
  
  // query back what we have set
  if (-1 == xioctl (m_tvfd, VIDIOC_G_FMT, &fmt)){
    perror("v4l2: VIDIOC_G_FMT");//exit
  }

  m_gotFormat=fmt.fmt.pix.pixelformat;
  switch(m_gotFormat){
  case V4L2_PIX_FMT_RGB32: debugPost("v4l2: ARGB");break;
  case V4L2_PIX_FMT_RGB24: debugPost("v4l2: RGB");break;
  case V4L2_PIX_FMT_UYVY: debugPost("v4l2: YUV ");break;
  case V4L2_PIX_FMT_GREY: debugPost("v4l2: gray");break;
  case V4L2_PIX_FMT_YUV420: debugPost("v4l2: YUV 4:2:0");break;
  default: 
    /* hmm, we don't know how to handle this 
     * let's try formats that should be always supported by libv4l2
     */
    switch(m_reqFormat){
    case GL_YCBCR_422_GEM: 
    case GL_LUMINANCE: 
      fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420; 
      break;
    default: 
      fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24; 
      break;
    }
    if (-1 == xioctl (m_tvfd, VIDIOC_S_FMT, &fmt)){
      perror("v4l2: VIDIOC_S_FMT2");
    }
    // query back what we have set
    if (-1 == xioctl (m_tvfd, VIDIOC_G_FMT, &fmt)){
      perror("v4l2: VIDIOC_G_FMT2");
    }
    m_gotFormat=fmt.fmt.pix.pixelformat;
  }

  switch(m_gotFormat){
  case V4L2_PIX_FMT_RGB32: case V4L2_PIX_FMT_RGB24:
  case V4L2_PIX_FMT_UYVY: case V4L2_PIX_FMT_YUV420:
  case V4L2_PIX_FMT_GREY: 
    break;
  default: 
    error("unknown format '%c%c%c%c'",
          static_cast<char>(m_gotFormat),
          static_cast<char>(m_gotFormat>>8),
          static_cast<char>(m_gotFormat>>16),
          static_cast<char>(m_gotFormat>>24));
    /* we should really return here! */
  }

  verbose(1, "v4l2: got '%c%c%c%c'", 
	    static_cast<char>(m_gotFormat),
	    static_cast<char>(m_gotFormat>>8),
	    static_cast<char>(m_gotFormat>>16),
	    static_cast<char>(m_gotFormat>>24));

  /* Buggy driver paranoia. */
  min = fmt.fmt.pix.width * 2;
  if (fmt.fmt.pix.bytesperline < min)
    fmt.fmt.pix.bytesperline = min;
  min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
  if (fmt.fmt.pix.sizeimage < min)
    fmt.fmt.pix.sizeimage = min;

  if(!init_mmap ())goto closit;

  for (i = 0; i < m_nbuffers; ++i) {
    struct v4l2_buffer buf;
    
    memset (&(buf), 0, sizeof (buf));
    
    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = i;
    
    if (-1 == xioctl (m_tvfd, VIDIOC_QBUF, &buf)){
      perror("v4l2: VIDIOC_QBUF");//exit
    }
  }

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (-1 == xioctl (m_tvfd, VIDIOC_STREAMON, &type)){
    perror("v4l2: VIDIOC_STREAMON");//exit
  }
  
  debugPost("v4l2: format: %c%c%c%c -> %d", 
       static_cast<char>(m_gotFormat),
       static_cast<char>(m_gotFormat>>8),
       static_cast<char>(m_gotFormat>>16),
       static_cast<char>(m_gotFormat>>24),
       m_reqFormat);
  switch(m_gotFormat){
  case V4L2_PIX_FMT_GREY  : m_colorConvert=(m_reqFormat!=GL_LUMINANCE); break;
  case V4L2_PIX_FMT_RGB24 : m_colorConvert=(m_reqFormat!=GL_BGR); break;
#warning RGB32 vs BGRA confusion
  case V4L2_PIX_FMT_RGB32 : m_colorConvert=(m_reqFormat!=GL_BGRA); break;//RGB32!=RGBA; its ARGB
  case V4L2_PIX_FMT_UYVY  : m_colorConvert=(m_reqFormat!=GL_YCBCR_422_GEM); break;
  case V4L2_PIX_FMT_YUV420: m_colorConvert=1; break;
  default: m_colorConvert=true;
  }
  
  debugPost("v4l2: colorconvert=%d", m_colorConvert);


  /* fill in image specifics for Gem pixel object.  Could we have
     just used RGB, I wonder? */
  m_image.image.setCsizeByFormat(m_reqFormat);
  
  setDimen((int)m_width, (int)m_height,  0, 0,  0, 0);
  m_image.image.reallocate();

  post("v4l2: GEM: pix_video: Opened video connection 0x%X", m_tvfd);
  return true;

 closit:
  debugPost("v4l2: closing it!");
  stopTransfer();
  debugPost("v4l2: closed it");
  return false;
}

/////////////////////////////////////////////////////////
// stopTransfer
//
/////////////////////////////////////////////////////////
bool videoV4L2 :: stopTransfer()
{
  debugPost("v4l2: stoptransfer");
  unsigned int i=0;
  stopThread();

  if(!m_capturing)return false;

  // unmap the mmap
  debugPost("v4l2: unmapping %d buffers: %x", m_nbuffers, m_buffers);
  if(m_buffers){
    for (i = 0; i < m_nbuffers; ++i)
      if (-1 == v4l2_munmap (m_buffers[i].start, m_buffers[i].length)){
        // oops: couldn't unmap the memory
      }
    debugPost("v4l2: freeing buffers: %x", m_buffers);
    free (m_buffers);
  }
  m_buffers=NULL;
  debugPost("v4l2: freed");

  // stop streaming
  if(m_tvfd){
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl (m_tvfd, VIDIOC_STREAMOFF, &type)){
      perror("v4l2: VIDIOC_STREAMOFF");
    }
  }

  m_capturing=false;
  debugPost("v4l2: stoppedtransfer");
  return true;
}

/////////////////////////////////////////////////////////
// dimenMess
//
/////////////////////////////////////////////////////////
int videoV4L2 :: setDimen(int x, int y, int leftmargin, int rightmargin,
                          int topmargin, int bottommargin)
{
  if(m_tvfd>=0) {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    if(m_maxwidth>0 && x>m_maxwidth)
      error("width too great: %d > %d", x, m_maxwidth);
    if(m_minwidth>0 && x<m_minwidth)
      error("width too small: %d < %d", x, m_minwidth);
    
    if(m_maxheight>0 && y>m_maxheight)
      error("height too great: %d > %d", y, m_maxheight);
    if(m_minheight>0 && y<m_minheight)
      error("height too small: %d < %d", y, m_minheight);

    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl (m_tvfd, VIDIOC_G_FMT, &fmt)){
      perror("v4l2: VIDIOC_G_FMT");
    }

    fmt.fmt.pix.width       = x;
    fmt.fmt.pix.height      = y;

    if (-1 == xioctl (m_tvfd, VIDIOC_S_FMT, &fmt)){
      perror("v4l2: VIDIOC_S_FMT");//exit
    }
    
    if(x!=fmt.fmt.pix.width || y!=fmt.fmt.pix.height){
      post("v4l2: changed size from %dx%d to %dx%d", 
           x, y,
           fmt.fmt.pix.width,fmt.fmt.pix.height);
    }
    x=fmt.fmt.pix.width;
    y=fmt.fmt.pix.height;
  }

  m_width=x;
  m_height=y;

  lock();
  m_image.image.xsize = x;
  m_image.image.ysize = y;
  m_image.image.reallocate();
  unlock();
  //  restartTransfer();
  return 1;
}

int videoV4L2 :: setNorm(const char*norm)
{
  const char c=*norm;
  int i_norm=-1;

  switch (c){
  case 'p': case 'P':
    i_norm = V4L2_STD_PAL;
    break;
  case 'n': case 'N':
    i_norm = V4L2_STD_NTSC;
    break;
  case 's': case 'S':
    i_norm = V4L2_STD_SECAM;
    break;
  default:
    error("pix_video: unknown norm");
    return -1;
    break;
  }
  //  if (i_norm==m_norm)return 0;
  m_norm=i_norm;
  restartTransfer();
  return 0;
}

int videoV4L2 :: setChannel(int c, t_float f){
  error("v4l2: oops, no channel selection! please report this as a bug!!!");
  
  m_channel=c;
  
  restartTransfer();

  return 0;
}

int videoV4L2 :: setDevice(int d)
{
  m_devicename=NULL;
  if (d==m_devicenum)return 0;
  m_devicenum=d;
  restartTransfer();
  //  verbose(1, "new device set %d", m_devicenum);
  return 0;
}
int videoV4L2 :: setDevice(const char*name)
{
  m_devicenum=-1;
  m_devicename=name;
  restartTransfer();
  //  verbose(1, "new device set %d", m_devicenum);
  return 0;
}

int videoV4L2 :: setColor(int format)
{
  if (format<=0 || format==m_reqFormat)return -1;
  m_reqFormat=format;
  restartTransfer();
  return 0;
}
#endif /* HAVE_VIDEO4LINUX2 */

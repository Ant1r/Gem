////////////////////////////////////////////////////////
//
// GEM - Graphics Environment for Multimedia
//
// zmoelnig@iem.kug.ac.at
//
// Implementation file 
//
//    Copyright (c) 1997-1999 Mark Danks.
//    Copyright (c) G�nther Geiger.
//    Copyright (c) 2001-2002 IOhannes m zmoelnig. forum::f�r::uml�ute. IEM
//    For information on usage and redistribution, and for a DISCLAIMER OF ALL
//    WARRANTIES, see the file, "GEM.LICENSE.TERMS" in this distribution.
//
/////////////////////////////////////////////////////////

#include <string.h>
#include "Pixes/filmGMERLIN.h"


/////////////////////////////////////////////////////////
//
// filmGMERLIN
//
/////////////////////////////////////////////////////////
// Constructor
//
/////////////////////////////////////////////////////////

filmGMERLIN :: filmGMERLIN(int format) : film(format)
#ifdef HAVE_GMERLIN
				       ,
					 m_file(NULL),
					 m_opt(NULL),
					 m_gformat(NULL),
					 m_track(0),
					 m_stream(0),
					 m_gframe(NULL),
					 m_finalframe(NULL),
					 m_gconverter(NULL),
					 m_fps_num(0), m_fps_denum(1)
#endif /* GMERLIN */
{
  static bool first_time=true;
  if (first_time) {
#ifdef HAVE_GMERLIN
    post("pix_film:: gmerlin support");
#endif
    first_time = false;
  }
#ifdef HAVE_GMERLIN
  m_gconverter=gavl_video_converter_create ();
  m_finalformat = new gavl_video_format_t[1];

#endif
}

/////////////////////////////////////////////////////////
// Destructor
//
/////////////////////////////////////////////////////////
filmGMERLIN :: ~filmGMERLIN()
{
  close();
#ifdef HAVE_GMERLIN
  if(m_gconverter)gavl_video_converter_destroy(m_gconverter);m_gconverter=NULL;
#endif
}

#ifdef HAVE_GMERLIN
void filmGMERLIN :: close(void)
{
  if(m_file)bgav_close(m_file);m_file=NULL;

  /* LATER: free frame buffers */

}

void filmGMERLIN::log(bgav_log_level_t level, const char *log_domain, const char *message)
{
  switch(level) {
  case BGAV_LOG_DEBUG:
    verbose(2, "[pix_film:%s] %s", log_domain, message);
    break;
  case BGAV_LOG_INFO:
    verbose(1, "[pix_film:%s] %s", log_domain, message);
    break;
  case BGAV_LOG_WARNING:
    post("[pix_film:%s] %s", log_domain, message);
    break;
  case BGAV_LOG_ERROR:
    error("[pix_film:%s!] %s", log_domain, message);
    break;
  default:break;
  }
}

void filmGMERLIN::log_callback (void *data, bgav_log_level_t level, const char *log_domain, const char *message)
{
  //  post("gmerlin[%d:%s] %s", level, log_domain, message);
  ((filmGMERLIN*)(data))->log(level, log_domain, message);
}

/////////////////////////////////////////////////////////
// really open the file ! (OS dependent)
//
/////////////////////////////////////////////////////////
bool filmGMERLIN :: open(char *filename, int format)
{
  close();

  m_track=0;

  m_file = bgav_create();
  m_opt = bgav_get_options(m_file);
  /*
  bgav_options_set_connect_timeout(m_opt,   connect_timeout);
  bgav_options_set_read_timeout(m_opt,      read_timeout);
  bgav_options_set_network_bandwidth(m_opt, network_bandwidth);
  */
  bgav_options_set_seek_subtitles(m_opt, 0);
  bgav_options_set_sample_accurate(m_opt, 1);

  bgav_options_set_log_callback(m_opt,
				log_callback,
				this); 	



  if(!strncmp(filename, "vcd://", 6))
    {
    if(!bgav_open_vcd(m_file, filename + 5))
      {
	error("Could not open VCD Device %s",
              filename + 5);
      return false;
      }
    }
  else if(!strncmp(filename, "dvd://", 6))
    {
    if(!bgav_open_dvd(m_file, filename + 5))
      {
	error("Could not open DVD Device %s",
              filename + 5);
      return false;
      }
    }
  else if(!strncmp(filename, "dvb://", 6))
    {
    if(!bgav_open_dvb(m_file, filename + 6))
      {
	error("Could not open DVB Device %s",
              filename + 6);
      return false;
      }
    }
  else {
    if(!bgav_open(m_file, filename)) {
      error("Could not open file %s",
            filename);
      close();
      return false;
    }
  }
  if(bgav_is_redirector(m_file))
    {
      int i=0;
      int num_urls=bgav_redirector_get_num_urls(m_file);
      post("Found redirector:");
      for(i = 0; i < num_urls; i++)
      {
	post("#%d: '%s' -> %s", i, bgav_redirector_get_name(m_file, i), bgav_redirector_get_url(m_file, i));
      }
      if(true){
	filename=(char*)bgav_redirector_get_url(m_file, 0);
	close();
	return open(filename);
      }
    }


  /*
   * ok, we have been able to open the "file"
   * now get some information from it...
   */
  m_numTracks = bgav_num_tracks(m_file);
  // LATER: check whether this track has a video-stream...
  bgav_select_track(m_file, m_track);

  m_seekable=bgav_can_seek_sample(m_file);

  bgav_set_video_stream(m_file, m_stream, BGAV_STREAM_DECODE);
  if(!bgav_start(m_file)) {
    close();
    return false;
  }

  m_gformat = (gavl_video_format_t*)bgav_get_video_format (m_file, m_stream);
  m_gframe = gavl_video_frame_create_nopad(m_gformat);

  m_finalformat->frame_width = m_gformat->frame_width;
  m_finalformat->frame_height = m_gformat->frame_height;
  m_finalformat->image_width = m_gformat->image_width;
  m_finalformat->image_height = m_gformat->image_height;
  m_finalformat->pixel_width = m_gformat->pixel_width;
  m_finalformat->pixel_height = m_gformat->pixel_height;
  m_finalformat->frame_duration = m_gformat->frame_duration;
  m_finalformat->timescale = m_gformat->timescale;

  m_finalformat->pixelformat=GAVL_RGBA_32;

  m_finalframe = gavl_video_frame_create_nopad(m_finalformat);
  gavl_video_converter_init (m_gconverter, m_gformat, m_finalformat);
  m_image.image.xsize=m_gformat->frame_width;
  m_image.image.ysize=m_gformat->frame_height;
  m_image.image.setCsizeByFormat(GL_RGBA);
  m_image.image.notowned=true;
  m_image.image.upsidedown=true;
  m_image.newfilm=true;

  m_fps = m_gformat->timescale / m_gformat->frame_duration;

  m_fps_num=m_gformat->timescale;
  m_fps_denum=m_gformat->frame_duration;

  gavl_time_t dur=bgav_get_duration (m_file, m_track);
  m_numFrames = gavl_time_to_frames(m_fps_num, 
				    m_fps_denum, 
				    dur);

  return true;
}

/////////////////////////////////////////////////////////
// render
//
/////////////////////////////////////////////////////////
pixBlock* filmGMERLIN :: getFrame(){
  if(!m_file)return NULL;

  bgav_read_video(m_file, m_gframe, m_stream);
  gavl_video_convert (m_gconverter, m_gframe, m_finalframe);


  m_image.newimage=true;
  m_image.image.data=m_finalframe->planes[0];
  return &m_image;
}

int filmGMERLIN :: changeImage(int imgNum, int trackNum){
  // LATER implement track-switching
  if(!m_file)return FILM_ERROR_FAILURE;
  if(imgNum>m_numFrames || imgNum<0)return FILM_ERROR_FAILURE;
  if  (imgNum>0)m_curFrame=imgNum;
  if(trackNum>0)m_curTrack=trackNum;

  if(bgav_can_seek(m_file)) {
    int64_t seekposOrg = imgNum;
    int64_t seekpos = seekposOrg;
    // LATER lookup the docs for the 3rd parameter:
    // it should be the same "timebase" as the original source in order to get frame-accurate seeking
    bgav_seek_scaled(m_file, &seekpos, m_fps);
    if(seekposOrg == seekpos)
      return FILM_ERROR_SUCCESS;
    /* never mind: always return success... */
    return FILM_ERROR_SUCCESS;
  }
  return FILM_ERROR_FAILURE;
}
#endif // GMERLIN

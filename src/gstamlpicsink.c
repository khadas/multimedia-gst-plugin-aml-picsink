/*
 * GStreamer
 * Copyright (C) 2020 pengbing.deng <<pengbing.deng@amlogic.com>>
 *
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-amlpicsink
 *
 * The amlpicsink element do picture decode&render directly
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! amlpicsink ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <stdio.h>
#include <gst/gst.h>

#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>


#include "gstamlpicsink.h"
#include "imageplayer.h"


/* flag of witch picture sink */
int cur_sink = 0;

GstMapInfo pic_map;
GstBuffer * pic_buffer;

/* properties */
enum
{
  PROP_0,
  PROP_SILENT,
  PROP_STANDALONE,
  PROP_INIT,
  /* output window properties */
  PROP_OUTPUT_X0,
  PROP_OUTPUT_Y0,
  PROP_OUTPUT_WIDTH,
  PROP_OUTPUT_HEIGHT,
  PROP_OUTPUT_FORCE,
  /* decoding properties */
  PROP_SCALE_X, /* allowed: 0(auto), 1, 2, 4, 8 */
  PROP_SCALE_Y,
  PROP_ANGLE,   /* 0, 90, 180, 270 */

  PROP_BLANK,
  PROP_IMAGE_MODE,

  PROP_DISABLE_WORKAROUNDS,

  PROP_LAST
};

enum
{
  SIGNAL_FIRSTFRAME,
  SIGNAL_START,
  SIGNAL_STOP,
  SIGNAL_DECODEERROR,
  SIGNAL_EOS,
  MAX_SIGNAL
};

#define GST_AML_PIC_SINK_GET_PRIVATE(obj)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_AMLPICSINK, GstAmlPicSinkPrivate))


#define NATIVE_DISPLAY_WIDTH   1920 // 3840 1920 1280
#define NATIVE_DISPLAY_HEIGHT  1080 // 2160 1080 720
static guint g_signals[MAX_SIGNAL]= {0};

struct _GstAmlPicSinkPrivate
{
  gboolean standalone;
  guint32 seqnum; /* for eos */
  struct {
    guint x0, y0;
    guint width, height;
    guint framewidth, frameheight;
    gboolean force; // ignore source aspect ratio
  } output;

  gpointer _gst_reserved[GST_PADDING];
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
      "format = (string) NV12, "
      "framerate = (fraction) [ 0,  MAX], "
      "width = (int) [ 1, 16383 ], " "height = (int) [ 1, 16383 ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_amlpicsink_parent_class parent_class
G_DEFINE_TYPE (Gstamlpicsink, gst_amlpicsink, GST_TYPE_ELEMENT);

static void gst_amlpicsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amlpicsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
void
gst_amlpicsink_finalize (GObject * object);
gboolean
gst_amlpicsink_sink_setcaps (GstBaseSink * sink, GstCaps * caps);
gboolean
gst_amlpicsink_sink_getcaps (GstBaseSink * sink, GstCaps * caps);

gboolean
gst_amlpicsink_sink_start(GstBaseSink * sink);
gboolean
gst_amlpicsink_sink_stop (GstBaseSink * sink);

static GstStateChangeReturn gst_amlpicsink_change_state(GstElement *
    element, GstStateChange transition);

static gboolean gst_amlpicsink_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_amlpicsink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the amlpicsink's class */
  static void
gst_amlpicsink_class_init (GstamlpicsinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  //GstVideoSinkClass *videosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  //videosink_class = (GstVideoSinkClass *) klass;

  g_type_class_add_private (klass, sizeof (GstAmlPicSinkPrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_amlpicsink_set_property;
  gobject_class->get_property = gst_amlpicsink_get_property;
  gobject_class->finalize = gst_amlpicsink_finalize;

  gst_element_class_set_details_simple(gstelement_class,
      "amlpicsink",
      "sink/picture",
      "Amlogic plugin for picture playing",
      "pengbing.deng@amlogic.com");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
        FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "JPEG/PNG image decoder",
      "Codec/Decoder/Image", "Decode images from JPEG/PNG format",
      "pengbing.deng@amlogic.com");


  g_object_class_install_property(gobject_class, PROP_INIT, g_param_spec_uint
      ("init", "do initialization", "Init some HAL stuff",
       0, 2, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_STANDALONE, g_param_spec_boolean
      ("standalone", "Usage mode", "element used in plain GStreamer/jplayer or Mediaplayer",
       TRUE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_OUTPUT_X0, g_param_spec_uint
      ("output-x0", "Origin X", "Output origin X",
       0, NATIVE_DISPLAY_WIDTH, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_OUTPUT_Y0, g_param_spec_uint
      ("output-y0", "Origin Y", "Output origin Y",
       0, NATIVE_DISPLAY_HEIGHT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_OUTPUT_WIDTH, g_param_spec_uint
      ("output-width", "Width", "Output width",
       0, NATIVE_DISPLAY_WIDTH, 1920, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_OUTPUT_HEIGHT, g_param_spec_uint
      ("output-height", "Height", "Output height",
       0, NATIVE_DISPLAY_HEIGHT, 1080, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_OUTPUT_FORCE, g_param_spec_boolean
      ("output-force", "Force Size", "Force dimensions, ignore aspect ratio",
       FALSE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_SCALE_X, g_param_spec_uint
      ("scale-x", "X-Scale", "Horizontal scaling factor {0=auto, 1, 2, 4, 8}",
       0, 8, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_SCALE_Y, g_param_spec_uint
      ("scale-y", "Y-Scale", "Vertical scaling factor {0=auto, 1, 2, 4, 8}",
       0, 8, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_ANGLE, g_param_spec_int
      ("angle", "Angle", "Rotation angle: degrees, clockwise [-90, 270]",
       -90, 270, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_BLANK, g_param_spec_boolean
      ("blank", "Blank", "set tube dark",
       FALSE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_IMAGE_MODE, g_param_spec_boolean
      ("image-mode", "Image Mode", "End the Image decoder mode",
       FALSE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_DISABLE_WORKAROUNDS, g_param_spec_uint
      ("disable-workarounds", "disable workarounds", "disable some workarounds",
       0, 0xFFFFFFFF, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_amlpicsink_sink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_amlpicsink_sink_getcaps);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_amlpicsink_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_amlpicsink_sink_stop);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_amlpicsink_sink_event);

  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_amlpicsink_chain);
  gstelement_class->change_state =
    GST_DEBUG_FUNCPTR (gst_amlpicsink_change_state);

  g_signals[SIGNAL_EOS]= g_signal_new( "eos-detect-callback",
      G_TYPE_FROM_CLASS(GST_ELEMENT_CLASS(klass)),
      (GSignalFlags) (G_SIGNAL_RUN_LAST),
      0,	  /* class offset */
      NULL, /* accumulator */
      NULL, /* accu data */
      g_cclosure_marshal_VOID__UINT_POINTER,
      G_TYPE_NONE,
      2,
      G_TYPE_UINT,
      G_TYPE_POINTER );

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
  static void
gst_amlpicsink_init (Gstamlpicsink * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  GstAmlPicSinkPrivate * priv = GST_AML_PIC_SINK_GET_PRIVATE(filter);
  filter->priv = priv;
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR(gst_amlpicsink_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR(gst_amlpicsink_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->silent = FALSE;

  priv->standalone = TRUE;
  priv->output.x0 = 0;
  priv->output.y0 = 0;
  priv->output.width = NATIVE_DISPLAY_WIDTH;
  priv->output.height = NATIVE_DISPLAY_HEIGHT;

}

  static void
gst_amlpicsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstamlpicsink *amlpicsink = GST_AMLPICSINK (object);
  GstAmlPicSinkPrivate * priv = amlpicsink->priv;

  switch (prop_id) {
    case PROP_STANDALONE:
      amlpicsink->priv->standalone = g_value_get_boolean (value);
      break;
    case PROP_INIT: /* this will blank the screen, so call only once */
      if (1 == g_value_get_boolean (value))
        //Thal_Svp_SetMPSource(INPUT_HIDTVPRO_HDTV1, 0, 0);
        break;
    case PROP_OUTPUT_X0:
      priv->output.x0 = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_Y0:
      priv->output.y0 = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_WIDTH:
      priv->output.width = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_HEIGHT:
      priv->output.height = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_FORCE:
      //priv->output.force = g_value_get_boolean (value);
      //set_output_window (amlpicsink);
      break;
    case PROP_SCALE_Y:
      //priv->scale.y = _int2scale (g_value_get_uint (value));
      break;
    case PROP_ANGLE:
      //priv->scale.angle = _int2angle (g_value_get_int (value));
      break;
    case PROP_BLANK:
      //_gst_hvs_blank (amlpicsink, g_value_get_int (value));
      break;
    case PROP_IMAGE_MODE:
      //_gst_hvs_image_mode (amlpicsink, g_value_get_int (value));
      break;
    case PROP_DISABLE_WORKAROUNDS:
      //amlpicsink->dbg_disable_workarounds = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

  static void
gst_amlpicsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstamlpicsink *amlpicsink = GST_AMLPICSINK (object);
  GstAmlPicSinkPrivate * priv = amlpicsink->priv;

  switch (prop_id) {
    case PROP_OUTPUT_X0:
      g_value_set_uint (value, priv->output.x0);
      break;
    case PROP_OUTPUT_Y0:
      g_value_set_uint (value, priv->output.y0);
      break;
    case PROP_OUTPUT_WIDTH:
      g_value_set_uint (value, priv->output.width);
      break;
    case PROP_OUTPUT_HEIGHT:
      g_value_set_uint (value, priv->output.height);
      break;
    case PROP_SCALE_X:
      //g_value_set_uint (value, _scale2int[amlpicsink->scale.x]);
      break;
    case PROP_SCALE_Y:
      //g_value_set_uint (value, _scale2int[amlpicsink->scale.y]);
      break;
    case PROP_ANGLE:
      //g_value_set_int (value, _angle2int[amlpicsink->scale.angle]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

  void
gst_amlpicsink_finalize (GObject * object)
{
  Gstamlpicsink * amlpicsink = GST_AMLPICSINK (object);
  GST_DEBUG_OBJECT (amlpicsink, "finalize");

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* GstElement vmethod implementations */
/* notify subclass of new caps */
  gboolean
gst_amlpicsink_sink_setcaps (GstBaseSink * sink, GstCaps * caps)
{
  Gstamlpicsink * amlpicsink = GST_AMLPICSINK (sink);
  GstAmlPicSinkPrivate * priv = amlpicsink->priv;

  GST_INFO_OBJECT (amlpicsink, "set_caps: %" GST_PTR_FORMAT, caps);

  return TRUE;
}

  gboolean
gst_amlpicsink_sink_getcaps (GstBaseSink * sink, GstCaps * caps)
{
  Gstamlpicsink * amlpicsink = GST_AMLPICSINK (sink);
  GST_INFO_OBJECT (amlpicsink, "get_caps: %" GST_PTR_FORMAT, caps);
  return TRUE;
}

  static GstStateChangeReturn
gst_amlpicsink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  Gstamlpicsink *sink = GST_AMLPICSINK (element);
  GstAmlPicSinkPrivate *priv = sink->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      {
        g_print("gst_amlpicsink_change_state : null to ready\n");
        break;
      }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      {
        g_print("gst_amlpicsink_change_state : ready to paused\n");
        //gst_base_sink_set_async_enabled (GST_BASE_SINK_CAST(sink), FALSE);
        break;
      }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      {
        g_print("gst_amlpicsink_change_state : paused to playing\n");
        break;
      }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      {
        g_print("gst_amlpicsink_change_state : playing to paused\n");
        //priv->paused = TRUE;
        break;
      }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      {
        g_print("gst_amlpicsink_change_state : paused to ready\n");

        GST_OBJECT_LOCK (sink);
        release();
        if (pic_buffer != NULL) {
          gst_buffer_unmap (pic_buffer, &pic_map);
          gst_buffer_unref (pic_buffer);
          pic_buffer = NULL;
        }
        GST_OBJECT_UNLOCK (sink);

        g_print("gst_amlpicsink_change_state : paused to ready done\n");
        break;
      }
    default:
      break;
  }
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      {
        g_print("gst_amlpicsink_change_state : ready to null\n");

        break;
      }
    default:
      break;
  }

  return ret;
}

/* this function handles sink events */
  static gboolean
gst_amlpicsink_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  Gstamlpicsink *amlpicsink;
  gboolean ret;
  amlpicsink = GST_AMLPICSINK (parent);
  GstAmlPicSinkPrivate * priv = amlpicsink->priv;
  GstSample *sample;
  gboolean res;
  gint framewidth, frameheight;
  GstStructure *structure;

  GST_LOG_OBJECT (amlpicsink, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      g_print ("received GST_EVENT_EOS event\n");
      GstMessage *message;
      g_signal_emit (G_OBJECT (amlpicsink), g_signals[SIGNAL_EOS], 0, 2, NULL);

      /* ok, now we can post the message */
      GST_WARNING_OBJECT (amlpicsink, "Now posting EOS");
      priv->seqnum = gst_event_get_seqnum (event);
      GST_DEBUG_OBJECT (amlpicsink, "Got seqnum #%" G_GUINT32_FORMAT, priv->seqnum);

      message = gst_message_new_eos (GST_OBJECT_CAST (amlpicsink));
      gst_message_set_seqnum (message, priv->seqnum);
      gst_element_post_message (GST_ELEMENT_CAST (amlpicsink), message);
      break;
    case GST_EVENT_CAPS:
      {
        GstCaps * caps;

        gst_event_parse_caps (event, &caps);
        /* do something with the caps */
        gchar *str= gst_caps_to_string (caps);
        g_print ("gst_amlpicsink_sink_event parse caps: %s\n", str);
        g_free(str);

        structure = gst_caps_get_structure (caps, 0);
        /* we need to get the final caps on the buffer to get the size */
        res = gst_structure_get_int (structure, "width", &framewidth);
        res |= gst_structure_get_int (structure, "height", &frameheight);
        if (!res) {
          g_print ("could not get frame dimension\n");
        }
        priv->output.framewidth = framewidth;
        priv->output.frameheight = frameheight;
        g_print ("framewidth : %d , frameheight : %d\n",	priv->output.framewidth, priv->output.frameheight);
        /* and forward */
        ret = gst_pad_event_default (pad, parent, event);
        break;
      }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
  static GstFlowReturn
gst_amlpicsink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  Gstamlpicsink *amlpicsink;
  GstFlowReturn ret = GST_FLOW_OK;

  GstMapInfo map;
  int size = 0;

  amlpicsink = GST_AMLPICSINK (parent);
  GST_OBJECT_LOCK (amlpicsink);
  pic_buffer = buffer;
  pic_map = map;

  GstAmlPicSinkPrivate * priv = amlpicsink->priv;
  gint width, height, scale_width, scale_height;
  scale_width = priv->output.width;
  scale_height = priv->output.height;
  size = priv->output.width*priv->output.height;
  if (amlpicsink->silent == FALSE)
    g_print ("I'm picture plugged, therefore I'm in.\n");

  if (pic_buffer != NULL && (gst_buffer_map (pic_buffer, &pic_map, GST_MAP_READ)))
  {
    g_print ("get GstBuffer is not null. map.size : %d\n" , pic_map.size);
    if (initEnv(NATIVE_DISPLAY_WIDTH,NATIVE_DISPLAY_HEIGHT) < 0) {
      ret = GST_FLOW_ERROR;
    }else{
      g_print ("initEnv(%d,%d) is ok.\n",  priv->output.width, priv->output.height);
    }
    if (priv->output.framewidth == 0 || priv->output.frameheight == 0) {
      priv->output.framewidth = 1536;
      priv->output.frameheight = 849;
    }
    if (setPictureAttr(priv->output.framewidth,priv->output.frameheight,1) < 0) {
      ret = GST_FLOW_ERROR;
    }else{
      g_print ("setPictureAttr(%d,%d) is ok.\n" ,  priv->output.framewidth, priv->output.frameheight);
    }

    showBuf(pic_map.size, pic_map.data);

  }else{
    g_print ("get GstBuffer is null.\n");
    ret = GST_FLOW_ERROR;
  }
  GST_OBJECT_UNLOCK (amlpicsink);

  return ret;
}

/* start and stop processing, ideal for opening/closing the resource */
  gboolean
gst_amlpicsink_sink_start (GstBaseSink * sink)
{
  Gstamlpicsink * amlpicsink = GST_AMLPICSINK (sink);

  GST_DEBUG_OBJECT (amlpicsink, "start");

  return TRUE;
}

  gboolean
gst_amlpicsink_sink_stop (GstBaseSink * sink)
{
  Gstamlpicsink * amlpicsink = GST_AMLPICSINK (sink);
  GstAmlPicSinkPrivate * priv = amlpicsink->priv;

  LOCK_ELEMENT (amlpicsink);
  GST_DEBUG_OBJECT (amlpicsink, "stop");
  release();

  return TRUE;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
  static gboolean
amlpicsink_init (GstPlugin * amlpicsink)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template amlpicsink' with your description
   */
  if (!gst_element_register (amlpicsink, "amlpicsink",
        GST_RANK_PRIMARY, GST_TYPE_AMLPICSINK))
    return FALSE;
  /*GST_DEBUG_CATEGORY_INIT (gst_amlpicsink_debug, "amlpicsink",
    0, "Template amlpicsink");
    */


  return TRUE;
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef VERSION
#define VERSION "0.1.0"
#endif
#ifndef PACKAGE
#define PACKAGE "aml_package"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "aml_media"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://amlogic.com"
#endif

/* gstreamer looks for this structure to register amlpicsinks
 *
 * exchange the string 'Template amlpicsink' with your amlpicsink description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    amlpicsink,
    "Amlogic plugin for picture decoder/render",
    amlpicsink_init,
    PACKAGE_VERSION,
    GST_LICENSE,
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
    )

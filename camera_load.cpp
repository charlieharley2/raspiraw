/* https://raspberrypi.stackexchange.com/questions/120708/how-to-directly-render-pi-camera-output-to-display-using-c */
/* compile with
 g++ -I/opt/vc/include -pipe -W -Wall -Wextra -g -O0 -MD -o cam_load camera_load.cpp -L/opt/vc/lib -lrt -lbcm_host -lvcos -lmmal_core -lmmal_util -lmmal_vc_client -lvcsm -lpthread
*/


#include <iostream>

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

MMAL_COMPONENT_T *preview_component;
MMAL_COMPONENT_T *camera_component;
MMAL_CONNECTION_T *preview_connection; // Global variable

MMAL_STATUS_T raspipreview_create()
{
   MMAL_COMPONENT_T *preview = 0;
   MMAL_PORT_T *preview_port = NULL;
   MMAL_STATUS_T status;

   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER,
                                  &preview);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to create preview component");
      goto error;
   }

   if (!preview->input_num)
   {
      status = MMAL_ENOSYS;
      vcos_log_error("No input ports found on component");
      goto error;
   }

   preview_port = preview->input[0];

   MMAL_DISPLAYREGION_T param;
   param.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
   param.hdr.size = sizeof(MMAL_DISPLAYREGION_T);

   param.set = MMAL_DISPLAY_SET_LAYER;
   param.layer = 2;

   param.set |= MMAL_DISPLAY_SET_ALPHA;
   param.alpha = 255;

   /* I don't know whether a full screen preview is needed or a windowed
       * view, but I'm keeping the full screen for now.
      if (state->wantFullScreenPreview)
      {
         param.set |= MMAL_DISPLAY_SET_FULLSCREEN;
         param.fullscreen = 1;
      }
      else
      {
         param.set |= (MMAL_DISPLAY_SET_DEST_RECT | MMAL_DISPLAY_SET_FULLSCREEN);
         param.fullscreen = 0;
         param.dest_rect = state->previewWindow;
      }
      */

   param.set |= MMAL_DISPLAY_SET_FULLSCREEN;
   param.fullscreen = 1;

   /* Defaults to -1 so this code is not needed
      if (state->display_num >= 0)
      {
         param.set |= MMAL_DISPLAY_SET_NUM;
         param.display_num = state->display_num;
      }
      */

   status = mmal_port_parameter_set(preview_port, &param.hdr);

   if (status != MMAL_SUCCESS && status != MMAL_ENOSYS)
   {
      vcos_log_error("unable to set preview port parameters (%u)", status);
      goto error;
   }

   /* Enable component */
   status = mmal_component_enable(preview);
   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to enable preview/null sink component (%u)", status);
      goto error;
   }

   preview_component = preview;

   return status;

   error:

   if (preview)
      mmal_component_destroy(preview);
   return status;
}

MMAL_STATUS_T connect_ports(MMAL_PORT_T * output_port, MMAL_PORT_T * input_port, MMAL_CONNECTION_T ** connection) {
  MMAL_STATUS_T status;
  status = mmal_connection_create(connection, output_port, input_port, MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);
  if (status == MMAL_SUCCESS) {
    status = mmal_connection_enable( * connection);
    if (status != MMAL_SUCCESS) mmal_connection_destroy( * connection);
  }
  return status;
}

void raspipreview_destroy()
{
   if (preview_component)
   {
      mmal_component_destroy(preview_component);
      preview_component = NULL;
   }
}

void default_camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
   fprintf(stderr, "Camera control callback  cmd=0x%08x", buffer->cmd);

   if (buffer->cmd == MMAL_EVENT_PARAMETER_CHANGED)
   {
      MMAL_EVENT_PARAMETER_CHANGED_T *param = (MMAL_EVENT_PARAMETER_CHANGED_T *)buffer->data;
      switch (param->hdr.id)
      {
      case MMAL_PARAMETER_CAMERA_SETTINGS:
      {
         MMAL_PARAMETER_CAMERA_SETTINGS_T *settings = (MMAL_PARAMETER_CAMERA_SETTINGS_T *)param;
         vcos_log_error("Exposure now %u, analog gain %u/%u, digital gain %u/%u",
                        settings->exposure,
                        settings->analog_gain.num, settings->analog_gain.den,
                        settings->digital_gain.num, settings->digital_gain.den);
         vcos_log_error("AWB R=%u/%u, B=%u/%u",
                        settings->awb_red_gain.num, settings->awb_red_gain.den,
                        settings->awb_blue_gain.num, settings->awb_blue_gain.den);
      }
      break;
      }
   }
   else if (buffer->cmd == MMAL_EVENT_ERROR)
   {
      vcos_log_error("No data received from sensor. Check all connections, including the Sunny one on the camera board");
   }
   else
   {
      vcos_log_error("Received unexpected camera control callback event, 0x%08x", buffer->cmd);
   }

   mmal_buffer_header_release(buffer);
}

static MMAL_STATUS_T create_camera_component()
{
   MMAL_COMPONENT_T *camera = 0;
   MMAL_ES_FORMAT_T *format;
   MMAL_PORT_T *preview_port = NULL, *video_port = NULL, *still_port = NULL;
   MMAL_STATUS_T status;

   /* Create the component */
   status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Failed to create camera component");
      if (camera) mmal_component_destroy(camera);
      return status;

   }

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Could not set stereo mode : error %d", status);
      if (camera) mmal_component_destroy(camera);
      return status;

   }

   MMAL_PARAMETER_INT32_T camera_num =
       {{MMAL_PARAMETER_CAMERA_NUM, sizeof(camera_num)}, 0};

   status = mmal_port_parameter_set(camera->control, &camera_num.hdr);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Could not select camera : error %d", status);
      if (camera) mmal_component_destroy(camera);
      return status;

   }

   if (!camera->output_num)
   {
      status = MMAL_ENOSYS;
      vcos_log_error("Camera doesn't have output ports");
      if (camera) mmal_component_destroy(camera);
      return status;

   }

   status = mmal_port_parameter_set_uint32(camera->control, MMAL_PARAMETER_CAMERA_CUSTOM_SENSOR_CONFIG, 0);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Could not set sensor mode : error %d", status);
      if (camera) mmal_component_destroy(camera);
      return status;
   }

   preview_port = camera->output[0];
   video_port = camera->output[1];
   still_port = camera->output[2];

   // Enable the camera, and tell it its control callback function
   status = mmal_port_enable(camera->control, default_camera_control_callback);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to enable control port : error %d", status);
      if (camera) mmal_component_destroy(camera);
      return status;
   }

   //  set up the camera configuration
   {
      MMAL_PARAMETER_CAMERA_CONFIG_T cam_config =
          {
              {MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config)},
              .max_stills_w = 1280,
              .max_stills_h = 720,
              .stills_yuv422 = 0,
              .one_shot_stills = 1,
              .max_preview_video_w = 1280,
              .max_preview_video_h = 720,
              .num_preview_video_frames = 3,
              .stills_capture_circular_buffer_height = 0,
              .fast_preview_resume = 0,
              .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC};

      cam_config.max_preview_video_w = 1280;
      cam_config.max_preview_video_h = 720;

      mmal_port_parameter_set(camera->control, &cam_config.hdr);
   }

   // Now set up the port formats

   format = preview_port->format;
   format->encoding = MMAL_ENCODING_OPAQUE;
   format->encoding_variant = MMAL_ENCODING_I420;

   // In this mode we are forcing the preview to be generated from the full capture resolution.
   // This runs at a max of 15fps with the OV5647 sensor.
   format->es->video.width = VCOS_ALIGN_UP(1280, 32);
   format->es->video.height = VCOS_ALIGN_UP(720, 16);
   format->es->video.crop.x = 0;
   format->es->video.crop.y = 0;
   format->es->video.crop.width = 1280;
   format->es->video.crop.height = 720;
   format->es->video.frame_rate.num = 0;
   format->es->video.frame_rate.den = 1;

   status = mmal_port_format_commit(preview_port);
   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera viewfinder format couldn't be set");
      if (camera) mmal_component_destroy(camera);
      return status;
   }

   // Set the same format on the video  port (which we don't use here)
   mmal_format_full_copy(video_port->format, format);
   status = mmal_port_format_commit(video_port);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera video format couldn't be set");
      if (camera) mmal_component_destroy(camera);
      return status;
   }

   // Ensure there are enough buffers to avoid dropping frames
   if (video_port->buffer_num < 3)
      video_port->buffer_num = 3;

   /* Ensure there are enough buffers to avoid dropping frames */
   if (still_port->buffer_num < 3)
      still_port->buffer_num = 3;

   /* Enable component */
   status = mmal_component_enable(camera);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("camera component couldn't be enabled");
      if (camera) mmal_component_destroy(camera);
      return status;
   }

   camera_component = camera;

   return status;
}


static void destroy_camera_component()
{
   if (camera_component)
   {
      mmal_component_destroy(camera_component);
      camera_component = NULL;
   }
}

int main()
{
   MMAL_PORT_T * camera_preview_port = NULL;
   MMAL_PORT_T * preview_input_port = NULL;    
   std::cout << mmal_status_to_string(raspipreview_create());
   std::cout << mmal_status_to_string(create_camera_component());
   camera_preview_port = camera_component -> output[0];
   preview_input_port = preview_component -> input[0];
   connect_ports(camera_preview_port, preview_input_port, & preview_connection);
   sleep(10);
   destroy_camera_component();
   raspipreview_destroy();
   return 0;
}
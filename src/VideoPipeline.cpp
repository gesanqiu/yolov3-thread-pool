//
// headers included
//
#include <VideoPipeline.h>

//
// cb_osd_buffer_probe
//
static GstPadProbeReturn
cb_osd_buffer_probe (
    GstPad* pad, 
    GstPadProbeInfo* info,
    gpointer user_data)
{
    // TS_INFO_MSG_V ("cb_osd_buffer_probe called");

    VideoPipeline* vp = (VideoPipeline*) user_data;
    GstBuffer* buffer = (GstBuffer*) info->data;

    // sync
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        g_mutex_lock (&vp->wait_lock_);
        while (g_atomic_int_get (&vp->sync_count_) <= 0)
            g_cond_wait (&vp->wait_cond_, &vp->wait_lock_);
        if (!g_atomic_int_dec_and_test (&vp->sync_count_)) {
            // TS_INFO_MSG_V ("sync_count_:%d", vp->sync_count_);
        }
        g_mutex_unlock (&vp->wait_lock_);
    }

    // osd the result
    if (vp->get_result_func_) {
        const std::shared_ptr<std::vector<CLASSIFY_DATA> > results =
            vp->get_result_func_ (vp->get_result_args_);
        if (results && vp->proc_result_func_) {
            vp->proc_result_func_ (buffer, results);
        }
    }

    // TS_INFO_MSG_V ("cb_osd_buffer_probe exited");

    return GST_PAD_PROBE_OK;
}

//
// cb_sync_before_buffer_probe
//
static GstPadProbeReturn
cb_sync_before_buffer_probe (
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer user_data)
{
    //TS_INFO_MSG_V ("cb_sync_before_buffer_probe called");

    VideoPipeline* vp = (VideoPipeline*) user_data;
    GstBuffer* buffer = (GstBuffer*) info->data;

    return GST_PAD_PROBE_OK;
}

//
// cb_sync_buffer_probe
//
static GstPadProbeReturn
cb_sync_buffer_probe (
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer user_data)
{
    //TS_INFO_MSG_V ("cb_sync_buffer_probe called");

    VideoPipeline* vp = (VideoPipeline*) user_data;
    GstBuffer* buffer = (GstBuffer*) info->data;

    // sync
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        g_mutex_lock (&vp->wait_lock_);
        g_atomic_int_inc (&vp->sync_count_);
        g_cond_signal (&vp->wait_cond_);
        g_mutex_unlock (&vp->wait_lock_);
    }

    //TS_INFO_MSG_V ("cb_sync_buffer_probe exited");

    return GST_PAD_PROBE_OK;
}

//
// seek_decoded_file
//
static gboolean
seek_decoded_file (
    gpointer user_data)
{
    TS_INFO_MSG_V ("seek_decoded_file called");

	VideoPipeline* vp = (VideoPipeline*) user_data;

    gst_element_set_state (vp->pipeline_, GST_STATE_PAUSED);

    if (!gst_element_seek (vp->pipeline_, 1.0, GST_FORMAT_TIME,
        (GstSeekFlags) (GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        GST_WARNING ("Error in seeking pipeline");
    }

    gst_element_set_state (vp->pipeline_, GST_STATE_PLAYING);

    return false;
}

//
// restart_stream_buffer_probe
//
static GstPadProbeReturn
restart_stream_buffer_probe (
	GstPad* pad, 
	GstPadProbeInfo* info,
    gpointer user_data)
{
    TS_INFO_MSG_V ("restart_stream_buffer_probe called");

	VideoPipeline* vp = (VideoPipeline*) user_data;
	GstEvent* event = GST_EVENT (info->data);

	if ((info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
		GST_BUFFER_PTS(GST_BUFFER(info->data)) += vp->prev_accumulated_base_;
	}

	if ((info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH)) {
		if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
			g_timeout_add (1, seek_decoded_file, vp);
		}

		if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
			GstSegment *segment;
			gst_event_parse_segment (event, (const GstSegment **) &segment);
			segment->base = vp->accumulated_base_;
			vp->prev_accumulated_base_ = vp->accumulated_base_;
			vp->accumulated_base_ += segment->stop;
		}

		switch (GST_EVENT_TYPE (event)) {
			case GST_EVENT_EOS:
			case GST_EVENT_QOS:
			case GST_EVENT_SEGMENT:
			case GST_EVENT_FLUSH_START:
			case GST_EVENT_FLUSH_STOP:
				return GST_PAD_PROBE_DROP;
			default:
				break;
		}
	}
    
	return GST_PAD_PROBE_OK;
}

//
// cb_decodebin_child_added
//
static void
cb_decodebin_child_added (
    GstChildProxy* child_proxy, 
    GObject* object,
    gchar* name, 
    gpointer user_data)
{
    TS_INFO_MSG_V ("cb_decodebin_child_added called");

    TS_INFO_MSG_V ("Element '%s' added to decodebin", name);
	VideoPipeline* vp = (VideoPipeline*) user_data;

    if (g_strstr_len (name, -1, "qtivdec") == name) {
        g_object_set (object, "turbo", vp->config_.qtivdec_turbo_, NULL);
        g_object_set (object, "skip-frames", vp->config_.qtivdec_skip_frames_, NULL);

        if (g_strstr_len(vp->config_.uri_.c_str(), -1, "file:/") == vp->config_.uri_.c_str() &&
            vp->config_.file_loop_) {
            TS_ELEM_ADD_PROBE (vp->source_buffer_probe_, GST_ELEMENT(object),
                "sink", restart_stream_buffer_probe, (GstPadProbeType) (
                GST_PAD_PROBE_TYPE_EVENT_BOTH | GST_PAD_PROBE_TYPE_EVENT_FLUSH | 
                GST_PAD_PROBE_TYPE_BUFFER), vp);
        }
    }

done:
    return;
}

//
// cb_uridecodebin_source_setup
//
static void
cb_uridecodebin_source_setup (
	GstElement* object, 
	GstElement* arg0, 
	gpointer user_data)
{
	VideoPipeline* vp = (VideoPipeline*) user_data;

    TS_INFO_MSG_V ("cb_uridecodebin_source_setup called");

	if (g_object_class_find_property (G_OBJECT_GET_CLASS (arg0), "latency")) {
		TS_INFO_MSG_V ("cb_uridecodebin_source_setup set %d latency", 
            vp->config_.rtsp_latency_);
		g_object_set (G_OBJECT (arg0), "latency", vp->config_.rtsp_latency_, NULL);
	}
}

//
// cb_uridecodebin_pad_added
//
static void
cb_uridecodebin_pad_added (
    GstElement* decodebin, 
    GstPad* pad, 
    gpointer user_data)
{
    GstCaps* caps = gst_pad_query_caps (pad, NULL);
    const GstStructure* str = gst_caps_get_structure (caps, 0);
    const gchar* name = gst_structure_get_name (str);

    TS_INFO_MSG_V ("cb_uridecodebin_pad_added called");
    TS_INFO_MSG_V ("structure:%s", gst_structure_to_string(str));

    if (!strncmp (name, "video", 5)) {
        VideoPipeline* vp = (VideoPipeline*) user_data;
        GstPad* sinkpad = gst_element_get_static_pad (vp->tee0_, "sink");

        if (sinkpad && gst_pad_link (pad, sinkpad) == GST_PAD_LINK_OK) {
            TS_INFO_MSG_V ("Success to link uridecodebin to pipeline");
            GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(vp->pipeline_), 
                GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");
        } else {
            TS_ERR_MSG_V ("Failed to link uridecodebin to pipeline");
        }

        if (sinkpad) {
            gst_object_unref (sinkpad);
        }
    }

    gst_caps_unref (caps);
}
    
//
// cb_uridecodebin_child_added
//
static void
cb_uridecodebin_child_added (
    GstChildProxy* child_proxy, 
    GObject* object,
    gchar* name, 
    gpointer user_data)
{
    TS_INFO_MSG_V ("cb_uridecodebin_child_added called");

    TS_INFO_MSG_V ("Element '%s' added to uridecodebin", name);
    VideoPipeline* vp = (VideoPipeline*) user_data;

    if (g_strrstr (name, "decodebin") == name) {
        g_signal_connect (G_OBJECT (object), "child-added",
            G_CALLBACK (cb_decodebin_child_added), vp);
    }

    if ((g_strrstr (name, "h264parse") == name) ||
        (g_strrstr (name, "h265parse") == name)) {
        g_object_set (object, "config_-interval", -1, NULL);
    }

done:
    return;
}

//
// cb_appsink_new_sample
//
GstFlowReturn 
cb_appsink_new_sample (
    GstElement* sink, 
    gpointer user_data)
{
    //TS_INFO_MSG_V ("cb_appsink_new_sample called");

    VideoPipeline* vp = (VideoPipeline*) user_data;
    GstSample* sample = NULL;
    GstBuffer* buffer = NULL;
    GstMapInfo* map;
    GstStructure* info = NULL;
    int sample_width = 0;
    int sample_height = 0;
    GstCaps* caps = NULL;

    g_signal_emit_by_name (sink, "pull-sample", &sample);

    if (sample) {
        // control the frame rate of video submited to analyzer
        if (0 == vp->first_frame_timestamp_) {
            vp->first_frame_timestamp_ = g_get_real_time ();
        } else {
            gint64 period_us_count = g_get_real_time () - vp->first_frame_timestamp_;
            double cur = (double)(vp->appsinked_frame_count_*1000*1000) / (double)(period_us_count);
            double dst = (double)(vp->config_.output_fps_n_) / (double)(vp->config_.output_fps_d_);
            // TS_INFO_MSG_V ("%2.2f, %2.2f, %d/%d", cur, dst, vp->config_.output_fps_n_,
            //    vp->config_.output_fps_d_);

            if (cur > dst) {
                 gst_sample_unref (sample);
                 return GST_FLOW_OK;
            }
        }

        if (vp->put_frame_func_) {
            vp->put_frame_func_(sample, vp->put_frame_args_);
            vp->appsinked_frame_count_++;
        } else {
            gst_sample_unref (sample);
        }
    }

    return GST_FLOW_OK;
}

//
// VideoPipeline
//
VideoPipeline::VideoPipeline (
    const VideoPipelineConfig& config)
{
    g_mutex_init (&wait_lock_);
    g_cond_init  (&wait_cond_);
    g_mutex_init (&lock_);
    pipeline_ = NULL;
    config_ = config;
    live_source_ = false;
    osd_buffer_probe_ = 0;
    source_buffer_probe_ = 0;
    sync_before_buffer_probe_ = 0;
    sync_buffer_probe_ = 0;
    accumulated_base_ = 0;
    prev_accumulated_base_ = 0;
    appsinked_frame_count_ = 0;
    first_frame_timestamp_ = 0;
    last_frame_timestamp_ = 0;
    sync_count_ = 0;
    put_frame_func_ = NULL;
    put_frame_args_ = NULL;
    get_result_func_ = NULL;
    get_result_args_ = NULL;
    proc_result_func_ = NULL;
    proc_result_args_ = NULL;
}

//
// ~VideoPipeline
//
VideoPipeline::~VideoPipeline (void)
{

}

//
// Create
//
bool
VideoPipeline::Create (void)
{
    GstCapsFeatures* feature;
    GstCaps* caps;

    if (g_strrstr (config_.uri_.c_str(), "file:/")) {
        live_source_ = false;
    } else {
        live_source_ = true;
    }

    if (!(pipeline_ = gst_pipeline_new ("video-pipeline"))) {
        TS_ERR_MSG_V ("Failed to create pipeline named video");
        goto done;
    }

    if (!(source_ = gst_element_factory_make ("uridecodebin", "source"))) {
        TS_ERR_MSG_V ("Failed to create element uridecodebin named source");
        goto done;
    }

    g_object_set (G_OBJECT (source_), "uri", config_.uri_.c_str(), NULL);

    g_signal_connect (G_OBJECT (source_), "source-setup", G_CALLBACK (
        cb_uridecodebin_source_setup), this);
    g_signal_connect (G_OBJECT (source_), "pad-added",    G_CALLBACK (
        cb_uridecodebin_pad_added),    this);
    g_signal_connect (G_OBJECT (source_), "child-added",  G_CALLBACK (
        cb_uridecodebin_child_added),  this);

    gst_bin_add_many (GST_BIN (pipeline_), source_, NULL);

    if (!(tee0_ = gst_element_factory_make ("tee", "tee0"))) {
        TS_ERR_MSG_V ("Failed to create element tee named tee0");
        goto done;
    }
    
    gst_bin_add_many (GST_BIN (pipeline_), tee0_, NULL);
    
    if (!(queue0_ = gst_element_factory_make ("queue", "queue0"))) {
        TS_ERR_MSG_V ("Failed to create element queue named queue0");
        goto done;
    }
    
    gst_bin_add_many (GST_BIN (pipeline_), queue0_, NULL);
    
    if (!(queue1_ = gst_element_factory_make ("queue", "queue1"))) {
        TS_ERR_MSG_V ("Failed to create element queue named queue1");
        goto done;
    }
    
    gst_bin_add_many (GST_BIN (pipeline_), queue1_, NULL);
    
    TS_LINK_ELEMENT (tee0_, queue0_);
    TS_LINK_ELEMENT (tee0_, queue1_);

    if (!(qtioverlay_ = gst_element_factory_make ("qtioverlay", "osd"))) {
        TS_ERR_MSG_V ("Failed to create element qtioverlay named osd");
        goto done;
    }

    gst_bin_add_many (GST_BIN (pipeline_), qtioverlay_, NULL);

    if (!(display_ = gst_element_factory_make ("waylandsink", "display"))) {
        TS_ERR_MSG_V ("Failed to create element waylandsink named display");
        goto done;
    }
    
    g_object_set (G_OBJECT (display_), "x",      config_.display_x_,      NULL);
    g_object_set (G_OBJECT (display_), "y",      config_.display_y_,      NULL);
    g_object_set (G_OBJECT (display_), "width",  config_.display_width_,  NULL);
    g_object_set (G_OBJECT (display_), "height", config_.display_height_, NULL);
    g_object_set (G_OBJECT (display_), "sync",   config_.display_sync_,   NULL);
    
    gst_bin_add_many (GST_BIN (pipeline_), display_, NULL);

    TS_LINK_ELEMENT (queue0_, qtioverlay_);
    TS_LINK_ELEMENT (qtioverlay_, display_);

    if (!(transform_ = gst_element_factory_make ("qtivtransform", "transform"))) {
        TS_ERR_MSG_V ("Failed to create element qtivtransform named transform");
        goto done;
    }

    if (config_.crop_x_ >= 0) {
        g_object_set (G_OBJECT(transform_), "crop-x", 
            config_.crop_x_, NULL);
    }

    if (config_.crop_x_ >= 0) {
        g_object_set (G_OBJECT(transform_), "crop-y", 
            config_.crop_y_, NULL);
    }

    if (config_.crop_width_ > 0) {
        g_object_set (G_OBJECT(transform_), "crop-width", 
            config_.crop_width_, NULL);
    }

    if (config_.crop_height_ > 0) {
        g_object_set (G_OBJECT(transform_), "crop-height", 
            config_.crop_height_,NULL);
    }

    gst_bin_add_many (GST_BIN ((pipeline_)), transform_, NULL);
    
    caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, 
        config_.output_format_.c_str(), "width", G_TYPE_INT, config_.output_width_,
          "height", G_TYPE_INT, config_.output_height_, NULL);
    if (!(capfilter_ = gst_element_factory_make("capsfilter", "capfilter"))) {
        TS_ERR_MSG_V ("Failed to create element capsfilter named capfilter");
        goto done;
    }
    
    g_object_set (G_OBJECT(capfilter_), "caps", caps, NULL);
    gst_caps_unref (caps);
    
    gst_bin_add_many (GST_BIN (pipeline_), capfilter_, NULL);
    
    if (!(appsink_ = gst_element_factory_make ("appsink", "appsink"))) {
        TS_ERR_MSG_V ("Failed to create element appsink named appsink");
        goto done;
    }
    
    g_object_set (appsink_, "emit-signals", true, NULL);

    g_signal_connect (appsink_, "new-sample", G_CALLBACK (
        cb_appsink_new_sample), this);

    gst_bin_add_many (GST_BIN (pipeline_), appsink_, NULL);

    TS_LINK_ELEMENT (queue1_,   transform_);
    TS_LINK_ELEMENT (transform_, capfilter_);
    TS_LINK_ELEMENT (capfilter_, appsink_);

    TS_ELEM_ADD_PROBE (sync_before_buffer_probe_, GST_ELEMENT(transform_),
        "sink", cb_sync_before_buffer_probe, (GstPadProbeType) (
        GST_PAD_PROBE_TYPE_BUFFER), this);

    TS_ELEM_ADD_PROBE (sync_buffer_probe_, GST_ELEMENT(transform_),
        "src", cb_sync_buffer_probe, (GstPadProbeType) (
        GST_PAD_PROBE_TYPE_BUFFER), this);

    TS_ELEM_ADD_PROBE (osd_buffer_probe_, GST_ELEMENT(queue0_),
        "src", cb_osd_buffer_probe, (GstPadProbeType) (
        GST_PAD_PROBE_TYPE_BUFFER), this);

    return true;
    
done:
    TS_ERR_MSG_V ("Failed to create video pipeline");
    
    return false;
}

//
// Start
//
bool VideoPipeline::Start(void)
{
    if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline_,
        GST_STATE_PLAYING)) {
        TS_ERR_MSG_V ("Failed to set pipeline to playing state");
        return false;
    }

    return true;
}

//
// Pause
//
bool VideoPipeline::Pause(void)
{
    GstState state, pending;
    
    TS_INFO_MSG_V ("StartPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        pipeline_, &state, &pending, 5 * GST_SECOND / 1000)) {
        TS_WARN_MSG_V ("Failed to get state of pipeline");
        return false;
    }

    if (state == GST_STATE_PLAYING) {
        return true;
    } else if (state == GST_STATE_PAUSED) {
        gst_element_set_state (pipeline_, GST_STATE_PLAYING);
        gst_element_get_state (pipeline_, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        TS_WARN_MSG_V ("Invalid state of pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

//
// Resume
//
bool VideoPipeline::Resume (void)
{
    GstState state, pending;
    
    TS_INFO_MSG_V ("StopPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        pipeline_, &state, &pending, 5 * GST_SECOND / 1000)) {
        TS_WARN_MSG_V ("Failed to get state of pipeline");
        return false;
    }

    if (state == GST_STATE_PAUSED) {
        return true;
    } else if (state == GST_STATE_PLAYING) {
        gst_element_set_state (pipeline_, GST_STATE_PAUSED);
        gst_element_get_state (pipeline_, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        TS_WARN_MSG_V ("Invalid state of pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

//
// Destroy
//
void VideoPipeline::Destroy (void)
{
    if (pipeline_) {
        gst_element_set_state (pipeline_, GST_STATE_NULL);
        gst_object_unref (pipeline_);
        pipeline_ = NULL;
    }

    g_mutex_clear (&lock_);
    g_mutex_clear (&wait_lock_);
    g_cond_clear  (&wait_cond_);
}

//ta
// SetCallback
//
void VideoPipeline::SetCallback (
    cbPutData cb, 
    void* args)
{
    put_frame_func_ = cb;
    put_frame_args_ = args;
}

//
// SetCallback
//
void VideoPipeline::SetCallback (
    cbGetResult func, 
    void* args)
{
    get_result_func_ = func;
    get_result_args_ = args;
}

//
// SetCallback
//
void VideoPipeline::SetCallback (
    cbProcResult func, 
    void* args)
{
    proc_result_func_ = func;
    proc_result_args_ = args;
}


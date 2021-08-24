#ifndef __VIDEO_PIPELINE_H__
#define __VIDEO_PIPELINE_H__

#include "Common.h"

//
// TS_LINK_ELEMENT
//
#define TS_LINK_ELEMENT(elem1, elem2) \
    do { \
        if (!elem1 || !elem2) { \
            TS_INFO_MSG_V("WOW! Elments(%p,%p) to link are not all non-null", elem1, elem2); \
            goto done; \
        } \
		if (!gst_element_link (elem1,elem2)) { \
            GstCaps* src_caps = gst_pad_query_caps ((GstPad *) (elem1)->srcpads->data, NULL); \
            GstCaps* sink_caps = gst_pad_query_caps ((GstPad *) (elem2)->sinkpads->data, NULL); \
			TS_ERR_MSG_V ("Failed to link '%s' (%s) and '%s' (%s)", \
				GST_ELEMENT_NAME (elem1), gst_caps_to_string (src_caps), \
				GST_ELEMENT_NAME (elem2), gst_caps_to_string (sink_caps)); \
			goto done; \
		} else { \
            GstCaps* src_caps = (GstPad *) (elem1)->srcpads?gst_pad_query_caps ( \
                (GstPad *) (elem1)->srcpads->data, NULL):NULL; \
            GstCaps* sink_caps = (GstPad *) (elem2)->sinkpads?gst_pad_query_caps ( \
                (GstPad *) (elem2)->sinkpads->data, NULL):NULL; \
    		TS_INFO_MSG_V ("Success to link '%s' (%s) and '%s' (%s)", \
                GST_ELEMENT_NAME (elem1), src_caps?gst_caps_to_string (src_caps):"NONE", \
                GST_ELEMENT_NAME (elem2), sink_caps?gst_caps_to_string (sink_caps):"NONE"); \
        } \
    } while (0)

//
// TS_ELEM_ADD_PROBE
//
#define TS_ELEM_ADD_PROBE(probe_id, elem, pad, probe_func, probe_type, probe_data) \
	do { \
		GstPad *gstpad = gst_element_get_static_pad (elem, pad); \
		if (!gstpad) { \
			TS_ERR_MSG_V ("Could not find '%s' in '%s'", pad, GST_ELEMENT_NAME(elem)); \
			goto done; \
		} \
		probe_id = gst_pad_add_probe(gstpad, (probe_type), probe_func, probe_data, NULL); \
		gst_object_unref (gstpad); \
	} while (0)

//
// TS_ELEM_REMOVE_PROBE
//
#define TS_ELEM_REMOVE_PROBE(probe_id, elem, pad) \
    do { \
        if (probe_id == 0 || !elem) { \
            break; \
        } \
        GstPad *gstpad = gst_element_get_static_pad (elem, pad); \
        if (!gstpad) { \
            TS_ERR_MSG_V ("Could not find '%s' in '%s'", pad, \
                GST_ELEMENT_NAME(elem)); \
            break; \
        } \
        gst_pad_remove_probe(gstpad, probe_id); \
        gst_object_unref (gstpad); \
    } while (0)

typedef struct _VideoPipelineConfig
{
	/*-------------------------------rtspsrc-------------------------------*/
	std::string  uri_                          { "rtsp://admin:ZKCD1234@10.0.23.227:554" };
	unsigned int rtsp_latency_                 { 0 };
    bool         file_loop_                    { false };
    int          rtsp_reconnect_interval_secs_ { -1 };
    unsigned int rtp_protocols_select_         { 7 };
    /*-----------------------------waylandsink-----------------------------*/
	unsigned int display_x_                    { 0 };
	unsigned int display_y_                    { 0 };
	unsigned int display_width_                { 1920 };
	unsigned int display_height_               { 1080 };
	bool         display_sync_                 { false };
    /*----------------------------qtivtransform----------------------------*/
    int          crop_x_                       { -1 };
	int          crop_y_                       { -1 };
	int          crop_width_                   { -1 };
	int          crop_height_                  { -1 };
	std::string  output_format_                { "BGR" };
    unsigned int output_width_                 { 1920 };
    unsigned int output_height_                { 1080 };
    int          output_fps_n_                 { 25 };
    int          output_fps_d_                 { 50 };
    /*-------------------------------qtivdec-------------------------------*/
	bool         qtivdec_turbo_                { true };
	bool         qtivdec_skip_frames_          { true };
    /*------------------------------omxh264enc-----------------------------*/
    bool         rtmp_enable_                  { false };
	unsigned int enc_bitrate_                  { 4000 };
	unsigned int enc_fps_                      { 25 };
    /*-------------------------------rtmpsink------------------------------*/
	unsigned int rtmp_latency_                 { 0 };
	bool         rtmp_sync_                    { false };
	std::string  rtmp_url_                     { "rtmp://52.81.79.48:1935/live/mask/1" };
}VideoPipelineConfig;

//
// VideoPipeline
//
class VideoPipeline
{
public:
    VideoPipeline (const VideoPipelineConfig& config);
	bool Create   (void);
    bool Start    (void);
    bool Pause    (void);
    bool Resume   (void);
    void Destroy  (void);
    void SetCallback (cbPutData    func, void* args);
    void SetCallback (cbGetResult  func, void* args);
    void SetCallback (cbProcResult func, void* args);
    ~VideoPipeline(void);

public:
	VideoPipelineConfig config_;
    GMutex              wait_lock_;
    GCond               wait_cond_;
    GMutex              lock_;
    unsigned long       osd_buffer_probe_;
    unsigned long       source_buffer_probe_;
    unsigned long       sync_before_buffer_probe_;
    unsigned long       sync_buffer_probe_;
    bool                live_source_;
    uint64              accumulated_base_;
    uint64              prev_accumulated_base_;
    uint64              appsinked_frame_count_;
    uint64              first_frame_timestamp_;
    uint64              last_frame_timestamp_;
    volatile int        sync_count_;

    cbPutData           put_frame_func_;
    void*               put_frame_args_;
    cbGetResult         get_result_func_;
    void*               get_result_args_;
    cbProcResult        proc_result_func_;
    void*               proc_result_args_;


    GstElement* pipeline_    { NULL };
	GstElement* source_      { NULL };
    GstElement* tee0_        { NULL };
    GstElement* queue0_      { NULL };
    GstElement* queue1_      { NULL };
    GstElement* qtioverlay_  { NULL };
    GstElement* display_     { NULL };
    GstElement* h264parse1_  { NULL };
    GstElement* transform_   { NULL };
    GstElement* capfilter_   { NULL };
    GstElement* appsink_     { NULL };
};


#endif // __VIDEO_PIPELINE_H__
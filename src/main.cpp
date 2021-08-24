#include <unistd.h>
#include <sys/stat.h>
#include <gflags/gflags.h>
#include <ml-meta/ml_meta.h>

#include "PipelineInterface.h"
#include "ThreadPool.h"
#include "DataDoubleCache.h"
#include "DataMailbox.h"

std::vector<std::string> g_labels;

static bool validateCfg(const char* name, const std::string& value)
{
    if (!value.compare("")) {
        TS_ERR_MSG_V ("Config file required!");
        return false;
    }

    struct stat statbuf;
    if (!stat(value.c_str(), &statbuf)) {
        TS_INFO_MSG_V ("Found config file: %s", value.c_str());
        return true;
    }

    TS_ERR_MSG_V ("Invalid config file.");
    return false;
}

DEFINE_string(cfg, "../configs/yolov3.json", "config file in json format.");
DEFINE_validator(cfg, &validateCfg);

static bool validateCnt(const char* name, int value)
{
    if (value > 0&& value < 7) return true;

    TS_INFO_MSG_V ("Invalid runtime count.");
    return false;
}

DEFINE_int32(count, 0, "number of SNPE runtime Entity(Thread pool size).(0-6)");
DEFINE_validator(count, &validateCnt);

static bool validateLabelPath(const char* name, const std::string& value)
{
    if (!value.compare("")) {
        TS_ERR_MSG_V ("Label file required!");
        return false;
    }

    struct stat statbuf;
    if (!stat(value.c_str(), &statbuf)) {
        TS_INFO_MSG_V ("Found Label file: %s", value.c_str());
        return true;
    }

    TS_ERR_MSG_V ("Invalid Label file.");
    return false;
}

DEFINE_string(labelpath, "../models/labels.txt", "label file of AI model.");
DEFINE_validator(labelpath, &validateLabelPath);

static GMainLoop* gbl_main_loop = NULL;

static void drawYUVRect (
    unsigned char* data, 
    int   width, 
    int   height, 
    char* format, 
    int   rect_x, 
    int   rect_y, 
    int   rect_w, 
    int   rect_h, 
    unsigned char color_y,
    unsigned char color_u, 
    unsigned char color_v,
    int thick)
{
    rect_x = ((rect_x / 2) * 2);
    rect_y = ((rect_y / 2) * 2);
    rect_w = ((rect_w / 2) * 2);
    rect_h = ((rect_h / 2) * 2);
    thick  = ((thick+1)/2) * 2 ;
    
    if (rect_x + rect_w + thick >= width) {
        rect_w = width  - rect_x - thick - 1;
        rect_w = (rect_w / 2) * 2;
    }
    if (rect_y + rect_h + thick >= height) {
        rect_h = height - rect_y - thick - 1;
        rect_h = (rect_h / 2) * 2;
    }
    if (rect_w <= 0 || rect_h <= 0) {
        return;
    }

    unsigned char* py;
    unsigned char* pu;
    unsigned char* pv;
    int offset;

    if (0 == strcmp (format, "YUY2"))   // YUY2 FORMAT IMAGE
    {
        offset = width  * 2 * rect_y;
        py = data + offset + rect_x * 2;
        pu = py + 1;
        pv = py + 3;
        
        for (int i = 0; i < rect_w / 2; i++) {
            for (int j = 0; j < thick / 2; j++) {
                *(py+width*2*j+i*4+0) = color_y;
                *(py+width*2*j+i*4+2) = color_y;
                *(pu+width*2*j+i*4)   = color_u;
                *(pv+width*2*j+i*4)   = color_v;
            }
        }
        
        offset = width  * 2 * (rect_y + rect_h);
        py = data + offset     + rect_x * 2;
        pu = py + 1;
        pv = py + 3;
        
        for (int i = 0; i < (rect_w + thick / 2) / 2; i++) {
            for (int j = 0; j < thick / 2; j++) {
                *(py+width*2*j+i*4+0) = color_y;
                *(py+width*2*j+i*4+2) = color_y;
                *(pu+width*2*j+i*4)   = color_u;
                *(pv+width*2*j+i*4)   = color_v;
            }
        }
        
        offset = width  * 2 * rect_y;
        py = data + offset + rect_x * 2;
        pu = py + 1;
        pv = py + 3;
        
        for (int i = 0; i < rect_h; i++) {
            for (int j = 0; j < thick / 4; j++) {
                *(py+width*i*2+j*4+0) = color_y;
                *(py+width*i*2+j*4+2) = color_y;
                *(pu+width*i*2+j*4)   = color_u;
                *(pv+width*i*2+j*4)   = color_v;
            }
        }
        
        offset = width  * 2 * rect_y;
        py = data + offset + (rect_x + rect_w) * 2;
        pu = py + 1;
        pv = py + 3;
        
        for (int i = 0; i < rect_h; i++) {
            for (int j = 0; j < thick / 4; j++) {
                *(py+width*i*2+j*4+0) = color_y;
                *(py+width*i*2+j*4+2) = color_y;
                *(pu+width*i*2+j*4)   = color_u;
                *(pv+width*i*2+j*4)   = color_v;
            }
        }
    }
    
    else if (0 == strcmp (format, "NV12"))  // NV12 FORMAT IMAGE
    {
        unsigned char* y = data;
        unsigned char* u = data + width * height;
        unsigned char* v = u + 1;
        
        offset = width      * rect_y;
        py = y + offset     + rect_x;
        pu = u + offset / 2 + rect_x;
        pv = v + offset / 2 + rect_x;
        
        for (int i = 0; i < rect_w / 2; i++) {
            for (int j = 0; j < thick; j++) {
                *(py+width*j+i*2+0) = color_y;
                *(py+width*j+i*2+1) = color_y;
            }
        }
        for (int i = 0; i < rect_w / 2; i++) {
            for (int j = 0; j < thick / 2; j++) {
                *(pu+width*j+i*2) = color_u;
                *(pv+width*j+i*2) = color_v;
            }
        }

        offset = width      * (rect_y + rect_h);
        py = y + offset     +  rect_x;
        pu = u + offset / 2 +  rect_x;
        pv = v + offset / 2 +  rect_x;
        
        for (int i = 0; i < (rect_w + thick) / 2; i++) {
            for (int j = 0; j < thick; j++) {
                *(py+width*j+i*2+0) = color_y;
                *(py+width*j+i*2+1) = color_y;
            }
        }
        for (int i=0; i < (rect_w + thick) / 2; i++) {
            for (int j = 0; j < thick / 2; j++) {
                *(pu+width*j+i*2) = color_u;
                *(pv+width*j+i*2) = color_v;
            }
        }
        
        offset = width      * rect_y;
        py = y + offset     + rect_x;
        pu = u + offset / 2 + rect_x;
        pv = v + offset / 2 + rect_x;
        
        for (int i = 0; i < rect_h; i++) {
            for (int j = 0; j < thick; j++) {
                *(py+i*width+j) = color_y;
            }
        }
        for (int i=0; i<rect_h/2; i++) {
            for (int j = 0; j < thick / 2; j++) {
                *(pu+width*i+j*2) = color_u;
                *(pv+width*i+j*2) = color_v;
            }
        }
        
        offset = width      *  rect_y;
        py = y + offset     + (rect_x + rect_w);
        pu = u + offset / 2 + (rect_x + rect_w);
        pv = v + offset / 2 + (rect_x + rect_w);
        
        for (int i=0; i<rect_h; i++) {
            for (int j = 0; j < thick; j++) {
                *(py+i*width+j) = color_y;
            }
        }
        for (int i=0; i<rect_h/2; i++) {
            for (int j = 0; j < thick / 2; j++) {
                *(pu+width*i+j*2) = color_u;
                *(pv+width*i+j*2) = color_v;
            }
        }
    }
}


static void putData(GstSample* sample, void* user_data)
{
    // TS_INFO_MSG_V ("putData called");

    ThreadPool *tp = (ThreadPool*) user_data;

    int width, height; 
    GstCaps* caps = gst_sample_get_caps (sample);
    GstStructure* structure = gst_caps_get_structure (caps, 0);
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);
    std::string format ((char*)gst_structure_get_string (
        structure, "format"));
    if (0 != format.compare("BGR")) {
        TS_ERR_MSG_V ("Invalid format(%s!=BGR)", format.c_str());
        gst_sample_unref(sample);
        return ;
    }

    GstMapInfo map;
    GstBuffer* buf = gst_sample_get_buffer (sample);
    gst_buffer_map (buf, &map, GST_MAP_READ);
    cv::Mat tmpMat(height, width, CV_8UC3,
                (unsigned char*)map.data, cv::Mat::AUTO_STEP);
    tmpMat = tmpMat.clone();
    gst_buffer_unmap (buf, &map);
    gst_sample_unref(sample);

    tp->submit(std::make_shared<cv::Mat>(tmpMat));
}

bool putResult(
    const std::shared_ptr<std::vector<CLASSIFY_DATA> >& result,
    void* user_data)
{
    // TS_INFO_MSG_V ("putResult called");

    DataDoubleCache<std::vector<CLASSIFY_DATA> >* dm =
        (DataDoubleCache<std::vector<CLASSIFY_DATA> >*) user_data;

     if (result) {
        if (!dm->Post(result)) {
            TS_WARN_MSG_V ("Failed to post a CLASSIFY_DATA to result manager");
            return false;
        }
    }

    return true;
}

std::shared_ptr<std::vector<CLASSIFY_DATA> > getResult(void* user_data)
{
    // TS_INFO_MSG_V ("getResult called");

    DataDoubleCache<std::vector<CLASSIFY_DATA> >* dm =
        (DataDoubleCache<std::vector<CLASSIFY_DATA> >*) user_data;
    
    std::shared_ptr<std::vector<CLASSIFY_DATA> > result =  NULL;

    if (!dm->Pend(result, 200)) {
        TS_WARN_MSG_V ("Failed to get a CLASSIFY_DATA to result manager");
        return NULL;
    }

    return result;
}

void osdResult(GstBuffer* buffer,
    const std::shared_ptr<std::vector<CLASSIFY_DATA>>& results)
{
    // TS_INFO_MSG_V ("osdResult called");
    
    GstMapInfo info;
    if (!gst_buffer_map(buffer, &info, 
        (GstMapFlags) (GST_MAP_READ | GST_MAP_WRITE))) {
        TS_WARN_MSG_V ("WHY? WHAT PROBLEM ABOUT SYNC?");
        return;
    }

    GstStructure* structure = (GstStructure*)
        gst_mini_object_get_qdata (GST_MINI_OBJECT (buffer), 
        g_quark_from_static_string ("VIDEOINFO"));
    if (!structure) {
        gst_buffer_unmap(buffer, &info);
        return;
    }

    int image_w = 0, image_h = 0; char image_f[8] = { '\0' };
    gst_structure_get_int (structure, "width",  &image_w);
    gst_structure_get_int (structure, "height", &image_h);
    strncpy (image_f, gst_structure_get_string(structure, 
        "format"), sizeof(image_f));
    
    for (int i = 0; i < results->size(); i++) {
        // const unsigned char R = 0, G = 255, B = 0;
        // const unsigned char Y = 0.257*R + 0.504*G + 0.098*B + 16;
        // const unsigned char U =-0.148*R - 0.291*G + 0.439*B + 128;
        // const unsigned char V = 0.439*R - 0.368*G - 0.071*B + 128;
        if (results->at(i).rect[0]>=0 && results->at(i).rect[0]+results->at(i).rect[2]<=image_w &&
            results->at(i).rect[1]>=0 && results->at(i).rect[1]+results->at(i).rect[3]<=image_h) {
            // drawYUVRect(info.data, image_w, image_h, image_f,
            //     results->at(i).rect[0], results->at(i).rect[1], results->at(i).rect[2], 
            //     results->at(i).rect[3], Y, U, V, 6);
            GstMLDetectionMeta *meta = gst_buffer_add_detection_meta(buffer);
            if (!meta) {
                TS_ERR_MSG_V ("Failed to create metadata");
                return ;
            }

            GstMLClassificationResult *box_info = (GstMLClassificationResult*)malloc(
                sizeof(GstMLClassificationResult));

            uint32_t label_size = g_labels[results->at(i).label].size() + 1;
            box_info->name = (char *)malloc(label_size);
            snprintf(box_info->name, label_size, "%s", g_labels[results->at(i).label].c_str());

            box_info->confidence = results->at(i).confidence;
            meta->box_info = g_slist_append (meta->box_info, box_info);

            meta->bounding_box.x = results->at(i).rect[0];
            meta->bounding_box.y = results->at(i).rect[1];
            meta->bounding_box.width = results->at(i).rect[2];
            meta->bounding_box.height = results->at(i).rect[3];
        }
    }

    gst_buffer_unmap(buffer, &info);
}

int main(int argc, char* argv[])
{
    google::ParseCommandLineFlags (&argc, &argv, true);
    gst_init (&argc, &argv);

    GMainLoop* ml = NULL;

    std::ifstream in(FLAGS_labelpath);
    std::string label;
    while (getline(in, label)) {
        g_labels.push_back(label);
    }
    
    DataDoubleCache<std::vector<CLASSIFY_DATA> >* dm =
        new DataDoubleCache<std::vector<CLASSIFY_DATA> > (NULL);

    ThreadPool* tp = new ThreadPool(FLAGS_count);
    tp->init(0.7, putResult, dm);

    PipelineCallbacks splcbs;
    void* spl;

    cbPutData m_putFrame =
        std::bind(putData, std::placeholders::_1, std::placeholders::_2);
    cbGetResult m_getResult =
        std::bind(getResult, std::placeholders::_1);
    cbProcResult m_procResult =
        std::bind(osdResult, std::placeholders::_1, std::placeholders::_2);

    if (!(spl = splInit (FLAGS_cfg))) {
        TS_ERR_MSG_V ("Failed to initialize stream pipeline instance");
        goto done;
    }

    splcbs.putData = m_putFrame;
    splcbs.getResult = m_getResult;
    splcbs.procResult = m_procResult;
    splcbs.args[0] = tp;
    splcbs.args[1] = dm;
    splcbs.args[2] = NULL;
    splSetCb (spl, splcbs);

    if (!splStart (spl)) {
        TS_ERR_MSG_V ("Failed to start stream pipeline instance");
        goto done;
    }

    if (!(ml = g_main_loop_new (NULL, FALSE))) {
        TS_ERR_MSG_V ("Failed to new a object with type GMainLoop");
        goto done;
    }

    g_main_loop_run (ml);
done:
    if (ml) g_main_loop_unref (ml);

    if (spl) {
        splStop (spl);
        splFina (spl);
        spl = NULL;
    }

    if (tp) {
        tp->shutdown();
        delete tp;
    }

    if (dm) delete dm;

    return 0;
}
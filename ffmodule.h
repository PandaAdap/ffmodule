#pragma once

#include <vector>
#include <string>

#define _CRT_SECURE_NO_WARNINGS

#ifdef __cplusplus
extern "C" {
#endif 

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h" 

#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/channel_layout.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "libavutil/error.h"
#include "libavutil/dict.h"
#include "libavutil/pixfmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/timestamp.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/mathematics.h"
#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"

#ifdef __cplusplus
}
#endif

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "postproc.lib")
#pragma comment(lib, "swscale.lib")

static std::string string_format(const char* format, ...)
{
	char buff[1024] = { 0 };

	va_list args;
	va_start(args, format);
	vsprintf_s(buff, sizeof(buff), format, args);
	va_end(args);

	std::string str(buff);
	return str;
}

/**
ReturnInfo struct.
For function return information.
*/
struct ReturnInfo
{
	std::string ret_str = "";
	int ret_code = 0;
};

/**
MediaInfo struct.
Include media duration,video and audio information.
*/
struct MediaInfo
{
	int duration = 0;				//Second
	//Video
	float video_bitrate = 0.0;		//Kbit/s (Kbps)
	float video_framerate = 0.0;	//fps
	long video_frames = 0;
	int height = 0, width = 0;
	std::string video_encodename;

	//Audio
	std::string audio_encodename;
	int audio_channels = 0;
	float audio_bitrate = 0.0;		//Kbps
	int sample_rate = 0;			//KHz

	ReturnInfo ret;
};

/**
Type for storing frames.
Must call free_frames() after using.
*/
typedef std::vector<AVFrame*> Frames;

/**
Free memory allocated in Frames.
*/
static int free_frames(Frames* src)
{
	for (int a = 0; a < src->size(); a++)
		av_frame_free(&(*src)[a]);
	src->clear();
	return 0;
}

/**
For transcode.
*/
typedef struct StreamContext {
	AVCodecContext* dec_ctx;
	AVCodecContext* enc_ctx;
} StreamContext;


class ffmodule
{
public:
	ffmodule();
	virtual ~ffmodule();

public:

	/**
	* Get media information.
	*
	* @param inputfile - Video file.
	*
	* @return Code 0 on success, a negative value on failure, string return information.
	*
	*/
	MediaInfo GetMediaInfo(std::string inputfile);

	ReturnInfo Transcode(std::string input, std::string output);

	/**
	* Convert AVFrame to PNG file.
	*
	* @param pFrame  - Pointer to user-supplied AVFrame.
	*                  pFrame must be allocated by user before call this function.
	*
	* @param out_file - Path to save the PNG.
	*
	* @return Code 0 on success, a negative value on failure, string return information.
	*
	*/
	ReturnInfo Frame2PNG(AVFrame* pFrame, std::string out_file);

	/**
	* Separate video frames to PNG files.
	*
	* @param inputfile - Video file you want to separate.
	*
	* @param output - Path to save images.
	* 
	* @param startframe - Start separating from here. 
	*					  Must be within the frame range.
	* 
	* @param framecount - The frams you want to separate.
	*					  Negative or 0 to separate all frames after startframe.
	*
	* @param decoder - Decoder.See ffpmeg AVHWDeviceType.
	* 
	* @return Code 0 on success, a negative value on failure, string return information.
	*
	*/
	ReturnInfo SeparateFrames(std::string inputfile, std::string output, int startframe, int framecount, enum AVHWDeviceType decoder);//cuda dxva2 qsv d3d11va opencl vulkan

	/**
	* Separate video frames to memory.
	*
	* @param inputfile - Video file you want to separate.
	*
	* @param output - Pointer to store frames data.
	*
	* @param startframe - Start separating from here.
	*					  Must be within the frame range.
	*
	* @param framecount - The frams you want to separate.
	*					  Negative or 0 to separate all frames after startframe.
	*
	* @param decoder - Decoder.See ffpmeg AVHWDeviceType.
	*
	* @return Code 0 on success, a negative value on failure, string return information.
	*
	*/
	ReturnInfo SeparateFrames(std::string inputfile, Frames *output, int startframe, int framecount, enum AVHWDeviceType decoder);//cuda dxva2 qsv d3d11va opencl vulkan

	/**
	* Separate audio to media files.
	*
	* @param inputfile - Video file you want to separate.
	*
	* @param output - Path to save audio.
	*				  File name force to "output_audio",the format is the same as the original format.
	*
	* @return Code 0 on success, a negative value on failure, string return information.
	*
	*/
	ReturnInfo SeparateAudio(std::string inputfile, std::string output);

	/**
	* Compose image trail to video.
	*
	* @param images_input - Image files path.
	*						Naming must be in the same format. (Such as image-00001.png, image-00002.png...)
	*
	* @param outfile - Path and file name to save media composed.
	* 
	* @param codec - Video encoder.
	*				 (Support libx264, libx265, nvenc, nvenc_hevc, h264_qsv, hevc_qsv)
	*				 The availability of the encoder depends on your hardware platform.
	*
	* @param bitrate - Video bitrate.
	* 
	* @param framerate - Video frame rate.
	* 
	* @return Code 0 on success, a negative value on failure, string return information.
	*
	*/
	ReturnInfo ComposeVideo(std::string images_input, std::string outfile, std::string codec, int bitrate, int framerate);

	/**
	* Compose audio and video.
	*
	* @param video_input - Video file.
	* 
	* @param audio_input - Audio file.
	*
	* @param output - Path and file name to save media composed.
	*
	* @return Code 0 on success, a negative value on failure, string return information.
	*
	*/
	ReturnInfo ComposeAudio(std::string video_input, std::string audio_input, std::string output);

private:

};
#include "ffmodule.h"

enum AVPixelFormat hw_pix_fmt;

ffmodule::ffmodule()
{
    //_format_context = avformat_alloc_context();
}

ffmodule::~ffmodule()
{
    //avformat_close_input(&_format_context);
}

/*Public*/
static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts)
{
	const enum AVPixelFormat* p;
	for (p = pix_fmts; *p != -1; p++) 
	{
		if (*p == hw_pix_fmt)
			return *p;
	}
	return AV_PIX_FMT_NONE;
}


MediaInfo ffmodule::GetMediaInfo(std::string inputfile)
{
    MediaInfo ret;
    AVFormatContext* _format_context = NULL;
	if ((ret.ret.ret_code = avformat_open_input(&_format_context, inputfile.c_str(), NULL, NULL)) < 0)
	{
		ret.ret.ret_str = "Cannot open file.";
		return ret;
	}

    if ((ret.ret.ret_code = avformat_find_stream_info(_format_context, NULL)) < 0)
	{
        ret.ret.ret_str = "Cannot find stream information.";
        return ret;
    }

    ret.video_bitrate = _format_context->bit_rate / 1000.0;//Bitrate Kbit/s(also Kbps)
    ret.duration = _format_context->duration / 1000000.0;//Duration s

    for (unsigned int i = 0; i < _format_context->nb_streams; i++) 
    {
        AVStream* input_stream = _format_context->streams[i];
        if (input_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            ret.video_framerate = (float)input_stream->avg_frame_rate.num / input_stream->avg_frame_rate.den;

            AVCodecParameters* codec_par = input_stream->codecpar;

            ret.width = codec_par->width;
            ret.height = codec_par->height;
            ret.video_frames = input_stream->nb_frames;

            AVCodecContext* avctx_video;
            avctx_video = avcodec_alloc_context3(NULL);
            ret.ret.ret_code = avcodec_parameters_to_context(avctx_video, codec_par);
            if (ret.ret.ret_code < 0)
			{
				ret.ret.ret_str = "Video avcodec_parameters_to_context() failed.";
                avcodec_free_context(&avctx_video);
                return ret;
            }
            char buf[128];
            avcodec_string(buf, sizeof(buf), avctx_video, 0);
            ret.video_encodename = avcodec_get_name((codec_par->codec_id));
			avcodec_free_context(&avctx_video);
        }
        else if (input_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            AVCodecParameters* codec_par = input_stream->codecpar;
            AVCodecContext* avctx_audio;
            avctx_audio = avcodec_alloc_context3(NULL);
            ret.ret.ret_code = avcodec_parameters_to_context(avctx_audio, codec_par);
            if (ret.ret.ret_code < 0) 
			{
				ret.ret.ret_str = "Audio avcodec_parameters_to_context() failed.";
				avcodec_free_context(&avctx_audio);
                return ret;
            }
            ret.audio_encodename = avcodec_get_name(avctx_audio->codec_id);
            ret.audio_bitrate = codec_par->bit_rate / 1000.0;
            ret.audio_channels = codec_par->channels;
            ret.sample_rate = codec_par->sample_rate;
			avcodec_free_context(&avctx_audio);
        }
    }
    avformat_close_input(&_format_context);
    return ret;
}

ReturnInfo ffmodule::Frame2PNG(AVFrame* pFrame, std::string out_file)
{
	ReturnInfo ret;
	AVFormatContext* pFormatCtx = NULL;
	AVStream* video_st = NULL;
	AVCodecContext* pCodecCtx = NULL;
	AVCodec* pCodec = NULL;
	AVPacket* pkt = NULL;

	pFormatCtx = avformat_alloc_context();
	ret.ret_code = avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, out_file.c_str());
	if (ret.ret_code < 0)
	{
		ret.ret_str = "avformat_alloc_output_context2() failed.";
		return ret;
	}

	AVFrame* rgbFrame = av_frame_alloc();
	rgbFrame->width = pFrame->width;
	rgbFrame->height = pFrame->height;
	rgbFrame->format = AV_PIX_FMT_RGB24;

	struct SwsContext* swCtx = sws_getContext(
		pFrame->width,
		pFrame->height,
		AVPixelFormat(pFrame->format),
		pFrame->width,
		pFrame->height,
		AV_PIX_FMT_RGB24,
		SWS_GAUSS, 0, 0, 0);

	av_image_alloc(rgbFrame->data, rgbFrame->linesize, pFrame->width, pFrame->height, AV_PIX_FMT_RGB24, 1);
	sws_scale(swCtx, pFrame->data, pFrame->linesize, 0, pFrame->height, rgbFrame->data, rgbFrame->linesize);

	pCodecCtx = avcodec_alloc_context3(NULL);
	pCodecCtx->codec_id = AV_CODEC_ID_PNG;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_RGB24;
	pCodecCtx->width = pFrame->width;
	pCodecCtx->height = pFrame->height;
	pCodecCtx->time_base = { 1, 30 };

	pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
	if (!pCodec) 
	{
		ret.ret_str = "Encodec not found.";
		ret.ret_code = -1;
		return ret;
	}

	ret.ret_code = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (ret.ret_code < 0) 
	{
		ret.ret_str = "Codec open failed.";
		return ret;
	}

	// start encoder
	ret.ret_code = avcodec_send_frame(pCodecCtx, rgbFrame);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "avcodec_send_frame() failed.";
		return ret;
	}

	pkt = av_packet_alloc();
	av_new_packet(pkt, pFrame->width * pFrame->height * 3);

	//Read encoded data from the encoder.
	ret.ret_code = avcodec_receive_packet(pCodecCtx, pkt);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "avcodec_receive_packet() failed.";
		return ret;
	}

	video_st = avformat_new_stream(pFormatCtx, 0);

	//Write Header
	avformat_write_header(pFormatCtx, NULL);
	//Write body
	av_write_frame(pFormatCtx, pkt);
	//Write Trailer
	av_write_trailer(pFormatCtx);

	avcodec_close(pCodecCtx);
	av_freep(&rgbFrame->data[0]);
	av_frame_free(&rgbFrame);	//av_frame_alloc()
	av_packet_free(&pkt);		//av_packet_alloc()
	avformat_free_context(pFormatCtx);  //avformat_alloc_context()

	sws_freeContext(swCtx);

	pkt = NULL;
	rgbFrame = NULL;
	pFormatCtx = NULL;

	return ret;
}

ReturnInfo ffmodule::SeparateFrames(std::string inputfile, std::string output, int startframe, int framecount, enum AVHWDeviceType decoder)
{
	ReturnInfo ret;		//Return information.

	AVFormatContext* pFormatCtx = NULL;	//ffmpeg format context.
	AVStream* video_st = NULL;			//ffmpeg stream.
	AVCodecContext* pCodecCtx = NULL;	//ffmpeg codec context.
	AVCodec* pCodec = NULL;				//ffmpeg codec.
	AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));	//ffmpeg packet.

	AVFrame* pFrame = av_frame_alloc(),	//ffmpeg frame, recive from packet.
		* hw_frame = av_frame_alloc(),	//ffmpeg frame, store frame datas transfered from device
		* tmp_frame = NULL;				//ffmpeg frame, store frame temporary.

	AVBufferRef* hw_device_ctx = NULL;	//ffmpeg buffer reference.

	int frame_read = 0,					//av_read_frame counter.
		frame_process = 0;				//Frames after processing.

	int v_stream_idx = -1;				//Stream ID from ffmpeg format context.

	ret.ret_code = avformat_open_input(&pFormatCtx, inputfile.c_str(), NULL, NULL);
	if (ret.ret_code != 0)
	{
		ret.ret_str = "avformat_open_input() failed.";
		goto end;
	}

	ret.ret_code = avformat_find_stream_info(pFormatCtx, NULL);
	if(ret.ret_code < 0)
	{
		ret.ret_str = "Cannot find input stream information.";
		goto end;
	}

	/* find the video stream information */
	v_stream_idx = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
	if (v_stream_idx < 0) 
	{
		ret.ret_str = "Cannot find a video stream in the input file";
		ret.ret_code = -1;
		goto end;
	}

	if (startframe < 0 || startframe > pFormatCtx->streams[v_stream_idx]->nb_frames)
	{
		ret.ret_str = "Startframe out of range.";
		ret.ret_code = -1;
		goto end;
	}

	if (decoder != AV_HWDEVICE_TYPE_NONE)
	{
		for (int i = 0;; i++)
		{
			const AVCodecHWConfig* config = avcodec_get_hw_config(pCodec, i);
			if (!config)
			{
				ret.ret_code = -1;
				ret.ret_str = string_format("Decoder %s does not support device type %s.\n", pCodec->name, av_hwdevice_get_type_name(decoder));
				goto end;
			}
			if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == decoder)
			{
				hw_pix_fmt = config->pix_fmt;
				break;
			}
		}
	}
	
	pCodecCtx = avcodec_alloc_context3(pCodec);

	video_st = pFormatCtx->streams[v_stream_idx];
	avcodec_parameters_to_context(pCodecCtx, video_st->codecpar);

	if (decoder != AV_HWDEVICE_TYPE_NONE)
	{
		pCodecCtx->get_format = get_hw_format;

		if ((ret.ret_code = av_hwdevice_ctx_create(&hw_device_ctx, decoder, NULL, NULL, 0)) < 0)
		{
			ret.ret_str = "Failed to create specified hardware device.";
			goto end;
		}
		pCodecCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
	}

	if ((ret.ret_code = avcodec_open2(pCodecCtx, pCodec, NULL)) < 0)
	{
		ret.ret_str = "Failed to open codec.";
		goto end;
	}

	while (av_read_frame(pFormatCtx, packet) >= 0)
	{
		if (packet->stream_index == v_stream_idx)
		{
			if (frame_read >= startframe)
			{
				ret.ret_code = avcodec_send_packet(pCodecCtx, packet);
				if (ret.ret_code < 0)
				{
					ret.ret_str = "Codec open failed.";
					goto end;
				}

				if (avcodec_receive_frame(pCodecCtx, pFrame) != 0)
					continue;

				if (decoder != AV_HWDEVICE_TYPE_NONE && pFrame->format == hw_pix_fmt)
				{
					/* retrieve data from GPU to CPU */
					if ((ret.ret_code = av_hwframe_transfer_data(hw_frame, pFrame, 0)) < 0)
					{
						ret.ret_str = "Error transferring the data to system memory"; 
						goto end;
					}
					tmp_frame = hw_frame;// printf("hw:%d\n", frame_read);
				}
				else
				{
					tmp_frame = pFrame;// printf("sw:%d\n", frame_read);
				}

				std::string savefile = string_format(".\\opt\\img-%06d.png", frame_read);
				Frame2PNG(tmp_frame, savefile);
				frame_process++;
			}

			if (frame_process == framecount)
			{
				av_packet_unref(packet);
				break;
			}
			frame_read++;
		}av_packet_unref(packet);
	}

	/*flush*/
	packet->data = NULL;
	packet->size = 0;
	ret.ret_code = avcodec_send_packet(pCodecCtx, packet);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "Codec open failed.";
		goto end;
	}

	while (1)
	{
		ret.ret_code = avcodec_receive_frame(pCodecCtx, pFrame);
		if (ret.ret_code == AVERROR(EAGAIN) || ret.ret_code == AVERROR_EOF)
		{
			break;
		}
		if (decoder != AV_HWDEVICE_TYPE_NONE && pFrame->format == hw_pix_fmt)
		{
			/* retrieve data from GPU to CPU */
			if ((ret.ret_code = av_hwframe_transfer_data(hw_frame, pFrame, 0)) < 0)
			{
				ret.ret_str = "Error transferring the data to system memory";
				goto end;
			}
			tmp_frame = hw_frame; printf("hw:%d\n", frame_read);
		}
		else
		{
			tmp_frame = pFrame; printf("sw:%d\n", frame_read);
		}

		std::string savefile = string_format(".\\opt\\img-%06d.png", frame_read);
		Frame2PNG(tmp_frame, savefile);

		frame_process++;
	}av_packet_unref(packet);

end:
	av_frame_free(&pFrame);
	av_frame_free(&hw_frame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	avformat_free_context(pFormatCtx);

	if (decoder != AV_HWDEVICE_TYPE_NONE)
		av_buffer_unref(&hw_device_ctx);

	return ret;
}

ReturnInfo ffmodule::SeparateFrames(std::string inputfile, Frames* output, int startframe, int framecount, enum AVHWDeviceType decoder)
{
	ReturnInfo ret;		//Return information.

	AVFormatContext* pFormatCtx = NULL;	//ffmpeg format context.
	AVStream* video_st = NULL;			//ffmpeg stream.
	AVCodecContext* pCodecCtx = NULL;	//ffmpeg codec context.
	AVCodec* pCodec = NULL;				//ffmpeg codec.
	AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));	//ffmpeg packet.

	AVFrame* pFrame = av_frame_alloc(),	//ffmpeg frame, recive from packet.
		* hw_frame = av_frame_alloc();	//ffmpeg frame, store frame datas transfered from device

	AVBufferRef* hw_device_ctx = NULL;	//ffmpeg buffer reference.

	int frame_read = 0,					//av_read_frame counter.
		frame_process = 0;				//Frames after processing.

	int v_stream_idx = -1;				//Stream ID from ffmpeg format context.

	ret.ret_code = avformat_open_input(&pFormatCtx, inputfile.c_str(), NULL, NULL);
	if (ret.ret_code != 0)
	{
		ret.ret_str = "avformat_open_input() failed.";
		goto end;
	}

	ret.ret_code = avformat_find_stream_info(pFormatCtx, NULL);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "Cannot find input stream information.";
		goto end;
	}

	/* find the video stream information */
	v_stream_idx = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
	if (v_stream_idx < 0)
	{
		ret.ret_str = "Cannot find a video stream in the input file";
		ret.ret_code = -1;
		goto end;
	}

	if (startframe < 0 || startframe > pFormatCtx->streams[v_stream_idx]->nb_frames)
	{
		ret.ret_str = "Startframe out of range.";
		ret.ret_code = -1;
		goto end;
	}

	if (decoder != AV_HWDEVICE_TYPE_NONE)
	{
		for (int i = 0;; i++)
		{
			const AVCodecHWConfig* config = avcodec_get_hw_config(pCodec, i);
			if (!config)
			{
				ret.ret_code = -1;
				ret.ret_str = string_format("Decoder %s does not support device type %s.\n", pCodec->name, av_hwdevice_get_type_name(decoder));
				goto end;
			}
			if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == decoder)
			{
				hw_pix_fmt = config->pix_fmt;
				break;
			}
		}
	}

	pCodecCtx = avcodec_alloc_context3(pCodec);

	video_st = pFormatCtx->streams[v_stream_idx];
	avcodec_parameters_to_context(pCodecCtx, video_st->codecpar);

	if (decoder != AV_HWDEVICE_TYPE_NONE)
	{
		pCodecCtx->get_format = get_hw_format;

		if ((ret.ret_code = av_hwdevice_ctx_create(&hw_device_ctx, decoder, NULL, NULL, 0)) < 0)
		{
			ret.ret_str = "Failed to create specified hardware device.";
			goto end;
		}
		pCodecCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
	}

	if ((ret.ret_code = avcodec_open2(pCodecCtx, pCodec, NULL)) < 0)
	{
		ret.ret_str = "Failed to open codec.";
		goto end;
	}

	while (av_read_frame(pFormatCtx, packet) >= 0)
	{
		if (packet->stream_index == v_stream_idx)
		{
			if (frame_read >= startframe)
			{
				ret.ret_code = avcodec_send_packet(pCodecCtx, packet);
				if (ret.ret_code < 0)
				{
					ret.ret_str = "Codec open failed.";
					goto end;
				}

				while (1)
				{
					int _ret = avcodec_receive_frame(pCodecCtx, pFrame);
					if (_ret == AVERROR(EAGAIN) || _ret == AVERROR_EOF)
					{
						break;
					}

					output->push_back(av_frame_alloc());

					if (decoder != AV_HWDEVICE_TYPE_NONE && pFrame->format == hw_pix_fmt)
					{
						/* retrieve data from GPU to CPU */
						if ((ret.ret_code = av_hwframe_transfer_data((*output)[output->size() - 1], pFrame, 0)) < 0)
						{
							ret.ret_str = "Error transferring the data to system memory";
							goto end;
						}
						//printf("hw:%d\n", frame_read);
					}
					else
					{
						(*output)[output->size() - 1] = av_frame_clone(pFrame);
						//printf("sw:%d\n", frame_read);
					}
					frame_process++;

					if (frame_process == framecount)
					{
						av_packet_unref(packet);
						break;
					}
				}

			}
			frame_read++;
		}av_packet_unref(packet);
	}

	/*flush*/
	packet->data = NULL;
	packet->size = 0;
	ret.ret_code = avcodec_send_packet(pCodecCtx, packet);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "Codec open failed.";
		goto end;
	}
	while (1)
	{
		int _ret = avcodec_receive_frame(pCodecCtx, pFrame);
		if (_ret == AVERROR(EAGAIN) || _ret == AVERROR_EOF)
		{
			break;
		}

		output->push_back(av_frame_alloc());

		if (decoder != AV_HWDEVICE_TYPE_NONE && pFrame->format == hw_pix_fmt)
		{
			/* retrieve data from GPU to CPU */
			if ((ret.ret_code = av_hwframe_transfer_data((*output)[output->size() - 1], pFrame, 0)) < 0)
			{
				ret.ret_str = "Error transferring the data to system memory";
				goto end;
			}
			//printf("hw:%d\n", frame_read);
		}
		else
		{
			(*output)[output->size() - 1] = av_frame_clone(pFrame);
			//printf("sw:%d\n", frame_read);
		}
		frame_process++;
	}av_packet_unref(packet);

end:
	av_frame_free(&pFrame);
	av_frame_free(&hw_frame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	avformat_free_context(pFormatCtx);

	pFormatCtx = NULL;

	if (decoder != AV_HWDEVICE_TYPE_NONE)
		av_buffer_unref(&hw_device_ctx);

	return ret;
}

ReturnInfo ffmodule::SeparateAudio(std::string inputfile, std::string output)
{
	ReturnInfo ret;							//Return information.

	std::string outfile = "";				//Output file name.

	AVFormatContext* fmt_ctx = NULL,		//ffmpeg input format context.
		* ofmt_ctx = NULL;					//ffmpeg output format context.

	AVOutputFormat* output_fmt = NULL;		//ffmpeg output format.

	AVCodecParameters* in_codecpar = NULL;	//ffmpeg input codecpar.

	AVStream* in_stream = NULL,				//ffmpeg input stream.
		* out_stream = NULL;				//ffmpeg output stream.

	AVPacket* pkt = av_packet_alloc();		//ffmpeg packet.

	AVCodec* decodec = NULL,				//ffmpeg decoder.
		* encodec = NULL;					//ffmpeg encoder.
	AVCodecContext* en_CodecCtx = NULL,		//ffmpeg encodec context.
		* de_CodecCtx = NULL;				//ffmpeg decodec context.

	int audio_stream_index;					//Audio stream index.

	Frames audio;							//Audio frame.

	if ((ret.ret_code = avformat_open_input(&fmt_ctx, inputfile.c_str(), NULL, NULL)) < 0) 
	{
		ret.ret_str = "avformat_open_input() failed.";
		goto end;
	}

	if ((ret.ret_code = avformat_find_stream_info(fmt_ctx, NULL)) < 0) 
	{
		ret.ret_str = "avformat_find_stream_info() failed.";
		goto end;
	}

	if (fmt_ctx->nb_streams < 2) {
		ret.ret_str = "Streams less than 2.";
		ret.ret_code = -1;
		goto end;
	}

	audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (audio_stream_index < 0) 
	{
		ret.ret_str = "av_find_best_stream() failed.";
		ret.ret_code = -1;
		goto end;
	}

	in_stream = fmt_ctx->streams[audio_stream_index];
	in_codecpar = in_stream->codecpar;

	decodec = avcodec_find_decoder(in_codecpar->codec_id);
	if (!decodec)
	{
		ret.ret_str = "Codec not found.";
		ret.ret_code = -1;
		goto end;
	}
	de_CodecCtx = avcodec_alloc_context3(decodec);
	if (!de_CodecCtx)
	{
		ret.ret_str = "Could not allocate audio codec context.";
		ret.ret_code = -1;
		goto end;
	}
	avcodec_parameters_to_context(de_CodecCtx, in_codecpar);
	/* open it */
	if ((ret.ret_code = avcodec_open2(de_CodecCtx, decodec, NULL)) < 0)
	{
		ret.ret_str = "Could not open codec.";
		goto end;
	}
	while (av_read_frame(fmt_ctx, pkt) >= 0)
	{
		if (pkt->stream_index == audio_stream_index)
		{
			ret.ret_code = avcodec_send_packet(de_CodecCtx, pkt);
			if (ret.ret_code < 0)
			{
				ret.ret_str = "Codec open failed.";
				goto end;
			}
			while (1)
			{
				AVFrame* pFrame = av_frame_alloc();
				int _ret = avcodec_receive_frame(de_CodecCtx, pFrame);
				if (_ret == AVERROR(EAGAIN) || _ret == AVERROR_EOF)
				{
					av_frame_free(&pFrame);
					break;
				}
				audio.push_back(av_frame_alloc());
				audio[audio.size() - 1] = av_frame_clone(pFrame);
				av_frame_free(&pFrame);
				av_packet_unref(pkt);
			}
		}
		av_packet_unref(pkt);
	}
	/*Flush*/
	ret.ret_code = avcodec_send_packet(de_CodecCtx, NULL);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "Codec open failed.";
		goto end;
	}
	while (1)
	{
		AVFrame* pFrame = av_frame_alloc();
		int _ret = avcodec_receive_frame(de_CodecCtx, pFrame);
		if (_ret == AVERROR(EAGAIN) || _ret == AVERROR_EOF)
		{
			av_frame_free(&pFrame);
			av_packet_unref(pkt);
			break;
		}
		audio.push_back(av_frame_alloc());
		audio[audio.size() - 1] = av_frame_clone(pFrame);
		av_frame_free(&pFrame);
		av_packet_unref(pkt);
	}

	//Encode
	ofmt_ctx = avformat_alloc_context();
	if (!ofmt_ctx)
	{
		ret.ret_code = -1;
		ret.ret_str = "avformat_alloc_output_context2() failed.";
		goto end;
	}

	if ((ret.ret_code = avio_open(&ofmt_ctx->pb, string_format("%s\\output_audio.aac", output.c_str()).c_str(), AVIO_FLAG_WRITE)) < 0)
	{
		ret.ret_str = "avio_open() failed.";
		goto end;
	}

	/* Guess the desired container format based on the file extension. */
	if (!(ofmt_ctx->oformat = av_guess_format(NULL, string_format("%s\\output_audio.aac", output.c_str()).c_str(), NULL)))
	{
		ret.ret_str = "Could not find output file format.";
		ret.ret_code = -1;
		goto end;
	}

	encodec = avcodec_find_encoder_by_name("aac");
	//encodec = avcodec_find_encoder(AV_CODEC_ID_VORBIS);
	if (!encodec)
	{
		ret.ret_str = "Encoder not found.";
		ret.ret_code = -1;
		goto end;
	}
	out_stream = avformat_new_stream(ofmt_ctx, NULL);
	en_CodecCtx = avcodec_alloc_context3(encodec);
	if (!en_CodecCtx)
	{
		ret.ret_str = "Could not allocate audio codec context.";
		ret.ret_code = -1;
		goto end;
	}

	en_CodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
	en_CodecCtx->time_base = de_CodecCtx->time_base;
	/* put sample parameters */
	en_CodecCtx->bit_rate = de_CodecCtx->bit_rate;
	/* check that the encoder supports s16 pcm input */
	en_CodecCtx->sample_fmt = encodec->sample_fmts[0];
	/* select other audio parameters supported by the encoder */
	en_CodecCtx->sample_rate = de_CodecCtx->sample_rate;
	en_CodecCtx->channel_layout = de_CodecCtx->channel_layout;
	en_CodecCtx->channels = de_CodecCtx->channels;

	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		en_CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	ofmt_ctx->duration = fmt_ctx->duration;

	if ((ret.ret_code = avcodec_open2(en_CodecCtx, encodec, NULL)) < 0) 
	{
		ret.ret_str = "Could not open codec.";
		goto end;
	}

	ret.ret_code = avcodec_parameters_from_context(out_stream->codecpar, en_CodecCtx);
	if (ret.ret_code < 0) 
	{
		ret.ret_str = "Could not initialize stream parameters.";
		goto end;
	}

	if ((ret.ret_code = avformat_write_header(ofmt_ctx, NULL)) < 0)
	{
		ret.ret_str = "avformat_write_header() failed.";
		goto end;
	}

	for (int pos = 0; pos <= audio.size(); pos++)
	{
		if (pos < audio.size())
			audio[pos]->pts = pos;

		int retx = avcodec_send_frame(en_CodecCtx, ((pos == audio.size()) ? NULL : audio[pos]));
		if (retx < 0) 
		{
			ret.ret_str = "Error sending the frame to the encoder.";
			ret.ret_code = retx;
			goto end;
		}

		/* read all the available output packets (in general there may be any
		 * number of them */
		while (retx >= 0)
		{
			retx = avcodec_receive_packet(en_CodecCtx, pkt);
			if (retx == AVERROR(EAGAIN) || retx == AVERROR_EOF)
				break;
			else if (retx < 0) 
			{
				ret.ret_str = "Error encoding audio frame.";
				ret.ret_code = retx;
				goto end;
			}
			av_interleaved_write_frame(ofmt_ctx, pkt);
			av_packet_unref(pkt);
		}
	}
	av_write_trailer(ofmt_ctx);

end:
	av_packet_free(&pkt);
	avformat_close_input(&fmt_ctx);
	avformat_free_context(fmt_ctx);
	avformat_free_context(ofmt_ctx);

	free_frames(&audio);

	return ret;
}

ReturnInfo ffmodule::ComposeVideo(std::string images_input, std::string outfile, std::string codec, AVPixelFormat pix_fmt, int bitrate, int framerate)
{
	ReturnInfo ret;						//Return information.

	Frames frm;							//Frames.

	AVFormatContext* ofmt_ctx = NULL;	//ffmpeg output format context. 

	AVPacket* pkt = NULL;				//ffmpeg output packet.

	std::string inputfile = "";			//Input file name.

	//Init original information.
	MediaInfo init = GetMediaInfo(string_format("%s\\img-%06d.png", images_input.c_str(), 0));

	AVStream* v_out_stream;				//ffmpeg output video stream.
	AVCodecContext* enc_ctx;			//ffmpeg output codec context.
	AVCodec* encoder;					//ffmpeg output codec.

	int process = 0, frame = 0;			//Counter.

	ofmt_ctx = NULL;
	ret.ret_code = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outfile.c_str());
	if (ret.ret_code < 0)
	{
		ret.ret_str = "Could not create output context.";
		goto end;
	}

	v_out_stream = avformat_new_stream(ofmt_ctx, NULL);
	if (!v_out_stream)
	{
		ret.ret_str = "Failed allocating output video stream.";
		ret.ret_code = -1;
		goto end;
	}

	encoder = avcodec_find_encoder_by_name(codec.c_str());
	//encoder = avcodec_find_encoder(AV_CODEC_ID_H265);
	if (!encoder)
	{
		ret.ret_str = "Necessary encoder not found.";
		ret.ret_code = -1;
		goto end;
	}
	enc_ctx = avcodec_alloc_context3(encoder);
	if (!enc_ctx)
	{
		ret.ret_str = "Failed to allocate the encoder context.";
		ret.ret_code = -1;
		goto end;
	}

	/* In this example, we transcode to same properties (picture size,
			 * sample rate etc.). These properties can be changed for output
			 * streams easily using filters */
	enc_ctx->bit_rate = bitrate;
	enc_ctx->height = init.height;
	enc_ctx->width = init.width;
	enc_ctx->sample_aspect_ratio = { 1 , 1 };
	/* take first format from list of supported formats */
	if (encoder->pix_fmts)
	{
		const enum AVPixelFormat* p;
		for (p = encoder->pix_fmts; *p != -1; p++)
		{
			if (*p == pix_fmt)
			{
				enc_ctx->pix_fmt = *p;
				break;
			}
		}
		enc_ctx->pix_fmt = encoder->pix_fmts[0];
	}
	else
	{
		ret.ret_str = "Pixel format not found.";
		ret.ret_code = -1;
		goto end;
	}
	/* video time_base can be set to whatever is handy and supported by encoder */
	enc_ctx->time_base = av_inv_q({ framerate , 1 });

	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	/* Third parameter can be used to pass settings to encoder */
	ret.ret_code = avcodec_open2(enc_ctx, encoder, NULL);
	if (ret.ret_code < 0) 
	{
		ret.ret_str = "Cannot open video encoder for stream.";
		goto end;
	}
	ret.ret_code = avcodec_parameters_from_context(v_out_stream->codecpar, enc_ctx);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "Failed to copy encoder parameters to output stream.";
		goto end;
	}

	if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) 
	{
		ret.ret_code = avio_open(&ofmt_ctx->pb, outfile.c_str(), AVIO_FLAG_WRITE);
		if (ret.ret_code < 0) 
		{
			ret.ret_str = "Could not open output file.";
			goto end;
		}
	}

	/* init muxer, write output file header */
	ret.ret_code = avformat_write_header(ofmt_ctx, NULL);
	if (ret.ret_code< 0)
	{
		ret.ret_str = "Error occurred when opening output file.";
		goto end;
	}

	while (1)
	{
		inputfile = string_format("%s\\img-%06d.png", images_input.c_str(), frame); frame++;
		ret = SeparateFrames(inputfile, &frm, 0, 0, AV_HWDEVICE_TYPE_NONE);
		if (ret.ret_code == -2)
		{
			if (frm.size() == 0)
			{
				goto flush;
			}
			goto proc;// break;
		}
		else if (ret.ret_code != -2 && ret.ret_code < 0)
		{
			goto end;
		}
		process++; //printf("Proc:%d\n", frame);

		if (process >= 300)
		{
		proc:
			/*Compose video*/
			for (int compose_frm = 0; compose_frm < frm.size(); compose_frm++)
			{
				AVFrame* yuvFrame = av_frame_alloc();
				yuvFrame->width = frm[compose_frm]->width;
				yuvFrame->height = frm[compose_frm]->height;
				yuvFrame->format = enc_ctx->pix_fmt;// AV_PIX_FMT_YUV420P;

				struct SwsContext* swCtx = sws_getContext(
					frm[compose_frm]->width,
					frm[compose_frm]->height,
					AVPixelFormat(frm[compose_frm]->format),
					frm[compose_frm]->width,
					frm[compose_frm]->height,
					enc_ctx->pix_fmt,//AV_PIX_FMT_YUV420P,
					SWS_GAUSS, 0, 0, 0);

				av_image_alloc(yuvFrame->data, yuvFrame->linesize, frm[compose_frm]->width, frm[compose_frm]->height, enc_ctx->pix_fmt, 1);
				sws_scale(swCtx, frm[compose_frm]->data, frm[compose_frm]->linesize, 0, frm[compose_frm]->height, yuvFrame->data, yuvFrame->linesize);

				yuvFrame->pts = frame - (frm.size() - 1 - compose_frm);

				pkt = av_packet_alloc();
				av_new_packet(pkt, frm[compose_frm]->width * frm[compose_frm]->height * 3);

				int retx = avcodec_send_frame(enc_ctx, yuvFrame);
				if (retx < 0)
				{
					ret.ret_str = "Error sending a frame for encoding.";
					ret.ret_code = retx;
					goto end;
				}

				while (retx >= 0)
				{
					retx = avcodec_receive_packet(enc_ctx, pkt);
					if (retx == AVERROR(EAGAIN) || retx == AVERROR_EOF)
					{
						break;
					}
					else if (retx < 0)
					{
						ret.ret_str = "Error during encoding.";
						ret.ret_code = retx;
						goto end;
					}

					av_packet_rescale_ts(pkt, enc_ctx->time_base, v_out_stream->time_base);
					pkt->stream_index = v_out_stream->index;
					av_interleaved_write_frame(ofmt_ctx, pkt);

					av_packet_unref(pkt);
				}

				av_freep(&yuvFrame->data[0]);
				av_frame_free(&yuvFrame);	//av_frame_alloc()
				sws_freeContext(swCtx);

			}//end for

			process = 0;
			free_frames(&frm);
		}//end if

	}//eng while

		/*flush*/
flush:

	ret.ret_code = avcodec_send_frame(enc_ctx, NULL);
	if (ret.ret_code < 0)
	{
		ret.ret_str = "Error sending a frame for encoding.(Flushing data)";
		goto end;
	}
	while (ret.ret_code >= 0)
	{
		ret.ret_code = avcodec_receive_packet(enc_ctx, pkt);
		if (ret.ret_code == AVERROR(EAGAIN) || ret.ret_code == AVERROR_EOF)
			break;
		else if (ret.ret_code < 0)
		{
			ret.ret_str = "Error during encoding.(Flushing data)";
			goto end;
		}
		av_packet_rescale_ts(pkt, enc_ctx->time_base, v_out_stream->time_base);
		pkt->stream_index = v_out_stream->index;
		av_interleaved_write_frame(ofmt_ctx, pkt); //printf("Flush.\n");
		av_packet_unref(pkt);
	}

	/* add sequence end code to have a real MPEG file */
	av_write_trailer(ofmt_ctx);

end:
	if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
		avio_closep(&ofmt_ctx->pb);
	avcodec_free_context(&enc_ctx);
	av_packet_free(&pkt);
	avformat_free_context(ofmt_ctx);

	return ret;
}

ReturnInfo ffmodule::ComposeAudio(std::string video_input, std::string audio_input, std::string output)
{
	ReturnInfo ret;						//Return information.

	AVOutputFormat* ofmt = NULL;		//ffmpeg output format.
	AVFormatContext* ifmt_ctx_v = NULL,	//ffmpeg input video format context.
		* ifmt_ctx_a = NULL,			//ffmpeg input audio format context.
		* ofmt_ctx = NULL;				//ffmpeg output format context.

	AVPacket* pkt = av_packet_alloc();	//ffmpeg packet.

	int videoindex_v = -1, videoindex_out = -1;	//Video stream index.
	int audioindex_a = -1, audioindex_out = -1;	//Audio stream index.
	int frame_index = 0;						//Frame index.
	int64_t cur_pts_v = 0, cur_pts_a = 0;		//Video and audio pts recoder.

	AVBSFContext* h264_bsf_ctx = NULL,			//BSF context for H264
		* aac_bsf_ctx = NULL;					//BSF context for AAC

	av_bsf_alloc(av_bsf_get_by_name("h264_mp4toannexb"), &h264_bsf_ctx);	//Alloc H264 BSF context.
	av_bsf_alloc(av_bsf_get_by_name("aac_adtstoasc"), &aac_bsf_ctx);		//Alloc AAC BSF context.

	//Input
	if ((ret.ret_code = avformat_open_input(&ifmt_ctx_v, video_input.c_str(), 0, 0)) < 0) 
	{
		ret.ret_str = "Could not open input file.";
		goto end;
	}
	if ((ret.ret_code = avformat_find_stream_info(ifmt_ctx_v, 0)) < 0) 
	{
		ret.ret_str = "Failed to retrieve input stream information.";
		goto end;
	}
	if ((ret.ret_code = avformat_open_input(&ifmt_ctx_a, audio_input.c_str(), 0, 0)) < 0) 
	{
		ret.ret_str = "Could not open input file.";
		goto end;
	}
	if ((ret.ret_code = avformat_find_stream_info(ifmt_ctx_a, 0)) < 0) 
	{
		ret.ret_str = "Failed to retrieve input stream information";
		goto end;
	}

	//Output
	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output.c_str());
	if (!ofmt_ctx) 
	{
		ret.ret_str = "Could not create output context.";
		ret.ret_code = AVERROR_UNKNOWN;
		goto end;
	}
	ofmt = ofmt_ctx->oformat;	//Set output format.

	//Create output video streams according to input.
	for (int i = 0; i < ifmt_ctx_v->nb_streams; i++)
	{
		if (ifmt_ctx_v->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			AVStream* in_stream = ifmt_ctx_v->streams[i]; 
			AVStream* out_stream = avformat_new_stream(ofmt_ctx, avcodec_find_encoder(in_stream->codecpar->codec_id));
			videoindex_v = i;
			if (!out_stream) 
			{
				ret.ret_str = "Failed allocating output stream.";
				ret.ret_code = AVERROR_UNKNOWN;
				goto end;
			}
			videoindex_out = out_stream->index;
			//Copy the settings of AVCodecContext
			AVCodecContext* codecCtx = avcodec_alloc_context3(avcodec_find_encoder(in_stream->codecpar->codec_id));
			if ((ret.ret_code = avcodec_parameters_to_context(codecCtx, in_stream->codecpar)) < 0)
			{
				ret.ret_str = "Failed to copy in_stream codecpar to codec context.";
				avcodec_free_context(&codecCtx);
				goto end;
			}
			codecCtx->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			{
				codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			}
			if ((ret.ret_code = avcodec_parameters_from_context(out_stream->codecpar, codecCtx)) < 0)
			{
				ret.ret_str = "Failed to copy codec context to out_stream codecpar context.";
				avcodec_free_context(&codecCtx);
				goto end;
			}
			avcodec_free_context(&codecCtx);
			break;
		}
	}

	//Create output audio streams according to input.
	for (int i = 0; i < ifmt_ctx_a->nb_streams; i++) 
	{
		if (ifmt_ctx_a->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			AVStream* in_stream = ifmt_ctx_a->streams[i];
			AVStream* out_stream = avformat_new_stream(ofmt_ctx, avcodec_find_encoder(in_stream->codecpar->codec_id));
			audioindex_a = i;
			if (!out_stream)
			{
				ret.ret_str = "Failed allocating output stream.";
				ret.ret_code = AVERROR_UNKNOWN;
				goto end;
			}
			audioindex_out = out_stream->index;
			//Copy the settings of AVCodecContext
			AVCodecContext* codecCtx = avcodec_alloc_context3(avcodec_find_encoder(in_stream->codecpar->codec_id));
			if ((ret.ret_code = avcodec_parameters_to_context(codecCtx, in_stream->codecpar)) < 0)
			{
				ret.ret_str = "Failed to copy in_stream codecpar to codec context.";
				avcodec_free_context(&codecCtx);
				goto end;
			}
			codecCtx->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			{
				codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			}
			if ((ret.ret_code = avcodec_parameters_from_context(out_stream->codecpar, codecCtx)) < 0)
			{
				ret.ret_str = "Failed to copy codec context to out_stream codecpar context.";
				avcodec_free_context(&codecCtx);
				goto end;
			}
			avcodec_free_context(&codecCtx);
			break;
		}
	}

	//Open output file.
	if (!(ofmt->flags & AVFMT_NOFILE))
	{
		if ((ret.ret_code = avio_open(&ofmt_ctx->pb, output.c_str(), AVIO_FLAG_WRITE)) < 0)
		{
			ret.ret_str = "Could not open output file.";
			goto end;
		}
	}
	//Write file header
	if ((ret.ret_code = avformat_write_header(ofmt_ctx, NULL)) < 0) 
	{
		ret.ret_str = "Error occurred when opening output file.";
		goto end;
	}

	while (1) 
	{
		AVFormatContext* ifmt_ctx;				//Temporary format context.
		int stream_index = 0;					//Stream index.
		AVStream* in_stream, * out_stream;		//Temporary streams.

		//Get an packet
		if (av_compare_ts(cur_pts_v, ifmt_ctx_v->streams[videoindex_v]->time_base, cur_pts_a, ifmt_ctx_a->streams[audioindex_a]->time_base) <= 0)
		{
			ifmt_ctx = ifmt_ctx_v;
			stream_index = videoindex_out;
			if (av_read_frame(ifmt_ctx, pkt) >= 0)
			{
				do 
				{
					in_stream = ifmt_ctx->streams[pkt->stream_index];
					out_stream = ofmt_ctx->streams[stream_index];

					if (pkt->stream_index == videoindex_v)
					{
						if (pkt->pts == AV_NOPTS_VALUE)
						{
							//Write PTS
							AVRational time_base1 = in_stream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
							//Parameters
							pkt->pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							pkt->dts = pkt->pts;
							pkt->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							frame_index++;
						}
						av_bsf_send_packet(h264_bsf_ctx, pkt);
						av_bsf_receive_packet(h264_bsf_ctx, pkt);
						cur_pts_v = pkt->pts;
						break;
					}
				} while (av_read_frame(ifmt_ctx, pkt) >= 0);
			}
			else 
			{
				break;
			}
		}
		else 
		{
			ifmt_ctx = ifmt_ctx_a;
			stream_index = audioindex_out;
			if ((ret.ret_code = av_read_frame(ifmt_ctx, pkt)) >= 0) 
			{
				do 
				{
					in_stream = ifmt_ctx->streams[pkt->stream_index];
					out_stream = ofmt_ctx->streams[stream_index];
					if (pkt->stream_index == audioindex_a)
					{
						if (pkt->pts == AV_NOPTS_VALUE)
						{
							//Write PTS
							AVRational time_base1 = in_stream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
							//Parameters
							pkt->pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							pkt->dts = pkt->pts;
							pkt->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							frame_index++;
						}
						av_bsf_send_packet(aac_bsf_ctx, pkt);
						av_bsf_receive_packet(aac_bsf_ctx, pkt);
						cur_pts_a = pkt->pts;
						break;
					}
				} while (av_read_frame(ifmt_ctx, pkt) >= 0);
			}
			else 
			{
				break;
			}
		}

		//Convert PTS/DTS
		pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
		pkt->pos = -1;
		pkt->stream_index = stream_index;

		//Write
		if ((ret.ret_code = av_interleaved_write_frame(ofmt_ctx, pkt)) < 0) 
		{
			ret.ret_str = "Error muxing packet.";
			break;
		}
		av_packet_unref(pkt);
	}
	//Write file trailer
	av_write_trailer(ofmt_ctx);

end:

	av_packet_free(&pkt);

	av_bsf_free(&h264_bsf_ctx);
	av_bsf_free(&aac_bsf_ctx);

	avformat_close_input(&ifmt_ctx_v);
	avformat_close_input(&ifmt_ctx_a);
	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);

	return ret;
}
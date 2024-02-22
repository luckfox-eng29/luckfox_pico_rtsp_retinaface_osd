#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "rtsp_demo.h"
#include "sample_comm.h"

#include "retinaface.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>


static RK_S32 g_s32FrameCnt = -1;

//test param
MPP_CHN_S stSrcChn, stSrcChn1, stvpssChn, stvencChn;
VENC_RECV_PIC_PARAM_S stRecvParam;

rknn_app_context_t rknn_app_ctx;	
object_detect_result_list od_results;

rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session;


//大小端问题，设置的ARGB 但是颜色是BGRA
// for argb8888
#define TEST_ARGB32_PIX_SIZE 4
#define TEST_ARGB32_RED 0xFF0000FF
#define TEST_ARGB32_GREEN 0x00FF00FF
#define TEST_ARGB32_BLUE 0x0000FFFF
#define TEST_ARGB32_TRANS 0x00000000
#define TEST_ARGB32_BLACK 0x000000FF

bool quit = false;
static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
}

RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}


static void set_argb8888_line(RK_U32 *buf, int sX, int sY, int eX, int eY, int type, RK_U32 color) {

	switch(type)
	{
		case 0:
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = sY; j < (sY+2); j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = sX; i < (sX+2); i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
		case 1:
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = sY; j < (sY+2); j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = (eX-2); i < eX; i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
		case 2:
			for (RK_U32 i = (eX-2); i < eX; i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = (eY-2); j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
		case 3:
			for (RK_U32 i = sX; i < eX; i++)
			{
				for(RK_U32 j = (eY-2); j < eY; j++)
					*(buf + i + j*20) = color;
			}
			for (RK_U32 i = sX; i < (sX+2); i++)
			{
				for(RK_U32 j = sY; j < eY; j++)
					*(buf + i + j*20) = color;
			}
			break;
	}


	return;
}

RK_S32 test_rgn_overlay_line_process(int sX ,int sY,int type, int group) {
	printf("========%s========\n", __func__);
	RK_S32 s32Ret = RK_SUCCESS;
	RGN_HANDLE RgnHandle = group * 4 + type;
	BITMAP_S stBitmap;
	RGN_ATTR_S stRgnAttr;
	RGN_CHN_ATTR_S stRgnChnAttr;

	int u32Width = 20;
	int u32Height = 20;
	int s32X = sX;
	int s32Y = sY;

	MPP_CHN_S stMppChn;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;
	/****************************************
	step 1: create overlay regions
	****************************************/
	stRgnAttr.enType = OVERLAY_RGN;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = (PIXEL_FORMAT_E)RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = u32Width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = u32Height;
	stRgnAttr.unAttr.stOverlay.u32ClutNum = 0;

	s32Ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_RGN_Create (%d) failed with %#x!", RgnHandle, s32Ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	RK_LOGI("The handle: %d, create success!", RgnHandle);

	/*********************************************
	step 2: display overlay regions to groups
	*********************************************/
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = RK_TRUE;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = s32X;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = s32Y;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = 0;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bEnable = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bForceIntra = RK_TRUE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.bAbsQp = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stQpInfo.s32Qp = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[0] = 0x00;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32ColorLUT[1] = 0xFFFFFFF;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.bInvColEn = RK_FALSE;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.stInvColArea.u32Width = 16;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.stInvColArea.u32Height = 16;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.enChgMod = LESSTHAN_LUM_THRESH;
	stRgnChnAttr.unChnAttr.stOverlayChn.stInvertColor.u32LumThresh = 100;
	s32Ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
	if (RK_SUCCESS != s32Ret) {
		RK_LOGE("RK_MPI_RGN_AttachToChn (%d) failed with %#x!", RgnHandle, s32Ret);
		return RK_FAILURE;
	}
	RK_LOGI("Display region to chn success!");

	/*********************************************
	step 3: show bitmap
	*********************************************/
	stBitmap.enPixelFormat = (PIXEL_FORMAT_E)RK_FMT_ARGB8888;
	stBitmap.u32Width = u32Width;
	stBitmap.u32Height = u32Height;

	RK_U16 ColorBlockSize = stBitmap.u32Height * stBitmap.u32Width;
	stBitmap.pData = malloc(ColorBlockSize * TEST_ARGB32_PIX_SIZE);
	memset(stBitmap.pData, 0, ColorBlockSize * TEST_ARGB32_PIX_SIZE);	
	RK_U8 *ColorData = (RK_U8 *)stBitmap.pData;

	set_argb8888_line((RK_U32 *)ColorData, 0, 0, 20, 20, type, TEST_ARGB32_GREEN); 		
		
	s32Ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_RGN_SetBitMap failed with %#x!", s32Ret);
		return RK_FAILURE;
	}

	return 0;
}

RK_S32 rgn_overlay_release(int group)
{	
	MPP_CHN_S stMppChn;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	stMppChn.s32ChnId = 0;

	RGN_HANDLE RgnHandle = 0;
	for(RGN_HANDLE i = 0;i < 4;i++)
	{
		RK_MPI_RGN_DetachFromChn(i, &stMppChn);
		RK_MPI_RGN_Destroy(i);
	}
	return 0;	
}

static void *GetVpssBuffer(void *arg)
{
	(void)arg;
	printf("========%s========\n", __func__);

	int width = 640;
	int height = 640;
	char text[16];
	int sX,sY,eX,eY;
	float scale_x = 1.125;
	float scale_y = 0.75;

	int s32Ret;
	int group_count = 0;
	void *vpssData = RK_NULL;
	VIDEO_FRAME_INFO_S vpssFrame;

	while(!quit)
	{
		s32Ret = RK_MPI_VPSS_GetChnFrame(0, 0, &vpssFrame, -1);
		if(s32Ret == RK_SUCCESS)
		{
			vpssData = RK_MPI_MB_Handle2VirAddr(vpssFrame.stVFrame.pMbBlk);
			if(vpssData != RK_NULL)
			{
				memcpy(rknn_app_ctx.input_mems[0]->virt_addr, vpssData, 640*640*3);
				inference_retinaface_model(&rknn_app_ctx, &od_results);

				for(int i = 0; i < od_results.count; i++)
				{					
					// 获取框的四个坐标 
					object_detect_result *det_result = &(od_results.results[i]);
					printf("%d %d %d %d\n",det_result->box.left ,det_result->box.top,det_result->box.right,det_result->box.bottom);

					// 只标记一个	
					if(i == 0)
					{
						sX = (int)((float)det_result->box.left 	 *scale_x);	
						sY = (int)((float)det_result->box.top 	 *scale_y);	
						eX = (int)((float)det_result->box.right  *scale_x);	
						eY = (int)((float)det_result->box.bottom *scale_y);

						// OSD 坐标要求为偶数
						sX = sX - (sX % 2);
						sY = sY - (sY % 2);
						eX = eX	- (eX % 2);				
						eY = eY	- (eY % 2);					
					
						if((eX > sX) && (eY > sY) && (sX > 0) && (sY > 0))
						{
							test_rgn_overlay_line_process(sX,sY,0,i);
							test_rgn_overlay_line_process(eX,sY,1,i);
							test_rgn_overlay_line_process(eX,eY,2,i);
							test_rgn_overlay_line_process(sX,eY,3,i);
						}
						group_count++;
					}

				}			
				RK_MPI_VPSS_ReleaseChnFrame(0, 0, &vpssFrame);
			}
		}
		usleep(500000);
		for(int i = 0;i < group_count; i++)
		{
			rgn_overlay_release(i);
		}
		group_count = 0;
	}			
	return NULL;
}

static void *GetMediaBuffer(void *arg) {
	(void)arg;
	printf("========%s========\n", __func__);
	void *pData = RK_NULL;
	void *vpssData = RK_NULL;

	int loopCount = 0;
	int s32Ret;
	static RK_U32 jpeg_id = 0;
	char jpeg_path[128];

	VENC_STREAM_S stFrame;
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
	VIDEO_FRAME_INFO_S vpssFrame;

	while (!quit) {
		s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, 200000);
		if (s32Ret == RK_SUCCESS) {
			if (g_rtsplive && g_rtsp_session) {
				pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
				rtsp_tx_video(g_rtsp_session,(uint8_t *)pData, stFrame.pstPack->u32Len,
							  stFrame.pstPack->u64PTS);
				rtsp_do_event(g_rtsplive);
			}

			RK_U64 nowUs = TEST_COMM_GetNowUs();

			RK_LOGD("chn:0, loopCount:%d enc->seq:%d wd:%d pts=%lld delay=%lldus\n",
					loopCount, stFrame.u32Seq, stFrame.pstPack->u32Len,
					stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);

			s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
			if (s32Ret != RK_SUCCESS) {
				RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
			}
			loopCount++;
		}

		if ((g_s32FrameCnt >= 0) && (loopCount > g_s32FrameCnt)) {
			quit = true;
			break;
		}
		usleep(10 * 1000);
	}

	printf("\n======exit %s=======\n", __func__);

	free(stFrame.pstPack);
	return NULL;
}



static RK_S32 test_venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
	printf("================================%s==================================\n",
	       __func__);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_ATTR_S stAttr;
	memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

	// RTSP H264
	stAttr.stVencAttr.enType = enType;
	stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	//stAttr.stVencAttr.enPixelFormat = RK_FMT_RGB888;	
	stAttr.stVencAttr.u32Profile = H264E_PROFILE_MAIN;

	stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
	stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
	stAttr.stRcAttr.stH264Cbr.u32Gop = 60;
	
	stAttr.stVencAttr.u32PicWidth = width;
	stAttr.stVencAttr.u32PicHeight = height;
	stAttr.stVencAttr.u32VirWidth = width;
	stAttr.stVencAttr.u32VirHeight = height;
	stAttr.stVencAttr.u32StreamBufCnt = 2;
	stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
	RK_MPI_VENC_CreateChn(chnId, &stAttr);

	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);

	return 0;
}

int vi_dev_init() {
	printf("%s\n", __func__);
	int ret = 0;
	int devId = 0;
	int pipeId = devId;

	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_SetDevAttr already\n");
	}
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(devId);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(devId);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = pipeId;
		stBindPipe.PipeId[0] = pipeId;
		ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

int vi_chn_init(int channelId, int width, int height) {
	printf("================================%s==================================\n",
	       __func__);
	int ret;
	int buf_cnt = 2;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType =
	    VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 2;
	ret  = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(0, channelId);
	if (ret) {
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}
	return ret;
}

int test_vpss_init(int VpssChn, int width, int height) {
	printf("================================%s==================================\n",
	       __func__);
	int s32Ret;
	VPSS_CHN_ATTR_S stVpssChnAttr;
	VPSS_GRP_ATTR_S stGrpVpssAttr;

	int s32Grp = 0;

	stGrpVpssAttr.u32MaxW = 4096;
	stGrpVpssAttr.u32MaxH = 4096;
	stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
	stGrpVpssAttr.stFrameRate.s32SrcFrameRate = -1;
	stGrpVpssAttr.stFrameRate.s32DstFrameRate = -1;
	stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;

	stVpssChnAttr.enChnMode = VPSS_CHN_MODE_USER;
	stVpssChnAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;
	stVpssChnAttr.enPixelFormat = RK_FMT_RGB888;
	stVpssChnAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssChnAttr.stFrameRate.s32DstFrameRate = -1;
	stVpssChnAttr.u32Width = width;
	stVpssChnAttr.u32Height = height;
	stVpssChnAttr.enCompressMode = COMPRESS_MODE_NONE;

	s32Ret = RK_MPI_VPSS_CreateGrp(s32Grp, &stGrpVpssAttr);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

	s32Ret = RK_MPI_VPSS_SetChnAttr(s32Grp, VpssChn, &stVpssChnAttr);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	s32Ret = RK_MPI_VPSS_EnableChn(s32Grp, VpssChn);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}

	s32Ret = RK_MPI_VPSS_StartGrp(s32Grp);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	return s32Ret;
}

int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	RK_U32 u32Width    = 720;
	RK_U32 u32Height   = 480;
	RK_U32 disp_width  = 640;
	RK_U32 disp_height = 640;

	RK_CHAR *pOutPath = NULL;
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	RK_CHAR *pCodecName = (RK_CHAR *)"H264";
	RK_S32 s32chnlId = 0;
	RK_S32 s32chnlId1 = 1;

	// Rknn model init
    int ret;
	const char *model_path = "./model/retinaface.rknn";
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));	
    init_retinaface_model(model_path, &rknn_app_ctx);
	printf("init rknn model success!\n");

	// rkaiq init 
	RK_BOOL multi_sensor = RK_FALSE;	
	const char *iq_dir = "/etc/iqfiles";
	rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
	//hdr_mode = RK_AIQ_WORKING_MODE_ISP_HDR2;
	SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
	SAMPLE_COMM_ISP_Run(0);

	// rtsp init	
	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
	rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED;
	}

	// Ctrl-c quit
	signal(SIGINT, sigterm_handler);

	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED;
	}

	// vi init
	vi_dev_init();
	vi_chn_init(s32chnlId, u32Width, u32Height);
	vi_chn_init(s32chnlId1, u32Width, u32Height);

	// vpss init
	test_vpss_init(0, disp_width, disp_height);
	
	// venc init
	enCodecType = RK_VIDEO_ID_AVC; 
	test_venc_init(0, u32Width, u32Height, enCodecType);

	// bind vi to venc	
	stSrcChn.enModId = RK_ID_VI;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = 0;
		
	stvencChn.enModId = RK_ID_VENC;
	stvencChn.s32DevId = 0;
	stvencChn.s32ChnId = 0;
	printf("====RK_MPI_SYS_Bind vpss0 to venc0====\n");
	s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stvencChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 1 ch venc failed");
		goto __FAILED;
	}
	// bind vi to vpss
	stSrcChn1.enModId = RK_ID_VI;
	stSrcChn1.s32DevId = 0;
	stSrcChn1.s32ChnId = 1;

	stvpssChn.enModId = RK_ID_VPSS;
	stvpssChn.s32DevId = 0;
	stvpssChn.s32ChnId = 0;
	printf("====RK_MPI_SYS_Bind vi0 to vpss0====\n");
	s32Ret = RK_MPI_SYS_Bind(&stSrcChn1, &stvpssChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 0 ch venc failed");
		goto __FAILED;
	}

			
	pthread_t main_thread;
	pthread_create(&main_thread, NULL, GetMediaBuffer, NULL);
	pthread_t vpss_thread;
	pthread_create(&vpss_thread, NULL, GetVpssBuffer, NULL);


	while (!quit) {	
		usleep(50000);
	}

	pthread_join(main_thread, NULL);
	pthread_join(vpss_thread, NULL);

__FAILED:

	s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stvencChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
	}

	s32Ret = RK_MPI_SYS_UnBind(&stSrcChn1, &stvpssChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_SYS_UnBind fail %x", s32Ret);
	}

	s32Ret = RK_MPI_VI_DisableChn(0, s32chnlId);
	RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);

	s32Ret = RK_MPI_VI_DisableChn(0, s32chnlId1);
	RK_LOGE("RK_MPI_VI_DisableChn %x", s32Ret);
	
	RK_MPI_VPSS_StopGrp(0);
	RK_MPI_VPSS_DestroyGrp(0);

	s32Ret = RK_MPI_VENC_StopRecvFrame(0);
	if (s32Ret != RK_SUCCESS) {
		return s32Ret;
	}
	s32Ret = RK_MPI_VENC_DestroyChn(0);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("RK_MPI_VDEC_DestroyChn fail %x", s32Ret);
	}

	s32Ret = RK_MPI_VI_DisableDev(0);
	RK_LOGE("RK_MPI_VI_DisableDev %x", s32Ret);

	RK_LOGE("test running exit:%d", s32Ret);
	RK_MPI_SYS_Exit();

	// Stop RKAIQ
	SAMPLE_COMM_ISP_Stop(0);

	// Release rknn model
    release_retinaface_model(&rknn_app_ctx);	

	return 0;
}

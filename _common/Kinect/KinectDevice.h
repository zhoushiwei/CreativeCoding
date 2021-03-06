//a multi-threaded device wrapper for Microsoft Kinect SDK V1.0
//author: vinjn.z@gmail.com
//http://www.weibo.com/vinjnmelanie
//
//Note: although more than one Kinect is supported at one PC, only one device is capable of skeleton tracking.
//You will get warning if more than one device is initialized with SkeletonTracking flag.
//

/*
The Kinect sensor returns x, y, and z values in the following ranges:

Values of x can range from approximately �C2.2 to 2.2.
Values of y can range from approximately �C1.6 to 1.6.
Values of z can range from 0.8 to 4.0.
*/

#ifndef KINECT_DEVICE_H
#define KINECT_DEVICE_H

#pragma warning( disable: 4244 )

#include <windows.h>
#include <objbase.h>
#include <MMSystem.h>
#include "NuiApi.h"
#include <opencv2/opencv.hpp>
#include "../MiniThread.h"
#include "../MiniMutex.h"

#pragma comment(lib, "winmm.lib")

#define SHOW_FPS 1
#if SHOW_FPS
#define FPS_CALC(fps) \
	do \
{ \
	static unsigned count = 0;\
	static DWORD last = timeGetTime();\
	DWORD now = timeGetTime(); \
	++count; \
	if (now - last >= 1000) \
	{ \
	fps = count; \
	count = 0; \
	last = now; \
	} \
}while(false)
#else
#define FPS_CALC(_WHAT_) \
	do \
{ \
}while(false)
#endif 
#define MSG_BOX(str) ::MessageBox(NULL, TEXT(str), TEXT("Kinect App"), MB_OK | MB_ICONHAND)

static char* state_desc[3] = {"not_tracked","half_tracked","tracked"};

static CvScalar default_colors[] =
{
	{{169, 176, 155}},
	{{169, 176, 155}}, 
	{{168, 230, 29}},
	{{200, 0,   0}},
	{{79,  84,  33}}, 
	{{84,  33,  42}},
	{{255, 126, 0}},
	{{215,  86, 0}},
	{{33,  79,  84}}, 
	{{33,  33,  84}},
	{{77,  109, 243}}, 
	{{37,   69, 243}},
	{{77,  109, 243}},
	{{69,  33,  84}},
	{{229, 170, 122}}, 
	{{255, 126, 0}},
	{{181, 165, 213}}, 
	{{71, 222,  76}},
	{{245, 228, 156}}, 
	{{77,  109, 243}}
};

const int sizeOfColors = sizeof(default_colors)/sizeof(CvScalar);
static CvScalar vDefaultColor(int idx){ return default_colors[idx%sizeOfColors];}

class KinectDevice;

enum{
	DEPTH_WIDTH = 320,
	DEPTH_HEIGHT = 240,
	RGB_WIDTH = 640,
	RGB_HEIGHT = 480,
	Z_NEAR = 800,//mm
	Z_FAR = 4000,//mm
};

class KinectDevice
{
protected:
	struct DepthThread : public MiniThread
	{
		DepthThread(KinectDevice& ref):ref_(ref),MiniThread("DepthThread"){}
		void threadedFunction()
		{
			while (isThreadRunning())
			{
				::WaitForSingleObject(ref_.evt_nextDepth, INFINITE);
				ref_.Nui_GotDepthAlert();
			}
		}
		KinectDevice& ref_;
	};

	struct RgbThread : public MiniThread
	{
		RgbThread(KinectDevice& ref):ref_(ref),MiniThread("RgbThread"){}
		void threadedFunction()
		{
			while (isThreadRunning())
			{
				::WaitForSingleObject(ref_.evt_nextRgb, INFINITE);
				ref_.Nui_GotRgbAlert();
			}
		}
		KinectDevice& ref_;
	};

	struct SkeletonThread : public MiniThread
	{
		SkeletonThread(KinectDevice& ref):ref_(ref),MiniThread("SkeletonThread"){}
		void threadedFunction()
		{
			while (isThreadRunning())
			{
				::WaitForSingleObject(ref_.evt_nextSkeleton, INFINITE);
				ref_.Nui_GotSkeletonAlert();
			}
		}
		KinectDevice& ref_;
	};
public:
	//////////////////////////////////////////////////////////////////////////
	//basic callbacks
	//called when depth stream comes, which stored at cv::Mat& depth_u16
    //A depth data value of 0 indicates that no depth data is available at that position because all the objects 
    //were either too close to the camera or too far away from it.
	virtual void onDepthData(const cv::Mat& depth_u16, const cv::Mat& rgb_depth){}

	//called when rgb stream comes, which stored at cv::Mat& rgb
	virtual void onRgbData(const cv::Mat& rgb){}

	//called when a skeleton is found, the joint data are stored at skel_points
	//playerIdx indicates the player index, which ranges from 0 to 5
	//isNewPlayer is true when it's the playerIdx didn't appear at previous frame
	virtual void onPlayerData(cv::Point3f skel_points[NUI_SKELETON_POSITION_COUNT], int playerIdx, bool isNewPlayer ){}

	//////////////////////////////////////////////////////////////////////////
	//advanced callbacks
	//it's useful when you want to do initialization before/after each skeleton data comes 
	virtual void onSkeletonEventBegin(){}
	virtual void onSkeletonEventEnd(){}

	//called when a player enters
	virtual void onPlayerEnter(int playerIdx){}

	//called when a player leaves
	virtual void onPlayerLeave(int playerIdx){}

public:
	//angle ~ [0, 54]
	void setDeviceAngle(int angle)
	{
		angle += NUI_CAMERA_ELEVATION_MINIMUM;
		if (m_pNuiSensor)
		{
			if (angle < NUI_CAMERA_ELEVATION_MINIMUM)
				angle = NUI_CAMERA_ELEVATION_MINIMUM;
			else
				if (angle > NUI_CAMERA_ELEVATION_MAXIMUM)
					angle = NUI_CAMERA_ELEVATION_MAXIMUM;
			m_pNuiSensor->NuiCameraElevationSetAngle(angle);
		}
	}

	//return [0, 54]
	int getDeviceAngle()
	{
		LONG angle = 0;
		if (m_pNuiSensor)
		{
			m_pNuiSensor->NuiCameraElevationGetAngle(&angle);
		}
		return angle-NUI_CAMERA_ELEVATION_MINIMUM;
	}

	static int getDeviceCount()
	{
		int count = 0;
		HRESULT hr = NuiGetSensorCount(&count);
		return count;
	}

	KinectDevice(int deviceId = 0)
	{
		iplColor = NULL;
		m_DeviceId = deviceId;
 		m_pNuiSensor = NULL;

		evt_nextDepth = NULL;
		evt_nextRgb = NULL;
		evt_nextSkeleton = NULL;
		m_pDepthStreamHandle = NULL;
		m_pVideoStreamHandle = NULL;

		fps_depth = 0;
		fps_rgb = 0;
		fps_skeleton = 0;

		::ZeroMemory(m_PrevPoints, sizeof(m_PrevPoints));

//		registerDeviceCallback();
	}

	virtual ~KinectDevice()
	{
		release();
	}

	//TODO: be more OO
	cv::Mat renderedSkeleton;//rendered
	IplImage* iplColor;//for rendering
	cv::Mat renderedDepth;//rendered
	cv::Mat rawDepth;//raw data

	HRESULT setup(bool isColor = false, bool isDepth = true, bool isSkeleton = true)
	{
		printf("KinServer is trying to launch Kinect #%d with\n", m_DeviceId);
		if (isColor)
			printf(" * RGB frame\n");
		if (isDepth)
			printf(" * depth frame\n");
		if (isSkeleton)
			printf(" * body tracking\n\n");

		int flag = 0;
		if (isDepth) flag |= NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX;
		if (isColor) flag |= NUI_INITIALIZE_FLAG_USES_COLOR;
		if (isSkeleton) flag |= NUI_INITIALIZE_FLAG_USES_SKELETON;
		HRESULT hr = 0;

		hr = NuiCreateSensorByIndex(m_DeviceId, &m_pNuiSensor);

		if (FAILED(hr))
		{
			MSG_BOX("Kinect is not detected.");
			return hr;
		}

		hr = m_pNuiSensor->NuiInitialize(flag);
		if (E_NUI_SKELETAL_ENGINE_BUSY == hr)
		{
			MSG_BOX("Skeletal engine is used by another Kinect, thus disabled.");
			flag &= ~NUI_INITIALIZE_FLAG_USES_SKELETON;
			hr = m_pNuiSensor->NuiInitialize(flag);
		}
		if( FAILED( hr ) )
		{
			MSG_BOX("Kinect failed to initialize.");
			return hr;
		}

		renderedSkeleton.create(cv::Size(RGB_WIDTH,RGB_HEIGHT), CV_8UC3);
		iplColor = cvCreateImageHeader(cvSize(RGB_WIDTH,RGB_HEIGHT), 8, 4);
		renderedDepth.create(cv::Size(DEPTH_WIDTH,DEPTH_HEIGHT), CV_8UC3);
		rawDepth.create(cv::Size(DEPTH_WIDTH,DEPTH_HEIGHT), CV_16UC1);

		renderedSkeleton = cv::Scalar(0,0,0);
		renderedDepth = cv::Scalar(0,0,0);
		rawDepth = cv::Scalar(0,0,0);

		if (isColor)
		{
			evt_nextRgb = CreateEvent( NULL, TRUE, FALSE, NULL );
			hr = m_pNuiSensor->NuiImageStreamOpen(
				NUI_IMAGE_TYPE_COLOR,
				NUI_IMAGE_RESOLUTION_640x480,
				0,
				2,
				evt_nextRgb,
				&m_pVideoStreamHandle );
			if( FAILED( hr ) )
			{
				MSG_BOX("failed on NuiImageStreamOpen(NUI_IMAGE_TYPE_COLOR)");
				return hr;
			}
			_threads[0] = new RgbThread(*this);
			_threads[0]->startThread();
		}

		if (isDepth)
		{
			evt_nextDepth = CreateEvent( NULL, TRUE, FALSE, NULL );
			hr = m_pNuiSensor->NuiImageStreamOpen(
				HasSkeletalEngine(m_pNuiSensor) ? NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX : NUI_IMAGE_TYPE_DEPTH,
				NUI_IMAGE_RESOLUTION_320x240,
				0,
				2,
				evt_nextDepth,
				&m_pDepthStreamHandle );
			if( FAILED( hr ) )
			{
				MSG_BOX("failed on NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX)");
				return hr;
			}
			_threads[1] = new DepthThread(*this);
			_threads[1]->startThread();
		}

		if (isSkeleton && HasSkeletalEngine(m_pNuiSensor))
		{
			evt_nextSkeleton = CreateEvent( NULL, TRUE, FALSE, NULL );
			hr = m_pNuiSensor->NuiSkeletonTrackingEnable( evt_nextSkeleton, 0 );
			if( FAILED( hr ) )
			{
				MSG_BOX("failed on NuiSkeletonTrackingEnable()");
				return hr;
			}
			_threads[2] = new SkeletonThread(*this);
			_threads[2]->startThread();
		}

		::ZeroMemory(isPlayerVisible, sizeof(isPlayerVisible));

		return hr;
	}

	void release()
	{
		if( evt_nextSkeleton && ( evt_nextSkeleton != INVALID_HANDLE_VALUE ) )
		{
			CloseHandle( evt_nextSkeleton );
			evt_nextSkeleton = NULL;
		}
		if( evt_nextDepth && ( evt_nextDepth != INVALID_HANDLE_VALUE ) )
		{
			CloseHandle( evt_nextDepth );
			evt_nextDepth = NULL;
		}
		if( evt_nextRgb && ( evt_nextRgb != INVALID_HANDLE_VALUE ) )
		{
			CloseHandle( evt_nextRgb );
			evt_nextRgb = NULL;
		}
		
		for (int i=0;i<3;i++)
		{
			if (!_threads[i].empty())
				_threads[i]->stopThread();
		}

		if (iplColor)
			cvReleaseImageHeader(&iplColor);

		if (m_pNuiSensor)
		{
			m_pNuiSensor->NuiShutdown();
			m_pNuiSensor->Release();
			m_pNuiSensor = NULL;
		}
	}

	static std::string getStateString(int state )
	{
		return state_desc[state];
	}

protected:
	cv::Point2f getUVFromDepthPixel(int x, int y, ushort depthValue ) 
	{		
		LONG u,v;
		HRESULT hr = m_pNuiSensor->NuiImageGetColorPixelCoordinatesFromDepthPixel(
			NUI_IMAGE_RESOLUTION_640x480,NULL,x,y,depthValue<<3,&u,&v);
		return cv::Point2f(u/(float)RGB_WIDTH, v/(float)RGB_HEIGHT);
	}

	INuiSensor* m_pNuiSensor;//the object represents the physical Kinect device

	int m_DeviceId;
	cv::Point3f   m_PrevPoints[NUI_SKELETON_COUNT][NUI_SKELETON_POSITION_COUNT];
	cv::Point3f   m_Points[NUI_SKELETON_POSITION_COUNT];
	float	m_Confidences[NUI_SKELETON_POSITION_COUNT];

	bool isPlayerVisible[NUI_SKELETON_COUNT];//valid id->0/1/2/3/4/5
	//TODO: should be 1/2/3/4/5/6
private:
	static void registerDeviceCallback()
	{
		static bool first_time = true;
		if (first_time)
		{
			first_time = false;
			NuiSetDeviceStatusCallback(onDeviceStatus, NULL);
		}
	}

	static void CALLBACK onDeviceStatus(HRESULT hrStatus, const OLECHAR* instanceName, const OLECHAR* uniqueDeviceName, void* pUserData)
	{
		//MSG_BOX(instanceName);
		if (FAILED(hrStatus))
			MSG_BOX("Kinect disconnected!");
	}

	void Nui_GotDepthAlert( )
	{
		FPS_CALC(fps_depth);

		NUI_IMAGE_FRAME imageFrame;

		HRESULT hr = m_pNuiSensor->NuiImageStreamGetNextFrame(
			m_pDepthStreamHandle,
			0,
			&imageFrame );

		if( FAILED( hr ) )
		{
			return;
		}

		INuiFrameTexture * pTexture = imageFrame.pFrameTexture;
		NUI_LOCKED_RECT LockedRect;
		pTexture->LockRect( 0, &LockedRect, NULL, 0 );
		if( LockedRect.Pitch != 0 )
		{
			BYTE * pBuffer = (BYTE*) LockedRect.pBits;
			BYTE * pixels = (BYTE*)renderedDepth.ptr();
			USHORT * pBufferRun = (USHORT*) pBuffer;
			USHORT * pRaw = rawDepth.ptr<ushort>();
			int i=0;
			for( int i =0;i<DEPTH_WIDTH*DEPTH_HEIGHT;i++,pBufferRun++,pRaw++)
			{ 
				//The low-order 3 bits (bits 0-2) contain the skeleton (player) ID.
				//The high-order bits (bits 3�C15) contain the depth value in millimeters. 
				cv::Vec3b quad = Nui_ShortToQuad_Depth( *pBufferRun );

				pixels[i*3+0] = quad.val[0];
				pixels[i*3+1] = quad.val[1];
				pixels[i*3+2] = quad.val[2];

				*pRaw = NuiDepthPixelToDepth(*pBufferRun);//(*pBufferRun & 0xfff8) >> 3;
			}

			char buf[10];
			sprintf(buf,"fps: %d", fps_depth);
			cv::putText(renderedDepth, buf, cv::Point(20,20), 0, 0.6, CV_RGB(255,255,255));

			onDepthData(rawDepth, renderedDepth);
		}
		else
		{
			OutputDebugString( L"Buffer length of received texture is bogus\r\n" );
		}

		m_pNuiSensor->NuiImageStreamReleaseFrame( m_pDepthStreamHandle, &imageFrame );
	}

	void Nui_GotRgbAlert( )
	{
		NUI_IMAGE_FRAME imageFrame;

		HRESULT hr = m_pNuiSensor->NuiImageStreamGetNextFrame(
			m_pVideoStreamHandle,
			0,
			&imageFrame );
		if( FAILED( hr ) )
		{
			return;
		}

		INuiFrameTexture * pTexture = imageFrame.pFrameTexture;
		NUI_LOCKED_RECT LockedRect;
		pTexture->LockRect( 0, &LockedRect, NULL, 0 );
		if( LockedRect.Pitch != 0 )
		{
			cvSetData(iplColor, LockedRect.pBits, iplColor->widthStep);
			cv::Mat ref(iplColor);
			onRgbData(ref);
		}
		else
		{
			OutputDebugString( L"Buffer length of received texture is bogus\r\n" );
		}

		m_pNuiSensor->NuiImageStreamReleaseFrame( m_pVideoStreamHandle, &imageFrame );
	}

	void Nui_GotSkeletonAlert( )
	{
		FPS_CALC(fps_skeleton);

		NUI_SKELETON_FRAME SkeletonFrame;

		HRESULT hr = m_pNuiSensor->NuiSkeletonGetNextFrame( 0, &SkeletonFrame );

		bool bFoundSkeleton = false;
		for( int i = 0 ; i < NUI_SKELETON_COUNT ; i++ )
		{
			if( SkeletonFrame.SkeletonData[i].eTrackingState == NUI_SKELETON_TRACKED )
			{
				bFoundSkeleton = true;
			}
			else
			{
				if (isPlayerVisible[i])//the player is leaving us
				{
					printf("player %d -\n",i);					
					onPlayerLeave(i);
				}
				isPlayerVisible[i] = false;
			}
		}

		renderedSkeleton = cv::Scalar(0,0,0);

		if(!bFoundSkeleton )
		{
			return;
		}

		//trigger the event
		onSkeletonEventBegin();

		static NUI_TRANSFORM_SMOOTH_PARAMETERS smooth_param=
		{
			0.5f,//fSmoothing
			0.5f,//fCorrection
			0.5f,//fPrediction
			0.1f,//fJitterRadius
			0.08f,//fMaxDeviationRadius
		};
		// smooth out the skeleton data
		m_pNuiSensor->NuiTransformSmooth(&SkeletonFrame,&smooth_param);

		// we found a skeleton, re-start the timer
		m_bScreenBlanked = false;
		m_LastSkeletonFoundTime = -1;

		// draw each skeleton color according to the slot within they are found.
		// 
		char buf[100];
		sprintf(buf,"fps: %d", fps_skeleton);
		cv::putText(renderedSkeleton, buf, cv::Point(20,20), 0, 0.6, CV_RGB(255,255,255));

		bFoundSkeleton = false;
		for( int i = 0 ; i < NUI_SKELETON_COUNT ; i++ )
		{
			NUI_SKELETON_DATA* pSkel = &SkeletonFrame.SkeletonData[i];
			if(pSkel->eSkeletonPositionTrackingState[NUI_SKELETON_POSITION_HAND_RIGHT] == NUI_SKELETON_POSITION_NOT_TRACKED &&
				pSkel->eSkeletonPositionTrackingState[NUI_SKELETON_POSITION_HAND_LEFT] == NUI_SKELETON_POSITION_NOT_TRACKED
				//|| pSkel->eSkeletonPositionTrackingState[NUI_SKELETON_POSITION_HEAD] != NUI_SKELETON_POSITION_NOT_TRACKED
				)
			{//if both hands not tracked
				//TODO: replacing with SkeletonFrame.SkeletonData[i].dwTrackingID
				if (isPlayerVisible[i])//the player is leaving us
				{
					printf("player %d -\n",i);
					onPlayerLeave(i);
				}
				isPlayerVisible[i] = false;
			}
			else
			{
				bFoundSkeleton = true;//good news!!

				LONG lx=0,ly=0;
				USHORT uDepth;

				for (int k = 0; k < NUI_SKELETON_POSITION_COUNT; k++)
				{					
					NuiTransformSkeletonToDepthImage( pSkel->SkeletonPositions[k], &lx, &ly, &uDepth);

					m_Points[k].x = lx/(float)DEPTH_WIDTH;
					m_Points[k].y = ly/(float)DEPTH_HEIGHT;
					m_Points[k].z = pSkel->SkeletonPositions[k].z;

					m_Confidences[k] = (int)pSkel->eSkeletonPositionTrackingState[k]*0.5f;

					//for rendering
					cv::Point pos(m_Points[k].x*RGB_WIDTH+10, m_Points[k].y*RGB_HEIGHT);
					cv::circle(renderedSkeleton, pos, 5, vDefaultColor(k),3, -1);
					sprintf(buf, "%.2f", m_Points[k].z);
					cv::putText(renderedSkeleton, buf, pos, 0, 0.5, CV_RGB(255,255,255));
				}
				sprintf(buf, "#%d", i);
				NuiTransformSkeletonToDepthImage(pSkel->Position, &lx, &ly, &uDepth);
				cv::putText(renderedDepth, buf, cv::Point(lx/**DEPTH_WIDTH*/, ly/**DEPTH_HEIGHT*/), 0, 0.9, CV_RGB(0,0,0));

				Nui_DrawSkeletonSegment(renderedSkeleton,pSkel,4,NUI_SKELETON_POSITION_HIP_CENTER, NUI_SKELETON_POSITION_SPINE, NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_HEAD);
				Nui_DrawSkeletonSegment(renderedSkeleton,pSkel,5,NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SHOULDER_LEFT, NUI_SKELETON_POSITION_ELBOW_LEFT, NUI_SKELETON_POSITION_WRIST_LEFT, NUI_SKELETON_POSITION_HAND_LEFT);
				Nui_DrawSkeletonSegment(renderedSkeleton,pSkel,5,NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SHOULDER_RIGHT, NUI_SKELETON_POSITION_ELBOW_RIGHT, NUI_SKELETON_POSITION_WRIST_RIGHT, NUI_SKELETON_POSITION_HAND_RIGHT);
				Nui_DrawSkeletonSegment(renderedSkeleton,pSkel,5,NUI_SKELETON_POSITION_HIP_CENTER, NUI_SKELETON_POSITION_HIP_LEFT, NUI_SKELETON_POSITION_KNEE_LEFT, NUI_SKELETON_POSITION_ANKLE_LEFT, NUI_SKELETON_POSITION_FOOT_LEFT);
				Nui_DrawSkeletonSegment(renderedSkeleton,pSkel,5,NUI_SKELETON_POSITION_HIP_CENTER, NUI_SKELETON_POSITION_HIP_RIGHT, NUI_SKELETON_POSITION_KNEE_RIGHT, NUI_SKELETON_POSITION_ANKLE_RIGHT, NUI_SKELETON_POSITION_FOOT_RIGHT);

				bool isNewPlayer = false;
				if (!isPlayerVisible[i])
				{
					isNewPlayer = true;
					//trigger the event
					printf("player %d +\n",i);
					onPlayerEnter(i);
				}
				//trigger the event
				onPlayerData(m_Points, i, isNewPlayer );

				for (int k=0;k<NUI_SKELETON_POSITION_COUNT;k++)
					m_PrevPoints[i][k] = m_Points[k];

				isPlayerVisible[i] = true;
			}

			//trigger the event
			if (bFoundSkeleton)
				onSkeletonEventEnd();
		}
	}

	cv::Ptr<MiniThread> _threads[3];

	cv::Vec3b Nui_ShortToQuad_Depth( ushort s )
	{		
		USHORT RealDepth = NuiDepthPixelToDepth(s);//(s & 0xfff8) >> 3;
		USHORT Player = NuiDepthPixelToPlayerIndex(s);// & 7;

		// transform 13-bit depth information into an 8-bit intensity appropriate
		// for display (we disregard information in most significant bit)
		BYTE l = 255 - (BYTE)(256*RealDepth/0x0fff);

		int r,g,b;
		r = g = b = 0;

		switch( Player )
		{
		case 0:
			r = l / 2;
			g = l / 2;
			b = l / 2;
			break;
		case 1:
			r = l;
			break;
		case 2:
			g = l;
			break;
		case 3:
			r= l / 4;
			g= l;
			b= l;
			break;
		case 4:
			r = l;
			g = l;
			b = l / 4;
			break;
		case 5:
			r = l;
			g = l / 4;
			b = l;
			break;
		case 6:
			r = l / 2;
			g = l / 2;
			b = l;
			break;
		case 7:
			r = 255 - ( l / 2 );
			g = 255 - ( l / 2 );
			b = 255 - ( l / 2 );
		}

		return cv::Vec3b(r,g,b);
	}

	void Nui_DrawSkeletonSegment(cv::Mat& frame, NUI_SKELETON_DATA * pSkel, int numJoints, ... )
	{
		va_list vl;
		va_start(vl,numJoints);
		cv::Point prev;

		for (int i = 0; i < numJoints; i++)
		{
			NUI_SKELETON_POSITION_INDEX jointIndex = va_arg(vl,NUI_SKELETON_POSITION_INDEX);

			cv::Point curr(m_Points[jointIndex].x*RGB_WIDTH, m_Points[jointIndex].y*RGB_HEIGHT);
			if (pSkel->eSkeletonPositionTrackingState[jointIndex] != NUI_SKELETON_POSITION_NOT_TRACKED)
			{
				if (i > 0)
					cv::line(renderedSkeleton, prev, curr, vDefaultColor(i), 4);
				prev = curr;
			}
			else if (i == 0)
				prev = curr;
		}
		va_end(vl);
	}

	int	m_LastSkeletonFoundTime;
	bool m_bScreenBlanked;

	int fps_depth;
	int fps_rgb;
	int fps_skeleton;

	HANDLE evt_nextDepth;
	HANDLE evt_nextRgb;
	HANDLE evt_nextSkeleton;
	HANDLE m_pDepthStreamHandle;
	HANDLE m_pVideoStreamHandle;

	friend struct DepthThread;
	friend struct RgbThread;
	friend struct SkeletonThread;
};

#endif
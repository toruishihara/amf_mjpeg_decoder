#include <stdio.h>
#include <tchar.h>
#include <evr.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <fstream>

class MJPEGDecoder
{
public :
	MJPEGDecoder();
	~MJPEGDecoder();
	HRESULT Find();
	HRESULT Configure(UINT32 width, UINT32 height, UINT32 framerate);
	HRESULT Start();
	IMFSample * DecodeOneFrame(IMFSample* pInSample);
	HRESULT Close();

	// AMF MJPEG Decoder setup
	IMFTransform* m_pDecoderTransform;
	IMFMediaEventGenerator* m_pEventGen;
	DWORD m_inputStreamID;
	DWORD m_outputStreamID;

	UINT32 m_inWidth;
	UINT32 m_inHeight;
	UINT32 m_framerate;
	UINT32 m_outWidth;
	UINT32 m_outHeight;
	int m_sampleCount;
};


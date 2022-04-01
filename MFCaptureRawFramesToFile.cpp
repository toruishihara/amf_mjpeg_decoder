/******************************************************************************
* Filename: MFCaptureRawFramesToFile.cpp
*
* Description:
* This file contains a C++ console application that captures individual frames 
* from a webcam and dumps them in binary format to an output file.
*
* To convert the raw yuv data dumped at the end of this sample use the ffmpeg 
* commands below:
* ffmpeg -vcodec rawvideo -s 640x480 -pix_fmt yuv420p -i rawframes.yuv -vframes 1 out.jpeg
* ffmpeg -vcodec rawvideo -s 640x480 -pix_fmt yuv420p -i rawframes.yuv out.avi
*
* More info see: https://ffmpeg.org/ffmpeg.html#Video-and-Audio-file-format-conversion
*
* Note: The webcam index and the source reader media output type will need 
* adjustment depending on the the configuration of video devices on the machine 
* running this sample.
*
* Author:
* Aaron Clauson (aaron@sipsorcery.com)
*
* History:
* 06 Mar 2015		Aaron Clauson (aaron@sipsorcery.com)	Created.
*
* License: Public Domain (no warranty, use at own risk)
/******************************************************************************/

#include "MFUtility.h"

#include <stdio.h>
#include <tchar.h>
#include <evr.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <fstream>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")

#define WEBCAM_DEVICE_INDEX 0	// Adjust according to desired video capture device.
#define SAMPLE_COUNT 100			// Adjust depending on number of samples to capture.
#define PLANE_Y_FILENAME L"planeY.bmp"
#define PLANE_UV_FILENAME L"planeUV.bmp"
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240

class MJPEGDecoder
{
public:
	MJPEGDecoder(void* pCWVideoCapture);
	~MJPEGDecoder(void);
	void SendDecodedSample(IMFSample* sample);
	HRESULT FindDecoder();
	void ConfigureDecoder(int widht, int height, int framerate);
	void StartDecoder();
	IMFSample* DecodeOneFrame(IMFSample* sample);

private:
	void* m_pCWVideoCapture;
	IMFTransform* m_pDecoderTransform;  // AMD MJPEG encoder
	IMFMediaEventGenerator* m_pEventGen;
	DWORD m_inputStreamID;
	DWORD m_outputStreamID;
	int m_nWidth;
	int m_nHeight;
	int m_nFramerate;
	int m_nUVStartPosition;
	int m_nCnt;
	unsigned char* m_pUVBuffer;
};

int main()
{
	IMFMediaSource* videoSource = NULL;
	UINT32 videoDeviceCount = 0;
	IMFAttributes* videoConfig = NULL;
	IMFActivate** videoDevices = NULL;
	IMFSourceReader* videoReader = NULL;
	WCHAR* webcamFriendlyName = NULL;
	IMFMediaType* videoSourceOutputType = NULL;
	IMFMediaType* pSrcOutMediaType = NULL;
	UINT webcamNameLength = 0;
	MJPEGDecoder* decoder = NULL;

	//std::ofstream outputBuffer(CAPTURE_FILENAME, std::ios::out | std::ios::binary);

	CHECK_HR(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE),
		"COM initialisation failed.");

	CHECK_HR(MFStartup(MF_VERSION),
		"Media Foundation initialisation failed.");

	// Get the first available webcam.
	CHECK_HR(MFCreateAttributes(&videoConfig, 1), 
		"Error creating video configuation.");

	// Request video capture devices.
	CHECK_HR(videoConfig->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), 
		"Error initialising video configuration object.");

	CHECK_HR(MFEnumDeviceSources(videoConfig, &videoDevices, &videoDeviceCount), 
		"Error enumerating video devices.");

	CHECK_HR(videoDevices[WEBCAM_DEVICE_INDEX]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &webcamFriendlyName, &webcamNameLength),
		"Error retrieving video device friendly name.");

	wprintf(L"First available webcam: %s\n", webcamFriendlyName);

	CHECK_HR(videoDevices[WEBCAM_DEVICE_INDEX]->ActivateObject(IID_PPV_ARGS(&videoSource)), 
		"Error activating video device.");

	// Create a source reader.
	CHECK_HR(MFCreateSourceReaderFromMediaSource(
		videoSource,
		videoConfig,
		&videoReader), 
		"Error creating video source reader.");

	// The list of media types supported by the webcam.
	//ListModes(videoReader);

	/*CHECK_HR(videoReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &videoSourceOutputType),
		"Error retrieving current media type from first video stream.");*/

	// Note the webcam needs to support this media type.
	CHECK_HR(MFCreateMediaType(&pSrcOutMediaType), "Failed to create media type.");
	CHECK_HR(pSrcOutMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set video media type.");
	CHECK_HR(pSrcOutMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG /*WMMEDIASUBTYPE_I420*/), "Failed to set video media sub type to MJPG.");
	CHECK_HR(MFSetAttributeSize(pSrcOutMediaType, MF_MT_FRAME_SIZE, FRAME_WIDTH, FRAME_HEIGHT), "Failed to set frame size.");
	//CHECK_HR(CopyAttribute(videoSourceOutputType, pSrcOutMediaType, MF_MT_DEFAULT_STRIDE), "Failed to copy default stride attribute.");

	CHECK_HR(videoReader->SetCurrentMediaType(0, NULL, pSrcOutMediaType), 
		"Failed to set media type on source reader.");

	decoder = new MJPEGDecoder(NULL);
	CHECK_HR(decoder->FindDecoder(), "Failed find decoder");
	decoder->ConfigureDecoder(FRAME_WIDTH, FRAME_HEIGHT, 30);
	decoder->StartDecoder();
	printf("Reading video samples from webcam.");

	IMFSample *videoSample = NULL;
	IMFSample *decodedSample = NULL;
	DWORD streamIndex, flags;
	LONGLONG llVideoTimeStamp, llSampleDuration;
	int sampleCount = 0;

	while (sampleCount <= SAMPLE_COUNT)
	{
		CHECK_HR(videoReader->ReadSample(
			MF_SOURCE_READER_FIRST_VIDEO_STREAM,
			0,                              // Flags.
			&streamIndex,                   // Receives the actual stream index. 
			&flags,                         // Receives status flags.
			&llVideoTimeStamp,              // Receives the time stamp.
			&videoSample                    // Receives the sample or NULL.
			), "Error reading video sample.");

		if (flags & MF_SOURCE_READERF_STREAMTICK)
		{
			printf("Stream tick.\n");
		}

		if (videoSample)
		{
			printf("Decode sample %i.\n", sampleCount);
			decodedSample = decoder->DecodeOneFrame(videoSample);

			printf("Writing sample %i.\n", sampleCount);

			CHECK_HR(videoSample->SetSampleTime(llVideoTimeStamp), "Error setting the video sample time.");
			CHECK_HR(videoSample->GetSampleDuration(&llSampleDuration), "Error getting video sample duration.");

			IMFMediaBuffer *buf = NULL;
			DWORD bufLength;
			CHECK_HR(videoSample->ConvertToContiguousBuffer(&buf), "ConvertToContiguousBuffer failed.");
			CHECK_HR(buf->GetCurrentLength(&bufLength), "Get buffer length failed.");

			printf("Sample length %i.\n", bufLength);

			byte *byteBuffer;
			DWORD buffCurrLen = 0;
			DWORD buffMaxLen = 0;
			CHECK_HR(buf->Lock(&byteBuffer, &buffMaxLen, &buffCurrLen), "Failed to lock video sample buffer.");
			
			//outputBuffer.write((char *)byteBuffer, bufLength);
			//outputBuffer.close();

			CHECK_HR(buf->Unlock(), "Failed to unlock video sample buffer.");

			buf->Release();
			videoSample->Release();
		}
		sampleCount++;
	}

	//outputBuffer.close();

done:

	printf("finished.\n");
	int c = getchar();

	SAFE_RELEASE(videoSource);
	SAFE_RELEASE(videoConfig);
	SAFE_RELEASE(videoDevices);
	SAFE_RELEASE(videoReader);
	SAFE_RELEASE(videoSourceOutputType);
	SAFE_RELEASE(pSrcOutMediaType);

	return 0;
}

void print_guid(GUID guid);
void dump_sample(IMFSample* pSample);
void save_bmp(IMFSample* pSample, int width, int height);
void print_attr(IMFAttributes* pAttr);

struct __declspec(uuid("687CBC51-25DA-4FFC-A678-1E64943285A7")) AMD_MJPEG_DECODER_Cls; // AMD Hardware MJPEG decoder
struct __declspec(uuid("CB17E772-E1CC-4633-8450-5617AF577905")) MS_MJPEG_DECODER_Cls; // Windows software MJPEG decoder

MJPEGDecoder::MJPEGDecoder(void* pCWVideoCapture)
{
	m_pCWVideoCapture = pCWVideoCapture;
	m_pEventGen = NULL;
	m_inputStreamID = 0;
	m_outputStreamID = 0;
	m_pDecoderTransform = NULL;  // MJPEG decoder
	m_nCnt = 0;
	m_pUVBuffer = NULL;
}

MJPEGDecoder::~MJPEGDecoder(void)
{
	if (m_pUVBuffer) {
		free(m_pUVBuffer);
	}
}

void MJPEGDecoder::SendDecodedSample(IMFSample* sample)
{
	IMFMediaBuffer* buffer;
	HRESULT hr;
	BYTE* data;
	DWORD size;

	hr = sample->ConvertToContiguousBuffer(&buffer);
	hr = buffer->Lock(&data, NULL, &size);
	buffer->Unlock();
	buffer->Release();
	sample->Release();
	sample = nullptr;
}

HRESULT MJPEGDecoder::FindDecoder()
{
	IUnknown* iunkown = NULL;
	IMFAttributes* attributes;
	// Find encoder
	HRESULT hr;
	UINT32 activateCount = 0;

	MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };
	UINT32 flags1 = MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;

	hr = CoCreateInstance(__uuidof(AMD_MJPEG_DECODER_Cls), NULL, CLSCTX_INPROC_SERVER,
		IID_IUnknown, (void**)&iunkown);
	if (hr != S_OK) {
		printf("CoCreateInstance faild. Not AMD CPU\n");
		return hr;
	}
	hr = iunkown->QueryInterface(IID_PPV_ARGS(&m_pDecoderTransform));
	if (hr != S_OK) {
		printf("QueryInterface faild.\n");
		return hr;
	}

	// Unlock the encoderTransform for async use and get event generator
	hr = m_pDecoderTransform->GetAttributes(&attributes);
	if (SUCCEEDED(hr))
	{
		hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
		attributes->Release();
	}

	// Get stream IDs (expect 1 input and 1 output stream)
	hr = m_pDecoderTransform->GetStreamIDs(1, &m_inputStreamID, 1, &m_outputStreamID);
	if (hr == E_NOTIMPL)
	{
		m_inputStreamID = 0;
		m_outputStreamID = 0;
	}
	return S_OK;
}

void MJPEGDecoder::ConfigureDecoder(int width, int height, int framerate)
{
	//IMFAttributes* attributes;
	HRESULT hr;
	IMFMediaType* inputType;
	IMFMediaType* outputType;
	UINT32 val32;
	int found = 0;
	int i = 0;

	printf("ConfigureDecoder(%d %d %d)", width, height, framerate);
	m_nWidth = width;
	m_nHeight = height;
	m_nFramerate = framerate;
	m_nUVStartPosition = 0;

	if (m_pEventGen) m_pEventGen->Release();
	hr = m_pDecoderTransform->QueryInterface(&m_pEventGen);

	hr = MFCreateMediaType(&inputType);
	hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG);
	hr = inputType->SetUINT32(MF_MT_COMPRESSED, 1);
	hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, width, height);
	hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, framerate, 1);
	hr = MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	hr = m_pDecoderTransform->SetInputType(m_inputStreamID, inputType, 0);
	if (hr != S_OK) {
		printf("SetInputType failed hr=%x\n", hr);
	}

	IMFMediaType* pOutType = NULL;
	hr = S_OK;
	found = 0;
	while (hr == S_OK) {
		IMFMediaType* pType;
		GUID subtype = { 0 };
		UINT32 val32;
		hr = m_pDecoderTransform->GetOutputAvailableType(0, i, &pType);
		if (hr != 0) {
			break;
		}
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		printf("GetOutputAvailableType i=%d %s\n", i, GetGUIDNameConst(subtype));
		if (subtype == MFVideoFormat_NV12) {
			print_attr(pType);
			hr = m_pDecoderTransform->SetOutputType(m_outputStreamID, pType, 0);
			if (hr != S_OK) {
				printf("SetOutputType failed hr=%x\n", hr);
			}
			pType->Release();
			found = 1;
			break;
		}
		pType->Release();
		++i;
	}
	if (found == 0) {
		printf("Failed to set output media type on AMD_MJPEG decoder MFT.");
	}
}

void MJPEGDecoder::StartDecoder()
{
	HRESULT hr;

	// Start encoder
	hr = m_pDecoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL);
	hr = m_pDecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL);
	hr = m_pDecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL);
}

HRESULT GetDecoderTransformOutput(IMFTransform* pencoderTransform, IMFSample** pOutSample, BOOL* decoderTransformFlushed, int oid)
{
	MFT_OUTPUT_STREAM_INFO StreamInfo = { 0 };
	MFT_OUTPUT_DATA_BUFFER outputDataBuffer = { 0 };
	DWORD processOutputStatus = 0;
	IMFMediaType* pChangedOutMediaType = NULL;

	HRESULT hr = S_OK;
	*decoderTransformFlushed = FALSE;
	*pOutSample = NULL;
	hr = pencoderTransform->GetOutputStreamInfo(oid, &StreamInfo);

	outputDataBuffer.dwStreamID = oid;
	outputDataBuffer.dwStatus = 0;
	outputDataBuffer.pEvents = NULL;

	auto mftProcessOutput = pencoderTransform->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
	//Loge("Process output result %.2X, MFT status %.2X.\n", mftProcessOutput, processOutputStatus);
	if (mftProcessOutput == S_OK) {
		// Sample is ready and allocated on the encoderTransform output buffer.
		*pOutSample = outputDataBuffer.pSample;
	}
	else if (mftProcessOutput == MF_E_TRANSFORM_STREAM_CHANGE) {
		printf("MF_E_TRANSFORM_STREAM_CHANGE\n");
	}
	else if (mftProcessOutput == MF_E_TRANSFORM_NEED_MORE_INPUT) {
		printf("MF_E_TRANSFORM_NEED_MORE_INPUT\n");
		// More input is not an error condition but it means the allocated output sample is empty.
		(*pOutSample)->Release();
		if (*pOutSample != NULL)*pOutSample = NULL;
		hr = MF_E_TRANSFORM_NEED_MORE_INPUT;
	}
	else {
		printf("MFT ProcessOutput error result %.2X, MFT status %.2X.\n", mftProcessOutput, processOutputStatus);
		hr = mftProcessOutput;
		if (*pOutSample != NULL) (*pOutSample)->Release();
		*pOutSample = NULL;
	}

	if (pChangedOutMediaType) pChangedOutMediaType->Release();

	return hr;
}

// captureThread is conbimed with h264 encoder
IMFSample* MJPEGDecoder::DecodeOneFrame(IMFSample* sample)
{
	HRESULT hr;
	OutputDebugStringA("MJPEGDecoder::DecodeOneFrame in\n");
	m_nCnt++;
	while (1) {
		MediaEventType eventType;
		IMFMediaEvent* event;
		IMFSample* pDecodedSample;
		BOOL decoderTransformFlushed = FALSE;
		HRESULT getDecoderResult;

		hr = m_pEventGen->GetEvent(0, &event);
		hr = event->GetType(&eventType);
		printf("GetEvent eventType=%d\n", eventType);
		switch (eventType)
		{
		case METransformNeedInput:
			// Apply AMD MJPEG decoderTransform
			hr = m_pDecoderTransform->ProcessInput(m_inputStreamID, sample, 0);
			sample->Release();
			break;
		case METransformHaveOutput:
			getDecoderResult = S_OK;
			while (getDecoderResult == S_OK) {
				pDecodedSample = nullptr;
				getDecoderResult = GetDecoderTransformOutput(m_pDecoderTransform, &pDecodedSample, &decoderTransformFlushed, m_outputStreamID);
				if (getDecoderResult != S_OK && getDecoderResult != MF_E_TRANSFORM_NEED_MORE_INPUT) {
					break;
				}
				if (decoderTransformFlushed == TRUE) {
					break;
				}
				else if (pDecodedSample != NULL) {
					if (m_nCnt == 1) {
						printf("dump compressed sample ");
						dump_sample(sample);
						printf("dump pDecodedSample ");
						dump_sample(pDecodedSample);
						save_bmp(pDecodedSample, m_nWidth, m_nHeight);
					}
					OutputDebugStringA("MJPEGDecoder::DecodeOneFrame out\n");
					return pDecodedSample;
				}
			}
			break;
		}
	}
}

void print_guid(GUID guid) {
	printf("Guid = {%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}\n",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}

void dump_sample(IMFSample* pSample)
{
	HRESULT hr;
	HANDLE file;
	BITMAPFILEHEADER fileHeader;
	BITMAPINFOHEADER fileInfo;
	DWORD len;
	DWORD bufferCount;
	IMFMediaBuffer* mediaBuffer = NULL;
	BYTE* pData = NULL;
	int cnt = 0;

	hr = pSample->GetBufferCount(&bufferCount);
	printf("dump_sample bufferCount=%d\n", bufferCount);
	pSample->ConvertToContiguousBuffer(&mediaBuffer);
	hr = mediaBuffer->Lock(&pData, NULL, NULL);
	hr = mediaBuffer->GetCurrentLength(&len);
	printf("dump_sample len=%d\n", len);
	char buf[32 * 4 + 32];
	char tmp[8];
	snprintf(buf, sizeof(buf), "%08x: ", cnt);
	for (int i = 0; i < len; ++i) {
		++cnt;
		snprintf(tmp, sizeof(tmp), "%02x ", pData[i]);
		strncat(buf, tmp, sizeof(buf));
		if (i % 32 == 31) {
			printf(buf);
			printf("\n");
			snprintf(buf, sizeof(buf), "%08x: ", cnt);
		}
	}
	mediaBuffer->Unlock();
}
void save_bmp(IMFSample* pSample, int width, int height)
{
	HRESULT hr;
	HANDLE file;
	BITMAPFILEHEADER fileHeader;
	BITMAPINFOHEADER fileInfo;
	DWORD write = 0;
	IMFMediaBuffer* mediaBuffer = NULL;
	BYTE* pData = NULL;

	file = CreateFile(PLANE_Y_FILENAME, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  //Sets up the new bmp to be written to

	fileHeader.bfType = 19778;                                                  //Sets our type to BM or bmp
	fileHeader.bfSize = sizeof(fileHeader.bfOffBits) + sizeof(RGBTRIPLE);       //Sets the size equal to the size of the header struct
	fileHeader.bfReserved1 = 0;                                                 //sets the reserves to 0
	fileHeader.bfReserved2 = 0;
	fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER); //Sets offbits equal to the size of file and info header

	fileInfo.biSize = sizeof(BITMAPINFOHEADER);
	fileInfo.biWidth = width;
	fileInfo.biHeight = height;
	fileInfo.biPlanes = 1;
	fileInfo.biBitCount = 8;
	fileInfo.biCompression = BI_RGB;
	fileInfo.biSizeImage = width * height;
	fileInfo.biXPelsPerMeter = 2400;
	fileInfo.biYPelsPerMeter = 2400;
	fileInfo.biClrImportant = 0;
	fileInfo.biClrUsed = 256;

	WriteFile(file, &fileHeader, sizeof(fileHeader), &write, NULL);
	WriteFile(file, &fileInfo, sizeof(fileInfo), &write, NULL);
	// color table
	for (int i = 0; i < 256; ++i) {
		unsigned char col[4];
		col[0] = col[1] = col[2] = i;
		col[3] = 0;
		WriteFile(file, col, 4, &write, NULL);
	}
	pSample->ConvertToContiguousBuffer(&mediaBuffer);
	hr = mediaBuffer->Lock(&pData, NULL, NULL);
	WriteFile(file, pData, fileInfo.biSizeImage, &write, NULL);
	CloseHandle(file);

	write = 0;
	file = CreateFile(PLANE_UV_FILENAME, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);  // UV plane
	fileInfo.biWidth = width;
	fileInfo.biHeight = height / 2;
	fileInfo.biSizeImage = width * height / 2;
	WriteFile(file, &fileHeader, sizeof(fileHeader), &write, NULL);
	WriteFile(file, &fileInfo, sizeof(fileInfo), &write, NULL);
	// color table
	for (int i = 0; i < 256; ++i) {
		unsigned char col[4];
		col[0] = col[1] = col[2] = i;
		col[3] = 0;
		WriteFile(file, col, 4, &write, NULL);
	}
	WriteFile(file, pData + width * height, width * height / 2, &write, NULL);
	CloseHandle(file);
	mediaBuffer->Unlock();
}
void print_attr(IMFAttributes* pAttr)
{
	HRESULT hr;
	UINT32 cnt;
	GUID guid;
	PROPVARIANT pv;
	hr = pAttr->GetCount(&cnt);
	for (int i = 0; i < cnt; ++i) {
		hr = pAttr->GetItemByIndex(i, &guid, &pv);
		printf("attr i=%d %s\n", i, GetGUIDNameConst(guid));
		//print_guid(guid);
	}
}
/******************************************************************************
* Filename: MFCatpureDecodeSave.cpp
* Based on: MFCaptureRawFramesToFile.cpp
*
* Description:
* This file contains a C++ console application that captures individual frames 
* from a webcam and dumps them in BMP format to an output file.
*
* Note: The webcam index and the source reader media output type will need 
* adjustment depending on the the configuration of video devices on the machine 
* running this sample.
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
#define FRAME_RATE 30

struct __declspec(uuid("687CBC51-25DA-4FFC-A678-1E64943285A7")) AMD_MJPEG_DECODER_Cls; // AMD Hardware MJPEG decoder

// Util functions
void print_guid(GUID guid);
void dump_sample(IMFSample* pSample);
void save_bmp(IMFSample* pSample, int width, int height);
void print_attr(IMFAttributes* pAttr);

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

	// AMF MJPEG Decoder setup
	HRESULT hr;
	IUnknown* iunkown = NULL;
	IMFAttributes* attributes = NULL;
	IMFTransform* pDecoderTransform;
	IMFMediaEventGenerator* pEventGen;
	DWORD inputStreamID;
	DWORD outputStreamID;
	IMFMediaType* inputType;
	IMFMediaType* outputType;

	// Note the webcam needs to support this media type.
	CHECK_HR(MFCreateMediaType(&pSrcOutMediaType), "Failed to create media type.");
	CHECK_HR(pSrcOutMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set video media type.");
	CHECK_HR(pSrcOutMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG /*WMMEDIASUBTYPE_I420*/), "Failed to set video media sub type to MJPG.");
	CHECK_HR(MFSetAttributeSize(pSrcOutMediaType, MF_MT_FRAME_SIZE, FRAME_WIDTH, FRAME_HEIGHT), "Failed to set frame size.");
	//CHECK_HR(CopyAttribute(videoSourceOutputType, pSrcOutMediaType, MF_MT_DEFAULT_STRIDE), "Failed to copy default stride attribute.");

	CHECK_HR(videoReader->SetCurrentMediaType(0, NULL, pSrcOutMediaType), 
		"Failed to set media type on source reader.");

	// Find encoder
	CHECK_HR(CoCreateInstance(__uuidof(AMD_MJPEG_DECODER_Cls), NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&iunkown), 
		"Failed to create instance of AMF decoder");
	CHECK_HR(iunkown->QueryInterface(IID_PPV_ARGS(&pDecoderTransform)), "QueryInterface faild.");

	// Unlock the encoderTransform for async use and get event generator
	CHECK_HR(pDecoderTransform->GetAttributes(&attributes), "GetAttr failed");
	CHECK_HR(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE), "Transform unlock failed");
	attributes->Release();

	// Get stream IDs (expect 1 input and 1 output stream)
	hr = pDecoderTransform->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
	if (hr == E_NOTIMPL)
	{
		inputStreamID = 0;
		outputStreamID = 0;
	}
	else {
		printf("Failed GetStreamID hr=%x\n", hr);
	}

	// Configure Decoder
	CHECK_HR(pDecoderTransform->QueryInterface(&pEventGen), "Get EventGen failed");
	CHECK_HR(MFCreateMediaType(&inputType), "Create failed");
	CHECK_HR(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "SetGUID failed");
	CHECK_HR(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG), "SetGUID failed");
	CHECK_HR(inputType->SetUINT32(MF_MT_COMPRESSED, 1), "SetUINT32 failed");
	CHECK_HR(MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, FRAME_WIDTH, FRAME_HEIGHT), "SetAttr failed");
	CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, FRAME_RATE, 1), "SetAttr failed");
	CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "SetAttr failed");
	CHECK_HR(pDecoderTransform->SetInputType(inputStreamID, inputType, 0), "SetInputType failed");
	// Search decoder output type and set to NV12
	int found = 0;
	int i = 0;
	hr = S_OK;
	while (hr == S_OK) {
		IMFMediaType* pType;
		GUID subtype = { 0 };
		UINT32 val32;
		hr = pDecoderTransform->GetOutputAvailableType(0, i, &pType);
		if (hr != 0)
		{ 
			break; 
		}
		CHECK_HR(pType->GetGUID(MF_MT_SUBTYPE, &subtype), "Get subtype failed");
		printf("GetOutputAvailableType i=%d %s\n", i, GetGUIDNameConst(subtype));
		if (subtype == MFVideoFormat_NV12) {
			CHECK_HR(pDecoderTransform->SetOutputType(outputStreamID, pType, 0), "SetOutputType failed");
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

	CHECK_HR(pDecoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL), "Failed ProcessMessage");
	CHECK_HR(pDecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL), "Failed ProcessMessage");
	CHECK_HR(pDecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL), "FailedProcessMessage");

	IMFSample* videoSample = NULL;
	IMFSample* decodedSample = NULL;
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

		if (videoSample == NULL) {
			continue;
		}

		printf("Decode sample %i.\n", sampleCount);
		bool hasOutput = false;
		while (hasOutput == false) {
			MediaEventType eventType;
			IMFMediaEvent* event;
			IMFSample* pDecodedSample;
			BOOL decoderTransformFlushed = FALSE;
			HRESULT getDecoderResult;

			CHECK_HR(pEventGen->GetEvent(0, &event), "GetEvent failed");
			CHECK_HR(event->GetType(&eventType), "GetType failed");
			printf("GetEvent eventType=%d\n", eventType);
			switch (eventType)
			{
			case METransformNeedInput:
				if (sampleCount == 0) {
					printf("dump videoSample ");
					dump_sample(videoSample);
				}
				CHECK_HR(pDecoderTransform->ProcessInput(inputStreamID, videoSample, 0), "PorcessInput failed");
				videoSample->Release();
				break;
			case METransformHaveOutput:
				MFT_OUTPUT_STREAM_INFO StreamInfo = { 0 };
				MFT_OUTPUT_DATA_BUFFER outputDataBuffer = { 0 };
				DWORD processOutputStatus = 0;

				CHECK_HR(pDecoderTransform->GetOutputStreamInfo(outputStreamID, &StreamInfo), "GetOutputStreamInfo failed");
				outputDataBuffer.dwStreamID = outputStreamID;
				outputDataBuffer.dwStatus = 0;
				outputDataBuffer.pEvents = NULL;

				HRESULT hr = pDecoderTransform->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
				// Sample is ready and allocated on the encoderTransform output buffer.
				if (hr == S_OK) {
					decodedSample = outputDataBuffer.pSample;
					hasOutput = true;
					if (sampleCount == 0) {
						printf("dump decodedSample ");
						dump_sample(decodedSample);
						save_bmp(decodedSample, FRAME_WIDTH, FRAME_HEIGHT);
					}
					break;
				} else {
					printf("PorcessOutput failed hr=%x\n", hr);
					break;
				}
			}
		}
		sampleCount++;
	}

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
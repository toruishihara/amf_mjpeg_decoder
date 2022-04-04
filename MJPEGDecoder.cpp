#include "MJPEGDecoder.h"

// Util functions
#define CHECK_HR(hr, msg) if (hr != S_OK) { printf(msg); printf(" Error: %.2X.\n", hr); goto done; }
LPCSTR GetGUIDNameConst(const GUID& guid);

void print_guid(GUID guid);
void dump_sample(IMFSample* pSample);
void save_bmp(IMFSample* pSample, int width, int height);
void print_attr(IMFAttributes* pAttr);

struct __declspec(uuid("687CBC51-25DA-4FFC-A678-1E64943285A7")) AMD_MJPEG_DECODER_Cls; // AMD Hardware MJPEG decoder

MJPEGDecoder::MJPEGDecoder()
{
	m_pDecoderTransform = NULL;
	m_pEventGen = NULL;
	m_inputStreamID = 0;
	m_outputStreamID = 0;

	m_inWidth = 0;
	m_inHeight = 0;
	m_framerate = 0;
	m_outWidth = 0;
	m_outHeight = 0;
	m_sampleCount = 0;
}

MJPEGDecoder::~MJPEGDecoder()
{}


HRESULT MJPEGDecoder::Find()
{
	HRESULT hr;
	IUnknown* iunkown = NULL;
	IMFAttributes* attributes = NULL;

	// Find encoder
	CHECK_HR(CoCreateInstance(__uuidof(AMD_MJPEG_DECODER_Cls), NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&iunkown),
		"Failed to create instance of AMF decoder");
	CHECK_HR(iunkown->QueryInterface(IID_PPV_ARGS(&m_pDecoderTransform)), "QueryInterface faild.");

	// Unlock the encoderTransform for async use and get event generator
	CHECK_HR(m_pDecoderTransform->GetAttributes(&attributes), "GetAttr failed");
	CHECK_HR(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE), "Transform unlock failed");
	attributes->Release();

	// Get stream IDs (expect 1 input and 1 output stream)
	hr = m_pDecoderTransform->GetStreamIDs(1, &m_inputStreamID, 1, &m_outputStreamID);
	if (hr == E_NOTIMPL)
	{
		m_inputStreamID = 0;
		m_outputStreamID = 0;
	}
	else {
		printf("Failed GetStreamID hr=%x\n", hr);
	}

	return S_OK;
done:
	printf("Failed %s hr=%x\n", __FUNCTION__, hr);
	return -1;
}

HRESULT MJPEGDecoder::Configure(UINT32 width, UINT32 height, UINT32 framerate)
{
	HRESULT hr;
	IMFMediaType* inputType;
	// Configure Decoder
	CHECK_HR(m_pDecoderTransform->QueryInterface(&m_pEventGen), "Get EventGen failed");
	CHECK_HR(MFCreateMediaType(&inputType), "Create failed");
	CHECK_HR(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "SetGUID failed");
	CHECK_HR(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG), "SetGUID failed");
	CHECK_HR(inputType->SetUINT32(MF_MT_COMPRESSED, 1), "SetUINT32 failed");
	CHECK_HR(MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, width, height), "SetAttr failed");
	CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, framerate, 1), "SetAttr failed");
	CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "SetAttr failed");
	CHECK_HR(m_pDecoderTransform->SetInputType(m_inputStreamID, inputType, 0), "SetInputType failed");
	m_inWidth = width;
	m_inHeight = height;
	m_framerate = framerate;

	// Search decoder output type and set to NV12
	int found = 0;
	int i = 0;
	hr = S_OK;
	while (hr == S_OK) {
		IMFMediaType* pType;
		GUID subtype = { 0 };
		UINT32 val32;
		hr = m_pDecoderTransform->GetOutputAvailableType(0, i, &pType);
		if (hr != 0)
		{
			break;
		}
		CHECK_HR(pType->GetGUID(MF_MT_SUBTYPE, &subtype), "Get subtype failed");
		printf("GetOutputAvailableType i=%d %s\n", i, GetGUIDNameConst(subtype));
		if (subtype == MFVideoFormat_NV12) {
			print_attr(pType);
			CHECK_HR(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &m_outWidth, &m_outHeight), "Get MF_MT_FRAME_SIZE failed");
			printf("MJPEG decoder out %d x %d\n", m_outWidth, m_outHeight);
			CHECK_HR(m_pDecoderTransform->SetOutputType(m_outputStreamID, pType, 0), "SetOutputType failed");
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
	return S_OK;
done:
	printf("Failed %s hr=%x\n", __FUNCTION__, hr);
	return -1;
}

HRESULT MJPEGDecoder::Start()
{
	HRESULT hr;
	CHECK_HR(m_pDecoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL), "Failed ProcessMessage");
	CHECK_HR(m_pDecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL), "Failed ProcessMessage");
	CHECK_HR(m_pDecoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL), "FailedProcessMessage");
	return S_OK;
done:
	printf("Failed %s hr=%x\n", __FUNCTION__, hr);
	return -1;
}

IMFSample * MJPEGDecoder::DecodeOneFrame(IMFSample* pInSample)
{
	HRESULT hr = S_OK;
	IMFSample* pOutSample;
	MediaEventType eventType = 0;
	IMFMediaEvent* event;
	bool inputProcessed = false;
	bool hasOutput = false;

	OutputDebugStringA("DecodeOneFrame start\n");
	while (hasOutput == false) {
		hr = m_pEventGen->GetEvent(0, &event);
		if (hr != S_OK) { printf("Error %s %d hr=%x\n", __FILE__, __LINE__, hr); }
		hr = event->GetType(&eventType);
		if (hr != S_OK) { printf("Error %s %d hr=%x\n", __FILE__, __LINE__, hr); }
		printf("GetEvent eventType=%d\n", eventType);
		switch (eventType)
		{
		case METransformNeedInput:
			if (inputProcessed) {
				printf("Error Input processed 2nd time\n");
				CHECK_HR(m_pDecoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL), "Failed ProcessMessage");
				break;
			}
			if (m_sampleCount == 10) {
				printf("dump pInSample ");
				//dump_sample(pInSample);
			}
			hr = m_pDecoderTransform->ProcessInput(m_inputStreamID, pInSample, 0);
			if (hr != S_OK) { printf("Error %s %d hr=%x\n", __FILE__, __LINE__, hr); }
			pInSample->Release();
			inputProcessed = true;
			break;
		case METransformHaveOutput:
			MFT_OUTPUT_STREAM_INFO StreamInfo = { 0 };
			MFT_OUTPUT_DATA_BUFFER outputDataBuffer = { 0 };
			DWORD processOutputStatus = 0;

			hr = m_pDecoderTransform->GetOutputStreamInfo(m_outputStreamID, &StreamInfo);
			outputDataBuffer.dwStreamID = m_outputStreamID;
			outputDataBuffer.dwStatus = 0;
			outputDataBuffer.pEvents = NULL;

			HRESULT hr2 = m_pDecoderTransform->ProcessOutput(0, 1, &outputDataBuffer, &processOutputStatus);
			// Sample is ready and allocated on the encoderTransform output buffer.
			if (hr2 == S_OK) {
				pOutSample = outputDataBuffer.pSample;
				hasOutput = true;
				if (m_sampleCount == 10) {
					MFT_OUTPUT_STREAM_INFO stream_info;
					hr = m_pDecoderTransform->GetOutputStreamInfo(m_outputStreamID, &stream_info);
					printf("dump decodedSample ");
					//dump_sample(pOutSample);
					save_bmp(pOutSample, m_outWidth, m_outHeight);
				}
				break;
			}
			else {
				printf("PorcessOutput failed hr=%x\n", hr2);
				break;
			}
		}
	}
	m_sampleCount++;
	OutputDebugStringA("DecodeOneFrame end\n");
	return pOutSample;
done:
	printf("Failed %s hr=%x\n", __FUNCTION__, hr);
	return NULL;
}

HRESULT MJPEGDecoder::Close()
{
	HRESULT hr;
	return S_OK;
done:
	printf("Failed %s hr=%x\n", __FUNCTION__, hr);
	return -1;
}

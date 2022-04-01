# amf_mjpeg_decoder
AMF Motion JPEG HW decoder

Requirement :
	CP with AMD CPU
	USB camera device like Logitech C270 (MJPEG 320x240 30fps)
	Windows 10
	Visual Studio 2019

Usage : 
	Open MFCapgtureRawFramesToFile.sln by Visual Studio 2019
	Build and run by clicking "Local Windows Debugger"
	
Result :
	Input first video frame is dumped on console. JPEG format start with 0xff 0xd8.
	Decoded frame is dumped on console 320x240 NV12 format
	Expected size: 320*240*1.5=115200
	Actual size:122880
	Zeros Gap : 0x00012c00 - 0x00014000
	All data is zeros on the gap.
	Decoded Y plane is saved as grayscale BMP file as "planeY.bmp"
	Decoded UV plane is saved as grayscale BMP file as "planeUV.bmp"
	Some part of planeUV.bmp is all black, because of Zeros Gap.
	
Problem:
	I can detect Zeros Gap position and call memcpy() for UV plane. 
	But memcpy() takes 20ms, even decode takes 5ms, on 1920x1080 case.
	Total 25ms is slower than software decoder.
	
	
	
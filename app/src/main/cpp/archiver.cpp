//
// Created by MSI on 4/17/2021.
//

#include "huff.h"
#include <jni.h>
#include <cstdio>
#include <vector>
#include <queue>
#include <stack>
#include <cerrno>
#include <android/log.h>

#define TAG "RawCamDemo"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,    TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,     TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,     TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,    TAG, __VA_ARGS__)


#define 		PRINT_HISTOGRAM
#define 		PRINT_ALPHABET
#define 		PRINT_DATA

jbyteArray make_java_buffer(JNIEnv *env, const byte *data, int byteSize)
{
	jbyteArray ret=env->NewByteArray(byteSize);
	env->SetByteArrayRegion(ret, 0, byteSize, (jbyte*)data);
	return ret;
}

static float			*acc=nullptr;
static int				bw=0, bh=0, acc_count=0;
static unsigned short	*temp=nullptr;
static unsigned char 	*acc_data=nullptr;
static unsigned long long acc_data_size=0, acc_data_cap=0;

static void 	extract_r10(const byte *src, unsigned short *dst, int imSize)
{
	for(int ks=0, kd=0;kd<imSize;ks+=5, kd+=4)
	{
		dst[kd  ]=src[ks  ]<<2|(src[ks+4]   &3);
		dst[kd+1]=src[ks+1]<<2|(src[ks+4]>>2&3);
		dst[kd+2]=src[ks+2]<<2|(src[ks+4]>>4&3);
		dst[kd+3]=src[ks+3]<<2|(src[ks+4]>>6&3);
	}
}
static void		extract_r12(const byte *src, unsigned short *dst, int imSize)
{
	for(int ks=0, kd=0;kd<imSize;ks+=3, kd+=2)
	{
		dst[kd  ]=src[ks  ]<<4|(src[ks+2]   &15);
		dst[kd+1]=src[ks+1]<<4|(src[ks+2]>>4&15);
	}
}

//depth	10/12
//bayer	0: gray, 1: gray denoised, ...: color
//version	0: uncompressed (turns to 10/12/14), 1: v1, 5: RVL (channels are separated in case of color)
extern "C" jbyteArray Java_com_example_rawcamdemo_CameraFragment_compressAPI2(JNIEnv *env, jobject thiz, jbyteArray data, jint width, jint height, jint depth, jint bayer, jint version)
{
	switch(version)
	{
	case 0://uncompressed
	case 1://Huffman
	case 5://RVL

	case 6://long exposure - shot
	case 7://long exposure - save
	case 8://long red exp - shot
	case 9://long red exp - save
		break;
	default:
		LOGE("Invalid HUFF version");
		return nullptr;
	}

	auto packedImage=env->GetByteArrayElements(data, nullptr);
	if(!packedImage)
		return nullptr;

	if(version==6||version==7||version==8||version==9)
	{
		if(width!=bw||height!=bh)
		{
			acc_count=0;
			bw=width, bh=height;
			int bufsize=bw*bh;
			acc=(float*)realloc(acc, bufsize*sizeof(float));
			memset(acc, 0, bufsize*sizeof(float));
			temp=(unsigned short*)realloc(temp, bufsize*sizeof(unsigned short));
		//	memset(temp, 0, bufsize*sizeof(float));
		}
		int imSize=width*height;
		if(depth==10)//raw10
			extract_r10((byte*)packedImage, temp, imSize);
		else if(depth==12)//raw12
			extract_r12((byte*)packedImage, temp, imSize);
		for(int k=0;k<imSize;++k)
			acc[k]+=(float)temp[k];
		++acc_count;
		if(version==7)
		{
			if(acc_data)
				free(acc_data);
			acc_data=nullptr, acc_data_size=0, acc_data_cap=0;
			huff::compress_v7(acc, width, height, bayer, depth, acc_count, acc_data, acc_data_size, acc_data_cap);
			return make_java_buffer(env, (byte*)acc_data, (int)acc_data_size);
		}
		return nullptr;
	}

	std::vector<int> output;
	if(!version)//uncompressed
	{
		if(bayer!=0&&bayer!=1)//r10/12
			huff::pack_raw((const byte*)packedImage, width, height, depth, bayer, output);
		else if(depth==10)
			huff::pack_r10_g12((const byte*)packedImage, width, height, bayer, output);
		else if(depth==12)
			huff::pack_r12_g14((const byte*)packedImage, width, height, bayer, output);
	}
	else//compressed
	{
		short *image=nullptr;
		if(depth==10)
			image=unpack_r10((byte*)packedImage, width, height);
		else if(depth==12)
			image=unpack_r12((byte*)packedImage, width, height);
		switch(version)
		{
		case 1://Huffman
			huff::compress(image, width, height, depth, bayer, output);
			break;
		case 5://RVL
			huff::compress_v5(image, width, height, depth, bayer, output);
			break;
		default:
			LOGE("Invalid HUFF version");
			return nullptr;
		}
		delete[] image;
	}
	return make_java_buffer(env, (byte*)output.data(), output.size()*sizeof(int));
}
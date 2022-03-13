//huff.h - declarations for .huff codec used by RawCam Android app
//Copyright (C) 2022  Ayman Wagih Mohsen
//
//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once
#ifndef HUFF_H
#define HUFF_H
#include		<vector>


	#define 	PROFILER


#ifdef PROFILER
#include		<time.h>
void 			print(const char *format, ...);
void 			print_flush();
#ifdef __ANDROID__
inline double	now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return 1000.*(ts.tv_sec+1e-9*ts.tv_nsec);
}
#define			console_start_good()
#else
#include		"generic.h"
#include		<Windows.h>
inline double	now_ms()
{
	_LARGE_INTEGER li, freq;
	QueryPerformanceCounter(&li);
	QueryPerformanceFrequency(&freq);
	return 1000.*(double)li.QuadPart/freq.QuadPart;
}
#endif
extern double	time_marker;
inline void		time_start()
{
	time_marker=now_ms();
}
inline void		time_mark(const char *msg)
{
	double t2=now_ms();
	console_start_good();
	print("%s: %lfms", msg, t2-time_marker), print_flush();
	time_marker=t2;
}
#else//PROFILER
#define			time_start()
#define			time_mark(...)
#endif//PROFILER
enum			RequestedFormat
{
	RF_I8_RGBA,
	RF_I16_BAYER,
	RF_F32_BAYER,
};

typedef unsigned char byte;
struct			HuffHeader//24 bytes
{
	char HUFF[4];//'H'|'U'<<8|'F'<<16|'F'<<24
	unsigned version;//1: huffman, 2: encoded with palette, 10: uncompressed raw10 packing, 12: uncompressed raw12 packing
	unsigned width, height;//uncompressed dimensions
	char bayerInfo[4];//'G'|'R'<<8|'B'<<16|'G'<<24 for Galaxy A70
	unsigned nLevels;//1<<bitDepth, also histogram size for version==1
	unsigned histogram[];//compressed data begins at histogram+nLevels
};
struct			HuffDataHeader//16 bytes
{
	char DATA[4];//'D'|'A'<<8|'T'<<16|'A'<<24
	unsigned uPxCount;//uncompressed pixel count
	unsigned long long cBitSize;//compressed data size in bits
	unsigned data[];
};

void 			checkSIMD();
short*			unpack_r10(const byte* src, int width, int height);
short*			unpack_r12(const byte* src, int width, int height);
namespace		huff
{
	int			compress	(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data);
	int			compress_v2	(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data);//unfinished
	int			compress_v5	(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data);
	int			pack_raw	(const byte *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data);//depth: 10 or 12
	int			pack_r10_g12(const byte *buffer, int bw, int bh, int denoise, std::vector<int> &data);
	int			pack_r12_g14(const byte *buffer, int bw, int bh, int denoise, std::vector<int> &data);
	int			compress_v7(const float *buffer, int bw, int bh, int bayer, int depth, int nFrames, unsigned char *&dst, unsigned long long &dst_size, unsigned long long &dst_cap);
	bool		decompress(const byte *data, int bytesize, RequestedFormat format, void **pbuffer, int &bw, int &bh, int &depth, char *bayer_sh);//realloc will be used on buffer
}
#endif//HUFF_H
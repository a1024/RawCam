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
struct			HuffHeader
{
	char HUFF[4];//'H'|'U'<<8|'F'<<16|'F'<<24
	unsigned version;//1: huffman, 2: encoded with palette, 10: uncompressed raw10 packing, 12: uncompressed raw12 packing
	unsigned width, height;//uncompressed dimensions
	char bayerInfo[4];//'G'|'R'<<8|'B'<<16|'G'<<24 for Galaxy A70
	unsigned nLevels;//1<<bitDepth, also histogram size
	unsigned histogram[];//compressed data begins at histogram+nLevels
};
struct			HuffDataHeader
{
	char DATA[4];//'D'|'A'<<8|'T'<<16|'A'<<24
	unsigned uPxCount;//uncompressed pixel count
	unsigned long long cBitSize;//compressed data size in bits
	unsigned data[];
};

void 			checkSIMD();
namespace		huff
{
	int			compress(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data);
	int			pack_raw(const byte *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data);//depth: 10 or 12
	int			pack_r10_g12(const byte *buffer, int bw, int bh, std::vector<int> &data);
	int			pack_r12_g14(const byte *buffer, int bw, int bh, std::vector<int> &data);
	bool		decompress(const byte *data, int bytesize, RequestedFormat format, void **pbuffer, int &bw, int &bh, int &depth, char *bayer_sh);//realloc will be used on buffer
}
#endif//HUFF_H
//huff.cpp - implementation of .huf codec used by RawCam Android app
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

#include		"huff.h"
#include		"vector_bool.h"
#ifdef			__ANDROID__
#include		<android/log.h>
#define TAG		"RawCamDemo"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define			LOG_ERROR		LOGE
#define			console_start_good()
#define			console_pause()
#define			console_end()

#include		<cpu-features.h>
#if defined __arm__ || defined __aarch64__
#include		<arm_neon.h>
#endif
#else
#include		"generic.h"
#include		<tmmintrin.h>
const char		file[]=__FILE__;
inline void		shift_left_vector_small(__m128i const &x, int n, __m128i &ret_lo, __m128i &ret_hi)//n [0~63], used range: [0~31]
{
	__m128i xsl=_mm_slli_epi64(x, n);
	__m128i xsr=_mm_srli_epi64(x, 64-n);
	ret_hi=_mm_srli_si128(xsr, 8);
	xsr=_mm_slli_si128(xsr, 8);
	ret_lo=_mm_or_si128(xsl, xsr);
}
#endif
#include		<vector>
#include		<queue>
#include		<stack>


//	#define		DEBUG_ANS
//	#define		ANS_PRINT_STATE2	kc==15
//	#define		ANS_PRINT_EMITS		kc==15
//	#define		ANS_PRINT_READS		kc==15

//	#define		PRINT_MINMAX
//	#define		DEBUG_VEC_BOOL
//	#define		PRINT_TREE
//	#define		PRINT_ALPHABET
//	#define		PRINT_DATA
//	#define		PRINT_V2
//	#define		PRINT_V2_DATA


enum 			SIMDSupport
{
	SIMD_NOT_SUPPORTED,
	SIMD_SUPPORTED,
	SIMD_SUPPORTED2,
	SIMD_UNKNOWN,
};
char 			supportsSIMD=SIMD_UNKNOWN;
#ifdef PROFILER
double			time_marker=0;
#endif
#ifdef __ANDROID__
const int 		g_buf_size=4096;
char 			g_buf[g_buf_size]={};
int 			g_idx=0;
void 			print(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	g_idx+=vsnprintf(g_buf+g_idx, g_buf_size-g_idx, format, args);
	va_end(args);
}
void 			print_flush()
{
	LOGE("%s", g_buf);
	g_idx=0;
}
int				floor_log2(unsigned long long n)
{
	int logn=0;
	int sh=(n>=1ULL<<32)<<5;logn+=sh, n>>=sh;
		sh=(n>=1<<16)<<4;	logn+=sh, n>>=sh;
		sh=(n>=1<< 8)<<3;	logn+=sh, n>>=sh;
		sh=(n>=1<< 4)<<2;	logn+=sh, n>>=sh;
		sh=(n>=1<< 2)<<1;	logn+=sh, n>>=sh;
		sh= n>=1<< 1;		logn+=sh;
	return logn;
}
#else
void 			print(const char *format, ...)
{
	vprintf(format, (char*)(&format+1));
}
void 			print_flush()
{
	printf("\n");
}
#endif
void 			checkSIMD()
{
	if(supportsSIMD==SIMD_UNKNOWN)
	{
		supportsSIMD=SIMD_NOT_SUPPORTED;
#ifdef __ANDROID__
		AndroidCpuFamily family=android_getCpuFamily();
		uint64_t features=android_getCpuFeatures();
#ifdef HAVE_NEON
		print("HAVE_NEON"), print_flush();
#endif
		print("features: %016llX", features), print_flush();
		switch(family)
		{
		case ANDROID_CPU_FAMILY_ARM:
			if(features&ANDROID_CPU_ARM_FEATURE_ARMv7&&features&ANDROID_CPU_ARM_FEATURE_NEON)
				supportsSIMD=SIMD_SUPPORTED;
			break;
		case ANDROID_CPU_FAMILY_ARM64:
			if(features&ANDROID_CPU_ARM64_FEATURE_ASIMD)
				supportsSIMD=SIMD_SUPPORTED2;
			break;
		case ANDROID_CPU_FAMILY_X86:
		case ANDROID_CPU_FAMILY_X86_64:
			print("android on x86?"), print_flush();
			break;
		case ANDROID_CPU_FAMILY_MIPS:
		case ANDROID_CPU_FAMILY_MIPS64:
			print("MIPS processor"), print_flush();
			break;
		case ANDROID_CPU_FAMILY_UNKNOWN:
			print("Unknown processor"), print_flush();
			break;
		}
#endif
	}
}

static void		print_bin(const byte *data, int bytesize)
{
	for(int kb=0;kb<(bytesize<<3);++kb)
	{
		print("%d", data[kb>>3]>>(kb&7)&1);
		if((kb&7)==7)
			print("-");
	}
}
struct			AlphabetComparator
{
	vector_bool const *alphabet;
	explicit AlphabetComparator(vector_bool const *alphabet):alphabet(alphabet){}
	bool operator()(int idx1, int idx2)const
	{
		auto &s1=alphabet[idx1], &s2=alphabet[idx2];
		for(int kb=0;kb<s1.bitSize&&kb<s2.bitSize;++kb)
		{
			int bit1=s1.get(kb), bit2=s2.get(kb);
			if(bit1!=bit2)
				return bit1<bit2;
		}
		return s1.bitSize<s2.bitSize;//shortest symbol first
		//return s1.bitSize>s2.bitSize;//longest symbol first
	}
};
static void		sort_alphabet(vector_bool const *alphabet, int nLevels, std::vector<int> &idx)
{
	idx.resize(nLevels);
	for(int k=0;k<nLevels;++k)
		idx[k]=k;
	std::sort(idx.begin(), idx.end(), AlphabetComparator(alphabet));
}
static void		print_alphabet(vector_bool const *alphabet, const int *histogram, int nlevels, int symbols_to_compress, const int *sort_idx)
{
	print("symbol");
	if(histogram)
		print(", freq, %%");
	print_flush();
	for(int k=0;k<nlevels;++k)//print alphabet
	{
		int symbol=sort_idx?sort_idx[k]:k;
		if(histogram&&!histogram[symbol])
			continue;
		print("%4d ", symbol);
		if(histogram)
		{
			print("%6d ", histogram[symbol]);
			print("%2d ", histogram[symbol]*100/symbols_to_compress);
		}
		auto &code=alphabet[symbol];
		for(int k2=0, k2End=code.bitSize;k2<k2End;++k2)
			print("%c", char('0'+code.get(k2)));
		print_flush();
	}
}
static void		print_histogram(int *histogram, int nlevels, int scanned_count, int *sort_idx)
{
	int histmax=0;
	for(int k=0;k<nlevels;++k)
		if(histmax<histogram[k])
			histmax=histogram[k];
	const int consolechars=79-15-5*(sort_idx!=0);
	if(!histmax)
		return;

	if(sort_idx)
		print("idx, ");
	print("symbol, freq, %%");
	print_flush();
	for(int k=0;k<nlevels;++k)//print histogram
	{
		int symbol=sort_idx?sort_idx[k]:k;
		if(!histogram[symbol])
			continue;
		if(sort_idx)
			print("%4d ", k);
		print("%4d %6d %2d ", symbol, histogram[symbol], histogram[symbol]*100/scanned_count);
		for(int kr=0, count=histogram[symbol]*consolechars/histmax;kr<count;++kr)
			print("*");
		print_flush();
	}
}


struct			Node
{
	int branch[2];
	unsigned short value;
	int freq;
};
static std::vector<Node> tree;//root is at the end of array
static int		nLevels;
static int		make_node(int symbol, int freq, int left, int right)//https://gist.github.com/pwxcoo/72d7d3c5c3698371c21e486722f9b34b
{
	int idx=(int)tree.size();
	tree.push_back(Node());
	auto &n=*tree.rbegin();
	n.value=symbol, n.freq=freq;
	n.branch[0]=left, n.branch[1]=right;
	return idx;
}
struct			compare_nodes
{
	bool operator()(int idx1, int idx2)
	{
		if(tree[idx1].freq==tree[idx2].freq)
			return idx1<idx2;
		return tree[idx1].freq>tree[idx2].freq;
	}
};

static void		print_tree()
{
	for(int k=(int)tree.size()-1;k>=0;--k)
	{
		auto &node=tree[k];
		if(!node.freq)
			continue;
		print("[%d] 0:%d,1:%d, freq=%d, val=%d", k, node.branch[0], node.branch[1], node.freq, node.value);
		print_flush();
	}
}
static void		build_tree(int *histogram, int nLevels)
{
	::nLevels=nLevels;
	tree.clear();
	tree.reserve(nLevels);
	std::priority_queue<int, std::vector<int>, compare_nodes> pq((compare_nodes()));
	for(int k=0;k<nLevels;++k)
		pq.push(make_node(k, histogram[k], -1, -1));
	while(pq.size()>1)//build Huffman tree
	{
		int left=pq.top();	pq.pop();
		int right=pq.top();	pq.pop();
		pq.push(make_node(0, tree[left].freq+tree[right].freq, left, right));
	}
}
static void		make_alphabet(std::vector<vector_bool> &alphabet)
{
	alphabet.resize(nLevels);
	typedef std::pair<int, vector_bool> TraverseInfo;
	std::stack<TraverseInfo> s;
	s.push(TraverseInfo((int)tree.size()-1, vector_bool()));
	vector_bool left, right;
	while(s.size())//depth-first
	{
		auto &info=s.top();
		int idx=info.first;
		if(idx==-1)
		{
			s.pop();
			continue;
		}
		auto &r2=tree[idx];
		if(r2.branch[0]==-1&&r2.branch[1]==-1)
		{
			alphabet[r2.value]=std::move(info.second);
			s.pop();
			continue;
		}
		left=std::move(info.second);
		right=left;
		s.pop();
		if(r2.branch[1]!=-1)
		{
			right.push_back(true);
			s.push(TraverseInfo(r2.branch[1], std::move(right)));
		}
		if(r2.branch[0]!=-1)
		{
			left.push_back(false);
			s.push(TraverseInfo(r2.branch[0], std::move(left)));
		}
	}
}
static void		calculate_histogram(const short *image, int size, int *histogram, int nLevels)
{
	memset(histogram, 0, nLevels*sizeof(int));
	for(int k=0;k<size;++k)
		++histogram[image[k]];
}

short*			unpack_r10(const byte *src, int width, int height)
{
	int imSize=width*height;
	auto dst=new short[imSize];
	for(int ks=0, kd=0;kd<imSize;ks+=5, kd+=4)
	{
		dst[kd  ]=src[ks  ]<<2|(src[ks+4]   &3);
		dst[kd+1]=src[ks+1]<<2|(src[ks+4]>>2&3);
		dst[kd+2]=src[ks+2]<<2|(src[ks+4]>>4&3);
		dst[kd+3]=src[ks+3]<<2|(src[ks+4]>>6&3);
		//if(dst[kd]>=1024||dst[kd+1]>=1024||dst[kd+2]>=1024||dst[kd+3]>=1024)
		//{
		//	LOGE("Image [%d] = %d", kd, dst[kd]);
		//	LOGE("Image [%d] = %d", kd+1, dst[kd+1]);
		//	LOGE("Image [%d] = %d", kd+2, dst[kd+2]);
		//	LOGE("Image [%d] = %d", kd+3, dst[kd+3]);
		//}
	}
	return dst;
}
#if 0
short*			unpack_r10_simd(const byte* src, int width, int height)
{
	int imSize=width*height;
	auto dst=new short[imSize];
#ifdef __ARM_NEON
	for(int ks=0, kd=0;kd<imSize;ks+=5, kd+=4)
	{
	}
#elif !defined __ANDROID__
#endif
	return dst;
}
#endif
short*			unpack_r12(const byte *src, int width, int height)
{
	int imSize=width*height;
	auto dst=new short[imSize];
	for(int ks=0, kd=0;kd<imSize;ks+=3, kd+=2)
	{
		dst[kd  ]=src[ks  ]<<4|(src[ks+2]   &15);
		dst[kd+1]=src[ks+1]<<4|(src[ks+2]>>4&15);
	}
	return dst;
}

short*			bayer2gray(const short *src, int width, int height)//pass new (half) dimensions, +2 depth
{
	int w0=width<<1;
	int imsize=width*height;
	auto dst=new short[imsize];
	for(int ky=0;ky<height;++ky)
	{
		int ky2=ky<<1;
		const short *row=src+w0*ky2, *row2=row+w0;
		for(int kx=0;kx<width;++kx)
		{
			int kx2=kx<<1;
			dst[width*ky+kx]=row[kx2]+row[kx2+1]+row2[kx2]+row2[kx2+1];
		}
	}
	return dst;
}

short*			separateBayer(const short *src, int width, int height)
{
	int w2=width>>1, h2=height>>1;
	int imsize=width*height;
	auto dst=new short[imsize];
	for(int ky=0;ky<height;++ky)
	{
		int iy2=ky>=h2;
		int ky0=ky-(h2&-iy2);
		ky0=ky0<<1|iy2;

		const short *srow=src+width*ky0;
		short *drow=dst+width*ky;
		for(int kx=0;kx<width;++kx)
		{
			int ix2=kx>=w2;
			int kx0=kx-(w2&-ix2);
			kx0=kx0<<1|ix2;
			drow[kx]=srow[kx0];
		}
	}
	return dst;
}

inline void 	denoise_laplace4(short *buffer, int bw, int bh, int w2, int x, int y, int threshold)
{
	int idx=bw*y+x;
	int sum=(buffer[idx-2]+buffer[idx+2]+buffer[idx-w2]+buffer[idx+w2])>>2;
	if(abs(buffer[idx]-sum)>threshold)
		buffer[idx]=(short)sum;
}
inline void 	denoise_laplace8(short *buffer, int bw, int bh, int w2, int x, int y, int threshold)
{
	int idx=bw*y+x;
	int sum=(
		buffer[idx-w2-2]+buffer[idx-w2]+buffer[idx-w2+2]+
		buffer[idx-2]+buffer[idx+2]+
		buffer[idx+w2-2]+buffer[idx+w2]+buffer[idx+w2+2])>>3;
	if(abs(buffer[idx]-sum)>threshold)
		buffer[idx]=(short)sum;
}
void 			denoise_bayer(short *buffer, int bw, int bh, int depth)
{
	int threshold=1<<(depth-2);//quarter amplitude
	int wm2=bw-4, hm2=bh-4, w2=bw<<1;
	for(int ky=2;ky<hm2;ky+=2)
	{
		//LOGE("DENOISE ky=%d", ky);
		for(int kx=2;kx<wm2;kx+=2)
		{
			//LOGE("DENOISE (%d, %d)", kx, ky);
			//if(depth!=10||wm2!=bw-4||hm2!=bh-4)
			//{
			//	LOGE("DENOISE: CORRUPT STATE");
			//	continue;
			//}
			//if(kx-2<0||kx+2>=bw||ky-2<0||ky+2>=bh)
			//{
			//	LOGE("DENOISE: OOB");
			//	continue;
			//}

			denoise_laplace4(buffer, bw, bh, w2, kx  , ky  , threshold);
			denoise_laplace4(buffer, bw, bh, w2, kx+1, ky  , threshold);
			denoise_laplace4(buffer, bw, bh, w2, kx  , ky+1, threshold);
			denoise_laplace4(buffer, bw, bh, w2, kx+1, ky+1, threshold);

			//denoise_laplace8(buffer, bw, bh, w2, kx  , ky  , threshold);
			//denoise_laplace8(buffer, bw, bh, w2, kx+1, ky  , threshold);
			//denoise_laplace8(buffer, bw, bh, w2, kx  , ky+1, threshold);
			//denoise_laplace8(buffer, bw, bh, w2, kx+1, ky+1, threshold);
		}
	}
	//LOGE("DENOISE: DONE");
}
void			denoise_bayer_simd(short *buffer, int bw, int bh, int depth)
{
#ifdef __ARM_NEON
	short threshold=1<<(depth-2);//quarter amplitude
	int w2=bw<<1;
	int16x8_t th={threshold, threshold, threshold, threshold, threshold, threshold, threshold, threshold};
	int16x8_t minusone={-1, -1, -1, -1, -1, -1, -1, -1};
/*	for(int ky=2, yend=bh-2;ky<yend;++ky)
	{
		for(int kx=2, xend=bw-70;kx<xend;kx+=64)//81.26ms
		{
			int idx=bw*ky+kx;
			int16x8_t top[]={vld1q_s16(buffer+idx-w2), vld1q_s16(buffer+idx-w2+8), vld1q_s16(buffer+idx-w2+16), vld1q_s16(buffer+idx-w2+24), vld1q_s16(buffer+idx-w2+32), vld1q_s16(buffer+idx-w2+40), vld1q_s16(buffer+idx-w2+48), vld1q_s16(buffer+idx-w2+56)};
			int16x8_t mid[]={vld1q_s16(buffer+idx-2), vld1q_s16(buffer+idx-2+8), vld1q_s16(buffer+idx-2+16), vld1q_s16(buffer+idx-2+24), vld1q_s16(buffer+idx-2+32), vld1q_s16(buffer+idx-2+40), vld1q_s16(buffer+idx-2+48), vld1q_s16(buffer+idx-2+56), vld1q_s16(buffer+idx-2+64)};
			int16x8_t bot[]={vld1q_s16(buffer+idx+w2), vld1q_s16(buffer+idx+w2+8), vld1q_s16(buffer+idx+w2+16), vld1q_s16(buffer+idx+w2+24), vld1q_s16(buffer+idx+w2+32), vld1q_s16(buffer+idx+w2+40), vld1q_s16(buffer+idx+w2+48), vld1q_s16(buffer+idx+w2+56)};

			top[0]=vaddq_s16(top[0], bot[0]);//add top + bottom		//bot registers are not needed
			top[1]=vaddq_s16(top[1], bot[1]);
			top[2]=vaddq_s16(top[2], bot[2]);
			top[3]=vaddq_s16(top[3], bot[3]);
			top[4]=vaddq_s16(top[4], bot[4]);
			top[5]=vaddq_s16(top[5], bot[5]);
			top[6]=vaddq_s16(top[6], bot[6]);
			top[7]=vaddq_s16(top[7], bot[7]);

			top[0]=vaddq_s16(top[0], mid[0]);//add left
			top[1]=vaddq_s16(top[1], mid[1]);
			top[2]=vaddq_s16(top[2], mid[2]);
			top[3]=vaddq_s16(top[3], mid[3]);
			top[4]=vaddq_s16(top[4], mid[4]);
			top[5]=vaddq_s16(top[5], mid[5]);
			top[6]=vaddq_s16(top[6], mid[6]);
			top[7]=vaddq_s16(top[7], mid[7]);

			bot[0]=vextq_s16(mid[0], mid[1], 4);//extract right
			bot[1]=vextq_s16(mid[1], mid[2], 4);
			bot[2]=vextq_s16(mid[2], mid[3], 4);
			bot[3]=vextq_s16(mid[3], mid[4], 4);
			bot[4]=vextq_s16(mid[4], mid[5], 4);
			bot[5]=vextq_s16(mid[5], mid[6], 4);
			bot[6]=vextq_s16(mid[6], mid[7], 4);
			bot[7]=vextq_s16(mid[7], mid[8], 4);

			top[0]=vaddq_s16(top[0], bot[0]);//add right
			top[1]=vaddq_s16(top[1], bot[1]);
			top[2]=vaddq_s16(top[2], bot[2]);
			top[3]=vaddq_s16(top[3], bot[3]);
			top[4]=vaddq_s16(top[4], bot[4]);
			top[5]=vaddq_s16(top[5], bot[5]);
			top[6]=vaddq_s16(top[6], bot[6]);
			top[7]=vaddq_s16(top[7], bot[7]);

			top[0]=vshrq_n_s16(top[0], 2);//divide sum by 4
			top[1]=vshrq_n_s16(top[1], 2);
			top[2]=vshrq_n_s16(top[2], 2);
			top[3]=vshrq_n_s16(top[3], 2);
			top[4]=vshrq_n_s16(top[4], 2);//divide sum by 4
			top[5]=vshrq_n_s16(top[5], 2);
			top[6]=vshrq_n_s16(top[6], 2);
			top[7]=vshrq_n_s16(top[7], 2);

			bot[0]=vextq_s16(mid[0], mid[1], 2);//extract center	//mid registers are not needed
			bot[1]=vextq_s16(mid[1], mid[2], 2);
			bot[2]=vextq_s16(mid[2], mid[3], 2);
			bot[3]=vextq_s16(mid[3], mid[4], 2);
			bot[4]=vextq_s16(mid[4], mid[5], 2);
			bot[5]=vextq_s16(mid[5], mid[6], 2);
			bot[6]=vextq_s16(mid[6], mid[7], 2);
			bot[7]=vextq_s16(mid[7], mid[8], 2);

			//average: top[], center: bot[], free: mid[]
			mid[0]=vabdq_s16(bot[0], top[0]);//take abs difference
			mid[1]=vabdq_s16(bot[1], top[1]);
			mid[2]=vabdq_s16(bot[2], top[2]);
			mid[3]=vabdq_s16(bot[3], top[3]);
			mid[4]=vabdq_s16(bot[4], top[4]);//take abs difference
			mid[5]=vabdq_s16(bot[5], top[5]);
			mid[6]=vabdq_s16(bot[6], top[6]);
			mid[7]=vabdq_s16(bot[7], top[7]);

			mid[0]=vcgtq_s16(mid[0], th);//compare with threshold
			mid[1]=vcgtq_s16(mid[1], th);
			mid[2]=vcgtq_s16(mid[2], th);
			mid[3]=vcgtq_s16(mid[3], th);
			mid[4]=vcgtq_s16(mid[4], th);//compare with threshold
			mid[5]=vcgtq_s16(mid[5], th);
			mid[6]=vcgtq_s16(mid[6], th);
			mid[7]=vcgtq_s16(mid[7], th);

			top[0]=vandq_s16(top[0], mid[0]);
			top[1]=vandq_s16(top[1], mid[1]);
			top[2]=vandq_s16(top[2], mid[2]);
			top[3]=vandq_s16(top[3], mid[3]);
			top[4]=vandq_s16(top[4], mid[4]);
			top[5]=vandq_s16(top[5], mid[5]);
			top[6]=vandq_s16(top[6], mid[6]);
			top[7]=vandq_s16(top[7], mid[7]);

			mid[0]=veorq_s16(mid[0], minusone);
			mid[1]=veorq_s16(mid[1], minusone);
			mid[2]=veorq_s16(mid[2], minusone);
			mid[3]=veorq_s16(mid[3], minusone);
			mid[4]=veorq_s16(mid[4], minusone);
			mid[5]=veorq_s16(mid[5], minusone);
			mid[6]=veorq_s16(mid[6], minusone);
			mid[7]=veorq_s16(mid[7], minusone);

			bot[0]=vandq_s16(bot[0], mid[0]);
			bot[1]=vandq_s16(bot[1], mid[1]);
			bot[2]=vandq_s16(bot[2], mid[2]);
			bot[3]=vandq_s16(bot[3], mid[3]);
			bot[4]=vandq_s16(bot[4], mid[4]);
			bot[5]=vandq_s16(bot[5], mid[5]);
			bot[6]=vandq_s16(bot[6], mid[6]);
			bot[7]=vandq_s16(bot[7], mid[7]);

			top[0]=vorrq_s16(top[0], bot[0]);
			top[1]=vorrq_s16(top[1], bot[1]);
			top[2]=vorrq_s16(top[2], bot[2]);
			top[3]=vorrq_s16(top[3], bot[3]);
			top[4]=vorrq_s16(top[4], bot[4]);
			top[5]=vorrq_s16(top[5], bot[5]);
			top[6]=vorrq_s16(top[6], bot[6]);
			top[7]=vorrq_s16(top[7], bot[7]);

			vst1q_s16(buffer+idx   , top[0]);
			vst1q_s16(buffer+idx+ 8, top[1]);
			vst1q_s16(buffer+idx+16, top[2]);
			vst1q_s16(buffer+idx+24, top[3]);
			vst1q_s16(buffer+idx+32, top[4]);
			vst1q_s16(buffer+idx+40, top[5]);
			vst1q_s16(buffer+idx+48, top[6]);
			vst1q_s16(buffer+idx+56, top[7]);
		}
	}//*/
	for(int ky=2, yend=bh-2;ky<yend;++ky)
	{
		for(int kx=2, xend=bw-38;kx<xend;kx+=32)//81.23ms
		{
			int idx=bw*ky+kx;
			int16x8_t top[]={vld1q_s16(buffer+idx-w2), vld1q_s16(buffer+idx-w2+8), vld1q_s16(buffer+idx-w2+16), vld1q_s16(buffer+idx-w2+24)};
			int16x8_t mid[]={vld1q_s16(buffer+idx-2), vld1q_s16(buffer+idx-2+8), vld1q_s16(buffer+idx-2+16), vld1q_s16(buffer+idx-2+24), vld1q_s16(buffer+idx-2+32)};
			int16x8_t bot[]={vld1q_s16(buffer+idx+w2), vld1q_s16(buffer+idx+w2+8), vld1q_s16(buffer+idx+w2+16), vld1q_s16(buffer+idx+w2+24)};

			top[0]=vaddq_s16(top[0], bot[0]);//add top + bottom		//bot registers are not needed
			top[1]=vaddq_s16(top[1], bot[1]);
			top[2]=vaddq_s16(top[2], bot[2]);
			top[3]=vaddq_s16(top[3], bot[3]);

			top[0]=vaddq_s16(top[0], mid[0]);//add left
			top[1]=vaddq_s16(top[1], mid[1]);
			top[2]=vaddq_s16(top[2], mid[2]);
			top[3]=vaddq_s16(top[3], mid[3]);

			bot[0]=vextq_s16(mid[0], mid[1], 4);//extract right
			bot[1]=vextq_s16(mid[1], mid[2], 4);
			bot[2]=vextq_s16(mid[2], mid[3], 4);
			bot[3]=vextq_s16(mid[3], mid[4], 4);

			top[0]=vaddq_s16(top[0], bot[0]);//add right
			top[1]=vaddq_s16(top[1], bot[1]);
			top[2]=vaddq_s16(top[2], bot[2]);
			top[3]=vaddq_s16(top[3], bot[3]);

			top[0]=vshrq_n_s16(top[0], 2);//divide sum by 4
			top[1]=vshrq_n_s16(top[1], 2);
			top[2]=vshrq_n_s16(top[2], 2);
			top[3]=vshrq_n_s16(top[3], 2);

			bot[0]=vextq_s16(mid[0], mid[1], 2);//extract center	//mid registers are not needed
			bot[1]=vextq_s16(mid[1], mid[2], 2);
			bot[2]=vextq_s16(mid[2], mid[3], 2);
			bot[3]=vextq_s16(mid[3], mid[4], 2);

			//average: top[], center: bot[], free: mid[]
			mid[0]=vabdq_s16(bot[0], top[0]);//take abs difference
			mid[1]=vabdq_s16(bot[1], top[1]);
			mid[2]=vabdq_s16(bot[2], top[2]);
			mid[3]=vabdq_s16(bot[3], top[3]);

			mid[0]=vcgtq_s16(mid[0], th);//compare with threshold
			mid[1]=vcgtq_s16(mid[1], th);
			mid[2]=vcgtq_s16(mid[2], th);
			mid[3]=vcgtq_s16(mid[3], th);

			top[0]=vandq_s16(top[0], mid[0]);
			top[1]=vandq_s16(top[1], mid[1]);
			top[2]=vandq_s16(top[2], mid[2]);
			top[3]=vandq_s16(top[3], mid[3]);

			mid[0]=veorq_s16(mid[0], minusone);
			mid[1]=veorq_s16(mid[1], minusone);
			mid[2]=veorq_s16(mid[2], minusone);
			mid[3]=veorq_s16(mid[3], minusone);

			bot[0]=vandq_s16(bot[0], mid[0]);
			bot[1]=vandq_s16(bot[1], mid[1]);
			bot[2]=vandq_s16(bot[2], mid[2]);
			bot[3]=vandq_s16(bot[3], mid[3]);

			top[0]=vorrq_s16(top[0], bot[0]);
			top[1]=vorrq_s16(top[1], bot[1]);
			top[2]=vorrq_s16(top[2], bot[2]);
			top[3]=vorrq_s16(top[3], bot[3]);

			vst1q_s16(buffer+idx   , top[0]);
			vst1q_s16(buffer+idx+ 8, top[1]);
			vst1q_s16(buffer+idx+16, top[2]);
			vst1q_s16(buffer+idx+24, top[3]);
		}
	}//*/
/*	for(int ky=2;ky<bh-2;++ky)
	{
		for(int kx=2;kx<bw-10;kx+=8)//131.53ms
		{
			int idx=bw*ky+kx;
			auto vL=vld1q_s16(buffer+idx-2);
			auto vR=vld1q_s16(buffer+idx+2);
			auto vT=vld1q_s16(buffer+idx-w2);
			auto vB=vld1q_s16(buffer+idx+w2);
			auto vC=vld1q_s16(buffer+idx);

			auto av=vaddq_s16(vL, vR);
			av=vaddq_s16(av, vT);
			av=vaddq_s16(av, vB);
			av=vshrq_n_s16(av, 2);

			auto cross=vabdq_s16(av, vC);//abs difference?
			//auto cross=vsubq_s16(av, vC);
			cross=vcgtq_s16(cross, th);
			av=vandq_s16(av, cross);
			cross=veorq_s16(cross, minusone);
			vC=vandq_s16(vC, cross);
			vC=vorrq_s16(vC, av);
			vst1q_s16(buffer+idx, vC);
		}
	}//*/
#elif !defined __ANDROID__
	short threshold=1<<(depth-2);//quarter amplitude
	int w2=bw<<1;
	__m128i th=_mm_set1_epi16(threshold);
	__m128i minusone=_mm_set1_epi32(-1);
	for(int ky=2;ky<bh-2;++ky)
	{
		for(int kx=2;kx<bw-10;kx+=8)
		{
			int idx=bw*ky+kx;
			__m128i vL=_mm_loadu_si128((__m128i*)(buffer+idx-2));
			__m128i vR=_mm_loadu_si128((__m128i*)(buffer+idx+2));
			__m128i vT=_mm_loadu_si128((__m128i*)(buffer+idx-w2));
			__m128i vB=_mm_loadu_si128((__m128i*)(buffer+idx+w2));
			__m128i vC=_mm_loadu_si128((__m128i*)(buffer+idx));

			__m128i av=_mm_add_epi16(vL, vR);
			av=_mm_add_epi16(av, vT);
			av=_mm_add_epi16(av, vB);
			av=_mm_srli_epi16(av, 2);

			__m128i cross=_mm_sub_epi16(av, vC);
			cross=_mm_abs_epi16(cross);
			cross=_mm_cmpgt_epi16(cross, th);
			av=_mm_and_si128(av, cross);
			cross=_mm_xor_si128(cross, minusone);
			vC=_mm_and_si128(vC, cross);
			vC=_mm_or_si128(vC, av);
			_mm_storeu_si128((__m128i*)(buffer+idx), vC);
		}
	}
#endif
}


//ANS
#ifdef			DEBUG_ANS
const int iw=8, ih=8, imsize=iw*ih;
float			guide[imsize]={};
float			buffer[imsize]=
{
	0, 0, 1, 1, 2, 2, 3, 3,
	0, 0, 1, 1, 2, 2, 3, 3,
	4, 4, 5, 5, 6, 6, 7, 7,
	4, 4, 5, 5, 6, 6, 7, 7,
	0, 0, 1, 1, 2, 2, 3, 3,
	0, 0, 1, 1, 2, 2, 3, 3,
	4, 4, 5, 5, 6, 6, 7, 7,
	4, 4, 5, 5, 6, 6, 7, 7,

	//0, 1, 2, 3, 4, 5, 6, 7,
	//0, 1, 2, 3, 4, 5, 6, 7,
	//0, 1, 2, 3, 4, 5, 6, 7,
	//0, 1, 2, 3, 4, 5, 6, 7,
	//0, 1, 2, 3, 4, 5, 6, 7,
	//0, 1, 2, 3, 4, 5, 6, 7,
	//0, 1, 2, 3, 4, 5, 6, 7,
	//0, 1, 2, 3, 4, 5, 6, 7,
};
#endif
#define 		PROF(...)
#ifdef __ANDROID__
#define 		sprintf_s	snprintf
#define 		vsprintf_s	vsnprintf
int				error_count=0;
int				ceil_log2(unsigned long long n)
{
	int l2=floor_log2(n);
	l2+=(1ULL<<l2)<n;
	return l2;
}
static bool		set_error(const char *file, int line, const char *format, ...)
{
	int printed=0;
	printed+=sprintf_s(g_buf+printed, g_buf_size-printed, "[%d] %s(%d) ", error_count, file, line);
	if(format)
	{
		va_list args;
		va_start(args, format);
		printed+=vsprintf_s(g_buf+printed, g_buf_size-printed, format, args);
		va_end(args);
	}
	else
		sprintf_s(g_buf+printed, g_buf_size-printed, "Unknown error");

	LOGE("%s", g_buf);
	++error_count;
	return false;
}
#define			FAIL(REASON, ...)	return set_error(__FILE__, __LINE__, REASON, ##__VA_ARGS__)
#else
#define			FAIL(REASON, ...)	return (log_error(file, __LINE__, REASON, ##__VA_ARGS__), false)
#endif
const int		magic_an04='A'|'N'<<8|'0'<<16|'4'<<24;
const int
	ANS_PROB_BITS=16,//CHANNEL DEPTH <= 15
	ANS_L=1<<16,
	ANS_DEPTH=8,
	ANS_NLEVELS=1<<ANS_DEPTH;
struct			SortedHistInfo
{
	int idx,//symbol
		freq,//original freq
		qfreq;//quantized freq
	SortedHistInfo():idx(0), freq(0), qfreq(0){}
};
struct			SymbolInfo
{
	unsigned short
		freq,//quantized
		cmpl_freq,
		shift,
		reserved0;
	unsigned
		CDF,
		inv_freq,
		bias,
		renorm_limit;
};
inline bool		emit_pad(unsigned char *&out_data, const unsigned long long &out_size, unsigned long long &out_cap, int size)
{
	while(out_size+size>=out_cap)
	{
		auto newcap=out_cap?out_cap<<1:1;
		auto ptr=(unsigned char*)realloc(out_data, newcap);
		if(!ptr)
			FAIL("realloc null emit_pad %lld %lld", out_cap, newcap);
		out_data=ptr, out_cap=newcap;
	}
	memset(out_data+out_size, 0, size);
	return true;
}
inline void		store_int_le(unsigned char *base, unsigned long long &offset, int i)
{
	auto p=(unsigned char*)&i;
	base[offset  ]=p[0];
	base[offset+1]=p[1];
	base[offset+2]=p[2];
	base[offset+3]=p[3];
	offset+=4;
}
inline bool		emit_short_le(unsigned char *&dst, unsigned long long &dst_size, unsigned long long &dst_cap, unsigned short i)
{
	if(dst_size>=dst_cap)
	{
		auto newcap=dst_cap<<1;
		newcap+=!newcap<<1;
		auto ptr=(unsigned char*)realloc(dst, newcap);
		if(!ptr)
			FAIL("realloc null emit_short_le %lld %lld", dst_cap, newcap);
		dst=ptr, dst_cap=newcap;
	}
	*(unsigned short*)(dst+dst_size)=i;//assume all encoded data is 2 byte-aligned
	dst_size+=2;
	return true;
}
inline int		load_int_le(const unsigned char *buffer)
{
	int i=0;
	auto p=(unsigned char*)&i;
	p[0]=buffer[0];
	p[1]=buffer[1];
	p[2]=buffer[2];
	p[3]=buffer[3];
	return i;
}
static SortedHistInfo h[ANS_NLEVELS];
int				ans_calc_histogram(const unsigned char *buffer, int nsymbols, int bytestride, unsigned short *histogram, int prob_bits, int integrate)
{
	int prob_sum=1<<prob_bits;
	if(!nsymbols)
	{
		memset(histogram, 0, ANS_NLEVELS*sizeof(*histogram));
		FAIL("Symbol count is zero");
	}
	for(int k=0;k<ANS_NLEVELS;++k)
	{
		h[k].idx=k;
		h[k].freq=0;
	}
	int bytesize=nsymbols*bytestride;
	PROF(HISTOGRAM_INIT);
	for(int k=0;k<bytesize;k+=bytestride)//this loop takes 73% of encode time
		++h[buffer[k]].freq;
	PROF(HISTOGRAM_LOOKUP);
	for(int k=0;k<ANS_NLEVELS;++k)
		h[k].qfreq=(((long long)h[k].freq<<ANS_PROB_BITS)/nsymbols);

	if(nsymbols!=prob_sum)
	{
		const int prob_max=prob_sum-1;

		std::sort(h, h+ANS_NLEVELS, [](SortedHistInfo const &a, SortedHistInfo const &b)
		{
			return a.freq<b.freq;
		});
		int idx=0;
		for(;idx<ANS_NLEVELS&&!h[idx].freq;++idx);
		for(;idx<ANS_NLEVELS&&!h[idx].qfreq;++idx)
			++h[idx].qfreq;
		for(idx=ANS_NLEVELS-1;idx>=0&&h[idx].qfreq>=prob_max;--idx);
		for(++idx;idx<ANS_NLEVELS;++idx)
			h[idx].qfreq=prob_max;

		int error=-prob_sum;//too much -> +ve error & vice versa
		for(int k=0;k<ANS_NLEVELS;++k)
			error+=h[k].qfreq;
		if(error>0)
		{
			while(error)
			{
				for(int k=0;k<ANS_NLEVELS&&error;++k)
				{
					int dec=h[k].qfreq>1;
					h[k].qfreq-=dec, error-=dec;
				}
			}
		}
		else
		{
			while(error)
			{
				for(int k=ANS_NLEVELS-1;k>=0&&error;--k)
				{
					int inc=h[k].qfreq<prob_max;
					h[k].qfreq+=inc, error+=inc;
				}
			}
		}
		if(error)
			FAIL("Internal error: histogram adds up to %d != %d", prob_sum+error, prob_sum);
		std::sort(h, h+ANS_NLEVELS, [](SortedHistInfo const &a, SortedHistInfo const &b)
		{
			return a.idx<b.idx;
		});
	}
	int sum=0;
	for(int k=0;k<ANS_NLEVELS;++k)
	{
		if(h[k].qfreq>0xFFFF)
			FAIL("Internal error: symbol %d has probability %d", k, h[k].qfreq);
		histogram[k]=integrate?sum:h[k].qfreq;
		sum+=h[k].qfreq;
	}
	if(sum!=ANS_L)
		FAIL("Internal error: CDF ends with 0x%08X, should end with 0x%08X", sum, ANS_L);
	return true;
}
bool			rans4_prep(const void *hist_ptr, int bytespersymbol, SymbolInfo *&info, unsigned char *&CDF2sym, int loud)
{
	int tempsize=bytespersymbol*(ANS_NLEVELS*sizeof(SymbolInfo)+ANS_L);
	info=(SymbolInfo*)malloc(tempsize);
	if(!info)
		FAIL("Failed to allocate temp buffer");
	CDF2sym=(unsigned char*)info+bytespersymbol*ANS_NLEVELS*sizeof(SymbolInfo);
	for(int kc=0;kc<bytespersymbol;++kc)
	{
		auto c_histogram=(const unsigned short*)hist_ptr+(kc<<ANS_DEPTH);
		auto c_info=info+(kc<<ANS_DEPTH);
		auto c_CDF2sym=CDF2sym+(kc<<ANS_PROB_BITS);
		int sum=0;
		for(int k=0;k<ANS_NLEVELS;++k)
		{
			auto &si=c_info[k];
			si.freq=c_histogram[k];
			si.cmpl_freq=~si.freq;
			si.CDF=sum;
			si.reserved0=0;

			if(si.freq<2)//0 freq: don't care, 1 freq:		//Ryg's fast rANS encoder
			{
				si.shift=0;
				si.inv_freq=0xFFFFFFFF;
				si.bias=si.CDF+ANS_L-1;
			}
			else
			{
				si.shift=ceil_log2(c_histogram[k])-1;
				si.inv_freq=(unsigned)(((0x100000000ULL<<si.shift)+c_histogram[k]-1)/c_histogram[k]);
				si.bias=si.CDF;
			}

			si.renorm_limit=si.freq<<(32-ANS_PROB_BITS);

			if(CDF2sym&&k)
			{
				for(int k2=c_info[k-1].CDF;k2<(int)si.CDF;++k2)
					c_CDF2sym[k2]=k-1;
			}
			sum+=si.freq;
		}
		if(CDF2sym)
		{
			for(int k2=c_info[ANS_NLEVELS-1].CDF;k2<ANS_L;++k2)
				c_CDF2sym[k2]=ANS_NLEVELS-1;
		}
		if(sum!=ANS_L)
			FAIL("histogram sum = %d != %d", sum, ANS_L);
		if(loud)
		{
#ifdef ANS_PRINT_HISTOGRAM
			static int printed=0;
			if(printed<1)
			{
				printf("s\tf\tCDF\n");
				for(int k=0;k<ANS_NLEVELS;++k)
				{
					auto &si=c_info[k];
					if(c_histogram[k])
						printf("%3d\t%5d = %04X\t%04X\n", k, c_histogram[k], c_histogram[k], si.CDF);
				}
				++printed;
			}
#endif
		}
	}
	//	if(!calc_hist_derivaties((const unsigned short*)hist_ptr+kc*ANS_NLEVELS, info+(kc<<ANS_DEPTH), CDF2sym+ANS_L*kc, loud))
	//		return false;
	return true;
}
int				rans4_encode(void *src, int nsymbols, int bytespersymbol, unsigned char *&dst, unsigned long long &dst_size, unsigned long long &dst_cap, int loud)//bytespersymbol: up to 16
{
	PROF(WASTE);
	auto buffer=(const unsigned char*)src;
	auto dst_start=dst_size;
	int headersize=8+bytespersymbol*ANS_NLEVELS*sizeof(short);
	emit_pad(dst, dst_size, dst_cap, headersize);
	dst_size+=headersize;
	auto temp=dst_start;
	store_int_le(dst, temp, magic_an04);
	for(int kc=0;kc<bytespersymbol;++kc)
		if(!ans_calc_histogram(buffer+kc, nsymbols, bytespersymbol, (unsigned short*)(dst+dst_start+8+kc*(ANS_NLEVELS*sizeof(short))), 16, false))
			return false;

	//printf("idx = %lld\n", dst_start+8);//
	SymbolInfo *info=nullptr;
	unsigned char *CDF2sym=nullptr;
	if(!rans4_prep(dst+dst_start+8, bytespersymbol, info, CDF2sym, loud))
		return false;

	unsigned state[16]={};
	for(int kc=0;kc<bytespersymbol;++kc)
		state[kc]=0x00010000;
	int framebytes=nsymbols*bytespersymbol;
	auto srcptr=buffer;
	PROF(PREP);
	for(int ks=0;ks<framebytes;++ks)
	{
		//if(ks>=(framebytes>>1))//
		//	break;//
		if(srcptr>=(const unsigned char*)src+framebytes)
			FAIL("ks %d srcptr is OOB", ks);
		int kc=ks%bytespersymbol;
#ifdef ANS_PRINT_STATE
		unsigned s0=state[kc];//
#endif
		auto s=*srcptr;
		++srcptr;
		auto &si=info[kc<<ANS_DEPTH|s];
		PROF(FETCH);

		if(!si.freq)
			FAIL("k %d s 0x%02X has zero freq", ks, s);

		auto &x=state[kc];
		if(x>=si.renorm_limit)//renormalize
	//	if(state>=(unsigned)(si.freq<<(32-ANS_PROB_BITS)))
		{
			if(!emit_short_le(dst, dst_size, dst_cap, (unsigned short)x))
				return false;
#ifdef ANS_PRINT_EMITS
			if(ANS_PRINT_EMITS)
				printf("kc %d k %d emit %04X[%04X] size %lld\n", kc, ks/bytespersymbol, x>>16, (int)(unsigned short)x, dst_size);
#endif
			x>>=16;
		}
		PROF(RENORM);
#ifdef ANS_PRINT_STATE2
		if(ANS_PRINT_STATE2)
			printf("kc %d k %d x %08X->%08X freq %04X CDF %04X s %02X\n", kc, ks/bytespersymbol, x, (x/si.freq<<ANS_PROB_BITS)+x%si.freq+si.CDF, si.freq, si.CDF, s);
			//printf("enc: 0x%08X = 0x%08X+(0x%08X*0x%08X>>(32+%d))*0x%04X+0x%08X\n", x+((unsigned)((long long)x*si.inv_freq>>32)>>si.shift)*si.cmpl_freq+si.bias, x, x, si.inv_freq, si.shift, si.cmpl_freq, si.bias);
#endif
#ifdef ANS_ENC_DIV_FREE
		x+=(((long long)x*si.inv_freq>>32)>>si.shift)*si.cmpl_freq+si.bias;//Ryg's division-free rANS encoder	https://github.com/rygorous/ryg_rans/blob/master/rans_byte.h
#else
		x=(x/si.freq<<ANS_PROB_BITS)+x%si.freq+si.CDF;

	//	lldiv_t result=lldiv(x, si.freq);//because unsigned
	//	x=((result.quot<<ANS_PROB_BITS)|result.rem)+si.CDF;
#endif
		PROF(UPDATE);
		//if(!rans_encode(srcptr, dst, dst_size, dst_cap, x[kc], info+(kc<<ANS_DEPTH)))
		//{
		//	free(info);
		//	return false;
		//}
#ifdef ANS_PRINT_STATE
		printf("kc %d s=%02X x=%08X->%08X\n", kc, srcptr[-1]&0xFF, s0, x[kc]);//
#endif
	}
	for(int kc=0;kc<bytespersymbol;++kc)
	{
		if(!emit_short_le(dst, dst_size, dst_cap, (unsigned short)state[kc]))
			return false;
		if(!emit_short_le(dst, dst_size, dst_cap, (unsigned short)(state[kc]>>16)))
			return false;
#ifdef ANS_PRINT_EMITS
		if(ANS_PRINT_EMITS)
			printf("kc %d emit [%08X] size %lld\n", kc, state[kc], dst_size);
#endif
		//if(!rans_encode_finish(dst, dst_size, dst_cap, state[kc]))
		//{
		//	free(info);
		//	return false;
		//}
	}
	int csize=(int)(dst_size-dst_start);
	dst_start+=4;
	store_int_le(dst, dst_start, csize);
	//printf("\nenc csize=%d\n", csize);//
	free(info);
#ifdef __ANDROID__
	free(src);//CRASHES when freeing temp where it was allocated
#endif
	return true;
}
int				rans4_decode(const unsigned char *src, unsigned long long &src_idx, unsigned long long src_size, void *dst, int nsymbols, int bytespersymbol, int loud)
{
	PROF(WASTE);
	int tag=load_int_le(src+src_idx);
	if(tag!=magic_an04)
		FAIL("Lost at %lld: found 0x%08X, magic = 0x%08X", src_idx, tag, magic_an04);
	auto hist=(unsigned short*)(src+src_idx+8);

	//printf("idx = %lld\n", src_idx+8);//
	SymbolInfo *info=nullptr;
	unsigned char *CDF2sym=nullptr;
	if(!rans4_prep(hist, bytespersymbol, info, CDF2sym, loud))
		return false;

	unsigned state[16]={};
	const unsigned char *srcptr=nullptr;
	unsigned char *dstptr=nullptr;
	int headersize=8+bytespersymbol*ANS_NLEVELS*sizeof(short);
	auto src_start=src+src_idx+headersize;
	int csize=load_int_le(src+src_idx+4);
	//printf("\ndec csize=%d\n", csize);//
	int framebytes=nsymbols*bytespersymbol;
	if(src_start)
		srcptr=src+src_idx+csize;
	if((unsigned char*)dst)
		dstptr=(unsigned char*)dst+framebytes;
	for(int kc=bytespersymbol-1;kc>=0;--kc)
	{
		srcptr-=2, state[kc]=*(const unsigned short*)srcptr;
		srcptr-=2, state[kc]=state[kc]<<16|*(const unsigned short*)srcptr;
#ifdef ANS_PRINT_READS
		if(ANS_PRINT_READS)
			printf("kc %d read [%08X]\n", kc, state[kc]);
#endif
	}
	PROF(PREP);
	for(int ks=framebytes-1;ks>=0;--ks)
	{
		if(dstptr<dst)
			FAIL("dstptr is out of bounds: ks=%d, frame = %d bytes,\ndstptr=%p, start=%p", ks, framebytes, dstptr, dst);
		int kc=ks%bytespersymbol;
#ifdef ANS_PRINT_STATE
		unsigned s0=state[kc];//
#endif
		auto &x=state[kc];
		auto c=(unsigned short)x;
	//	int c=x&(ANS_L-1);
		auto s=CDF2sym[kc<<ANS_PROB_BITS|c];
		auto &si=info[kc<<ANS_DEPTH|s];
		if(!si.freq)
			FAIL("Symbol 0x%02X has zero frequency", s);

		--dstptr;
		*dstptr=s;
		PROF(FETCH);
#ifdef ANS_PRINT_STATE2
		if(ANS_PRINT_STATE2)
			printf("kc %d k %d x %08X->%08X freq %04X CDF %04X s %02X\n", kc, ks/bytespersymbol, x, si.freq*(x>>ANS_PROB_BITS)+c-si.CDF, si.freq, si.CDF, s);
			//printf("kc %d k %d x %08X->%08X freq %04X CDF %04X s %02X s0 %02X\n", kc, ks/bytespersymbol, x, si.freq*(x>>ANS_PROB_BITS)+c-si.CDF, si.freq, si.CDF, s, ((unsigned char*)guide)[ks]&0xFF);
			//printf("dec: 0x%08X = 0x%04X*(0x%08X>>%d)+0x%04X-0x%08X\n", si.freq*(x>>ANS_PROB_BITS)+c-si.CDF, (int)si.freq, x, ANS_PROB_BITS, c, si.CDF);
#endif
		x=si.freq*(x>>ANS_PROB_BITS)+c-si.CDF;
		PROF(UPDATE);

		if(x<ANS_L)
		{
#ifdef ANS_PRINT_READS
			if(ANS_PRINT_READS)
				printf("kc %d read %08X[%04X]\n", kc, x, *(const unsigned short*)srcptr);
#endif
			srcptr-=2, x=x<<16|*(const unsigned short*)srcptr;
		}
		PROF(RENORM);
		//if(!rans_decode(srcptr, dstptr, x[kc], info+(kc<<ANS_DEPTH), CDF2sym+(kc<<ANS_PROB_BITS)))
		//{
		//	free(info);
		//	return false;
		//}
		if(srcptr<src_start)
			FAIL("srcptr < start: ks=%d, frame = %d bytes, s = 0x%02X,\nsrcptr=%p, start=%p", ks, framebytes, *dstptr, srcptr, src_start);
#ifdef ANS_PRINT_STATE
		printf("kc %d s=%02X x=%08X->%08X\n", kc, *dstptr&0xFF, s0, x[kc]);//
#endif
#ifdef DEBUG_ANS
		if(guide&&((unsigned char*)guide)[ks]!=*dstptr)
			FAIL("Decode error at byte %d/%d kc %d: decoded 0x%02X != original 0x%02X", ks, framebytes, kc, *dstptr&0xFF, ((unsigned char*)guide)[ks]&0xFF);
#endif
	}

	src_idx+=csize;
	free(info);
	return true;
}

#ifdef DEBUG_ANS
void			debug_test()
{
	console_start();
	unsigned char *dst=nullptr;
	unsigned long long dst_size=0, dst_cap=0;
//	guide=buffer;
	int nFrames=1, depth=1;
	huff::compress_v7(buffer, iw, ih, 0, depth, nFrames, dst, dst_size, dst_cap);

	float gain=1/(float)(((1<<depth)-1)*nFrames);
	for(int ky=0, kd=0;ky<ih;ky+=2)//interleave Bayer channels
	{
		auto row=buffer+iw*ky;
		for(int kx=0;kx<iw;kx+=2, kd+=4)
		{
			guide[kd  ]=row[kx]*gain;
			guide[kd+1]=row[kx+1]*gain;
			guide[kd+2]=row[kx+iw]*gain;
			guide[kd+3]=row[kx+iw+1]*gain;
		}
	}
	//for(int k=0;k<dst_size;++k)
	//	printf("%02X-", dst[k]);

	float *b2=nullptr;
	int bw=0, bh=0;
	char bayer[4]={};
	huff::decompress(dst, dst_size, RF_F32_BAYER, (void**)&b2, bw, bh, depth, bayer);

	for(int k=imsize-1;k>=0;--k)
	{
		if(b2[k]!=buffer[k])
		{
			printf("Error at %d: %f!=%f, %08X!=%08X", k, b2[k], buffer[k], (int&)b2[k], (int&)buffer[k]);
			break;
		}
	}
	console_end();
	exit(0);
}
#endif

namespace		huff
{
	int			compress(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
	{
		time_start();
		checkSIMD();
		short *temp=nullptr;
		const short *b2;
		int width, height, imSize;
		if(bayer==0||bayer==1)//grayscale or gray denoised
		{
			width=bw>>1, height=bh>>1, imSize=width*height;
			depth+=2;
			temp=bayer2gray(buffer, width, height);
			time_mark("bayer2gray");
			if(bayer==1)
			{
				if(supportsSIMD)
				{
					denoise_bayer_simd(temp, width, height, depth);
					time_mark("denoise SIMD");
				}
				else
				{
					denoise_bayer(temp, width, height, depth);
					time_mark("denoise");
				}
			}
			b2=temp;
		}
		else//raw color
			width=bw, height=bh, imSize=width*height, b2=buffer;
		int nLevels=1<<depth;

		data.resize(sizeof(HuffHeader)/sizeof(int)+nLevels);
		auto header=(HuffHeader*)data.data();
		*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
		header->version=1;
		header->width=width;
		header->height=height;
		*(int*)header->bayerInfo=bayer;
		header->nLevels=nLevels;

		int *histogram=(int*)((HuffHeader*)data.data())->histogram;
		calculate_histogram(b2, imSize, histogram, nLevels);
		time_mark("calculate histogram");


		build_tree(histogram, nLevels);
		time_mark("build tree");
#ifdef PRINT_TREE
		console_start_good();
		print("Tree:"), print_flush();
		print_tree();
#endif

		std::vector<vector_bool> alphabet;
		make_alphabet(alphabet);
		time_mark("make alphabet");
#ifdef PRINT_ALPHABET
		std::vector<int> indices;
		sort_alphabet(alphabet.data(), nLevels, indices);
		//std::sort(alphabet.begin(), alphabet.end(), AlphabetComparator(alphabet.data()));
		print("Sorted alphabet"), print_flush();
		print_alphabet(alphabet.data(), histogram, nLevels, imSize, indices.data());//
#endif
#ifdef DEBUG_VEC_BOOL
		print("Codes:"), print_flush();
		//for(int k=0;k<70;++k)
		//{
		//	print("%d", k&7);
		//	if((k&7)==7)
		//		print(" ");
		//}
		//print("\n");
	/*	for(int k=0, col=0;k<imSize;++k)
		{
			if(col+alphabet[b2[k]].bitSize+1>=80)
			{
				print_flush();
				col=0;
			}
			alphabet[b2[k]].debug_print();
			print(" ");
			col+=alphabet[b2[k]].bitSize+1;
		}
		print_flush();//*/
		for(int ky=0, k=0;ky<bh;++ky)
		{
			for(int kx=0;kx<bw;++kx)
			{
				auto &code=alphabet[b2[bw*ky+kx]];
				code.debug_print(k);
				k+=code.bitSize;
				print(" ");
			}
			print_flush();
		}
#endif
		vector_bool bits;

		int length=0;
		for(int k=0;k<(int)alphabet.size();++k)
			length+=alphabet[k].bitSize;
		length/=nLevels;
		bits.data.reserve(length*width*height>>5);

//#ifdef __ANDROID__
		for(int k=0;k<imSize;++k)
			bits.push_back(alphabet[b2[k]]);
/*#else
		for(int k=0;k<(int)alphabet.size();++k)
			alphabet[k].set_size_factor(2);
		for(int k=0;k<imSize;++k)
		{
			auto &code=alphabet[b2[k]];
			int codepaddedintsize=code.data.size(),
				activeintidx=bits.bitSize>>vector_bool::LBPU,
				paddedintsize=activeintidx+codepaddedintsize;
			bits.data.resize(paddedintsize);

			__m128i mdst=_mm_loadu_si128((__m128i*)(bits.data.data()+activeintidx));
			for(int kd=activeintidx, ks=0;kd<paddedintsize;kd+=4, ks+=4)
			{
				__m128i msrc=_mm_loadu_si128((__m128i*)code.data.data()+ks);
				int bit_offset=bits.bitSize&vector_bool::BPU_MASK;
				__m128i lo, hi;
				shift_left_vector_small(msrc, bit_offset, lo, hi);
				mdst=_mm_or_si128(mdst, lo);
				_mm_storeu_si128((__m128i*)(bits.data.data()+kd), mdst);
				mdst=hi;
			}
			bits.bitSize+=code.bitSize;
		}
#endif//*/
		bits.clear_tail();
		time_mark("concatenate bits");
#ifdef PRINT_DATA
		print("Concatenated bits:"), print_flush();
		bits.debug_print(0);
		print_flush();
#endif
		int data_start=(int)data.size();
		data.resize(data_start+sizeof(HuffDataHeader)+bits.size_bytes()/sizeof(int));
		auto dataHeader=(HuffDataHeader*)(data.data()+data_start);
		*(int*)dataHeader->DATA='D'|'A'<<8|'T'<<16|'A'<<24;
		dataHeader->uPxCount=imSize;
		dataHeader->cBitSize=bits.bitSize;
		memcpy(dataHeader->data, bits.data.data(), bits.size_bytes());
		//memcpy(dataHeader->data, bits.data, bits.size_bytes());
		time_mark("memcpy");

		//delete[] alphabet;
		if(bayer==0||bayer==1)
		{
			delete[] temp;
			b2=buffer;
			time_mark("delete[] temp");
		}
		return data_start;
	}
	int			compress_v2(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
	{
		time_start();
		checkSIMD();
		int imSize=bw*bh, nLevels=1<<depth;
		data.resize(sizeof(HuffHeader)/sizeof(int)+nLevels);
		auto header=(HuffHeader*)data.data();
		*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
		header->width=bw;
		header->height=bh;
		*(int*)header->bayerInfo=bayer;
		header->nLevels=nLevels;
		time_mark("header");

		int *histogram=new int[nLevels];
		calculate_histogram(buffer, imSize, histogram, nLevels);
		time_mark("calculate histogram");

		int *palette=(int*)header->histogram;
		for(int k=0;k<nLevels;++k)
			palette[k]=k;
		time_mark("init palette");
		std::sort(palette, palette+nLevels, [&](int idx1, int idx2)
		{
			return histogram[idx1]>histogram[idx2];//descending order
		});
		time_mark("sort palette");

		int *invP=new int[nLevels];
		for(int k=0;k<nLevels;++k)
			invP[palette[k]]=k;
		time_mark("init inv palette");

		for(int k=0;k<nLevels;++k)
		{
			if(!histogram[palette[k]])
			{
				header->nLevels=k;
				break;
			}
		}
		time_mark("count present symbols");

#ifdef PRINT_V2
		print_histogram(histogram, nLevels, imSize, palette);
		time_mark("print histogram");
#endif

		//for(int k=0;k<imSize;++k)
		//	buffer[k]=invP[buffer[k]];
		//time_mark("substitute pixels");

		long long bitsize[3]={};
		for(int k=0;k<nLevels;++k)
		{
			int symbol=invP[k];
			int freq=histogram[k];

			//estimate size with code A
			if(symbol<3)
				bitsize[0]+=2*freq;
			else if(symbol<6)
				bitsize[0]+=4*freq;
			else if(symbol<21)
				bitsize[0]+=8*freq;
			else if(symbol<276)
				bitsize[0]+=16*freq;
			else
				bitsize[0]+=32*freq;

			//estimate size with code B
			if(symbol<15)
				bitsize[1]+=4*freq;
			else if(symbol<30)
				bitsize[1]+=8*freq;
			else if(symbol<285)
				bitsize[1]+=16*freq;
			else
				bitsize[1]+=32*freq;
			//if(bitsize[0]>20000000||bitsize[0]>20000000||bitsize[0]>20000000)
			//	int LOL_1=0;

			//estimate size with code C from RVL
			int bitlen=floor_log2(symbol);
			bitlen=bitlen/3+((bitlen%3)!=0);//ceil(b/3)
			bitlen<<=2;
			bitsize[2]+=bitlen*freq;
		}
		int code_id=0;
		for(int k=1;k<3;++k)
			if(bitsize[code_id]>bitsize[k])
				code_id=k;
		time_mark("estimate sizes");
#ifdef PRINT_V2
		print("Code A: %lld bits", bitsize[0]), print_flush();
		print("Code B: %lld bits", bitsize[1]), print_flush();
		print("Code C: %lld bits", bitsize[2]), print_flush();
		print("code_id: %d", code_id), print_flush();
		time_mark("printf");
#endif

		header->version=2+code_id;
		int intsize=(int)bitsize[code_id];
		intsize=(intsize>>5)+((intsize&31)!=0);
		int data_idx=(int)data.size();
		data.resize(data_idx+sizeof(HuffDataHeader)/sizeof(int)+intsize+1);//header invalidated
		auto hData=(HuffDataHeader*)(data.data()+data_idx);
		*(int*)hData->DATA='D'|'A'<<8|'T'<<16|'A'<<24;
		hData->uPxCount=imSize;
		hData->cBitSize=bitsize[code_id];
		int *bits=(int*)hData->data;

		int bitidx=0;
		int icode=0;
		auto code=(unsigned char*)&icode;
		switch(code_id)//all codes are little-endian
		{
		case 0://code A
			for(int k=0;k<imSize;++k)
			{
				int symbol=invP[buffer[k]];
				int intidx=bitidx>>5, bitoffset=bitidx&31;

				int bitlen;
				if(symbol<3)//frequent case fast
				{
					bits[intidx]|=symbol<<bitoffset, bitidx+=2;
					continue;
				}
				if(symbol<6)
					icode=(symbol-3)<<2|3, bitlen=4;
				else if(symbol<21)
					icode=(symbol-6)<<4|15, bitlen=8;
				else if(symbol<276)
					icode=(symbol-21)<<8|0xFF, bitlen=16;
				else
					icode=(symbol-276)<<16|0xFFFF, bitlen=32;
				bits[intidx  ]|=icode<<bitoffset;
				if(bitoffset)
					bits[intidx+1]|=icode>>(32-bitoffset)&-!bitoffset;
				bitidx+=bitlen;

			/*	int bitlen;
				if(symbol<3)//frequent case fast
				{
					bits[intidx]|=symbol<<bitoffset, bitidx+=2;
					continue;
				}
				if(symbol<6)
					icode=0xC|(symbol-3), bitlen=4;
				else if(symbol<21)
					icode=0xF0|(symbol-6), bitlen=8;
				else if(symbol<276)
					code[0]=0xFF, code[1]=symbol-21, code[2]=code[3]=0, bitlen=16;
				else
				{
					int c2=symbol-276;
					code[0]=0xFF, code[1]=0xFF, code[2]=c2>>8, code[3]=c2&0xFF, bitlen=32;
				}
				bits[intidx]|=icode<<bitoffset;
				bitidx+=bitlen;
				//if((bitidx>>5)>intidx)
					bits[bitidx>>5]|=icode>>(32-(bitidx&31));//*/

			/*	char code[4]={0};
				int bitlen;
				if(symbol<3)
					code[0]=symbol, bitlen=2;
				else if(symbol<6)
					code[0]=0xC|(symbol-3), bitlen=4;
				else if(symbol<21)
					code[0]=0xF0|(symbol-6), bitlen=8;
				else if(symbol<276)
					code[0]=0xFF, code[1]=symbol-21, bitlen=16;
				else
				{
					int c2=symbol-276;
					code[0]=0xFF, code[1]=0xFF, code[2]=c2>>8, code[3]=c2&0xFF, bitlen=32;
				}
				bitidx+=bitlen;//*/
			}
			time_mark("encode A");
			break;
		case 1://code B
			for(int k=0;k<imSize;++k)
			{
				int symbol=invP[buffer[k]];
				int intidx=bitidx>>5, bitoffset=bitidx&31;
				int bitlen;
				if(symbol<15)//frequent case fast
				{
					bits[intidx]|=symbol<<bitoffset, bitidx+=4;
					continue;
				}
				if(symbol<30)
					icode=(symbol-15)<<4|15, bitlen=8;
				else if(symbol<285)
					icode=(symbol-30)<<8|0xFF, bitlen=16;
				else
					icode=(symbol-285)<<16|0xFFFF, bitlen=32;
				bits[intidx  ]|=icode<<bitoffset;
				if(bitoffset)
					bits[intidx+1]|=icode>>(32-bitoffset)&-!bitoffset;
				bitidx+=bitlen;
			}
			time_mark("encode B");
			break;
		case 2://code C
			for(int k=0;k<imSize;++k)
			{
				int symbol=invP[buffer[k]];
				int intidx=bitidx>>5, bitoffset=bitidx&31;

				//if(symbol==8)
				//	int LOL_1=0;

				//split bits into groups of 3
				int x=(symbol&(7<<9))<<3|(symbol&(7<<6))<<2|(symbol&(7<<3))<<1|symbol&7;//up to 2^12 levels

				//set 4th bit
				x|=x<<1&0x8888;
				x|=x<<2&0x8888;
				x|=x<<3&0x8888;
				x|=(x&0x8888)>>4;
				x|=(x&0x8888)>>8;

				int bitlen=(!x+((x&8)>>3)+((x&0x80)>>7)+((x&0x800)>>11)+((x&0x8000)>>15))<<2;
				x=x&0x7777|(x&0x8888)>>4;//clear MSB

				bits[intidx]|=x<<bitoffset;
				if(bitoffset)
					bits[intidx+1]|=x>>(32-bitoffset);
					//bits[intidx+1]|=x>>(32-bitoffset)&-!bitoffset;//X undefined behavior
				bitidx+=bitlen;
				//bits[intidx]|=x<<bitoffset;
				//bitidx+=bitlen;
				//bits[bitidx>>5]|=x>>(32-(bitidx&31));

			/*	//slow reference
				int bitlen;
				if(symbol<8)
				{
					bits[intidx]|=symbol<<bitoffset, bitidx+=4;
					continue;
				}
				if(symbol<64)
					icode=(8|symbol&7)<<4|symbol>>3, bitlen=8;
				else if(symbol<(1<<9))
					icode=(8|symbol&7)<<4|symbol>>3, bitlen=12;//X //*/
			}
			time_mark("encode C");
			break;
		}
#ifdef PRINT_V2_DATA
		for(int k=0;k<intsize;++k)
			printf("%08X-", bits[k]);
		printf("\n");
#endif

		delete[] histogram;
		delete[] invP;
		time_mark("cleanup");
		return data_idx;
	}
	int			compress_v5(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
	{
		time_start();
		checkSIMD();
		const int hSize=sizeof(HuffHeader)/sizeof(int);

		short *temp=nullptr;
		const short *b2;
		int width, height, imSize;
		if(bayer==0||bayer==1)//grayscale or gray denoised
		{
			width=bw>>1, height=bh>>1, imSize=width*height;
			depth+=2;
			temp=bayer2gray(buffer, width, height);
			time_mark("bayer2gray");
			if(bayer==1)
			{
				if(supportsSIMD)
				{
					denoise_bayer_simd(temp, width, height, depth);
					time_mark("denoise SIMD");
				}
				else
				{
					denoise_bayer(temp, width, height, depth);
					time_mark("denoise");
				}
				bayer=0;
			}
		}
		else//raw color
		{
			width=bw, height=bh, imSize=width*height;
			temp=separateBayer(buffer, bw, bh);
			time_mark("separateBayer");
		}
		b2=temp;

		int nLevels=1<<depth;
		data.reserve(hSize+(imSize>>1));
		data.resize(hSize);
		auto header=(HuffHeader*)data.data();
		*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
		header->version=5;//RVL
		header->width=width;
		header->height=height;
		*(int*)header->bayerInfo=bayer;
		header->nLevels=nLevels;
		time_mark("header");

		int bitidx=0;
		for(int ky=0;ky<height;++ky)
		{
			data.resize(hSize+(bitidx>>5)+(width>>1));
			auto bits=((HuffHeader*)data.data())->histogram;
			int prev=0;
			const short *row=b2+width*ky;
			for(int kx=0;kx<width;++kx)
			{
				//if(ky==819&&kx==761)
				//	int LOL_1=0;
				int symbol=row[kx]-prev;
				prev=row[kx];

				int negative=symbol<0;
				symbol=abs(symbol+negative)<<1|negative;//zigzag coding
				//symbol=symbol<<1^symbol>>31;//1 is unused

				int x=(symbol&(7<<12))<<4|(symbol&(7<<9))<<3|(symbol&(7<<6))<<2|(symbol&(7<<3))<<1|symbol&7;//up to 2^14 levels

				//set 4th bit
				x|=x<<1&0x88888;
				x|=x<<2&0x88888;
				x|=x<<3&0x88888;
				x|=(x&0x88888)>>4;
				x|=(x&0x88888)>>4;
				x|=(x&0x88888)>>8;

				int bitlen=(!x+((x&8)>>3)+((x&0x80)>>7)+((x&0x800)>>11)+((x&0x8000)>>15)+((x&0x80000)>>19))<<2;
				x=x&0x77777|(x&0x88888)>>4;//clear MSB

				int intidx=bitidx>>5, bitoffset=bitidx&31;
				//if(intidx==6)//
				//	int LOL_1=0;//
				bits[intidx]|=x<<bitoffset;
				if(bitoffset)
					bits[intidx+1]|=x>>(32-bitoffset);
				bitidx+=bitlen;
			}
		}
		time_mark("encode RVL");
#ifdef PRINT_V2
		print("Compressed stream size = %d", bitidx), print_flush();
#endif
		data.resize(hSize+(bitidx>>5)+((bitidx&31)!=0));
		time_mark("resize");
		//if(bayer==0||bayer==1)
		//{
			delete[] temp;
			b2=buffer;
			time_mark("delete[] temp");
		//}
		return sizeof(HuffHeader)/sizeof(int);
	}
	int			pack_raw(const byte *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
	{
		time_start();
		int imsize=bw*bh, packedbytesize;
		if(depth==10)
			packedbytesize=imsize*5>>2;
		else if(depth==12)
			packedbytesize=imsize*3>>1;
		else
			return 0;
		int packedintsize=(packedbytesize>>2)+((packedbytesize&3)!=0);
		data.resize(sizeof(HuffHeader)/sizeof(int)+packedintsize);
		auto header=(HuffHeader*)data.data();
		*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
		header->version=depth;
		header->width=bw;
		header->height=bh;
		*(int*)header->bayerInfo=bayer;
		header->nLevels=1<<depth;
		time_mark("header");

		memcpy(header->histogram, buffer, packedbytesize);
		time_mark("memcpy");
		return sizeof(HuffHeader)/sizeof(int);
	}
	int			pack_r10_g12(const byte *buffer, int bw, int bh, int denoise, std::vector<int> &data)
	{
		time_start();
		checkSIMD();
		int w2=bw>>1, h2=bh>>1, imsize0=bw*bh, imsize2=imsize0>>2, packedbytesize=imsize2*3>>1, packedintsize=(packedbytesize>>2)+((packedbytesize&3)!=0);
		data.resize(sizeof(HuffHeader)/sizeof(int)+packedintsize);
		auto header=(HuffHeader*)data.data();
		*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
		header->version=12;
		header->width=w2;
		header->height=h2;
		*(int*)header->bayerInfo=0;
		header->nLevels=1<<12;
		time_mark("header");

		auto dst=(byte*)header->histogram;
		if(denoise)
		{
			auto src=unpack_r10(buffer, bw, bh);
			time_mark("unpack raw10");
			if(supportsSIMD)
			{
				denoise_bayer_simd(src, bw, bh, 10);
				time_mark("denoise SIMD");
			}
			else
			{
				denoise_bayer(src, bw, bh, 10);
				time_mark("denoise");
			}

			for(int ky=0, kd=0;ky<bh;ky+=2)//g12
			{
				auto row=src+bw*ky, row2=row+bw;
				for(int kx=0;kx<bw;kx+=4, kd+=3)
				{
					int g0=row[kx  ]+row[kx+1]+row2[kx  ]+row2[kx+1],
						g1=row[kx+2]+row[kx+3]+row2[kx+2]+row2[kx+3];
					dst[kd  ]=g0>>4;
					dst[kd+1]=g1>>4;
					dst[kd+2]=(g1&15)<<4|g0&15;
				}
			}
			time_mark("pack gray12");
			delete[] src;
		}
		else
		{
			int srcbytewidth=bw*5>>2;
			for(int ky=0, kd=0;ky<bh;ky+=2)//raw10 -> g12
			{
				auto row=buffer+srcbytewidth*ky, row2=row+srcbytewidth;
				for(int kx=0, ks=0;kx<bw;kx+=4, ks+=5, kd+=3)
				{
					int v00=row[ks  ]<<2|row[ks+4]   &3,
						v01=row[ks+1]<<2|row[ks+4]>>2&3,
						v02=row[ks+2]<<2|row[ks+4]>>4&3,
						v03=row[ks+3]<<2|row[ks+4]>>6&3;
					int v10=row2[ks  ]<<2|row2[ks+4]   &3,
						v11=row2[ks+1]<<2|row2[ks+4]>>2&3,
						v12=row2[ks+2]<<2|row2[ks+4]>>4&3,
						v13=row2[ks+3]<<2|row2[ks+4]>>6&3;
					int g0=v00+v01+v10+v11, g1=v02+v03+v12+v13;
					dst[kd  ]=g0>>4;
					dst[kd+1]=g1>>4;
					dst[kd+2]=(g1&15)<<4|g0&15;
				}
			}
			time_mark("pack 10->g12");
		}
	/*	for(int ky=0, kd=0;ky<bh;ky+=2)//unpacked -> g12
		{
			auto row=buffer+bw*ky, row2=row+bw;
			for(int kx=0;kx<bw;kx+=4, kd+=3)
			{
				int v0=row[kx  ]+row[kx+1]+row2[kx  ]+row2[kx+1],
					v1=row[kx+2]+row[kx+3]+row2[kx+2]+row2[kx+3];
				dst[kd  ]=v0>>4;
				dst[kd+1]=v1>>4;
				dst[kd+2]=(v1&15)<<4|v0&15;
			}
		}//*/
		return sizeof(HuffHeader)/sizeof(int);
	}
	int			pack_r12_g14(const byte *buffer, int bw, int bh, int denoise, std::vector<int> &data)
	{
		time_start();
		checkSIMD();
		int w2=bw>>1, h2=bh>>1, imsize0=bw*bh, imsize2=imsize0>>2, packedbytesize=imsize2*3>>1, packedintsize=(packedbytesize>>2)+((packedbytesize&3)!=0);
		data.resize(sizeof(HuffHeader)/sizeof(int)+packedintsize);
		auto header=(HuffHeader*)data.data();
		*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
		header->version=14;
		header->width=w2;
		header->height=h2;
		*(int*)header->bayerInfo=0;
		header->nLevels=1<<14;
		time_mark("header");

		auto dst=(byte*)header->histogram;
		if(denoise)
		{
			auto src=unpack_r10(buffer, bw, bh);
			time_mark("unpack raw12");
			if(supportsSIMD)
			{
				denoise_bayer_simd(src, bw, bh, 12);
				time_mark("denoise SIMD");
			}
			else
			{
				denoise_bayer(src, bw, bh, 12);
				time_mark("denoise");
			}

			for(int ky=0, kd=0;ky<bh;ky+=2)//raw12 -> g14
			{
				auto row=src+bw*ky, row2=row+bw;
				for(int kx=0;kx<bw;kx+=8, kd+=7)
				{
					int g0=row[kx  ]+row[kx+1]+row2[kx  ]+row2[kx+1],
						g1=row[kx+2]+row[kx+3]+row2[kx+2]+row2[kx+3],
						g2=row[kx+4]+row[kx+5]+row2[kx+4]+row2[kx+5],
						g3=row[kx+6]+row[kx+7]+row2[kx+6]+row2[kx+7];
					dst[kd  ]=g0>>6;
					dst[kd+1]=g1>>6;
					dst[kd+2]=g2>>6;
					dst[kd+3]=g3>>6;
					dst[kd+4]=(g1>>2&15)<<4|(g0>>2&15);
					dst[kd+5]=(g3>>2&15)<<4|(g2>>2&15);
					dst[kd+6]=(g3&3)<<6|(g2&3)<<4|(g1&3)<<2|g0&3;
				}
			}
			time_mark("pack gray14");
			delete[] src;
		}
		else
		{
			int srcbytewidth=bw*3>>1;
			for(int ky=0, kd=0;ky<bh;ky+=2)//raw12 -> g14
			{
				auto row=buffer+srcbytewidth*ky, row2=row+srcbytewidth;
				for(int kx=0, ks=0;kx<bw;kx+=8, ks+=20, kd+=7)
				{
					int v00=row[ks   ]<<4|row[ks   +3]&15, v01=row[ks   +1]<<4|row[ks   +3]>>4&15;
					int v02=row[ks+ 5]<<4|row[ks+ 5+3]&15, v03=row[ks+ 5+1]<<4|row[ks+ 5+3]>>4&15;
					int v04=row[ks+10]<<4|row[ks+10+3]&15, v05=row[ks+10+1]<<4|row[ks+10+3]>>4&15;
					int v06=row[ks+15]<<4|row[ks+15+3]&15, v07=row[ks+15+1]<<4|row[ks+15+3]>>4&15;
					int v10=row2[ks   ]<<4|row2[ks   +3]&15, v11=row2[ks   +1]<<4|row2[ks   +3]>>4&15;
					int v12=row2[ks+ 5]<<4|row2[ks+ 5+3]&15, v13=row2[ks+ 5+1]<<4|row2[ks+ 5+3]>>4&15;
					int v14=row2[ks+10]<<4|row2[ks+10+3]&15, v15=row2[ks+10+1]<<4|row2[ks+10+3]>>4&15;
					int v16=row2[ks+10]<<4|row2[ks+10+3]&15, v17=row2[ks+10+1]<<4|row2[ks+10+3]>>4&15;
					int g0=v00+v01+v10+v11,
						g1=v02+v03+v12+v13,
						g2=v04+v05+v14+v15,
						g3=v06+v07+v16+v17;
					dst[kd  ]=g0>>6;
					dst[kd+1]=g1>>6;
					dst[kd+2]=g2>>6;
					dst[kd+3]=g3>>6;
					dst[kd+4]=(g1>>2&15)<<4|(g0>>2&15);
					dst[kd+5]=(g3>>2&15)<<4|(g2>>2&15);
					dst[kd+6]=(g3&3)<<6|(g2&3)<<4|(g1&3)<<2|g0&3;
				}
			}
			time_mark("pack 12->g14");
		}
		return sizeof(HuffHeader)/sizeof(int);
	}
	int			compress_v7(const float *buffer, int bw, int bh, int bayer, int depth, int nFrames, unsigned char *&dst, unsigned long long &dst_size, unsigned long long &dst_cap)
	{
		time_start();
		checkSIMD();

		int imSize=bw*bh;
		auto temp=(float*)malloc(imSize*sizeof(float));

		float gain=1/(float)(((1<<depth)-1)*nFrames);
	//	float gain=255/(float)nFrames;//0~255 for better utilization of precision
#ifdef PRINT_MINMAX
		float vmin=0, vmax=0;//
#endif
		for(int ky=0, kd=0;ky<bh;ky+=2)//interleave Bayer channels
		{
			auto row=buffer+bw*ky;
			for(int kx=0;kx<bw;kx+=2, kd+=4)
			{
				temp[kd  ]=row[kx]*gain;
				temp[kd+1]=row[kx+1]*gain;
				temp[kd+2]=row[kx+bw]*gain;
				temp[kd+3]=row[kx+bw+1]*gain;
#ifdef PRINT_MINMAX
				if(!ky&&!kx)
					vmin=vmax=temp[kd];
				if(vmin>temp[kd])
					vmin=temp[kd];
				if(vmax<temp[kd])
					vmax=temp[kd];
				if(vmin>temp[kd+1])
					vmin=temp[kd+1];
				if(vmax<temp[kd+1])
					vmax=temp[kd+1];
				if(vmin>temp[kd+2])
					vmin=temp[kd+2];
				if(vmax<temp[kd+2])
					vmax=temp[kd+2];
				if(vmin>temp[kd+3])
					vmin=temp[kd+3];
				if(vmax<temp[kd+3])
					vmax=temp[kd+3];
#endif
			}
		}
#ifdef PRINT_MINMAX
		LOGE("%f~%f", vmin, vmax);
#endif
#ifdef DEBUG_ANS
		unsigned chidx[]={0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C};
		printf("    %08X-%08X-%08X-%08X\n", chidx[0], chidx[1], chidx[2], chidx[3]);
		for(int k=0;k<imSize;k+=4)
			printf("[%d] %08X-%08X-%08X-%08X-\n", k/4, (int&)temp[k], (int&)temp[k+1], (int&)temp[k+2], (int&)temp[k+3]);
#endif
		//for(int kcy=0, kd=0;kcy<2;++kcy)//separate the Bayer matrix
		//{
		//	for(int kcx=0;kcx<2;++kcx)
		//	{
		//		for(int ky=kcy;ky<bh;ky+=2)
		//		{
		//			auto row=buffer+bw*ky;
		//			for(int kx=kcx;kx<bw;kx+=2, ++kd)
		//				temp[kd]=row[kx]*gain;
		//		}
		//	}
		//}
		time_mark("interleave & gain");

		emit_pad(dst, dst_size, dst_cap, sizeof(HuffHeader));
		auto header=(HuffHeader*)(dst+dst_size);
		*(int*)header->HUFF='H'|'U'<<8|'F'<<16|'F'<<24;
		header->version=7;//ACC-ANS
		header->width=bw>>1;	//image size = header->width * header->height * 4*sizeof(float) bytes
		header->height=bh>>1;
		*(int*)header->bayerInfo=bayer;
		header->nLevels=1<<depth;
		//header->nLevels=4*sizeof(float);//bytespersymbol
		dst_size+=sizeof(HuffHeader);
		time_mark("header");
		rans4_encode(temp, imSize/4, 4*sizeof(float), dst, dst_size, dst_cap, false);
		time_mark("rANS_enc");
#ifndef __ANDROID__
		free(temp);//CRASHES when freeing temp where it was allocated
#endif
		return (int)dst_size;
	}
	bool		decompress(const byte *data, int bytesize, RequestedFormat format, void **pbuffer, int &bw, int &bh, int &depth, char *bayer_sh)//realloc will be used on buffer
	{
//#ifdef DEBUG_ANS
//		static int callcount=0;
//		++callcount;
//		if(callcount==1)
//			debug_test();
//#endif
		time_start();
		checkSIMD();
#ifdef DEBUG_ARCH
		console_start_good();
		print("decode_huff"), print_flush();
#endif
		auto header=(HuffHeader const*)data;
		if(*(int*)header->HUFF!=('H'|'U'<<8|'F'<<16|'F'<<24)||header->nLevels>(1<<16))
		{
			LOG_ERROR("Invalid file tag: %.4s, ver: %d, w=%d, h=%d", header->HUFF, header->version, header->width, header->height);
#ifdef DEBUG_ARCH
			console_pause();
			console_end();
#endif
			return false;
		}
		char bayer_sh2[4]={};
		if(*(int*)header->bayerInfo)
		{
			char *bayerInfo=(char*)&header->bayerInfo;
			for(int k=0;k<4;++k)
			{
				switch(bayerInfo[k])
				{
				case 'R':bayer_sh2[k]=16;break;
				case 'G':bayer_sh2[k]=8;break;
				case 'B':bayer_sh2[k]=0;break;
				default:
					LOG_ERROR("Invalid Bayer info: %c%c%c%c", bayerInfo[0], bayerInfo[1], bayerInfo[2], bayerInfo[3]);
#ifdef DEBUG_ARCH
					console_pause();
					console_end();
#endif
					return false;
				}
			}
		}
		else
			memset(bayer_sh2, -1, 4);
		int imSize=header->width*header->height;
		short *dst=nullptr;
		bool is_float=false;
		if(header->version==1)
		{
			dst=(short*)malloc(imSize*sizeof(short));
			auto hData=(HuffDataHeader*)(header->histogram+header->nLevels);
			if(*(int*)hData->DATA!=('D'|'A'<<8|'T'<<16|'A'<<24))
			{
				LOG_ERROR("Invalid data tag: %c%c%c%c, pxCount=%d, bitSize=%lld", hData->DATA[0], hData->DATA[1], hData->DATA[2], hData->DATA[3], hData->uPxCount, hData->cBitSize);
#ifdef DEBUG_ARCH
				console_pause();
				console_end();
#endif
				free(dst);
				return false;
			}

#ifdef PRINT_HISTOGRAM
			console_start_good();
			print_histogram((int*)header->histogram, header->nLevels, hData->uPxCount);
#endif
			auto src=hData->data;

			build_tree((int*)header->histogram, header->nLevels);
			time_mark("build tree");
#ifdef PRINT_TREE
			console_start_good();
			print("Tree:"), print_flush();
			print_tree();
#endif

#ifdef PRINT_ALPHABET
			console_start_good();
			std::vector<vector_bool> alphabet;
			make_alphabet(alphabet);
			std::vector<int> indices;
			sort_alphabet(alphabet.data(), header->nLevels, indices);
			print("Sorted alphabet"), print_flush();
			print_alphabet(alphabet.data(), (int*)header->histogram, header->nLevels, imSize, indices.data());//

			print_bin((byte*)src, (data+bytesize)-(byte*)src);
			print_flush();
#endif

		/*	build_dec_tree(root);
			int bit_idx=0, kd=0;
			for(;kd<imSize&&bit_idx<hData->cBitSize;++kd)//read bits, LSB-first
			{
				int ex_idx=bit_idx>>5, in_idx=bit_idx&31;
				int lookup;
				if(in_idx)
					lookup=src[ex_idx+1]<<(31-in_idx)|src[ex_idx]>>in_idx;//oldest on right
				else
					lookup=src[ex_idx];

				auto &node=dec_root[lookup&0xFF];
				if(node.next)//long code
				{
					bit_idx+=8;
					Node *prev=nullptr;
					for(auto n2=node.next;n2&&bit_idx<hData->cBitSize;++bit_idx)
					{
						char bit=src[ex_idx]>>in_idx&1;
						prev=n2;
						n2=n2->branch[bit];
					}
					--bit_idx;
					if(prev)
						dst[kd]=prev->value;
	#ifdef DEBUG_ARCH
					else
						print("error: unreachable"), print_flush();
	#endif
				}
				else//short code
				{
					dst[kd]=node.symbol;
					bit_idx+=node.bitlen;
				}
			}//*/

			int bit_idx=0, kd=0;//naive 1bit decode
			for(;kd<imSize&&bit_idx<hData->cBitSize;++kd)
			{
				int prev=(int)tree.size()-1, node=prev;
				while(bit_idx<hData->cBitSize&&(tree[node].branch[0]!=-1||tree[node].branch[1]!=-1))
				{
					int ex_idx=bit_idx>>5, in_idx=bit_idx&31;
					int bit=src[ex_idx]>>in_idx&1;
					prev=node;
					node=tree[node].branch[bit];
					++bit_idx;
				}
				dst[kd]=tree[node].value;
			}
			if(bit_idx!=hData->cBitSize)
			{
				console_start_good();
				//console_start();
				print("Decompression error:\n"), print_flush();
				print("\tbit_idx = %d", bit_idx), print_flush();
				print("\tbitsize = %lld", hData->cBitSize), print_flush();
				print_flush();
				//print("Decompression error: bit_idx=%d != bitsize=%lld\n", bit_idx, hData->cBitSize);
				console_pause();
			}
			time_mark("decode");
		}
		else if(header->version==2||header->version==3||header->version==4)//encoded with palette
		{
			dst=(short*)malloc(imSize*sizeof(short));
			auto hData=(HuffDataHeader const*)(header->histogram+header->nLevels);
			int *bits=(int*)hData->data;
			int bitidx=0;
			switch(header->version)
			{
			case 2://code A
				for(int k=0;k<imSize;++k)
				{
					int symbol=0;
					int code;
					int intidx=bitidx>>5, bitoffset=bitidx&31;
					if(bitoffset)
						code=bits[intidx+1]<<(32-bitoffset)|bits[intidx]>>bitoffset;
					else
						code=bits[intidx];
					if((code&3)<3)
						symbol=code, bitidx+=2;
					else if((code&15)<15)
						symbol=(code>>2)+3, bitidx+=4;
					else if((code&0xFF)<0xFF)
						symbol=(code>>4)+6, bitidx+=8;
					else if((code&0xFFFF)<0xFFFF)
						symbol=(code>>8)+21, bitidx+=16;
					else
						symbol=(code>>16)+276, bitidx+=32;
					dst[k]=symbol;
				}
				break;
			case 3://code B
				for(int k=0;k<imSize;++k)
				{
					int symbol=0;
					int code;
					int intidx=bitidx>>5, bitoffset=bitidx&31;
					if(bitoffset)
						code=bits[intidx+1]<<(32-bitoffset)|bits[intidx]>>bitoffset;
					else
						code=bits[intidx];
					if((code&15)<15)
						symbol=code, bitidx+=4;
					else if((code&0xFF)<0xFF)
						symbol=(code>>4)+15, bitidx+=8;
					else if((code&0xFFFF)<0xFFFF)
						symbol=(code>>8)+30, bitidx+=16;
					else
						symbol=(code>>16)+285, bitidx+=32;
					dst[k]=symbol;
				}
				break;
			case 4://code C
				for(int k=0;k<imSize;++k)
				{
					//if(k==28)
					//	int LOL_1=0;
					int symbol=0;
					int code, bitlen=0;
					do
					{
						code=bits[bitidx>>5]>>(bitidx&31)&15;
						symbol|=(code&7)<<bitlen;
						bitlen+=3, bitidx+=4;
					}while(code&8);
					dst[k]=symbol;
				}
				break;
			}
			time_mark("decode");

			int *palette=(int*)header->histogram;
			for(int k=0;k<imSize;++k)
				dst[k]=palette[dst[k]];
			time_mark("substitute");
		}
		else if(header->version==5)//RVL
		{
			dst=(short*)malloc(imSize*sizeof(short));
			int *bits=(int*)header->histogram;
			int bitidx=0;
			short *d2=nullptr;
			int interleave=*(int*)header->bayerInfo!=0&&*(int*)header->bayerInfo!=1;
			if(interleave)
				d2=new short[imSize];
			else
				d2=dst;
			for(int ky=0;ky<(int)header->height;++ky)
			{
				int prev=0;
				auto row=d2+header->width*ky;
				for(int kx=0;kx<(int)header->width;++kx)
				{
					int symbol=0;
					int code, bitlen=0;
					do
					{
						code=bits[bitidx>>5]>>(bitidx&31)&15;
						symbol|=(code&7)<<bitlen;
						bitlen+=3, bitidx+=4;
					}while(code&8);

					int s0=symbol;

					int negative=symbol&1;
					symbol=symbol>>1^-negative;

					row[kx]=prev+symbol;
					prev=row[kx];
				}
			}
			time_mark("decode RVL");

			if(interleave)
			{
				int w2=header->width>>1, h2=header->height>>1;
				for(int ky=0;ky<(int)header->height;++ky)
				{
					int iy2=ky>=h2;
					int ky0=ky-(h2&-iy2);
					ky0=ky0<<1|iy2;

					const short *srow=d2+header->width*ky;
					short *drow=dst+header->width*ky0;
					for(int kx=0;kx<(int)header->width;++kx)
					{
						int ix2=kx>=w2;
						int kx0=kx-(w2&-ix2);
						kx0=kx0<<1|ix2;
						drow[kx0]=srow[kx];
					}
				}
				time_mark("interleave Bayer channels");
				delete[] d2;
				time_mark("cleanup");
			}
		}
		else if(header->version==7)//ACC-ANS
		{
			is_float=true;
			dst=(short*)malloc(imSize*4*sizeof(float));//sic
			auto src=(byte*)header->histogram;
			unsigned long long src_idx=0, src_size=bytesize-sizeof(HuffHeader);
			rans4_decode(src, src_idx, src_size, dst, imSize, 4*sizeof(float), false);
		}
		else if(header->version==10)//uncompressed raw10
		{
			dst=(short*)malloc(imSize*sizeof(short));
			auto buf=(byte*)header->histogram;
			for(int kd=0, ks=0;kd<imSize&&(int)sizeof(HuffHeader)+ks<bytesize;kd+=4, ks+=5)
			{
				dst[kd  ]=buf[ks  ]<<2|buf[ks+4]   &3;
				dst[kd+1]=buf[ks+1]<<2|buf[ks+4]>>2&3;
				dst[kd+2]=buf[ks+2]<<2|buf[ks+4]>>4&3;
				dst[kd+3]=buf[ks+3]<<2|buf[ks+4]>>6&3;
			}
			time_mark("decode raw10");
		}
		else if(header->version==12)//uncompressed raw12
		{
			dst=(short*)malloc(imSize*sizeof(short));
			auto buf=(byte*)header->histogram;
			for(int kd=0, ks=0;kd<imSize&&(int)sizeof(HuffHeader)+ks<bytesize;kd+=2, ks+=3)
			{
				dst[kd  ]=buf[ks  ]<<4|buf[ks+2]   &15;
				dst[kd+1]=buf[ks+1]<<4|buf[ks+2]>>4&15;
			}
			time_mark("decode raw12");
		}
		else if(header->version==14)//uncompressed raw14
		{
			dst=(short*)malloc(imSize*sizeof(short));
			auto buf=(byte*)header->histogram;
			for(int kd=0, ks=0;kd<imSize&&(int)sizeof(HuffHeader)+ks<bytesize;kd+=4, ks+=7)
			{
				dst[kd  ]=buf[ks  ]<<6|buf[ks+4]   &15|buf[ks+6]   &3;
				dst[kd+1]=buf[ks+1]<<6|buf[ks+4]>>4&15|buf[ks+6]>>2&3;
				dst[kd+2]=buf[ks+2]<<6|buf[ks+5]   &15|buf[ks+6]>>4&3;
				dst[kd+3]=buf[ks+3]<<6|buf[ks+5]>>4&15|buf[ks+6]>>6&3;
			}
			time_mark("decode raw14");
		}

		//on success
		void *b2=nullptr;
		bw=header->width, bh=header->height, depth=floor_log2(header->nLevels);
		switch(format)
		{
		case RF_I8_RGBA:
			if(is_float)
			{
				//TODO: convert interleaved float Bayer to RGBA8
			}
			else
			{
				b2=realloc(*pbuffer, imSize<<2);
				if(b2)
					*pbuffer=b2;
				console_start_good();
				print("Decoding raw straight to RGBA is not supported yet."), print_flush();
				console_pause();
				console_end();
			}
			break;
		case RF_I16_BAYER:
			if(is_float)
			{
				//TODO: convert interleaved float Bayer to short Bayer
			}
			else
			{
				b2=realloc(*pbuffer, imSize<<1);
				if(b2)
					*pbuffer=b2;
				memcpy(*pbuffer, dst, imSize<<1);
			}
			break;
		case RF_F32_BAYER:
			if(is_float)
			{
				int w0=bw;
				bw<<=1, bh<<=1;
				b2=realloc(*pbuffer, imSize*4*sizeof(float));
				if(b2)
					*pbuffer=b2;
				auto src=(float*)dst;//sic
				auto fbuf=(float*)*pbuffer;
				for(int ky=0;ky<bh;ky+=2)
				{
					for(int kx=0;kx<bw;kx+=2)
					{
						int srcIdx=(w0*(ky>>1)+(kx>>1))<<2, dstIdx=bw*ky+kx;
						fbuf[dstIdx]=src[srcIdx];
						fbuf[dstIdx+1]=src[srcIdx+1];
						fbuf[dstIdx+bw]=src[srcIdx+2];
						fbuf[dstIdx+bw+1]=src[srcIdx+3];
					}
				}
			}
			else
			{
				float normal=1.f/(header->nLevels-1);
				b2=realloc(*pbuffer, imSize<<2);
				if(b2)
					*pbuffer=b2;
				auto fbuf=(float*)*pbuffer;
				for(int k=0;k<imSize;++k)
					fbuf[k]=dst[k]*normal;
			}
			break;
		}
		free(dst);
		if(bayer_sh)
			memcpy(bayer_sh, bayer_sh2, 4);
#ifdef DEBUG_ARCH
		console_pause();
		console_end();
#endif
		time_mark("format");
		return true;
	}
}//namespace huff
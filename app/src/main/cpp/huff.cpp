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
	int idx=tree.size();
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
	for(int k=tree.size()-1;k>=0;--k)
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
	s.push(TraverseInfo(tree.size()-1, vector_bool()));
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

short*			unpack_r10(const byte* src, int width, int height)
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
short*			unpack_r12(const byte* src, int width, int height)
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
		int data_start=data.size();
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
		int intsize=bitsize[code_id];
		intsize=(intsize>>5)+((intsize&31)!=0);
		int data_idx=data.size();
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
		if(bayer==0||bayer==1)
		{
			delete[] temp;
			b2=buffer;
			time_mark("delete[] temp");
		}
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
	bool		decompress(const byte *data, int bytesize, RequestedFormat format, void **pbuffer, int &bw, int &bh, int &depth, char *bayer_sh)//realloc will be used on buffer
	{
		time_start();
		checkSIMD();
#ifdef DEBUG_ARCH
		console_start_good();
		print("decode_huff"), print_flush();
#endif
		auto header=(HuffHeader const*)data;
		if(*(int*)header->HUFF!=('H'|'U'<<8|'F'<<16|'F'<<24)||header->nLevels>(1<<16))
		{
			LOG_ERROR("Invalid file tag: %c%c%c%c, ver: %d, w=%d, h=%d", header->HUFF[0], header->HUFF[1], header->HUFF[2], header->HUFF[3], header->version, header->width, header->height);
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
		short *dst=new short[imSize];
		if(header->version==1)
		{
			auto hData=(HuffDataHeader*)(header->histogram+header->nLevels);
			if(*(int*)hData->DATA!=('D'|'A'<<8|'T'<<16|'A'<<24))
			{
				LOG_ERROR("Invalid data tag: %c%c%c%c, pxCount=%d, bitSize=%lld", hData->DATA[0], hData->DATA[1], hData->DATA[2], hData->DATA[3], hData->uPxCount, hData->cBitSize);
#ifdef DEBUG_ARCH
				console_pause();
				console_end();
#endif
				delete[] dst;
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
				int prev=tree.size()-1, node=prev;
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
		else if(header->version==10)//uncompressed raw10
		{
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
		switch(format)
		{
		case RF_I8_RGBA:
			b2=realloc(*pbuffer, imSize<<2);
			if(b2)
				*pbuffer=b2;
			console_start_good();
			print("Decoding raw straight to RGBA is not supported yet."), print_flush();
			console_pause();
			console_end();
			break;
		case RF_I16_BAYER:
			b2=realloc(*pbuffer, imSize<<1);
			if(b2)
				*pbuffer=b2;
			memcpy(*pbuffer, dst, imSize<<1);
			break;
		case RF_F32_BAYER:
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
		delete[] dst;
		memcpy(bayer_sh, bayer_sh2, 4);
		bw=header->width, bh=header->height, depth=floor_log2(header->nLevels);
#ifdef DEBUG_ARCH
		console_pause();
		console_end();
#endif
		time_mark("format");
		return true;
	}
}//namespace huff
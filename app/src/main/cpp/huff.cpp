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
#ifdef HAVE_NEON
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

namespace		huff
{
	int			compress(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
	{
		time_start();
		checkSIMD();
		short *temp=nullptr;
		const short *b2;
		int width, height, imSize;
		if(bayer)//raw color
			width=bw, height=bh, imSize=width*height, b2=buffer;
		else//grayscale
		{
			width=bw>>1, height=bh>>1, imSize=width*height;
			depth+=2;
			temp=new short[imSize];
			for(int ky=0;ky<height;++ky)
			{
				int ky2=ky<<1;
				const short *row=buffer+bw*ky2, *row2=buffer+bw*(ky2+1);
				for(int kx=0;kx<width;++kx)
				{
					int kx2=kx<<1;
					temp[width*ky+kx]=row[kx2]+row[kx2+1]+row2[kx2]+row2[kx2+1];
				}
			}
			b2=temp;
		}
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

#ifdef __ANDROID__
		for(int k=0;k<imSize;++k)
			bits.push_back(alphabet[b2[k]]);
#else
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
#endif
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
		if(!bayer)
		{
			delete[] temp;
			b2=buffer;
		}
		return data_start;
	}
	int			compress_v2(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
	{
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
	int			pack_r10_g12(const byte *buffer, int bw, int bh, std::vector<int> &data)
	{
		time_start();
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
		time_mark("pack 10->g12");
		return sizeof(HuffHeader)/sizeof(int);
	}
	int			pack_r12_g14(const byte *buffer, int bw, int bh, std::vector<int> &data)
	{
		time_start();
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
		auto header=(HuffHeader*)data;
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
		else if(header->version==2)//encoded with palette
		{
		}
		else if(header->version==10)//uncompressed raw10
		{
			auto buf=(byte*)header->histogram;
			for(int kd=0, ks=0;kd<imSize&&sizeof(HuffHeader)+ks<bytesize;kd+=4, ks+=5)
			{
				dst[kd  ]=buf[ks  ]<<2|buf[ks+4]   &3;
				dst[kd+1]=buf[ks+1]<<2|buf[ks+4]>>2&3;
				dst[kd+2]=buf[ks+2]<<2|buf[ks+4]>>4&3;
				dst[kd+3]=buf[ks+3]<<2|buf[ks+4]>>6&3;
			}
		}
		else if(header->version==12)//uncompressed raw12
		{
			auto buf=(byte*)header->histogram;
			for(int kd=0, ks=0;kd<imSize&&sizeof(HuffHeader)+ks<bytesize;kd+=2, ks+=3)
			{
				dst[kd  ]=buf[ks  ]<<4|buf[ks+2]   &15;
				dst[kd+1]=buf[ks+1]<<4|buf[ks+2]>>4&15;
			}
		}
		else if(header->version==14)//uncompressed raw14
		{
			auto buf=(byte*)header->histogram;
			for(int kd=0, ks=0;kd<imSize&&sizeof(HuffHeader)+ks<bytesize;kd+=4, ks+=7)
			{
				dst[kd  ]=buf[ks  ]<<6|buf[ks+4]   &15|buf[ks+6]   &3;
				dst[kd+1]=buf[ks+1]<<6|buf[ks+4]>>4&15|buf[ks+6]>>2&3;
				dst[kd+2]=buf[ks+2]<<6|buf[ks+5]   &15|buf[ks+6]>>4&3;
				dst[kd+3]=buf[ks+3]<<6|buf[ks+5]>>4&15|buf[ks+6]>>6&3;
			}
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
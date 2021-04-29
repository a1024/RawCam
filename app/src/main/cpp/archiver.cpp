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

#if 0
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
static void		print_histogram(int *histogram, int nLevels, int scanned_size)
{
	int histMax=0;
	for(int k=0;k<nLevels;++k)
		if(histMax<histogram[k])
			histMax=histogram[k];
	const int consoleChars=79-15;
	if(!histMax)
		return;
	print("symbol, freq, %%"), print_flush();
	for(int k=0;k<nLevels;++k)
	{
		if(!histogram[k])
			continue;
		print("%4d %6d %2d ", k, histogram[k], 100*histogram[k]/scanned_size);
		for(int kr=0, count=histogram[k]*consoleChars/histMax;kr<count;++kr)
			print("*");
		print_flush();
	}
}


#define			CEIL_UNITS(BIT_SIZE)		(((BIT_SIZE)>>LBPU)+(((BIT_SIZE)&BPU_MASK)!=0))
#define			BYTE_SIZE(BIT_SIZE)			(CEIL_UNITS(BIT_SIZE)<<LOG_BYTES_PER_UNIT)
struct			vector_bool//LSB-first: MSB {b31...b0} LSB, b0 is the 1st bit in bitstring, bit-reverse required for proper array lookup
{
	static const int
		LOG_BYTES_PER_UNIT=2,

		LBPU=LOG_BYTES_PER_UNIT+3,
		BPU_MASK=(1<<LBPU)-1,
		UNIT_BYTES=1<<(LBPU-3);

	std::vector<int> data;
	int bitSize;
	vector_bool():bitSize(0){}//default constructor
	vector_bool(vector_bool const &v)=default;
	vector_bool(vector_bool &&v)noexcept
	{
		if(&v!=this)
		{
			data=std::move(v.data);
			bitSize=v.bitSize;
			v.bitSize=0;
		}
	}
	vector_bool& operator=(vector_bool const &v)
	{
		if(&v!=this)
		{
			bitSize=v.bitSize;
			data=v.data;
		}
		return *this;
	}
	vector_bool& operator=(vector_bool &&v)noexcept
  	{
		if(&v!=this)
		{
			bitSize=v.bitSize;
			data=std::move(v.data);
			v.bitSize=0;
		}
		return *this;
	}
	void realloc(int newBitSize)
	{
		assert(newBitSize>=0);
		if(CEIL_UNITS(newBitSize)!=CEIL_UNITS(bitSize))
		{
			int newSize=CEIL_UNITS(newBitSize);
			data.resize(newSize);
		}
	}
	void clear_tail()//call at the end before retrieving bits
	{
		data[(bitSize-1)>>LBPU]&=(1<<(bitSize&BPU_MASK))-1;//container of last bit
	}
	int size_bytes()const
	{
		return BYTE_SIZE(bitSize);
	}
	void set(int bitIdx, bool bit)
	{
		data[bitIdx>>LBPU]|=(bit!=false)<<(bitIdx&BPU_MASK);
	}
	int get(int bitIdx)const
	{
		return data[bitIdx>>LBPU]>>(bitIdx&BPU_MASK)&1;
	}
	void push_back(bool val)//LSB-first
	{
		realloc(bitSize+1);
		int container_idx=bitSize>>LBPU;
		if(container_idx>=(int)data.size())
			realloc(bitSize+1);
		if(bitSize&BPU_MASK)
			data[container_idx]|=(val!=false)<<(bitSize&BPU_MASK);
		else
			data[container_idx]=val!=false;
		++bitSize;
	}
	void push_back(vector_bool const &v)//LSB-first
	{
		if(v.bitSize<1)
			return;
		realloc(bitSize+v.bitSize);
		int unitOffset=bitSize>>LBPU, bitOffset=bitSize&BPU_MASK;

		//print("push_back: v="), v.debug_print(0), print_flush();//
		//print("before:\t"), debug_print(0), print_flush();//
		bitSize+=v.bitSize;
		//static int counter=0;
		//if(counter==5)
		//	int LOL_1=0;
		//++counter;

		int unitCount=v.data.size();
		//int floorUnitCount=v.bitSize>>LBPU;
		if(bitOffset>0&&bitOffset<BPU_MASK+1)
		{
			for(int k=0;k<unitCount;++k)
				data[unitOffset+k]|=v.data[k]<<bitOffset;
			//print("pt1:\t"), debug_print(0), print_flush();//
			for(int k=1;unitOffset+k<(int)data.size()&&k<=unitCount;++k)
				data[unitOffset+k]|=v.data[k-1]>>(BPU_MASK+1-bitOffset);
			//data[unitOffset]|=v.data[0]<<bitOffset;//fill till the end of unit
			//for(int k=1;k<unitCount;++k)//fill the rest units
			//	data[unitOffset+k]=v.data[k]<<bitOffset|v.data[k-1]>>(BPU_MASK-bitOffset);
		}
		else
		{
			for(int k=0;k<unitCount;++k)//assign all
				data[unitOffset+k]=v.data[k];
		}
		//print("after:\t"), debug_print(0), print_flush();//

//#ifdef DEBUG_VEC_BOOL
//		debug_print();
//#endif
	}

#if 1
	void debug_print(int start_bit_idx)const
	{
		for(int kb=0;kb<bitSize;++kb)
		{
			print("%d", get(kb));
			if(((start_bit_idx+kb)&7)==7)
				print("-");
			//	print("_");
			//if((kb&7)==7)
			//	print(" ");
		}
		//if(newlines)
		//	print_flush();
	}
#endif

	//bool operator<(vector_bool const &other)const
	//{
	//	int k=0, size1=data.size(), size2=other.data.size();
	//	for(;k<size1&&k<size2;++k)
	//		if(data[k]!=other.data[k])
	//			return data[k]<other.data[k];
	//}
};
#undef			CEIL_UNITS
#undef			BYTE_SIZE

//debug tools
void			print_alphabet(vector_bool const *alphabet, const int *histogram, int nlevels, int symbols_to_compress, const int *sort_idx)
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
void			sort_alphabet(vector_bool const *alphabet, int nLevels, std::vector<int> &idx)
{
	idx.resize(nLevels);
	for(int k=0;k<nLevels;++k)
		idx[k]=k;
	std::sort(idx.begin(), idx.end(), AlphabetComparator(alphabet));
}


typedef unsigned char byte;
struct			HuffHeader
{
	char HUFF[4];//'H'|'U'<<8|'F'<<16|'F'<<24
	unsigned version;//1
	unsigned width, height;//uncompressed dimensions
	char bayerInfo[4];//'G'|'R'<<8|'B'<<16|'G'<<24 for Galaxy A70
	unsigned nLevels;//1<<bitDepth, also histogram size
	unsigned histogram[];//data begins at histogram+nLevels
};
struct			HuffDataHeader
{
	char DATA[4];//'D'|'A'<<8|'T'<<16|'A'<<24
	unsigned uPxCount;//uncompressed pixel count
	unsigned long long cBitSize;//compressed data size in bits
	unsigned data[];
};

struct Node
{
	Node *branch[2];
	unsigned short value;
	int count;
};
Node*			make_node(int symbol, int count, Node *left, Node *right)//https://gist.github.com/pwxcoo/72d7d3c5c3698371c21e486722f9b34b
{
	Node *n=new Node();
	n->value=symbol, n->count=count;
	n->branch[0]=left, n->branch[1]=right;
	return n;
}
Node*			build_tree(int *histogram, int nLevels)
{
	auto cmp=[](Node* const &a, Node* const &b){return a->count>b->count;};
	std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> pq(cmp);
	for(int k=0;k<nLevels;++k)
		pq.push(make_node(k, histogram[k], nullptr, nullptr));
	while(pq.size()>1)//build Huffman tree
	{
		Node *left=pq.top();	pq.pop();
		Node *right=pq.top();	pq.pop();
		pq.push(make_node(0, left->count+right->count, left, right));
	}
	return pq.top();
}
void			free_tree(Node *root)
{
	if(root->branch[0])
		free_tree(root->branch[0]);
	if(root->branch[1])
		free_tree(root->branch[1]);
	free(root);
}
void			make_alphabet(Node *root, vector_bool *alphabet)
{
	typedef std::pair<Node*, vector_bool> TraverseInfo;
	std::stack<TraverseInfo> s;
	s.push(TraverseInfo(root, vector_bool()));
	vector_bool left, right;
	while(s.size())//depth-first
	{
		auto &info=s.top();
		Node *r2=info.first;
		if(!r2)
		{
			s.pop();
			continue;
		}
		if(!r2->branch[0]&&!r2->branch[1])
		{
			alphabet[r2->value]=std::move(info.second);
			s.pop();
			continue;
		}
		left=std::move(info.second);
		right=left;
		s.pop();
		if(r2->branch[1])
		{
			right.push_back(true);
			s.push(TraverseInfo(r2->branch[1], std::move(right)));
		}
		if(r2->branch[0])
		{
			left.push_back(false);
			s.push(TraverseInfo(r2->branch[0], std::move(left)));
		}
	}
}
void			calculate_histogram(const short *image, int size, int *histogram, int nLevels)
{
	memset(histogram, 0, nLevels*sizeof(int));
	for(int k=0;k<size;++k)
	{
		//if(image[k]>=nLevels)
		//	LOGE("Image [%d] = %d", k, image[k]);
		++histogram[image[k]];
	}
}
static int		compress_huff(const short *buffer, int bw, int bh, int depth, int bayer, std::vector<int> &data)
{
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
	//header->HUFF[0]='H';
	//header->HUFF[1]='U';
	//header->HUFF[2]='F';
	//header->HUFF[3]='F';
	header->version=1;
	header->width=width;
	header->height=height;
	*(int*)header->bayerInfo=bayer;
	header->nLevels=nLevels;

	int *histogram=(int*)((HuffHeader*)data.data())->histogram;
	calculate_histogram(b2, imSize, histogram, nLevels);
#ifdef PRINT_HISTOGRAM
	print_histogram(histogram, nLevels, imSize);
#endif


	Node *root=build_tree(histogram, nLevels);

	std::vector<vector_bool> alphabet(nLevels);
	make_alphabet(root, alphabet.data());
	//auto alphabet=new vector_bool[nLevels];
	//make_alphabet(root, alphabet);

	free_tree(root);

#ifdef PRINT_ALPHABET
	std::vector<int> indices;
	sort_alphabet(alphabet.data(), nLevels, indices);
	//std::sort(alphabet.begin(), alphabet.end(), AlphabetComparator(alphabet.data()));
	print("Original alphabet"), print_flush();
	print_alphabet(alphabet.data(), histogram, nLevels, imSize, nullptr);//
	print("Sorted alphabet"), print_flush();
	print_alphabet(alphabet.data(), histogram, nLevels, imSize, indices.data());//
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
	for(int k=0;k<imSize;++k)
		bits.push_back(alphabet[b2[k]]);
	bits.clear_tail();
#ifdef PRINT_DATA
	print("Concatenated bits:"), print_flush();
	bits.debug_print(0);
#endif
	int data_start=data.size();
	data.resize(data_start+sizeof(HuffDataHeader)+bits.size_bytes()/sizeof(int));
	auto dataHeader=(HuffDataHeader*)(data.data()+data_start);
	*(int*)dataHeader->DATA='D'|'A'<<8|'T'<<16|'A'<<24;
	//dataHeader->DATA[0]='D';
	//dataHeader->DATA[1]='A';
	//dataHeader->DATA[2]='T';
	//dataHeader->DATA[3]='A';
	dataHeader->uPxCount=imSize;
	dataHeader->cBitSize=bits.bitSize;
	memcpy(dataHeader->data, bits.data.data(), bits.size_bytes());
	//memcpy(dataHeader->data, bits.data, bits.size_bytes());

	//delete[] alphabet;
	if(!bayer)
	{
		delete[] temp;
		b2=buffer;
	}
	return data_start;
}
#endif

/*extern "C" jbyteArray Java_com_example_rawcamdemo_CameraFragment_compress(JNIEnv *env, jobject thiz, jbyteArray data, jint width, jint height, jint depth, jint bayer)
{
	auto packedImage=env->GetByteArrayElements(data, nullptr);
	if(!packedImage)
		return nullptr;

	short *image=nullptr;
	if(depth==10)
		image=unpack_r10((byte*)packedImage, width, height);
	else if(depth==12)
		image=unpack_r12((byte*)packedImage, width, height);
	if(!image)
		return nullptr;

	//width=8, height=8;
	//short image[]=
	//{
	//	21, 25,  4,  7, 17, 23,  5, 13,
	//	12, 14, 10, 20,  5, 16, 14,  8,
	//	16, 20,  7, 20, 27, 12, 16,  5,
	//	24, 14, 16,  9,  5, 14, 16, 27,
	//	 5,  1,  7,  8,  8, 11, 16, 12,
	//	13, 17, 19, 21, 22,  8,  6, 14,
	//	19,  3,  5, 20,  5, 22, 22, 19,
	//	27, 24, 19, 24,  7, 12,  3, 16,
	//};
	//width=4, height=4, depth=5;
	//short image[]=
	//{
	//	21, 25,  4,  7,
	//	12, 14, 10, 20,
	//	16, 20,  7, 20,
	//	24, 14, 16,  9,
	//};

	std::vector<int> output;
	huff::compress(image, width, height, depth, bayer, output);
	//compress_huff(image, width, height, depth, bayer, output);
	//delete[] image;

	int byteSize=output.size()*sizeof(int);
	jbyteArray ret=env->NewByteArray(byteSize);
	env->SetByteArrayRegion(ret, 0, byteSize, (jbyte*)output.data());
	return ret;
}
extern "C" jbyteArray Java_com_example_rawcamdemo_CameraFragment_pack_1raw(JNIEnv *env, jobject thiz, jbyteArray data, jint width, jint height, jint depth, jint bayer)
{
	auto packedImage=env->GetByteArrayElements(data, nullptr);
	if(!packedImage)
		return nullptr;

	std::vector<int> output;
	huff::pack_raw((byte*)packedImage, width, height, depth, bayer, output);

	int byteSize=output.size()*sizeof(int);
	jbyteArray ret=env->NewByteArray(byteSize);
	env->SetByteArrayRegion(ret, 0, byteSize, (jbyte*)output.data());
	return ret;
}
extern "C" jbyteArray Java_com_example_rawcamdemo_CameraFragment_pack_1r10g12(JNIEnv *env, jobject thiz, jbyteArray data, jint width, jint height, jint denoise)
{
	auto packedImage=env->GetByteArrayElements(data, nullptr);
	if(!packedImage)
		return nullptr;

	std::vector<int> output;
	huff::pack_r10_g12((byte*)packedImage, width, height, denoise, output);

	int byteSize=output.size()*sizeof(int);
	jbyteArray ret=env->NewByteArray(byteSize);
	env->SetByteArrayRegion(ret, 0, byteSize, (jbyte*)output.data());
	return ret;
}
extern "C" jbyteArray Java_com_example_rawcamdemo_CameraFragment_pack_1r12g14(JNIEnv *env, jobject thiz, jbyteArray data, jint width, jint height, jint denoise)
{
	auto packedImage=env->GetByteArrayElements(data, nullptr);
	if(!packedImage)
		return nullptr;

	std::vector<int> output;
	huff::pack_r12_g14((byte*)packedImage, width, height, denoise, output);

	int byteSize=output.size()*sizeof(int);
	jbyteArray ret=env->NewByteArray(byteSize);
	env->SetByteArrayRegion(ret, 0, byteSize, (jbyte*)output.data());
	return ret;
}//*/
jbyteArray make_java_buffer(JNIEnv *env, const byte *data, int byteSize)
{
	jbyteArray ret=env->NewByteArray(byteSize);
	env->SetByteArrayRegion(ret, 0, byteSize, (jbyte*)data);
	return ret;
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
		break;
	default:
		LOGE("Invalid HUFF version");
		return nullptr;
	}

	auto packedImage=env->GetByteArrayElements(data, nullptr);
	if(!packedImage)
		return nullptr;

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
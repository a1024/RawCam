#ifndef VECTOR_BOOL_H
#define VECTOR_BOOL_H
#include		<vector>
#include		<cassert>

#if __cplusplus<201103
#define			NOEXCEPT
#else
#define			NOEXCEPT	noexcept
#endif
void 			print(const char *format, ...);
void 			print_flush();

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
	vector_bool(vector_bool &&v)NOEXCEPT
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
	vector_bool& operator=(vector_bool &&v)NOEXCEPT
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

	void set_size_factor(int log_unit_factor)//resize to a multiple of 1<<log_unit_factor, (pass 2 for int and 128bit SIMD)
	{
		int unit_factor=1<<log_unit_factor, uf_mask=unit_factor-1;
		int size=data.size();
		data.resize((size&~uf_mask)+(((size&uf_mask)!=0)<<log_unit_factor));
	}
	void debug_print(int start_bit_idx)const
	{
		print("bitSize: %d", bitSize), print_flush();
		for(int kb=0;kb<bitSize;++kb)
		{
			print("%d", get(kb));
			if(((start_bit_idx+kb)&7)==7)
				print("-");
		}
	}
};
#undef			CEIL_UNITS
#undef			BYTE_SIZE
#endif//VECTOR_BOOL_H
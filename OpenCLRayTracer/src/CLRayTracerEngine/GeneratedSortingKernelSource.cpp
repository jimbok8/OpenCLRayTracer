const char* OpenCLKernelSource = 
"// Sort kernels\n"
"// EB Jun 2011\n"
"\n"
"#ifdef CONFIG_USE_VALUE\n"
"typedef uint2 data_t;\n"
"#define getKey(a) ((a).x)\n"
"#define getValue(a) ((a).y)\n"
"#define makeData(k,v) ((uint2)((k),(v)))\n"
"#else\n"
"typedef uint data_t;\n"
"#define getKey(a) (a)\n"
"#define getValue(a) (0)\n"
"#define makeData(k,v) (k)\n"
"#endif\n"
"\n"
"// One thread per record\n"
"__kernel void Copy(__global const data_t * in,__global data_t * out)\n"
"{\n"
"  int i = get_global_id(0); // current thread\n"
"  out[i] = in[i]; // copy\n"
"}\n"
"\n"
"// One thread per record\n"
"__kernel void ParallelSelection(__global const data_t * in,__global data_t * out)\n"
"{\n"
"  int i = get_global_id(0); // current thread\n"
"  int n = get_global_size(0); // input size\n"
"  data_t iData = in[i];\n"
"  uint iKey = getKey(iData);\n"
"  // Compute position of in[i] in output\n"
"  int pos = 0;\n"
"  for (int j=0;j<n;j++)\n"
"  {\n"
"    uint jKey = getKey(in[j]); // broadcasted\n"
"    bool smaller = (jKey < iKey) || (jKey == iKey && j < i); // in[j] < in[i] ?\n"
"    pos += (smaller)?1:0;\n"
"  }\n"
"  out[pos] = iData;\n"
"}\n"
"\n"
"#ifndef BLOCK_FACTOR\n"
"#define BLOCK_FACTOR 1\n"
"#endif\n"
"// One thread per record, local memory size AUX is BLOCK_FACTOR * workgroup size keys\n"
"__kernel void ParallelSelection_Blocks(__global const data_t * in,__global data_t * out,__local uint * aux)\n"
"{\n"
"  int i = get_global_id(0); // current thread\n"
"  int n = get_global_size(0); // input size\n"
"  int wg = get_local_size(0); // workgroup size\n"
"  data_t iData = in[i]; // input record for current thread\n"
"  uint iKey = getKey(iData); // input key for current thread\n"
"  int blockSize = BLOCK_FACTOR * wg; // block size\n"
"\n"
"  // Compute position of iKey in output\n"
"  int pos = 0;\n"
"  // Loop on blocks of size BLOCKSIZE keys (BLOCKSIZE must divide N)\n"
"  for (int j=0;j<n;j+=blockSize)\n"
"  {\n"
"    // Load BLOCKSIZE keys using all threads (BLOCK_FACTOR values per thread)\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    for (int index=get_local_id(0);index<blockSize;index+=wg)\n"
"    {\n"
"      aux[index] = getKey(in[j+index]);\n"
"    }\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"\n"
"    // Loop on all values in AUX\n"
"    for (int index=0;index<blockSize;index++)\n"
"    {\n"
"      uint jKey = aux[index]; // broadcasted, local memory\n"
"      bool smaller = (jKey < iKey) || ( jKey == iKey && (j+index) < i ); // in[j] < in[i] ?\n"
"      pos += (smaller)?1:0;\n"
"    }\n"
"  }\n"
"  out[pos] = iData;\n"
"}\n"
"\n"
"// N threads, WG is workgroup size. Sort WG input blocks in each workgroup.\n"
"__kernel void ParallelSelection_Local(__global const data_t * in,__global data_t * out,__local data_t * aux)\n"
"{\n"
"  int i = get_local_id(0); // index in workgroup\n"
"  int wg = get_local_size(0); // workgroup size = block size\n"
"\n"
"  // Move IN, OUT to block start\n"
"  int offset = get_group_id(0) * wg;\n"
"  in += offset; out += offset;\n"
"\n"
"  // Load block in AUX[WG]\n"
"  data_t iData = in[i];\n"
"  aux[i] = iData;\n"
"  barrier(CLK_LOCAL_MEM_FENCE);\n"
"\n"
"  // Find output position of iData\n"
"  uint iKey = getKey(iData);\n"
"  int pos = 0;\n"
"  for (int j=0;j<wg;j++)\n"
"  {\n"
"    uint jKey = getKey(aux[j]);\n"
"    bool smaller = (jKey < iKey) || ( jKey == iKey && j < i ); // in[j] < in[i] ?\n"
"    pos += (smaller)?1:0;\n"
"  }\n"
"\n"
"  // Store output\n"
"  out[pos] = iData;\n"
"}\n"
"\n"
"// N threads, WG is workgroup size. Sort WG input blocks in each workgroup.\n"
"__kernel void ParallelMerge_Local(__global const data_t * in,__global data_t * out,__local data_t * aux)\n"
"{\n"
"  int i = get_local_id(0); // index in workgroup\n"
"  int wg = get_local_size(0); // workgroup size = block size, power of 2\n"
"\n"
"  // Move IN, OUT to block start\n"
"  int offset = get_group_id(0) * wg;\n"
"  in += offset; out += offset;\n"
"\n"
"  // Load block in AUX[WG]\n"
"  aux[i] = in[i];\n"
"  barrier(CLK_LOCAL_MEM_FENCE); // make sure AUX is entirely up to date\n"
"\n"
"  // Now we will merge sub-sequences of length 1,2,...,WG/2\n"
"  for (int length=1;length<wg;length<<=1)\n"
"  {\n"
"    data_t iData = aux[i];\n"
"    uint iKey = getKey(iData);\n"
"    int ii = i & (length-1);  // index in our sequence in 0..length-1\n"
"    int sibling = (i - ii) ^ length; // beginning of the sibling sequence\n"
"    int pos = 0;\n"
"    for (int inc=length;inc>0;inc>>=1) // increment for dichotomic search\n"
"    {\n"
"      int j = sibling+pos+inc-1;\n"
"      uint jKey = getKey(aux[j]);\n"
"      bool smaller = (jKey < iKey) || ( jKey == iKey && j < i );\n"
"      pos += (smaller)?inc:0;\n"
"      pos = min(pos,length);\n"
"    }\n"
"    int bits = 2*length-1; // mask for destination\n"
"    int dest = ((ii + pos) & bits) | (i & ~bits); // destination index in merged sequence\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    aux[dest] = iData;\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"  }\n"
"\n"
"  // Write output\n"
"  out[i] = aux[i];\n"
"}\n"
"\n"
"// N threads, WG is workgroup size. Sort WG input blocks in each workgroup.\n"
"__kernel void ParallelBitonic_Local(__global const data_t * in,__global data_t * out,__local data_t * aux)\n"
"{\n"
"  int i = get_local_id(0); // index in workgroup\n"
"  int wg = get_local_size(0); // workgroup size = block size, power of 2\n"
"\n"
"  // Move IN, OUT to block start\n"
"  int offset = get_group_id(0) * wg;\n"
"  in += offset; out += offset;\n"
"\n"
"  // Load block in AUX[WG]\n"
"  aux[i] = in[i];\n"
"  barrier(CLK_LOCAL_MEM_FENCE); // make sure AUX is entirely up to date\n"
"\n"
"  // Loop on sorted sequence length\n"
"  for (int length=1;length<wg;length<<=1)\n"
"  {\n"
"    bool direction = ((i & (length<<1)) != 0); // direction of sort: 0=asc, 1=desc\n"
"    // Loop on comparison distance (between keys)\n"
"    for (int inc=length;inc>0;inc>>=1)\n"
"    {\n"
"      int j = i ^ inc; // sibling to compare\n"
"      data_t iData = aux[i];\n"
"      uint iKey = getKey(iData);\n"
"      data_t jData = aux[j];\n"
"      uint jKey = getKey(jData);\n"
"      bool smaller = (jKey < iKey) || ( jKey == iKey && j < i );\n"
"      bool swap = smaller ^ (j < i) ^ direction;\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"      aux[i] = (swap)?jData:iData;\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"  }\n"
"\n"
"  // Write output\n"
"  out[i] = aux[i];\n"
"}\n"
"__kernel void ParallelBitonic_Local_Optim(__global const data_t * in,__global data_t * out,__local data_t * aux)\n"
"{\n"
"  int i = get_local_id(0); // index in workgroup\n"
"  int wg = get_local_size(0); // workgroup size = block size, power of 2\n"
"\n"
"  // Move IN, OUT to block start\n"
"  int offset = get_group_id(0) * wg;\n"
"  in += offset; out += offset;\n"
"\n"
"  // Load block in AUX[WG]\n"
"  data_t iData = in[i];\n"
"  aux[i] = iData;\n"
"  barrier(CLK_LOCAL_MEM_FENCE); // make sure AUX is entirely up to date\n"
"\n"
"  // Loop on sorted sequence length\n"
"  for (int length=1;length<wg;length<<=1)\n"
"  {\n"
"    bool direction = ((i & (length<<1)) != 0); // direction of sort: 0=asc, 1=desc\n"
"    // Loop on comparison distance (between keys)\n"
"    for (int inc=length;inc>0;inc>>=1)\n"
"    {\n"
"      int j = i ^ inc; // sibling to compare\n"
"      data_t jData = aux[j];\n"
"      uint iKey = getKey(iData);\n"
"      uint jKey = getKey(jData);\n"
"      bool smaller = (jKey < iKey) || ( jKey == iKey && j < i );\n"
"      bool swap = smaller ^ (j < i) ^ direction;\n"
"      iData = (swap)?jData:iData; // update iData\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"      aux[i] = iData;\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"  }\n"
"\n"
"  // Write output\n"
"  out[i] = iData;\n"
"}\n"
"\n"
"// N threads\n"
"__kernel void ParallelBitonic_A(__global const data_t * in,__global data_t * out,int inc,int dir)\n"
"{\n"
"  int i = get_global_id(0); // thread index\n"
"  int j = i ^ inc; // sibling to compare\n"
"\n"
"  // Load values at I and J\n"
"  data_t iData = in[i];\n"
"  uint iKey = getKey(iData);\n"
"  data_t jData = in[j];\n"
"  uint jKey = getKey(jData);\n"
"\n"
"  // Compare\n"
"  bool smaller = (jKey < iKey) || ( jKey == iKey && j < i );\n"
"  bool swap = smaller ^ (j < i) ^ ((dir & i) != 0);\n"
"\n"
"  // Store\n"
"  out[i] = (swap)?jData:iData;\n"
"}\n"
"\n"
"// N/2 threads\n"
"__kernel void ParallelBitonic_B_test(__global const data_t * in,__global data_t * out,int inc,int dir)\n"
"{\n"
"  int t = get_global_id(0); // thread index\n"
"  int low = t & (inc - 1); // low order bits (below INC)\n"
"  int i = (t<<1) - low; // insert 0 at position INC\n"
"  int j = i | inc; // insert 1 at position INC\n"
"\n"
"  // Load values at I and J\n"
"  data_t iData = in[i];\n"
"  uint iKey = getKey(iData);\n"
"  data_t jData = in[j];\n"
"  uint jKey = getKey(jData);\n"
"\n"
"  // Compare\n"
"  bool smaller = (jKey < iKey) || ( jKey == iKey && j < i );\n"
"  bool swap = smaller ^ ((dir & i) != 0);\n"
"\n"
"  // Store\n"
"  out[i] = (swap)?jData:iData;\n"
"  out[j] = (swap)?iData:jData;\n"
"}\n"
"\n"
"#define ORDER(a,b) { bool swap = reverse ^ (getKey(a)<getKey(b)); data_t auxa = a; data_t auxb = b; a = (swap)?auxb:auxa; b = (swap)?auxa:auxb; }\n"
"\n"
"// N/2 threads\n"
"__kernel void ParallelBitonic_B2(__global data_t * data,int inc,int dir)\n"
"{\n"
"  int t = get_global_id(0); // thread index\n"
"  int low = t & (inc - 1); // low order bits (below INC)\n"
"  int i = (t<<1) - low; // insert 0 at position INC\n"
"  bool reverse = ((dir & i) == 0); // asc/desc order\n"
"  data += i; // translate to first value\n"
"\n"
"  // Load\n"
"  data_t x0 = data[  0];\n"
"  data_t x1 = data[inc];\n"
"\n"
"  // Sort\n"
"  ORDER(x0,x1)\n"
"\n"
"  // Store\n"
"  data[0  ] = x0;\n"
"  data[inc] = x1;\n"
"}\n"
"\n"
"// N/4 threads\n"
"__kernel void ParallelBitonic_B4(__global data_t * data,int inc,int dir)\n"
"{\n"
"  inc >>= 1;\n"
"  int t = get_global_id(0); // thread index\n"
"  int low = t & (inc - 1); // low order bits (below INC)\n"
"  int i = ((t - low) << 2) + low; // insert 00 at position INC\n"
"  bool reverse = ((dir & i) == 0); // asc/desc order\n"
"  data += i; // translate to first value\n"
"\n"
"  // Load\n"
"  data_t x0 = data[    0];\n"
"  data_t x1 = data[  inc];\n"
"  data_t x2 = data[2*inc];\n"
"  data_t x3 = data[3*inc];\n"
"\n"
"  // Sort\n"
"  ORDER(x0,x2)\n"
"  ORDER(x1,x3)\n"
"  ORDER(x0,x1)\n"
"  ORDER(x2,x3)\n"
"\n"
"  // Store\n"
"  data[    0] = x0;\n"
"  data[  inc] = x1;\n"
"  data[2*inc] = x2;\n"
"  data[3*inc] = x3;\n"
"}\n"
"\n"
"#define ORDERV(x,a,b) { bool swap = reverse ^ (getKey(x[a])<getKey(x[b])); \\\n"
"      data_t auxa = x[a]; data_t auxb = x[b]; \\\n"
"      x[a] = (swap)?auxb:auxa; x[b] = (swap)?auxa:auxb; }\n"
"#define B2V(x,a) { ORDERV(x,a,a+1) }\n"
"#define B4V(x,a) { for (int i4=0;i4<2;i4++) { ORDERV(x,a+i4,a+i4+2) } B2V(x,a) B2V(x,a+2) }\n"
"#define B8V(x,a) { for (int i8=0;i8<4;i8++) { ORDERV(x,a+i8,a+i8+4) } B4V(x,a) B4V(x,a+4) }\n"
"#define B16V(x,a) { for (int i16=0;i16<8;i16++) { ORDERV(x,a+i16,a+i16+8) } B8V(x,a) B8V(x,a+8) }\n"
"\n"
"// N/8 threads\n"
"__kernel void ParallelBitonic_B8(__global data_t * data,int inc,int dir)\n"
"{\n"
"  inc >>= 2;\n"
"  int t = get_global_id(0); // thread index\n"
"  int low = t & (inc - 1); // low order bits (below INC)\n"
"  int i = ((t - low) << 3) + low; // insert 000 at position INC\n"
"  bool reverse = ((dir & i) == 0); // asc/desc order\n"
"  data += i; // translate to first value\n"
"\n"
"  // Load\n"
"  data_t x[8];\n"
"  for (int k=0;k<8;k++) x[k] = data[k*inc];\n"
"\n"
"  // Sort\n"
"  B8V(x,0)\n"
"\n"
"  // Store\n"
"  for (int k=0;k<8;k++) data[k*inc] = x[k];\n"
"}\n"
"\n"
"// N/16 threads\n"
"__kernel void ParallelBitonic_B16(__global data_t * data,int inc,int dir)\n"
"{\n"
"  inc >>= 3;\n"
"  int t = get_global_id(0); // thread index\n"
"  int low = t & (inc - 1); // low order bits (below INC)\n"
"  int i = ((t - low) << 4) + low; // insert 0000 at position INC\n"
"  bool reverse = ((dir & i) == 0); // asc/desc order\n"
"  data += i; // translate to first value\n"
"\n"
"  // Load\n"
"  data_t x[16];\n"
"  for (int k=0;k<16;k++) x[k] = data[k*inc];\n"
"\n"
"  // Sort\n"
"  B16V(x,0)\n"
"\n"
"  // Store\n"
"  for (int k=0;k<16;k++) data[k*inc] = x[k];\n"
"}\n"
"\n"
"// N/2 threads, AUX[2*WG]\n"
"__kernel void ParallelBitonic_C2_pre(__global data_t * data,int inc,int dir,__local data_t * aux)\n"
"{\n"
"  int t = get_global_id(0); // thread index\n"
"\n"
"  // Terminate the INC loop inside the workgroup\n"
"  for ( ;inc>0;inc>>=1)\n"
"  {\n"
"    int low = t & (inc - 1); // low order bits (below INC)\n"
"    int i = (t<<1) - low; // insert 0 at position INC\n"
"    bool reverse = ((dir & i) == 0); // asc/desc order\n"
"\n"
"    barrier(CLK_GLOBAL_MEM_FENCE);\n"
"\n"
"    // Load\n"
"    data_t x0 = data[i];\n"
"    data_t x1 = data[i+inc];\n"
"\n"
"    // Sort\n"
"    ORDER(x0,x1)\n"
"\n"
"    barrier(CLK_GLOBAL_MEM_FENCE);\n"
"\n"
"    // Store\n"
"    data[i] = x0;\n"
"    data[i+inc] = x1;\n"
"  }\n"
"}\n"
"\n"
"// N/2 threads, AUX[2*WG]\n"
"__kernel void ParallelBitonic_C2(__global data_t * data,int inc0,int dir,__local data_t * aux)\n"
"{\n"
"  int t = get_global_id(0); // thread index\n"
"  int wgBits = 2*get_local_size(0) - 1; // bit mask to get index in local memory AUX (size is 2*WG)\n"
"\n"
"  for (int inc=inc0;inc>0;inc>>=1)\n"
"  {\n"
"    int low = t & (inc - 1); // low order bits (below INC)\n"
"    int i = (t<<1) - low; // insert 0 at position INC\n"
"    bool reverse = ((dir & i) == 0); // asc/desc order\n"
"    data_t x0,x1;\n"
"\n"
"    // Load\n"
"    if (inc == inc0)\n"
"    {\n"
"      // First iteration: load from global memory\n"
"      x0 = data[i];\n"
"      x1 = data[i+inc];\n"
"    }\n"
"    else\n"
"    {\n"
"      // Other iterations: load from local memory\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"      x0 = aux[i & wgBits];\n"
"      x1 = aux[(i+inc) & wgBits];\n"
"    }\n"
"\n"
"    // Sort\n"
"    ORDER(x0,x1)\n"
"\n"
"    // Store\n"
"    if (inc == 1)\n"
"    {\n"
"      // Last iteration: store to global memory\n"
"      data[i] = x0;\n"
"      data[i+inc] = x1;\n"
"    }\n"
"    else\n"
"    {\n"
"      // Other iterations: store to local memory\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"      aux[i & wgBits] = x0;\n"
"      aux[(i+inc) & wgBits] = x1;\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"// N/4 threads, AUX[4*WG]\n"
"__kernel void ParallelBitonic_C4_0(__global data_t * data,int inc0,int dir,__local data_t * aux)\n"
"{\n"
"  int t = get_global_id(0); // thread index\n"
"  int wgBits = 4*get_local_size(0) - 1; // bit mask to get index in local memory AUX (size is 4*WG)\n"
"\n"
"  for (int inc=inc0>>1;inc>0;inc>>=2)\n"
"  {\n"
"    int low = t & (inc - 1); // low order bits (below INC)\n"
"    int i = ((t - low) << 2) + low; // insert 00 at position INC\n"
"    bool reverse = ((dir & i) == 0); // asc/desc order\n"
"    data_t x[4];\n"
"    \n"
"    // Load\n"
"    if (inc == inc0>>1)\n"
"    {\n"
"      // First iteration: load from global memory\n"
"      for (int k=0;k<4;k++) x[k] = data[i+k*inc];\n"
"    }\n"
"    else\n"
"    {\n"
"      // Other iterations: load from local memory\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"      for (int k=0;k<4;k++) x[k] = aux[(i+k*inc) & wgBits];\n"
"    }\n"
"\n"
"    // Sort\n"
"    B4V(x,0);\n"
"\n"
"    // Store\n"
"    if (inc == 1)\n"
"    {\n"
"      // Last iteration: store to global memory\n"
"      for (int k=0;k<4;k++) data[i+k*inc] = x[k];\n"
"    }\n"
"    else\n"
"    {\n"
"      // Other iterations: store to local memory\n"
"      barrier(CLK_LOCAL_MEM_FENCE);\n"
"      for (int k=0;k<4;k++) aux[(i+k*inc) & wgBits] = x[k];\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"__kernel void ParallelBitonic_C4(__global data_t * data,int inc0,int dir,__local data_t * aux)\n"
"{\n"
"  int t = get_global_id(0); // thread index\n"
"  int wgBits = 4*get_local_size(0) - 1; // bit mask to get index in local memory AUX (size is 4*WG)\n"
"  int inc,low,i;\n"
"  bool reverse;\n"
"  data_t x[4];\n"
"\n"
"  // First iteration, global input, local output\n"
"  inc = inc0>>1;\n"
"  low = t & (inc - 1); // low order bits (below INC)\n"
"  i = ((t - low) << 2) + low; // insert 00 at position INC\n"
"  reverse = ((dir & i) == 0); // asc/desc order\n"
"  for (int k=0;k<4;k++) x[k] = data[i+k*inc];\n"
"  B4V(x,0);\n"
"  for (int k=0;k<4;k++) aux[(i+k*inc) & wgBits] = x[k];\n"
"  barrier(CLK_LOCAL_MEM_FENCE);\n"
"\n"
"  // Internal iterations, local input and output\n"
"  for ( ;inc>1;inc>>=2)\n"
"  {\n"
"    low = t & (inc - 1); // low order bits (below INC)\n"
"    i = ((t - low) << 2) + low; // insert 00 at position INC\n"
"    reverse = ((dir & i) == 0); // asc/desc order\n"
"    for (int k=0;k<4;k++) x[k] = aux[(i+k*inc) & wgBits];\n"
"    B4V(x,0);\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    for (int k=0;k<4;k++) aux[(i+k*inc) & wgBits] = x[k];\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"  }\n"
"\n"
"  // Final iteration, local input, global output, INC=1\n"
"  i = t << 2;\n"
"  reverse = ((dir & i) == 0); // asc/desc order\n"
"  for (int k=0;k<4;k++) x[k] = aux[(i+k) & wgBits];\n"
"  B4V(x,0);\n"
"  for (int k=0;k<4;k++) data[i+k] = x[k];\n"
"}\n"
;
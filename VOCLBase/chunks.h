

class chunk{
public:
    int memId;
    uint* chunkMem;
    cl_mem chunkCLMem;
    int MemLength;
    int LodLevel;
    chunk();
};
chunk::chunk(){
    MemLength = 0;
}

class world{
public:
    int Seed;
    //1 Block 10cm^3 1000Blocks 1m^3
    int ChunkSize[3];
    int ChunkSizeCube;
    //1 Chunk 6.4m^3
    int NoiseSize;
    int NoiseSize3;
    int NoiseCount;
    //Rendering
    cl_kernel chunkInitKernel1;
    cl_kernel chunkInitKernel2;
    cl_kernel chunkMemCpyKernel;
    cl_kernel chunkScanKernel;
    
    float* RandomNumbers;
    cl_mem RandomNumberCLMem;
    uint* tmpChunkData;
    cl_mem tmpChunkDataCLMem;
    size_t tmpChunkDataSize;
    uint* tmpScanData;
    size_t tmpScanDataSize;
    cl_mem tmpScanDataCLMem;
    uint* tmpScanLengthData;
    cl_mem tmpScanLengthDataCLMem;
    size_t tmpScanLengthDataSize;
    int tmpDataSize;
    int tmpDataSizeCube;
    int MaxChunkSize;
    world();
};

world::world(){
    Seed = 1;
    ChunkSize[0] = 128;
    ChunkSize[1] = 128;
    ChunkSize[2] = 128;
    ChunkSizeCube = (ChunkSize[0]*ChunkSize[1]*ChunkSize[2]);
    NoiseSize = 64;
    NoiseSize3 = NoiseSize*NoiseSize*NoiseSize;
    NoiseCount = 8;
    MaxChunkSize = 256;// -> 1024
}


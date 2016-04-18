#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <images.h>
#include <glew.h>
#include <glfw3.h>

#include <shader.hpp>


#include <OpenCL/cl_gl.h>
#define GL_SHARING_EXTENSION "cl_APPLE_gl_sharing"

#include <OpenGL/OpenGL.h>

#ifdef __APPLE__
	#include "OpenCL/opencl.h"
#else
	#include "CL/cl.h"
#endif

#include <chunks.h>
#include <camera.h>
#include <math.h>

chunk activeChunk;
world aworld;
cl_context context;
cl_kernel kernel;
cl_kernel chunkInitKernel1;
cl_command_queue queue;
cl_program program;

Camera mainCamera;

int width = 1024;
int height = 768;
std::string GetPlatformName (cl_platform_id id)
{
	size_t size = 0;
	clGetPlatformInfo (id, CL_PLATFORM_NAME, 0, nullptr, &size);

	std::string result;
	result.resize (size);
	clGetPlatformInfo (id, CL_PLATFORM_NAME, size,
		const_cast<char*> (result.data ()), nullptr);

	return result;
}

std::string GetDeviceName (cl_device_id id)
{
	size_t size = 0;
	clGetDeviceInfo (id, CL_DEVICE_NAME, 0, nullptr, &size);

	std::string result;
	result.resize (size);
	clGetDeviceInfo (id, CL_DEVICE_NAME, size,
		const_cast<char*> (result.data ()), nullptr);

	return result;
}

void CheckError (cl_int error, int line)
{
	if (error != CL_SUCCESS) {
		std::cerr << "OpenCL call failed with error " << error << " at " << line << std::endl;
	}
}
void CheckErrorGlew (GLenum error, int line)
{
    if (error != 0) {
        std::cerr << "Glew call failed with error " << gluErrorString(error) << " at " << line << std::endl;
    }
    
}
void CheckErrorCL (cl_int error, int line)
{
    if (error != 0) {
        std::cerr << "OpenCL call failed with error " << clgetErrorString(error) << " at " << line << std::endl;
    }
    
}


std::string LoadKernel (const char* name)
{
	std::ifstream in (name);
	std::string result (
		(std::istreambuf_iterator<char> (in)),
		std::istreambuf_iterator<char> ());
	return result;
}

cl_program CreateProgram (const std::string& source,
	cl_context context)
{
	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clCreateProgramWithSource.html
	size_t lengths [1] = { source.size () };
	const char* sources [1] = { source.data () };

	cl_int error = 0;
	cl_program program = clCreateProgramWithSource (context, 1, sources, lengths, &error);
	CheckError (error,__LINE__);

	return program;
}

inline bool exists_test (const std::string& name) {
    std::ifstream f(name.c_str());
    if (f.good()) {
        f.close();
        return true;
    } else {
        f.close();
        return false;
    }
}

bool createRNG(){
    aworld.RandomNumbers = new float[aworld.NoiseSize3*aworld.NoiseCount];
    cl_int error;
    
    aworld.RandomNumberCLMem = clCreateBuffer (context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                    sizeof (float) * aworld.NoiseSize3*aworld.NoiseCount, aworld.RandomNumbers, &error);
    
    CheckErrorCL(clSetKernelArg (kernel, 0, sizeof (cl_mem), &aworld.RandomNumberCLMem),__LINE__);
    CheckErrorCL(clSetKernelArg (kernel, 1, sizeof (int), &aworld.Seed),__LINE__);
    
    std::size_t glob_size [3] = { aworld.NoiseSize*2, aworld.NoiseSize*2, aworld.NoiseSize*2 };//2 -> 8
    clFinish(queue);
    CheckErrorCL (clEnqueueNDRangeKernel (queue, kernel, 3, nullptr, glob_size, nullptr,
                                          0, nullptr, nullptr),__LINE__);
    
    return true;
}

int toCube(int n){
    return (int)ceil((float)n/(float)aworld.MaxChunkSize)*aworld.MaxChunkSize;
}

bool setupChunkMem(){
    int i = (int)(aworld.ChunkSize[0]*aworld.ChunkSize[1]*aworld.ChunkSize[2]);
    int tmpDataSize = 0;
    while(i > 0){
        tmpDataSize += i;
        i = i>>3;
    }
    
    aworld.tmpDataSize = tmpDataSize;
    aworld.tmpDataSizeCube = toCube(tmpDataSize);
    
    std::cout << "dsize cubed " << aworld.tmpDataSizeCube << std::endl;
    
    cl_int error;
    
    aworld.tmpChunkData = new uint[aworld.tmpDataSizeCube];
    aworld.tmpChunkDataCLMem = clCreateBuffer (context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                               sizeof (uint) * aworld.tmpDataSizeCube, aworld.tmpChunkData, &error);
    aworld.tmpChunkDataSize = aworld.tmpDataSizeCube;
    
    aworld.tmpScanData = new uint[aworld.tmpDataSizeCube];
    aworld.tmpScanDataCLMem = clCreateBuffer (context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                               sizeof (uint) * aworld.tmpDataSizeCube, aworld.tmpScanData, &error);
    aworld.tmpScanDataSize = aworld.tmpDataSizeCube;
    
    aworld.tmpScanLengthData = new uint[aworld.tmpDataSizeCube/aworld.MaxChunkSize];
    aworld.tmpScanLengthDataCLMem = clCreateBuffer (context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                              sizeof (uint) * aworld.tmpDataSizeCube/aworld.MaxChunkSize, aworld.tmpScanLengthData, &error);
    aworld.tmpScanLengthDataSize = aworld.tmpDataSizeCube/aworld.MaxChunkSize;
    return true;
}

void SetKernelArgMem(cl_kernel k,int argIdx,cl_mem mem){
    CheckErrorCL(clSetKernelArg (k, argIdx, sizeof (cl_mem), &mem),__LINE__);
}

void SetKernelArgValue(cl_kernel k,int argIdx,int mem,size_t size){
    CheckErrorCL(clSetKernelArg (k, argIdx, size, &mem),__LINE__);
}

void SetKernelArgLocalMem (cl_kernel k,int argIdx,size_t size){
    CheckErrorCL(clSetKernelArg(k, argIdx, size, NULL),__LINE__);
}

/*extern "C" void EXPORT_API MemCpy_ClientToHost (int memId){
    error=clEnqueueReadBuffer(cq, clMemory[memId].clientMemPtr, CL_TRUE, 0, clMemory[memId].dsize*clMemory[memId].size, clMemory[memId].memPtr, 0, NULL, NULL);//TODO:Hardcoded int
    CheckError(error,  __LINE__);
}*/

void writeOut(uint *data,size_t length){
    
    std::cout << "file length: " << length << std::endl;
    
    FILE *fp = fopen("/Users/sebastian/Coding/VOCl/VOCLBase/output.bin","wb");
    
    for(int i = 0; i< length; i++){
        // We are just storing the indices, so value at i is equal to i
        uint f = data[i];
        fwrite(&f, sizeof(uint), 1, fp);
    }
    //int err = fwrite(&data,sizeof(uint),length,fp);
    //std::cout << "bytes written: " << err << std::endl;

    fclose(fp);
}

void initChunk(chunk ac){
    if(aworld.chunkInitKernel1 == nullptr){
        cl_int errCL;
        aworld.chunkInitKernel1 = clCreateKernel (program, "chunkInit1", &errCL);
        CheckErrorCL(errCL,__LINE__);
        aworld.chunkInitKernel2 = clCreateKernel (program, "chunkInit2", &errCL);
        CheckErrorCL(errCL,__LINE__);
        aworld.chunkScanKernel= clCreateKernel (program, "BScan", &errCL);
        CheckErrorCL(errCL,__LINE__);
        aworld.chunkMemCpyKernel= clCreateKernel (program, "chunkMemCpy", &errCL);
        CheckErrorCL(errCL,__LINE__);
    }
    
    //ChunkInit
    SetKernelArgMem(aworld.chunkInitKernel1,0,aworld.tmpChunkDataCLMem);
    SetKernelArgMem(aworld.chunkInitKernel1,1,aworld.RandomNumberCLMem);
    SetKernelArgValue(aworld.chunkInitKernel1,2,aworld.NoiseCount,sizeof(int));
    SetKernelArgValue(aworld.chunkInitKernel1,3,aworld.NoiseSize,sizeof(int));
    SetKernelArgValue(aworld.chunkInitKernel1,4,(int)(0),sizeof(int));
    SetKernelArgValue(aworld.chunkInitKernel1,5,(int)(0),sizeof(int));
    SetKernelArgValue(aworld.chunkInitKernel1,6,(int)(0),sizeof(int));
    
    
    std::size_t glob_size [3] = { aworld.ChunkSize[0]>>1, aworld.ChunkSize[1]>>1, aworld.ChunkSize[2]>>1 };//2 -> 8
    std::size_t local_size [3] = { 4,4,4 };//todo: MaxWorkGroupSize 4->8
    //std::cout << "Global size:" << glob_size[0] << "   local size: " << local_size[0] <<  std::endl;
    clFinish(queue);
    CheckErrorCL (clEnqueueNDRangeKernel (queue, aworld.chunkInitKernel1, 3, nullptr, glob_size, local_size,
                                          0, nullptr, nullptr),__LINE__);
    
    
     clEnqueueReadBuffer(queue, aworld.tmpChunkDataCLMem, CL_TRUE, 0, sizeof(uint)*aworld.tmpChunkDataSize, aworld.tmpChunkData, 0, NULL, NULL);//TODO:Needed?
    clFinish(queue);
    //std::cout << "some random chunk data: " << aworld.tmpChunkData[2097153] << std::endl;
    //writeOut(aworld.tmpChunkData,aworld.tmpChunkDataSize);
    
    int AbsSize = (int)(aworld.ChunkSize[0]*aworld.ChunkSize[1]*aworld.ChunkSize[2]);
    SetKernelArgMem(aworld.chunkInitKernel2,0,aworld.tmpChunkDataCLMem);
    int dispachSize = AbsSize;
    int dspchSize = 0;
    //Debug.Log("Abssize : "+AbsSize);
    for(dspchSize = (int)(AbsSize)>>16; dspchSize>0; dspchSize>>=1){
        //Debug.Log("i ="+i+"; dispachSize ="+dispachSize+";");
        SetKernelArgValue(aworld.chunkInitKernel2,1,dispachSize,sizeof(int));
        glob_size[0] = (int)(dspchSize);
        glob_size[1] = (int)(dspchSize);
        glob_size[2] = (int)(dspchSize);
        
        clFinish(queue);
        CheckErrorCL (clEnqueueNDRangeKernel (queue, aworld.chunkInitKernel2, 3, nullptr, glob_size, nullptr, 0, nullptr, nullptr),__LINE__);
        dispachSize += dspchSize*dspchSize*dspchSize*8;
    }
    
    clEnqueueReadBuffer(queue, aworld.tmpChunkDataCLMem, CL_TRUE, 0, sizeof(uint)*aworld.tmpChunkDataSize, aworld.tmpChunkData, 0, NULL, NULL);
    clFinish(queue);
    
   // writeOut(aworld.tmpChunkData,aworld.tmpChunkDataSize);
    
    //CPU finsih
    
    uint a,b,c,d,e,f,g,h;
    int x,y,z,id;
    
    for(int i=dspchSize;i>=0; i>>=1){
        for(int j = 0; j < (i<<6); j++){
            
            
            x = j % i;
            y = (j / i) % i;
            z = j / (i * i);
            
            id = j*8;
            
            a = aworld.tmpChunkData[dispachSize+id  ];
            b = aworld.tmpChunkData[dispachSize+id+1];
            c = aworld.tmpChunkData[dispachSize+id+2];
            d = aworld.tmpChunkData[dispachSize+id+3];
            e = aworld.tmpChunkData[dispachSize+id+4];
            f = aworld.tmpChunkData[dispachSize+id+5];
            g = aworld.tmpChunkData[dispachSize+id+6];
            h = aworld.tmpChunkData[dispachSize+id+7];
            
            if(a == b &&a == c &&a == d &&a == e &&a == f &&a == g &&a == h&&
               (a&0x40000000)!=0){
                aworld.tmpChunkData[dispachSize+id  ] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+id+1] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+id+2] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+id+3] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+id+4] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+id+5] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+id+6] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+id+7] = (int) 0x00000000;
                aworld.tmpChunkData[dispachSize+(i<<3)+
                                    (int)(x/2.0f)*8+x%2+
                                    ((int)(y/2.0f)*8)*(i>>1)+y%2+
                                    ((int)(z/2.0f)*8)*((i*i)>>2)+z%2] = (0xC0000000)|(0x00FFFFFF&a);
            }else{
                //#if UseAverage
                // tmpData[Off+(get_global_size(0)<<3)+global_id] =(int) (0x80000000)|(0x00FFFFFF&(average(a,b,c,d,e,f,g,h)));
                //  #else
                //  tmpData[Off+(get_global_size(0)<<3)+global_id] =(int) (0x80000000)|(0x00FFFFFF&a);//TODO: instead of "a" average of "a->h"?
                //#endif
                
                aworld.tmpChunkData[dispachSize+(i<<3)+
                                    (int)(x/2.0f)*8+x%2+
                                    ((int)(y/2.0f)*8)*(i>>1)+y%2+
                                    ((int)(z/2.0f)*8)*((i*i)>>2)+z%2] = (uint) ((0x80000000)|(0x00FFFFFF&(dispachSize+id)));
            }
        }
        dispachSize += i<<12;
        i = i == 0 ? -1 : i;
    }
    
    clEnqueueWriteBuffer(queue, aworld.tmpChunkDataCLMem, CL_TRUE, 0, sizeof(uint)*aworld.tmpChunkDataSize, aworld.tmpChunkData, 0, NULL, NULL);
    clFinish(queue);
    
    //writeOut(aworld.tmpChunkData,aworld.tmpChunkDataSize);
    //std::cout << "some random chunk data: " << aworld.tmpChunkData[2396743] << std::endl;
    
    //std::cout << aworld.tmpScanDataSize << "<- scan data length" << std::endl;
    
    //Dispatch chunkscan
    
    SetKernelArgMem(aworld.chunkScanKernel,0,aworld.tmpChunkDataCLMem);
    SetKernelArgMem(aworld.chunkScanKernel,1,aworld.tmpScanDataCLMem);
    SetKernelArgLocalMem(aworld.chunkScanKernel,2,(uint)(aworld.MaxChunkSize)*sizeof(int));//TODO:Hardcoded Max Work Size
    SetKernelArgValue(aworld.chunkScanKernel,3,aworld.MaxChunkSize,sizeof(int));
    SetKernelArgValue(aworld.chunkScanKernel,4,aworld.tmpDataSize,sizeof(int));
    SetKernelArgMem(aworld.chunkScanKernel,5,aworld.tmpScanLengthDataCLMem);
    glob_size[0] = (int)aworld.tmpDataSizeCube>>1;
    glob_size[1] = (int)1;
    glob_size[2] = (int)1;
    local_size [0] = (int)aworld.MaxChunkSize>>1;
    local_size [1] = (int)1;
    local_size [2] = (int)1;
    //t = Time.realtimeSinceStartup;
    
    CheckErrorCL (clEnqueueNDRangeKernel (queue, aworld.chunkScanKernel, 1, nullptr, glob_size, local_size, 0, nullptr, nullptr),__LINE__);
    
    clEnqueueReadBuffer(queue, aworld.tmpScanLengthDataCLMem, CL_TRUE, 0, sizeof(uint)*aworld.tmpScanLengthDataSize, aworld.tmpScanLengthData, 0, NULL, NULL);
    clFinish(queue);
    
    /*clEnqueueReadBuffer(queue, aworld.tmpScanDataCLMem, CL_TRUE, 0, sizeof(uint)*aworld.tmpScanDataSize, aworld.tmpScanData, 0, NULL, NULL);
    clFinish(queue);
    
    writeOut(aworld.tmpScanData,aworld.tmpScanDataSize);*/
    
    
    int dChunkSize = aworld.tmpDataSizeCube/aworld.MaxChunkSize;
    //CL_Handler.tmpScanLengthData[0] --;
    uint tmpFirst = 0;
    uint tmpSecond = 0;
    for(int iter = 0; iter < dChunkSize; iter++){
        ac.MemLength += (int)aworld.tmpScanLengthData[iter];
        //Debug.Log("Scan Data "+(int)CL_Handler.tmpScanLengthData[iter]);
        if(iter>0){
            tmpSecond = aworld.tmpScanLengthData[iter];
            aworld.tmpScanLengthData[iter] = tmpFirst;
            tmpFirst = tmpSecond+tmpFirst;
        }else{
            tmpFirst = aworld.tmpScanLengthData[0];
            aworld.tmpScanLengthData[0] = 0;
        }
        //Debug.Log(CL_Handler.tmpScanLengthData[iter]);
        //std::cout << aworld.tmpScanLengthData[iter] << " : Scan Length" << std::endl;
    }
    std::cout << ac.MemLength << " : file length at line:" << __LINE__ << std::endl;
    //Debug.Log("Memory Length: "+MemLength);
    //t = Time.realtimeSinceStartup;
    clEnqueueWriteBuffer(queue, aworld.tmpScanLengthDataCLMem, CL_TRUE, 0, sizeof(uint) * aworld.tmpScanLengthDataSize, aworld.tmpChunkData, 0, NULL, NULL);
    clFinish(queue);
    cl_int error;
    ac.chunkMem = new uint[ac.MemLength];
    ac.chunkCLMem = clCreateBuffer (context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                    sizeof (uint) * ac.MemLength, ac.chunkMem, &error);
    
    SetKernelArgMem(aworld.chunkMemCpyKernel,0,aworld.tmpChunkDataCLMem);
    SetKernelArgMem(aworld.chunkMemCpyKernel,1,aworld.tmpScanDataCLMem);
    SetKernelArgMem(aworld.chunkMemCpyKernel,2,ac.chunkCLMem);
    SetKernelArgMem(aworld.chunkMemCpyKernel,3,aworld.tmpScanLengthDataCLMem);
    SetKernelArgValue(aworld.chunkMemCpyKernel,4,aworld.MaxChunkSize,sizeof(int));
    
    glob_size[0] = (int)aworld.tmpDataSizeCube;
    //Debug.Log(CL_Handler.tmpDataSizeCube);
    CheckErrorCL (clEnqueueNDRangeKernel (queue, aworld.chunkMemCpyKernel, 1, nullptr, glob_size, nullptr, 0, nullptr, nullptr),__LINE__);
    
    
    clEnqueueReadBuffer(queue, ac.chunkCLMem, CL_TRUE, 0, sizeof(uint)*ac.MemLength, ac.chunkMem, 0, NULL, NULL);
    clFinish(queue);
    writeOut(ac.chunkMem,ac.MemLength);
    std::cout << ac.MemLength << " memory size." << std::endl;
    
}

int main ()
{
    
    if( !glfwInit() )
    {
        fprintf( stderr, "Failed to initialize GLFW\n" );
        return -1;
    }
    
    glfwWindowHint(GLFW_SAMPLES, 4); // 4x antialiasing
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // We want OpenGL 3.3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // To make MacOS happy; should not be needed
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); //We don't want the old OpenGL
    
    // Open a window and create its OpenGL context
    GLFWwindow* window; // (In the accompanying source code, this variable is global)
    window = glfwCreateWindow( 1024, 768, "VOCL", NULL, NULL);
    if( window == NULL ){
        fprintf( stderr, "Failed to open GLFW window. If you have an Intel GPU, they are not 3.3 compatible.\n" );
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window); // Initialize GLEW
    
    glewExperimental = true;
    GLenum errGlew = glewInit();
    if (GLEW_OK != errGlew)
    {
        std::cerr << "Glew init failed :" << glewGetErrorString(errGlew) << " at " << std::endl;
    }
    glGetError();
     CheckError(glGetError(), __LINE__);
    
    
	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clGetPlatformIDs.html
	cl_uint platformIdCount = 0;
	clGetPlatformIDs (0, nullptr, &platformIdCount);

	if (platformIdCount == 0) {
		std::cerr << "No OpenCL platform found" << std::endl;
		return 1;
	} else {
		std::cout << "Found " << platformIdCount << " platform(s)" << std::endl;
	}

	std::vector<cl_platform_id> platformIds (platformIdCount);
	clGetPlatformIDs (platformIdCount, platformIds.data (), nullptr);

	for (cl_uint i = 0; i < platformIdCount; ++i) {
		std::cout << "\t (" << (i+1) << ") : " << GetPlatformName (platformIds [i]) << std::endl;
	}

	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clGetDeviceIDs.html
	cl_uint deviceIdCount = 0;
	clGetDeviceIDs (platformIds [0], CL_DEVICE_TYPE_GPU, 0, nullptr,
		&deviceIdCount);

	if (deviceIdCount == 0) {
		std::cerr << "No OpenCL devices found" << std::endl;
		return 1;
	} else {
		std::cout << "Found " << deviceIdCount << " device(s)" << std::endl;
	}

	std::vector<cl_device_id> deviceIds (deviceIdCount);
	clGetDeviceIDs (platformIds [0], CL_DEVICE_TYPE_GPU, deviceIdCount,
		deviceIds.data (), nullptr);

	for (cl_uint i = 0; i < deviceIdCount; ++i) {
        size_t maxwgs = 0;
        clGetDeviceInfo(	deviceIds [i],
                               CL_DEVICE_MAX_WORK_GROUP_SIZE,
                               sizeof(size_t),
                               &maxwgs,
                               nullptr);
		std::cout << "\t (" << (i+1) << ") : " << GetDeviceName (deviceIds [i]) << "  MaxWorkGroupSize "<< maxwgs << std::endl;
	}
    CGLContextObj kCGLContext = CGLGetCurrentContext();
    CGLShareGroupObj kCGLShareGroup = CGLGetShareGroup(kCGLContext);
    
    cl_context_properties contextProperties [] = {
        CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
        (cl_context_properties)kCGLShareGroup, 0
    };

	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clCreateContext.html
	/*const cl_context_properties contextProperties [] =
	{
		CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties> (platformIds [0]),
		0, 0
	};*/

	cl_int error = CL_SUCCESS;
	context = clCreateContext (contextProperties, deviceIdCount,
		deviceIds.data (), nullptr, nullptr, &error);
	CheckErrorCL (error,__LINE__);
    
    /*cl_int error = CL_SUCCESS;
    context = clCreateContext (contextProperties, deviceIdCount,
                               deviceIds.data (), nullptr, nullptr, &error);
    CheckError (error,__LINE__);*/

	std::cout << "Context created" << std::endl;
    
    
    /*CGLContextObj cgl_context = CGLGetCurrentContext();
    CGLShareGroupObj sharegroup = CGLGetShareGroup(cgl_context);
    gcl_gl_set_sharegroup(sharegroup);*/

    
    //GLTexture creation
    
    GLuint frameBufferTextureId = 0;
    /*glGenTextures(1, &frameBufferTextureId);
    //binnding the texture
    glBindTexture(GL_TEXTURE_2D, frameBufferTextureId);
    //regular sampler params
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    //specify texture dimensions, format etc
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 768, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024, 768, 0, GL_RGBA, GL_FLOAT, tmp_data);
    
    CheckError(glGetError(), __LINE__);
    
    //GL to CL Texture
    cl_mem clMemImg = gcl_gl_create_image_from_texture(GL_TEXTURE_2D,0,frameBufferTextureId);
    std::cout << clMemImg << std::endl;
    CheckError(glGetError(), __LINE__);*/
	unsigned char * tmp_data = new unsigned char [width*height*4];
    for(int i = 0; i < width*height*4;i++){
        tmp_data[i] = 255;
    }
    glGenTextures(1, &frameBufferTextureId);
    
    glBindTexture(GL_TEXTURE_2D, frameBufferTextureId);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024, 768, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp_data);
    
    CheckError(glGetError(),  __LINE__);
    //glBindTexture(GL_TEXTURE_2D, 0);
    
    glFinish();
    
    cl_mem clMemImg = clCreateFromGLTexture(context, CL_MEM_WRITE_ONLY, GL_TEXTURE_2D, 0,frameBufferTextureId,&error);
    
    CheckErrorCL(error,  __LINE__);

	// Create a program from source
	program = CreateProgram (LoadKernel ("/Users/sebastian/Coding/VOCl/VOCLBase/kernels/image.cl"),
		context);
    if(exists_test("/Users/sebastian/Coding/VOCl/VOCLBase/kernels/image.cl")){
        std::cout << "Kernel found." << std::endl;
    }else{
        std::cout << "Kernel not found." << std::endl;
    }
    cl_int err = clBuildProgram (program, deviceIdCount, deviceIds.data (),
                            "-D FILTER_SIZE=1", nullptr, nullptr);
	CheckError (err,__LINE__);
    
    if (err == CL_BUILD_PROGRAM_FAILURE) {
        // Determine the size of the log
        size_t log_size;
        clGetProgramBuildInfo(program, deviceIds[0], CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        
        // Allocate memory for the log
        char *log = (char *) malloc(log_size);
        
        // Get the log
        clGetProgramBuildInfo(program, deviceIds[0], CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        
        std::cerr << "OpenCL build failed, compiler log: \n" << log << std::endl;
        std::exit (1);
        // Print the log
        //printf("%s\n", log);
    }

	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clCreateKernel.html
	kernel = clCreateKernel (program, "random_number_kernel", &error);
	CheckError (error,__LINE__);
    // http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clCreateCommandQueue.html
    queue = clCreateCommandQueue (context, deviceIds [0],
                                                   0, &error);
    CheckError (error,__LINE__);
    
    GLuint VertexArrayID;
    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);
    
    // Create and compile our GLSL program from the shaders
    GLuint programID = LoadShaders( "/Users/sebastian/Coding/VOCl/VOCLBase/SimpleVertexShader.vertexshader", "/Users/sebastian/Coding/VOCl/VOCLBase/SimpleFragmentShader.fragmentshader" );
    
    static const GLfloat g_vertex_buffer_data[] = {
        -1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        -1.0f,  -1.0f, 0.0f,
        
        -1.0f, -1.0f, 0.0f,
        1.0f, -1.0f, 0.0f,
        1.0f,  1.0f, 0.0f
    };
    
    static const GLfloat g_uv_buffer_data[] = {
        -1.0f, 1.0f,
        1.0f, 1.0f,
        -1.0f,  -1.0f,
        
        -1.0f, -1.0f,
        1.0f, -1.0f,
        1.0f,  1.0f
    };
    //std::cout << getenv("WD") << std::endl;
    GLuint TextureID  = glGetUniformLocation(programID, "myTextureSampler");
    
    GLuint vertexbuffer;
    glGenBuffers(1, &vertexbuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_buffer_data), g_vertex_buffer_data, GL_STATIC_DRAW);
    
    GLuint uvbuffer;
    glGenBuffers(1, &uvbuffer);
    glBindBuffer(GL_ARRAY_BUFFER, uvbuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_uv_buffer_data), g_uv_buffer_data, GL_STATIC_DRAW);
    
    glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.3f, 0.0f);
    CheckError(glGetError(), __LINE__);
    
    if(!createRNG()){
        std::cout << "Random Number creation unsuccesful." << std::endl;
    }
    
    if(!setupChunkMem()){
        
    }
    
    initChunk(activeChunk);
    
    kernel = clCreateKernel (program, "Raycaster", &error);
    CheckError(error,__LINE__);
    
    std::size_t offset [3] = { 0 };
    std::size_t size [3] = { 1024, 768, 1 };
    
    
    
    do{
        //clLoop:
        clEnqueueAcquireGLObjects(queue, 1,  &clMemImg, 0, 0, NULL);
        clFinish(queue);
        // Setup the kernel arguments
        
        CheckErrorCL(clSetKernelArg (kernel, 0, sizeof (cl_mem), &clMemImg),__LINE__);
        CheckErrorCL(clSetKernelArg (kernel, 1, sizeof (cl_mem), &(aworld.RandomNumberCLMem)),__LINE__);
        
        
        //CheckErrorCL(clSetKernelArg (kernel, 1, sizeof (int), &t),__LINE__);
        
        // Run the processing
        // http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clEnqueueNDRangeKernel.html
        
        CheckErrorCL (clEnqueueNDRangeKernel (queue, kernel, 2, offset, size, nullptr,
                                              0, nullptr, nullptr),__LINE__);
        
        clEnqueueReleaseGLObjects(queue, 1,  &clMemImg, 0, 0, NULL);
        clFinish(queue);
        //end clLoop
        
        // Clear the screen
        glClear( GL_COLOR_BUFFER_BIT );
        
        // Use our shader
        glUseProgram(programID);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, frameBufferTextureId);
        // Set our "myTextureSampler" sampler to user Texture Unit 0
        glUniform1i(TextureID, 0);
        
        // 1rst attribute buffer : vertices
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
        glVertexAttribPointer(
                              0,                  // attribute 0. No particular reason for 0, but must match the layout in the shader.
                              3,                  // size
                              GL_FLOAT,           // type
                              GL_FALSE,           // normalized?
                              0,                  // stride
                              (void*)0            // array buffer offset
                              );
        
        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, uvbuffer);
        glVertexAttribPointer(
                              1,                                // attribute. No particular reason for 1, but must match the layout in the shader.
                              2,                                // size : U+V => 2
                              GL_FLOAT,                         // type
                              GL_FALSE,                         // normalized?
                              0,                                // stride
                              (void*)0                          // array buffer offset
                              );
        
        // Draw the triangle !
        glDrawArrays(GL_TRIANGLES, 0, 6); // 3 indices starting at 0 -> 1 triangle
        
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        
        // Swap buffers
        glfwSwapBuffers(window);
        glfwPollEvents();
        
    } // Check if the ESC key was pressed or the window was closed
    while( glfwGetKey(window, GLFW_KEY_ESCAPE ) != GLFW_PRESS &&
          glfwWindowShouldClose(window) == 0 );
    
    // Cleanup VBO
    glDeleteBuffers(1, &vertexbuffer);
    glDeleteVertexArrays(1, &VertexArrayID);
   // glDeleteProgram(programID);
    
    clReleaseCommandQueue (queue);
    
    clReleaseKernel (kernel);
    clReleaseProgram (program);
    
    clReleaseContext (context);
    
    // Close OpenGL window and terminate GLFW
    glfwTerminate();
    
    return 0;
}
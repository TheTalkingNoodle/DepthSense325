#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <python2.7/Python.h>
#include <python2.7/numpy/arrayobject.h>

// MS completly untested
#ifdef _MSC_VER
#include <windows.h>
#endif

// C includes
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// C++ includes
#include <vector>
#include <exception>
#include <iostream>
#include <list>
#include<map>
//#include <thread>

// DepthSense SDK includes
#include <DepthSense.hxx>

using namespace DepthSense;
using namespace std;

// depth sense node inits
static Context g_context;
static DepthNode g_dnode;
static ColorNode g_cnode;
static AudioNode g_anode;

static bool g_bDeviceFound = false;

// unecassary frame counters
static uint32_t g_aFrames = 0;
static uint32_t g_cFrames = 0;
static uint32_t g_dFrames = 0;


// map dimensions
static int32_t dW = 320;
static int32_t dH = 240;
static int32_t cW = 640;
static int32_t cH = 480;

int dshmsz = dW*dH*sizeof(uint16_t);
int cshmsz = cW*cH*sizeof(uint8_t);
int vshmsz = dW*dH*sizeof(int16_t);
int ushmsz = dW*dH*sizeof(float);

// shared mem depth maps
static uint16_t *depthMap;
static uint16_t *depthFullMap;

// shared mem depth maps
static uint16_t *confidenceMap;
static uint16_t *confidenceFullMap;

// shared mem depth maps
static int16_t *vertexMap;
static int16_t *vertexFullMap;

// shared mem colour maps
static uint8_t *colourMap;
static uint8_t *colourFullMap;

// shared mem accel maps
static float *accelMap;
static float *accelFullMap;

// shared mem uv maps
static float *uvMap;
static float *uvFullMap;

// internal map copies
static uint8_t colourMapClone[640*480*3];
static uint16_t depthMapClone[320*240];
static uint16_t confidenceMapClone[320*240];
static int16_t vertexMapClone[320*240*3];
static float accelMapClone[3];
static float uvMapClone[320*240*2];
static uint8_t syncMapClone[320*240*3];


// clean up
int child_pid = 0;

// can't write atomic op but i can atleast do a swap
static void dptrSwap (uint16_t **pa, uint16_t **pb){
        uint16_t *temp = *pa;
        *pa = *pb;
        *pb = temp;
}

static void cptrSwap (uint8_t **pa, uint8_t **pb){
        uint8_t *temp = *pa;
        *pa = *pb;
        *pb = temp;
}

static void aptrSwap (float **pa, float **pb){
        float *temp = *pa;
        *pa = *pb;
        *pb = temp;
}

static void vptrSwap (int16_t **pa, int16_t **pb){
        int16_t *temp = *pa;
        *pa = *pb;
        *pb = temp;
}

//================= NEW COLOR SAMPLE =================//
static void onNewColorSample(ColorNode node, ColorNode::NewSampleReceivedData data)
{
    //printf("C#%u: %d\n",g_cFrames,data.colorMap.size());
    memcpy(colourMap, data.colorMap, 3*cshmsz);
    cptrSwap(&colourMap, &colourFullMap);
    g_cFrames++;
}

//================= NEW DEPTH SAMPLE =================//
static void onNewDepthSample(DepthNode node, DepthNode::NewSampleReceivedData data)
{
    // Depth
    memcpy(depthMap, data.depthMap, dshmsz);
    dptrSwap(&depthMap, &depthFullMap);

    // Confidence
    memcpy(confidenceMap, data.confidenceMap, dshmsz);
    dptrSwap(&confidenceMap, &confidenceFullMap);

    // Verticies
    Vertex vertex;
    for(int i=0; i < dH; i++) {
        for(int j=0; j < dW; j++) {
            vertex = data.vertices[i*dW + j];
            vertexMap[i*dW*3 + j*3 + 0] = vertex.x;
            vertexMap[i*dW*3 + j*3 + 1] = vertex.y;
            vertexMap[i*dW*3 + j*3 + 2] = vertex.z;
            //cout << vertex.x << vertex.y << vertex.z << endl;

        }
    }
    vptrSwap(&vertexMap, &vertexFullMap);

    // uv
    UV uv;
    for(int i=0; i < dH; i++) {
        for(int j=0; j < dW; j++) {
            uv = data.uvMap[i*dW + j];
            uvMap[i*dW*2 + j*2 + 0] = uv.u;
            uvMap[i*dW*2 + j*2 + 1] = uv.v;
            //cout << uv.u << uv.v << endl;

        }
    }
    aptrSwap(&uvMap, &uvFullMap);


    // Acceleration
    accelMap[0] = data.acceleration.x;
    accelMap[1] = data.acceleration.y;
    accelMap[2] = data.acceleration.z;
    aptrSwap(&accelMap, &accelFullMap);

    g_dFrames++;
}

//================= CONFIGURE DEPTH NODE =================//
static void configureDepthNode()
{
    g_dnode.newSampleReceivedEvent().connect(&onNewDepthSample);

    DepthNode::Configuration config = g_dnode.getConfiguration();
    config.frameFormat = FRAME_FORMAT_QVGA;
    config.framerate = 30;
    config.mode = DepthNode::CAMERA_MODE_CLOSE_MODE;
    config.saturation = true;

    try
    {
        g_context.requestControl(g_dnode,0);
        g_dnode.setConfidenceThreshold(50);
        g_dnode.setEnableConfidenceMap(true);
        g_dnode.setEnableDepthMap(true);
        g_dnode.setEnableVertices(true);
        g_dnode.setEnableAccelerometer(true);
        g_dnode.setEnableUvMap(true);

        g_dnode.setConfiguration(config);

    }
    catch (ArgumentException& e)
    {
        printf("Argument Exception: %s\n",e.what());
    }
    catch (UnauthorizedAccessException& e)
    {
        printf("Unauthorized Access Exception: %s\n",e.what());
    }
    catch (IOException& e)
    {
        printf("IO Exception: %s\n",e.what());
    }
    catch (InvalidOperationException& e)
    {
        printf("Invalid Operation Exception: %s\n",e.what());
    }
    catch (ConfigurationException& e)
    {
        printf("Configuration Exception: %s\n",e.what());
    }
    catch (StreamingException& e)
    {
        printf("Streaming Exception: %s\n",e.what());
    }
    catch (TimeoutException&)
    {
        printf("TimeoutException\n");
    }

}

//================= CONFIGURE COLOR NODE =================//
static void configureColorNode()
{

    // connect new color sample handler
    g_cnode.newSampleReceivedEvent().connect(&onNewColorSample);

    ColorNode::Configuration config = g_cnode.getConfiguration();
    config.frameFormat = FRAME_FORMAT_VGA;
    config.compression = COMPRESSION_TYPE_MJPEG;
    config.powerLineFrequency = POWER_LINE_FREQUENCY_60HZ;
    config.framerate = 30;

    g_cnode.setEnableColorMap(true);

    try
    {
        g_context.requestControl(g_cnode,0);
        g_cnode.setConfiguration(config);
        g_cnode.setBrightness(0);
        g_cnode.setContrast(5);
        g_cnode.setSaturation(5);
        g_cnode.setHue(0);
        g_cnode.setGamma(3);
        g_cnode.setWhiteBalance(4650);
        g_cnode.setSharpness(5);
        g_cnode.setWhiteBalanceAuto(true);

    }
    catch (ArgumentException& e)
    {
        printf("Argument Exception: %s\n",e.what());
    }
    catch (UnauthorizedAccessException& e)
    {
        printf("Unauthorized Access Exception: %s\n",e.what());
    }
    catch (IOException& e)
    {
        printf("IO Exception: %s\n",e.what());
    }
    catch (InvalidOperationException& e)
    {
        printf("Invalid Operation Exception: %s\n",e.what());
    }
    catch (ConfigurationException& e)
    {
        printf("Configuration Exception: %s\n",e.what());
    }
    catch (StreamingException& e)
    {
        printf("Streaming Exception: %s\n",e.what());
    }
    catch (TimeoutException&)
    {
        printf("TimeoutException\n");
    }

}

//================= CONFIGURE NODE =================//
static void configureNode(Node node)
{
    if ((node.is<DepthNode>())&&(!g_dnode.isSet()))
    {
        g_dnode = node.as<DepthNode>();
        configureDepthNode();
        g_context.registerNode(node);
    }

    if ((node.is<ColorNode>())&&(!g_cnode.isSet()))
    {
        g_cnode = node.as<ColorNode>();
        configureColorNode();
        g_context.registerNode(node);
    }
}

//================= NODE CONNECTED =================//
static void onNodeConnected(Device device, Device::NodeAddedData data)
{
    configureNode(data.node);
}

//================= NODE DISCONNECTED =================//
static void onNodeDisconnected(Device device, Device::NodeRemovedData data)
{
    if (data.node.is<AudioNode>() && (data.node.as<AudioNode>() == g_anode))
        g_anode.unset();
    if (data.node.is<ColorNode>() && (data.node.as<ColorNode>() == g_cnode))
        g_cnode.unset();
    if (data.node.is<DepthNode>() && (data.node.as<DepthNode>() == g_dnode))
        g_dnode.unset();
    printf("Node disconnected\n");
}

//================= DEVICE CONNECTED =================//
static void onDeviceConnected(Context context, Context::DeviceAddedData data)
{
    if (!g_bDeviceFound)
    {
        data.device.nodeAddedEvent().connect(&onNodeConnected);
        data.device.nodeRemovedEvent().connect(&onNodeDisconnected);
        g_bDeviceFound = true;
    }
}

//================= DEVICE DISCONNECTED =================//
static void onDeviceDisconnected(Context context, Context::DeviceRemovedData data)
{
    g_bDeviceFound = false;
    printf("Device disconnected\n");
}

//================= KILL DEPTHSENSE =================//
extern "C" {
    static void killds()
    {
        if (child_pid !=0) {
            kill(child_pid, SIGTERM);
            munmap(depthMap, dshmsz);
            munmap(depthFullMap, dshmsz);
            munmap(confidenceMap, dshmsz);
            munmap(confidenceFullMap, dshmsz);
            munmap(colourMap, cshmsz*3);
            munmap(colourFullMap, cshmsz*3);
            munmap(vertexMap, vshmsz*3);
            munmap(vertexFullMap, vshmsz*3);
            munmap(uvMap, ushmsz*2);
            munmap(uvFullMap, ushmsz*2);
        }

    }
}

static void * initmap(int sz) 
{
    void * map;     
    if ((map = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("mmap: cannot alloc shmem;");
        exit(1);
    }

    return map;
}

//================= INITIALIZE DEPTHSENSE =================//
static void initds()
{
    // shared mem double buffers
    depthMap = (uint16_t *) initmap(dshmsz); 
    depthFullMap = (uint16_t *) initmap(dshmsz); 

    confidenceMap = (uint16_t *) initmap(dshmsz); 
    confidenceFullMap = (uint16_t *) initmap(dshmsz); 

    accelMap = (float *) initmap(3*sizeof(float)); 
    accelFullMap = (float *) initmap(3*sizeof(float)); 

    colourMap = (uint8_t *) initmap(cshmsz*3); 
    colourFullMap = (uint8_t *) initmap(cshmsz*3); 

    vertexMap = (int16_t *) initmap(vshmsz*3); 
    vertexFullMap = (int16_t *) initmap(vshmsz*3); 
    
    uvMap = (float *) initmap(ushmsz*2); 
    uvFullMap = (float *) initmap(ushmsz*2); 

    child_pid = fork();
    // child goes into loop
    if (child_pid == 0) {
        g_context = Context::create("localhost");
        g_context.deviceAddedEvent().connect(&onDeviceConnected);
        g_context.deviceRemovedEvent().connect(&onDeviceDisconnected);

        // Get the list of currently connected devices
        vector<Device> da = g_context.getDevices();

        // We are only interested in the first device
        if (da.size() >= 1)
        {
            g_bDeviceFound = true;

            da[0].nodeAddedEvent().connect(&onNodeConnected);
            da[0].nodeRemovedEvent().connect(&onNodeDisconnected);

            vector<Node> na = da[0].getNodes();

            //printf("Found %u nodes\n",na.size());

            for (int n = 0; n < (int)na.size();n++)
                configureNode(na[n]);
        }

        g_context.startNodes();
        g_context.run();
        g_context.stopNodes();

        if (g_cnode.isSet()) g_context.unregisterNode(g_cnode);
        if (g_dnode.isSet()) g_context.unregisterNode(g_dnode);
        if (g_anode.isSet()) g_context.unregisterNode(g_anode);

        exit(EXIT_SUCCESS);
    }

}

//================= DEPTH AND COLOR IMAGE SYNCHRONIZED =================//
/*
 * Using (assumed to be) up-to-date depth/uv/colour maps build a colour map
 * with the resoloution of the depth map with pixels that exist in both the 
 * depth and colour map exclusively (that info is provided by the uv map)
 */
static void buildSyncMap()
{
    int ci, cj;
    uint8_t colx;
    uint8_t coly;
    uint8_t colz;
    float uvx;
    float uvy;

    for(int i=0; i < dH; i++) {
        for(int j=0; j < dW; j++) {
            uvx = uvMapClone[i*dW*2 + j*2 + 0];    
            uvy = uvMapClone[i*dW*2 + j*2 + 1];    
            colx = 0;
            coly = 0;
            colz = 0;
            
            if((uvx > 0 && uvx < 1 && uvy > 0 && uvy < 1) && 
                (depthMapClone[i*dW + j] < 32000)){
                ci = (int) (uvy * ((float) cH));
                cj = (int) (uvx * ((float) cW));
                colx = colourMapClone[ci*cW*3 + cj*3 + 0];
                coly = colourMapClone[ci*cW*3 + cj*3 + 1];
                colz = colourMapClone[ci*cW*3 + cj*3 + 2];
            }
          
            
            syncMapClone[i*dW*3 + j*3 + 0] = colx;
            syncMapClone[i*dW*3 + j*3 + 1] = coly;
            syncMapClone[i*dW*3 + j*3 + 2] = colz;

        }
    }
}


//================= PYTHON FUNCTIONS =================//
static PyObject *getColour(PyObject *self, PyObject *args)
{
    npy_intp dims[3] = {cH, cW, 3};

    memcpy(colourMapClone, colourFullMap, cshmsz*3);
    return PyArray_SimpleNewFromData(3, dims, NPY_UINT8, colourMapClone);
}

static PyObject *getDepth(PyObject *self, PyObject *args)
{
    npy_intp dims[2] = {dH, dW};

    memcpy(depthMapClone, depthFullMap, dshmsz);
    return PyArray_SimpleNewFromData(2, dims, NPY_UINT16, depthMapClone);
}

static PyObject *getConfidence(PyObject *self, PyObject *args)
{
    npy_intp dims[2] = {dH, dW};

    memcpy(confidenceMapClone, confidenceFullMap, dshmsz);
    return PyArray_SimpleNewFromData(2, dims, NPY_UINT16, confidenceMapClone);
}

static PyObject *getAccel(PyObject *self, PyObject *args)
{
    npy_intp dims[1] = {3};

    memcpy(accelMapClone, accelFullMap, 3*sizeof(float));
    return PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, accelMapClone);
}

static PyObject *getVertex(PyObject *self, PyObject *args)
{
    npy_intp dims[3] = {dH, dW, 3};
    memcpy(vertexMapClone, vertexFullMap, vshmsz*3);
    return PyArray_SimpleNewFromData(3, dims, NPY_INT16, vertexMapClone);
}

static PyObject *getUV(PyObject *self, PyObject *args)
{
    npy_intp dims[3] = {dH, dW, 3};
    memcpy(uvMapClone, uvFullMap, ushmsz*2);
    return PyArray_SimpleNewFromData(3, dims, NPY_FLOAT32, uvMapClone);
}

static PyObject *getSync(PyObject *self, PyObject *args)
{
    npy_intp dims[3] = {dH, dW, 3};

    memcpy(uvMapClone, uvFullMap, ushmsz*2);
    memcpy(colourMapClone, colourFullMap, cshmsz*3);
    memcpy(depthMapClone, depthFullMap, dshmsz);
    
    buildSyncMap();
    return PyArray_SimpleNewFromData(3, dims, NPY_UINT8, syncMapClone);
}


static PyObject *initDepthS(PyObject *self, PyObject *args)
{
    initds();
    return Py_None;
}

static PyObject *killDepthS(PyObject *self, PyObject *args)
{
    killds();
    return Py_None;
}

//================= DISPLAY FUNCTIONS =================//
static PyMethodDef DepthSenseMethods[] = {
    // GET MAPS
    {"getDepthMap",  getDepth, METH_VARARGS, "Get Depth Map"},
    {"getConfidenceMap",  getConfidence, METH_VARARGS, "Get Confidence Map"},
    {"getColourMap",  getColour, METH_VARARGS, "Get Colour Map"},
    {"getVertices",  getVertex, METH_VARARGS, "Get Vertex Map"},
    {"getUVMap",  getUV, METH_VARARGS, "Get UV Map"},
    {"getSyncMap",  getSync, METH_VARARGS, "Get Colour Overlay Map"},
    {"getAcceleration",  getAccel, METH_VARARGS, "Get Acceleration"},
    // CREATE MODULE
    {"start",  initDepthS, METH_VARARGS, "Start DepthSense"},
    {"stop",  killDepthS, METH_VARARGS, "Stop DepthSense"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


PyMODINIT_FUNC initDepthSense(void)
{
    (void) Py_InitModule("DepthSense", DepthSenseMethods);
    // Clean up forked process, attach it to the python exit hook
    (void) Py_AtExit(killds);
    import_array();
}

int main(int argc, char* argv[])
{

    /* Pass argv[0] to the Python interpreter */
    Py_SetProgramName((char *)"DepthSense");

    /* Initialize the Python interpreter.  Required. */
    Py_Initialize();

    /* Add a static module */
    initDepthSense();

    //initds(); //for testing

    return 0;
}

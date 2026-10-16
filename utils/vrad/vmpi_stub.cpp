//=============================================================================
// vmpi_stub.cpp
//
// Stub (no-op) implementation of the VMPI (Valve MPI) API for non-Windows
// builds of vrad.
//
// The vrad sources reference the VMPI API unconditionally (vrad.cpp,
// vradstaticprops.cpp, leaf_ambient_lighting.cpp, vraddetailprops.cpp,
// vismat.cpp, lightmap.cpp, and utils/common/cmdlib.cpp &
// utils/common/filesystem_tools.cpp). On Windows the real implementation lives
// in utils/vmpi/* (the "vmpi" library), utils/vrad/mpivrad.cpp and
// utils/common/vmpi_tools_shared.cpp -- all of which are only built for
// Windows (see utils/vrad/wscript, which only adds them under
// `if bld.env.DEST_OS == 'win32'`).
//
// On non-Windows we provide empty / no-op definitions so that vrad links and
// runs as a single-machine (non-distributed) build. The Windows code path is
// completely untouched: this entire translation unit is compiled out on
// Windows via the #ifndef _WIN32 guard below.
//=============================================================================

#ifndef _WIN32

#include "basetypes.h"
#include "interface.h"
#include "filesystem.h"
#include "tier0/platform.h"

#include "messbuf.h"
#include "vmpi.h"
#include "vmpi_filesystem.h"
#include "vmpi_distribute_work.h"
#include "vmpi_tools_shared.h"
#include "mpivrad.h"


// --- globals (declared extern in the headers above) -------------------------
bool g_bUseMPI = false;
bool g_bMPIMaster = false;
IFileSystem *g_pOriginalPassThruFileSystem = NULL;


// --- MessageBuffer (utils/vmpi/messbuf.cpp) ---------------------------------
MessageBuffer::MessageBuffer()
{
}

MessageBuffer::MessageBuffer( int size )
{
}

MessageBuffer::~MessageBuffer()
{
}

int MessageBuffer::write( void const *pData, int nBytes )
{
	return 0;
}

int MessageBuffer::read( void *pData, int nBytes )
{
	return 0;
}


// --- vmpi.cpp ---------------------------------------------------------------
void VMPI_SetCurrentStage( const char *pCurStage )
{
}

void VMPI_Finalize()
{
}

bool VMPI_IsSDKMode()
{
	return false;
}

const char* VMPI_GetParamString( EVMPICmdLineParam eParam )
{
	return "";
}

int VMPI_GetParamFlags( EVMPICmdLineParam eParam )
{
	return 0;
}

const char* VMPI_GetParamHelpString( EVMPICmdLineParam eParam )
{
	return "";
}

bool VMPI_Init( int &argc, char **&argv, const char *pDependencyFilename,
                VMPI_Disconnect_Handler handler, VMPIRunMode runMode,
                bool bConnectingAsService )
{
	return false;
}


// --- vmpi_distribute_work.cpp ----------------------------------------------
double DistributeWork( uint64 nWorkUnits, char cPacketID,
                       ProcessWorkUnitFn processFn, ReceiveWorkUnitFn receiveFn )
{
	return 0.0;
}


// --- vmpi_filesystem.cpp ----------------------------------------------------
IFileSystem* VMPI_FileSystem_Init( int maxFileSystemMemoryUsage, IFileSystem *pPassThru )
{
	return pPassThru;
}

IFileSystem* VMPI_FileSystem_Term()
{
	return g_pOriginalPassThruFileSystem;
}

CreateInterfaceFn VMPI_FileSystem_GetFactory()
{
	return 0;
}

void SendQDirInfo()
{
}

void RecvQDirInfo()
{
}


// --- vmpi_tools_shared.cpp --------------------------------------------------
void VMPI_ExceptionFilter( unsigned long uCode, void *pvExceptionInfo )
{
}

void VMPI_HandleCrash( const char *pMessage, void *pvExceptionInfo, bool bAssert )
{
}


// --- mpivrad.cpp ------------------------------------------------------------
void VRAD_SetupMPI( int &argc, char **&argv )
{
}

void RunMPIBuildFacelights( void )
{
}

void RunMPIBuildVisLeafs( void )
{
}

void VMPI_DistributeLightData()
{
}


// --- tier0 (Plat_GetModuleFilename is missing from the built libtier0.so) ---
void Plat_GetModuleFilename( char *pOut, int nMaxBytes )
{
	if ( pOut && nMaxBytes > 0 )
	{
		pOut[0] = '\0';
	}
}

#endif // _WIN32

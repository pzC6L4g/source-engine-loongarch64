//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// vvis_launcher.cpp : Defines the entry point for the console application.
//

#include "StdAfx.h"
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "tier1/strtools.h"
#include "tier0/icommandline.h"
#include "ilaunchabledll.h"


#ifdef _WIN32
char* GetLastErrorString()
{
	static char err[2048];
	
	LPVOID lpMsgBuf;
	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL 
	);

	strncpy( err, (char*)lpMsgBuf, sizeof( err ) );
	LocalFree( lpMsgBuf );

	err[ sizeof( err ) - 1 ] = 0;

	return err;
}
#else // POSIX / Linux
#include <dlfcn.h>
#include <string.h>
#include <errno.h>

char* GetLastErrorString()
{
	static char err[2048];

	// Prefer the dynamic loader's message (set by Sys_LoadModule -> dlopen).
	const char *pErr = dlerror();
	if ( pErr && *pErr )
	{
		Q_strncpy( err, pErr, sizeof( err ) );
	}
	else
	{
		Q_strncpy( err, strerror( errno ), sizeof( err ) );
	}

	err[ sizeof( err ) - 1 ] = 0;

	return err;
}
#endif

int main(int argc, char* argv[])
{
	CommandLine()->CreateCmdLine( argc, argv );
	const char *pDLLName = "vvis_dll.dll";
	
	CSysModule *pModule = Sys_LoadModule( pDLLName );
	if ( !pModule )
	{
		printf( "vvis launcher error: can't load %s\n%s", pDLLName, GetLastErrorString() );
		return 1;
	}

	CreateInterfaceFn fn = Sys_GetFactory( pModule );
	if( !fn )
	{
		printf( "vvis launcher error: can't get factory from %s\n", pDLLName );
		Sys_UnloadModule( pModule );
		return 2;
	}

	int retCode = 0;
	ILaunchableDLL *pDLL = (ILaunchableDLL*)fn( LAUNCHABLE_DLL_INTERFACE_VERSION, &retCode );
	if( !pDLL )
	{
		printf( "vvis launcher error: can't get IVVisDLL interface from %s\n", pDLLName );
		Sys_UnloadModule( pModule );
		return 3;
	}

	pDLL->main( argc, argv );
	Sys_UnloadModule( pModule );

	return 0;
}


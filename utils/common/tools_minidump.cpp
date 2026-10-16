//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
// POSIX / Linux crash-handling support.
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "tier0/minidump.h"
#include "tools_minidump.h"

static bool g_bToolsWriteFullMinidumps = false;
static ToolsExceptionHandler g_pCustomExceptionHandler = NULL;


// --------------------------------------------------------------------------------- //
// Internal helpers.
// --------------------------------------------------------------------------------- //

#ifdef _WIN32

static LONG __stdcall ToolsExceptionFilter( struct _EXCEPTION_POINTERS *ExceptionInfo )
{
	// Non VMPI workers write a minidump and show a crash dialog like normal.
	int iType = MiniDumpNormal;
	if ( g_bToolsWriteFullMinidumps )
		iType = MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory;
		
	WriteMiniDumpUsingExceptionInfo( ExceptionInfo->ExceptionRecord->ExceptionCode, ExceptionInfo, (MINIDUMP_TYPE)iType );
	return EXCEPTION_CONTINUE_SEARCH;
}


static LONG __stdcall ToolsExceptionFilter_Custom( struct _EXCEPTION_POINTERS *ExceptionInfo )
{
	// Run their custom handler.
	g_pCustomExceptionHandler( ExceptionInfo->ExceptionRecord->ExceptionCode, ExceptionInfo );
	return EXCEPTION_EXECUTE_HANDLER; // (never gets here anyway)
}

#else // POSIX / Linux

// Signal-driven crash handler. Writes a textual stack-trace crash dump
// (the Linux equivalent of a minidump) and then either runs the custom
// handler or re-raises so the OS can produce a core dump.
static void ToolsSignalHandler( int sig, siginfo_t *siginfo, void *context )
{
	// Write a uniquely-named textual stack trace crash dump.
	time_t t = time( NULL );
	struct tm *ptm = localtime( &t );
	char szFilename[ 256 ];
	snprintf( szFilename, sizeof( szFilename ),
	          "crash_%04d%02d%02d_%02d%02d%02d.dmp.txt",
	          ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
	          ptm->tm_hour, ptm->tm_min, ptm->tm_sec );

	FILE *f = fopen( szFilename, "w" );
	if ( f )
	{
		fprintf( f, "Crash signal %d (pid %d)\n", sig, (int)getpid() );

		void *pFrames[ 128 ];
		int n = backtrace( pFrames, 128 );
		fprintf( f, "Stack frames (%d):\n", n );

		if ( g_bToolsWriteFullMinidumps )
		{
			// Full mode: resolve symbols inline. Note backtrace_symbols()
			// allocates (not async-signal-safe) but is fine for a tool process.
			char **syms = backtrace_symbols( pFrames, n );
			for ( int i = 0; i < n; i++ )
				fprintf( f, "  %s\n", syms ? syms[ i ] : "??" );
			free( syms );
		}
		else
		{
			for ( int i = 0; i < n; i++ )
				fprintf( f, "  %p\n", pFrames[ i ] );
		}
		fclose( f );
	}

	if ( g_pCustomExceptionHandler )
	{
		// tools_minidump.h documents pvExceptionInfo is NULL on non-Win32.
		g_pCustomExceptionHandler( (unsigned long)sig, NULL );
	}

	// Restore the default handler and re-raise so the OS can produce a
	// core dump / default behavior.
	struct sigaction sa;
	memset( &sa, 0, sizeof( sa ) );
	sa.sa_handler = SIG_DFL;
	sigaction( sig, &sa, NULL );
	raise( sig );
}


static void InstallToolsSignalHandler()
{
	struct sigaction sa;
	memset( &sa, 0, sizeof( sa ) );
	sa.sa_sigaction = ToolsSignalHandler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset( &sa.sa_mask );

	const int crashSigs[] = { SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS };
	for ( size_t i = 0; i < sizeof( crashSigs ) / sizeof( crashSigs[0] ); i++ )
		sigaction( crashSigs[ i ], &sa, NULL );
}

#endif // _WIN32


// --------------------------------------------------------------------------------- //
// Interface functions.
// --------------------------------------------------------------------------------- //

void EnableFullMinidumps( bool bFull )
{
	g_bToolsWriteFullMinidumps = bFull;
}


void SetupDefaultToolsMinidumpHandler()
{
#ifdef _WIN32
	SetUnhandledExceptionFilter( ToolsExceptionFilter );
#else
	InstallToolsSignalHandler();
#endif
}


void SetupToolsMinidumpHandler( ToolsExceptionHandler fn )
{
	g_pCustomExceptionHandler = fn;
#ifdef _WIN32
	SetUnhandledExceptionFilter( ToolsExceptionFilter_Custom );
#else
	InstallToolsSignalHandler();
#endif
}

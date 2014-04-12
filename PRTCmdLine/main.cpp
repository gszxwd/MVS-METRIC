//----------------------------------------------------------------------------
// File: main.cpp
//
// Desc: 

// the optimization lib used in this application often fails to come out with the right
// answer in bearable time. So used the matlab optimization instead.
//-----------------------------------------------------------------------------
#include "DXUT.h"
#include "DXUTcamera.h"
#include "SDKmisc.h"
#include <stdio.h>
#include <conio.h>
#include <fstream>
#include "config.h"
#include "PRTSim.h"
#include "PlyLoader.h"
#include "VertexRef.h"
#include "EnvMapCalculator.h"
#include "MetricCal.h"
#include "EnvMapCal.h"

using namespace std;

CModelViewerCamera          g_Camera;               // A model viewing camera

//-----------------------------------------------------------------------------
// Function-prototypes
//-----------------------------------------------------------------------------
bool ParseCommandLine( SETTINGS* pSettings );
bool GetCmdParam( WCHAR*& strsettings, WCHAR* strFlag, int nFlagLen );
IDirect3DDevice9* CreateNULLRefDevice();
HRESULT LoadMeshes( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions, CONCAT_MESH* pPRTMesh,
				   CONCAT_MESH* pBlockerMesh, SETTINGS* pSettings );
void DisplayUsage();
void NormalizeNormals( ID3DXMesh* pMesh );
bool IsNextArg( WCHAR*& strsettings, WCHAR* strArg );
bool GetNextArg( WCHAR*& strsettings, WCHAR* strArg, int cchArg );
void SearchDirForFile( IDirect3DDevice9* pd3dDevice, WCHAR* strDir, WCHAR* strFile, SETTINGS* pSettings );
void SeachSubdirsForFile( IDirect3DDevice9* pd3dDevice, WCHAR* strDir, WCHAR* strFile, SETTINGS* pSettings );
HRESULT AdjustMeshDecl( IDirect3DDevice9* pd3dDevice, ID3DXMesh** ppMesh );
HRESULT ProcessOptionsFile( IDirect3DDevice9* pd3dDevice, WCHAR* strOptionsFileName, SETTINGS* pSettings );
HRESULT SetCameras( IDirect3DDevice9* pd3dDevice, CONCAT_MESH *prtMesh, float& fObjectRadius, D3DXVECTOR3& vObjectCenter);
void WINAPI SHCubeFill( D3DXVECTOR4* pOut, CONST D3DXVECTOR3* pTexCoord, CONST D3DXVECTOR3* pTexelSize,LPVOID pData );

//-----------------------------------------------------------------------------
struct SHCubeProj
{
	float* pRed,*pGreen,*pBlue;
	int iOrderUse; // order to use
	float   fConvCoeffs[6]; // convolution coefficients

	void    InitDiffCubeMap( float* pR, float* pG, float* pB )
	{
		pRed = pR;
		pGreen = pG;
		pBlue = pB;

		iOrderUse = 3; // go to 5 is a bit more accurate...
		fConvCoeffs[0] = 1.0f;

		fConvCoeffs[1] = 2.0f / 3.0f;
		fConvCoeffs[2] = 1.0f / 4.0f;
		fConvCoeffs[3] = fConvCoeffs[5] = 0.0f;
		fConvCoeffs[4] = -6.0f / 144.0f; // 
	}

	void    Init( float* pR, float* pG, float* pB )
	{
		pRed = pR;
		pGreen = pG;
		pBlue = pB;

		iOrderUse = 6;
		for( int i = 0; i < 6; i++ ) fConvCoeffs[i] = 1.0f;
	}
};

//--------------------------------------------------------------------------------------
void WINAPI SHCubeFill( D3DXVECTOR4* pOut,
					   CONST D3DXVECTOR3* pTexCoord,
					   CONST D3DXVECTOR3* pTexelSize,
					   LPVOID pData )
{
	SHCubeProj* pCP = ( SHCubeProj* ) pData;
	D3DXVECTOR3 vDir;

	D3DXVec3Normalize( &vDir,pTexCoord );

	float fVals[36];
	D3DXSHEvalDirection( fVals, pCP->iOrderUse, &vDir );

	( *pOut ) = D3DXVECTOR4( 0,0,0,0 ); // just clear it out...

	int l, m, uIndex = 0;
	for( l=0; l<pCP->iOrderUse; l++ )
	{
		const float fConvUse = pCP->fConvCoeffs[l];
		for( m=0; m<2*l+1; m++ )
		{
			pOut->x += fConvUse*fVals[uIndex]*pCP->pRed[uIndex];
			pOut->y += fConvUse*fVals[uIndex]*pCP->pGreen[uIndex];
			pOut->z += fConvUse*fVals[uIndex]*pCP->pBlue[uIndex];
			pOut->w = 1;

			uIndex++;
		}
	}
}

//-----------------------------------------------------------------------------
// Name: main()
// Desc: Entry point for the application.  We use just the console window
//-----------------------------------------------------------------------------
int wmain( int argc, wchar_t* argv[] )
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

	int nRet = 0;
	IDirect3DDevice9* pd3dDevice = NULL;
	SETTINGS settings;
	settings.bUserAbort = false;
	settings.bSubDirs = false;
	settings.bVerbose = false;

	if( argc < 2 )
	{
		DisplayUsage();
		return 1;
	}

	// Setup the camera's view parameters
	D3DXVECTOR3 vecEye( 0.0f, 0.0f, -5.0f );
	D3DXVECTOR3 vecAt ( 0.0f, 0.0f, -0.0f );
	g_Camera.SetViewParams( &vecEye, &vecAt );

	if( !ParseCommandLine( &settings ) )
	{
		nRet = 0;
		goto LCleanup;
	}

	if( settings.aFiles.GetSize() == 0 )
	{
		WCHAR* strNewArg = new WCHAR[256];
		if( strNewArg )
		{
			wcscpy_s( strNewArg, 256, L"options.xml" );
			settings.aFiles.Add( strNewArg );
		}
	}

	// Create NULLREF device 
	pd3dDevice = CreateNULLRefDevice();
	if( pd3dDevice == NULL )
	{
		wprintf( L"Error: Can not create NULLREF Direct3D device\n" );
		nRet = 1;
		goto LCleanup;
	}

	for( int i = 0; i < settings.aFiles.GetSize(); i++ )
	{
		WCHAR* strOptionsFileName = settings.aFiles[i];
		if( settings.bVerbose )
			wprintf( L"Processing command line arg filename '%s'\n", strOptionsFileName );

		// For this cmd line arg, extract the full dir & filename
		WCHAR strDir[MAX_PATH] = {0};
		WCHAR strFile[MAX_PATH] = {0};
		WCHAR* pFilePart;
		DWORD dwWrote = GetFullPathName( strOptionsFileName, MAX_PATH, strDir, &pFilePart );
		if( dwWrote > 1 && pFilePart )
		{
			wcscpy_s( strFile, MAX_PATH, pFilePart );
			*pFilePart = NULL;
		}
		if( settings.bVerbose )
		{
			wprintf( L"Base dir: %s\n", strDir );
			wprintf( L"Base file: %s\n", strFile );
		}

		if( settings.bSubDirs )
			SeachSubdirsForFile( pd3dDevice, strDir, strFile, &settings );
		else
			SearchDirForFile( pd3dDevice, strDir, strFile, &settings );

		if( settings.bUserAbort )
			break;
	}

LCleanup:
	wprintf( L"\n" );

	// Cleanup
	for( int i = 0; i < settings.aFiles.GetSize(); i++ )
		SAFE_DELETE_ARRAY( settings.aFiles[i] );
	SAFE_RELEASE( pd3dDevice );

	return nRet;
}


//--------------------------------------------------------------------------------------
// Parses the command line for parameters. 
//--------------------------------------------------------------------------------------
bool ParseCommandLine( SETTINGS* pSettings )
{
	bool bDisplayHelp = false;
	WCHAR strArg[256];
	WCHAR* strsettings = GetCommandLine();

	// Skip past program name (first token in command line).
	if( *strsettings == L'"' )  // Check for and handle quoted program name
	{
		strsettings++;

		// Skip over until another double-quote or a null 
		while( *strsettings && ( *strsettings != L'"' ) )
			strsettings++;

		// Skip over double-quote
		if( *strsettings == L'"' )
			strsettings++;
	}
	else
	{
		// First token wasn't a quote
		while( *strsettings && !iswspace( *strsettings ) )
			strsettings++;
	}

	for(; ; )
	{
		// Skip past any white space preceding the next token
		while( *strsettings && iswspace( *strsettings ) )
			strsettings++;
		if( *strsettings == 0 )
			break;

		// Handle flag args
		if( *strsettings == L'/' || *strsettings == L'-' )
		{
			strsettings++;

			if( IsNextArg( strsettings, L"s" ) )
			{
				pSettings->bSubDirs = true;
				continue;
			}

			if( IsNextArg( strsettings, L"v" ) )
			{
				pSettings->bVerbose = true;
				continue;
			}

			if( IsNextArg( strsettings, L"?" ) )
			{
				DisplayUsage();
				return false;
			}

			// Unrecognized flag
			GetNextArg( strsettings, strArg, 256 );
			wprintf( L"Unrecognized or incorrect flag usage: %s\n", strArg );
			bDisplayHelp = true;
		}
		else if( GetNextArg( strsettings, strArg, 256 ) )
		{
			// Handle non-flag args
			int nArgLen = ( int )wcslen( strArg );
			WCHAR* strNewArg = new WCHAR[nArgLen + 1];
			wcscpy_s( strNewArg, nArgLen + 1, strArg );
			pSettings->aFiles.Add( strNewArg );
			continue;
		}
	}

	if( bDisplayHelp )
	{
		wprintf( L"Type \"PRTsettings.exe /?\" for a complete list of options\n" );
		return false;
	}

	return true;
}


//--------------------------------------------------------------------------------------
bool IsNextArg( WCHAR*& strsettings, WCHAR* strArg )
{
	int nArgLen = ( int )wcslen( strArg );
	if( _wcsnicmp( strsettings, strArg, nArgLen ) == 0 &&
		( strsettings[nArgLen] == 0 || iswspace( strsettings[nArgLen] ) ) )
	{
		strsettings += nArgLen;
		return true;
	}

	return false;
}


//--------------------------------------------------------------------------------------
bool GetNextArg( WCHAR*& strsettings, WCHAR* strArg, int cchArg )
{
	HRESULT hr;

	// Place NULL terminator in strFlag after current token
	V( wcscpy_s( strArg, 256, strsettings ) );
	WCHAR* strSpace = strArg;
	while( *strSpace && !iswspace( *strSpace ) )
		strSpace++;
	*strSpace = 0;

	// Update strsettings
	int nArgLen = ( int )wcslen( strArg );
	strsettings += nArgLen;
	if( nArgLen > 0 )
		return true;
	else
		return false;
}


//--------------------------------------------------------------------------------------
void SeachSubdirsForFile( IDirect3DDevice9* pd3dDevice, WCHAR* strDir, WCHAR* strFile, SETTINGS* pSettings )
{
	// First search this dir for the file
	SearchDirForFile( pd3dDevice, strDir, strFile, pSettings );
	if( pSettings->bUserAbort )
		return;

	// Then search this dir for other dirs and recurse
	WCHAR strFullPath[MAX_PATH] = {0};
	WCHAR strSearchDir[MAX_PATH];
	wcscpy_s( strSearchDir, MAX_PATH, strDir );
	wcscat_s( strSearchDir, MAX_PATH, L"*" );

	WIN32_FIND_DATA fileData;
	ZeroMemory( &fileData, sizeof( WIN32_FIND_DATA ) );
	HANDLE hFindFile = FindFirstFile( strSearchDir, &fileData );
	if( hFindFile != INVALID_HANDLE_VALUE )
	{
		BOOL bSuccess = TRUE;
		while( bSuccess )
		{
			if( ( fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
			{
				// Don't process '.' and '..' dirs
				if( fileData.cFileName[0] != L'.' )
				{
					wcscpy_s( strFullPath, MAX_PATH, strDir );
					wcscat_s( strFullPath, MAX_PATH, fileData.cFileName );
					wcscat_s( strFullPath, MAX_PATH, L"\\" );
					SeachSubdirsForFile( pd3dDevice, strFullPath, strFile, pSettings );
				}
			}
			bSuccess = FindNextFile( hFindFile, &fileData );

			if( pSettings->bUserAbort )
				break;
		}
		FindClose( hFindFile );
	}
}


//--------------------------------------------------------------------------------------
void SearchDirForFile( IDirect3DDevice9* pd3dDevice, WCHAR* strDir, WCHAR* strFile, SETTINGS* pSettings )
{
	WCHAR strFullPath[MAX_PATH] = {0};
	WCHAR strSearchDir[MAX_PATH];
	wcscpy_s( strSearchDir, MAX_PATH, strDir );
	wcscat_s( strSearchDir, MAX_PATH, strFile );

	if( pSettings->bVerbose )
		wprintf( L"Searching dir %s for %s\n", strDir, strFile );

	WIN32_FIND_DATA fileData;
	ZeroMemory( &fileData, sizeof( WIN32_FIND_DATA ) );
	HANDLE hFindFile = FindFirstFile( strSearchDir, &fileData );
	if( hFindFile != INVALID_HANDLE_VALUE )
	{
		BOOL bSuccess = TRUE;
		while( bSuccess )
		{
			if( ( fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 )
			{
				wcscpy_s( strFullPath, MAX_PATH, strDir );
				wcscat_s( strFullPath, MAX_PATH, fileData.cFileName );
				ProcessOptionsFile( pd3dDevice, strFullPath, pSettings );
			}
			bSuccess = FindNextFile( hFindFile, &fileData );
			if( pSettings->bUserAbort )
				break;
		}
		FindClose( hFindFile );
	}
}

HRESULT GetSHCoeffsFromHDR(IDirect3DDevice9* pd3dDevice, const WCHAR* strCubeMapFile, float m_fSHData[3][D3DXSH_MAXORDER * D3DXSH_MAXORDER])
{
	HRESULT hr;


	LPDIRECT3DCUBETEXTURE9 m_pEnvironmentMap;
	LPDIRECT3DCUBETEXTURE9 m_pSHEnvironmentMap;

	WCHAR strPath[MAX_PATH];
	V_RETURN( DXUTFindDXSDKMediaFileCch( strPath, MAX_PATH, strCubeMapFile ) );
	V_RETURN( D3DXCreateCubeTextureFromFileEx( pd3dDevice, strPath, 512, 1, 0, D3DFMT_A16B16G16R16F,
		D3DPOOL_MANAGED, D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR, 0, NULL,
		NULL, &m_pEnvironmentMap ) );

	// Some devices don't support D3DFMT_A16B16G16R16F textures and thus
	// D3DX will return the texture in a HW compatible format with the possibility of losing its 
	// HDR lighting information.  This will change the SH values returned from D3DXSHProjectCubeMap()
	// as the cube map will no longer be HDR.  So if this happens, create a load the cube map on 
	// scratch memory and project using that cube map.  But keep the other one around to render the 
	// background texture with.
	bool bUsedScratchMem = false;
	D3DSURFACE_DESC desc;
	ZeroMemory( &desc, sizeof( D3DSURFACE_DESC ) );
	m_pEnvironmentMap->GetLevelDesc( 0, &desc );
	if( desc.Format != D3DFMT_A16B16G16R16F )
	{
		LPDIRECT3DCUBETEXTURE9 pScratchEnvironmentMap = NULL;
		hr = D3DXCreateCubeTextureFromFileEx( pd3dDevice, strPath, 512, 1, 0, D3DFMT_A16B16G16R16F,
			D3DPOOL_SCRATCH, D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR, 0, NULL,
			NULL, &pScratchEnvironmentMap );
		if( SUCCEEDED( hr ) )
		{
			// prefilter the lighting environment by projecting onto the order 6 SH basis.  
			V( D3DXSHProjectCubeMap( 6, pScratchEnvironmentMap, m_fSHData[0], m_fSHData[1], m_fSHData[2] ) );
			bUsedScratchMem = true;
			SAFE_RELEASE( pScratchEnvironmentMap );
		}
	}

	if( !bUsedScratchMem )
	{
		// prefilter the lighting environment by projecting onto the order 6 SH basis.  
		V( D3DXSHProjectCubeMap( 6, m_pEnvironmentMap, m_fSHData[0], m_fSHData[1], m_fSHData[2] ) );
	}


	// reconstruct the prefiltered lighting environment into a cube map
	V( D3DXCreateCubeTexture( pd3dDevice, 256, 1, 0, D3DFMT_A16B16G16R16F,
		D3DPOOL_MANAGED, &m_pSHEnvironmentMap ) );
	SHCubeProj projData;
	projData.Init( m_fSHData[0], m_fSHData[1], m_fSHData[2] );
	V( D3DXFillCubeTexture( m_pSHEnvironmentMap, SHCubeFill, &projData ) );

	// This code will save a cubemap texture that represents the SH projection of the light probe if you want it
	V( D3DXSaveTextureToFile( L"shprojection.dds", D3DXIFF_DDS, m_pSHEnvironmentMap, NULL ) ); 

	return S_OK;
}

void GetLightCoeffs( IDirect3DDevice9* pd3dDevice, UINT dwOrder, float* pSHCoeffsRed, float* pSHCoeffsGreen, float* pSHCoeffsBlue )
{
	LPCWSTR strCubeMapFileList[] = { 
		L"Light Probes\\rnl_cross.dds",
		L"Light Probes\\uffizi_cross.dds",
		L"Light Probes\\galileo_cross.dds",
		L"Light Probes\\grace_cross.dds",
		L"Light Probes\\stpeters_cross.dds"
	};

	int g_nNumActiveLights = 1;
	int g_nNumActiveHDR = 1;
	D3DXVECTOR3 lightDirObjectSpace;
	D3DXMATRIX mWorldInv;
	D3DXMatrixInverse( &mWorldInv, NULL, g_Camera.GetWorldMatrix() );

	ZeroMemory( pSHCoeffsRed, D3DXSH_MAXORDER * D3DXSH_MAXORDER * sizeof( float ) );
	ZeroMemory( pSHCoeffsGreen, D3DXSH_MAXORDER * D3DXSH_MAXORDER * sizeof( float ) );
	ZeroMemory( pSHCoeffsBlue, D3DXSH_MAXORDER * D3DXSH_MAXORDER * sizeof( float ) );

	int i;
	for( i = 0; i < g_nNumActiveLights; i++ )
	{
		float fLight[3][D3DXSH_MAXORDER*D3DXSH_MAXORDER];
		// Transform the world space light dir into object space
		// Note that if there's multiple objects using PRT in the scene, then
		// for each object you need to either evaulate the lights in object space
		// evaulate the lights in world and rotate the light coefficients 
		// into object space.
	/*	D3DXVECTOR3 vLight = D3DXVECTOR3( sinf( D3DX_PI * 2 * i / g_nNumActiveLights - D3DX_PI / 6 ),
			5, -cosf( D3DX_PI * 2 * i / g_nNumActiveLights - D3DX_PI / 6 ) );*/
		D3DXVECTOR3 vLight = D3DXVECTOR3(10.0, 500.0, 10.0);
		D3DXVec3TransformNormal( &lightDirObjectSpace, &vLight, &mWorldInv );

		// This sample uses D3DXSHEvalDirectionalLight(), but there's other 
		// types of lights provided by D3DXSHEval*.  Pass in the 
		// order of SH, color of the light, and the direction of the light 
		// in object space.
		// The output is the source radiance coefficients for the SH basis functions.  
		// There are 3 outputs, one for each channel (R,G,B). 
		// Each output is an array of m_dwOrder^2 floats.  
		 D3DXSHEvalDirectionalLight( dwOrder, &lightDirObjectSpace, 0.5, 0.5, 0.5,
		fLight[0], fLight[1], fLight[2] );


		// D3DXSHAdd will add Order^2 floats.  There are 3 color channels, 
		// so call it 3 times.
		D3DXSHAdd( pSHCoeffsRed, dwOrder, pSHCoeffsRed, fLight[0] );
		D3DXSHAdd( pSHCoeffsGreen, dwOrder, pSHCoeffsGreen, fLight[1] );
		D3DXSHAdd( pSHCoeffsBlue, dwOrder, pSHCoeffsBlue, fLight[2] );
	}

	for( i = 0; i < g_nNumActiveHDR; i++)
	{
		float m_fSHData[3][D3DXSH_MAXORDER * D3DXSH_MAXORDER];

		float fLightProbe[3][D3DXSH_MAXORDER * D3DXSH_MAXORDER];
		float fLightProbeRot[3][D3DXSH_MAXORDER * D3DXSH_MAXORDER];
		ZeroMemory( &fLightProbe, sizeof(float) * 3 * D3DXSH_MAXORDER * D3DXSH_MAXORDER);
		ZeroMemory( &fLightProbeRot, sizeof(float) * 3 * D3DXSH_MAXORDER * D3DXSH_MAXORDER);

		GetSHCoeffsFromHDR( pd3dDevice, strCubeMapFileList[i], m_fSHData);

 		D3DXSHScale( fLightProbe[0], dwOrder, m_fSHData[0], 1.0f/g_nNumActiveHDR );
		D3DXSHScale( fLightProbe[1], dwOrder, m_fSHData[1], 1.0f/g_nNumActiveHDR );
		D3DXSHScale( fLightProbe[2], dwOrder, m_fSHData[2], 1.0f/g_nNumActiveHDR );
		D3DXSHRotate( fLightProbeRot[0], dwOrder, &mWorldInv, fLightProbe[0] );
		D3DXSHRotate( fLightProbeRot[1], dwOrder, &mWorldInv, fLightProbe[1] );
		D3DXSHRotate( fLightProbeRot[2], dwOrder, &mWorldInv, fLightProbe[2] );
		D3DXSHAdd( pSHCoeffsRed, dwOrder, pSHCoeffsRed, fLightProbeRot[0] );
		D3DXSHAdd( pSHCoeffsGreen, dwOrder, pSHCoeffsGreen, fLightProbeRot[1] );
		D3DXSHAdd( pSHCoeffsBlue, dwOrder, pSHCoeffsBlue, fLightProbeRot[2] );
	}
}

HRESULT SetCameras( IDirect3DDevice9* pd3dDevice, CONCAT_MESH *prtMesh, float& fObjectRadius, D3DXVECTOR3& vObjectCenter)
{
	HRESULT hr;

	// Update camera's viewing radius based on the object radius

	// Lock the vertex buffer to get the object's radius & center
	// simply to help position the camera a good distance away from the mesh.
	IDirect3DVertexBuffer9* pVB = NULL;
	void* pVertices;
	V_RETURN( prtMesh->pMesh->GetVertexBuffer( &pVB ) );
	V_RETURN( pVB->Lock( 0, 0, &pVertices, 0 ) );

	D3DVERTEXELEMENT9 Declaration[MAXD3DDECLLENGTH + 1];
	prtMesh->pMesh->GetDeclaration( Declaration );
	DWORD dwStride = D3DXGetDeclVertexSize( Declaration, 0 );
	V_RETURN( D3DXComputeBoundingSphere( ( D3DXVECTOR3* )pVertices, prtMesh->pMesh->GetNumVertices(),
		dwStride, &vObjectCenter,
		&fObjectRadius ) );

	pVB->Unlock();
	SAFE_RELEASE( pVB );

	g_Camera.SetRadius( fObjectRadius * 3.0f, fObjectRadius * 1.2f, fObjectRadius * 20.0f );
	g_Camera.SetModelCenter( vObjectCenter );
	/*D3DXMATRIX world;
	world = *g_Camera.GetWorldMatrix();
	world._41 = -1.0f * vObjectCenter.x;
	world._42 = -1.0f * vObjectCenter.y;
	world._43 = -1.0f * vObjectCenter.z;
	g_Camera.SetWorldMatrix( world );*/

	return S_OK;
}

//--------------------------------------------------------------------------------------
HRESULT ProcessOptionsFile( IDirect3DDevice9* pd3dDevice, WCHAR* strOptionsFileName, SETTINGS* pSettings )
{
	HRESULT hr;

	WCHAR sz[256] = {0};
	SIMULATOR_OPTIONS options;
	COptionsFile optFile;
	CONCAT_MESH prtMesh;
	CONCAT_MESH blockerMesh;
	CGrowableArray <VERTEXREF> vertexRefArray;
	CGrowableArray<D3DCOLORVALUE> irradianceArray;
	CCOMPRTBUFFER m_prtBuffer;

	float fObjectRadius;
	D3DXVECTOR3 vObjectCenter;

	std::wifstream in, irraIn;

	CEnvMapCal envMapCal;

	// Init structs
	ZeroMemory( &options, sizeof( SIMULATOR_OPTIONS ) );
	prtMesh.pMesh = NULL;
	prtMesh.dwNumMaterials = 0;
	blockerMesh.pMesh = NULL;
	blockerMesh.dwNumMaterials = 0;

	D3DXMATRIX world;

	// Load options xml file
	swprintf_s( sz, 256, L"Reading options file: %s\n\n", strOptionsFileName );
	wprintf( sz );
	if( FAILED( hr = optFile.LoadOptions( strOptionsFileName, &options ) ) )
	{
		wprintf( L"Error: Failure reading options file.  Ensure schema matchs example options.xml file\n" );
		goto LCleanup;
	}

	// Load and concat meshes to a single mesh
	if( FAILED( hr = LoadMeshes( pd3dDevice, &options, &prtMesh, &blockerMesh, pSettings ) ) )
	{
		wprintf( L"Error: Can not load meshes\n" );
		goto LCleanup;
	}

	// Save mesh
	DWORD dwFlags = ( options.bBinaryXFile ) ? D3DXF_FILEFORMAT_BINARY : D3DXF_FILEFORMAT_TEXT;
	if( prtMesh.pMesh )
	{
		swprintf_s( sz, 256, L"Saving concatenated PRT meshes: %s\n", options.strOutputConcatPRTMesh );
		wprintf( sz );
		if( FAILED( hr = D3DXSaveMeshToX( options.strOutputConcatPRTMesh, prtMesh.pMesh, NULL,
			prtMesh.materialArray.GetData(), NULL, prtMesh.dwNumMaterials, dwFlags ) ) )
		{
			wprintf( L"Error: Failed saving mesh\n" );
			goto LCleanup;
		}
	}
	if( blockerMesh.pMesh )
	{
		swprintf_s( sz, 256, L"Saving concatenated blocker meshes: %s\n", options.strOutputConcatBlockerMesh );
		wprintf( sz );
		if( FAILED( hr = D3DXSaveMeshToX( options.strOutputConcatBlockerMesh, blockerMesh.pMesh, NULL,
			blockerMesh.materialArray.GetData(), NULL, blockerMesh.dwNumMaterials,
			dwFlags ) ) )
		{
			wprintf( L"Error: Failed saving mesh\n" );
			goto LCleanup;
		}
	}

	if( prtMesh.pMesh == NULL )
	{
		wprintf( L"Error: Need at least 1 non-blocker mesh for PRT simulator\n" );
		hr = E_FAIL;
		goto LCleanup;
	}

	
	// Calculate the outgoing irradiance based on given lighting coeffcients.
	/*SetCameras(pd3dDevice, &prtMesh, fObjectRadius, vObjectCenter);
	
	float *pSHCoeffsRed, *pSHCoeffsGreen, *pSHCoeffsBlue;
	pSHCoeffsRed = new float[D3DXSH_MAXORDER * D3DXSH_MAXORDER];
	pSHCoeffsGreen = new float[D3DXSH_MAXORDER * D3DXSH_MAXORDER];
	pSHCoeffsBlue = new float[D3DXSH_MAXORDER * D3DXSH_MAXORDER];
	GetLightCoeffs( pd3dDevice, options.dwOrder, pSHCoeffsRed, pSHCoeffsGreen, pSHCoeffsBlue );

	 
	if( FAILED( hr = RunPRTSimulator( pd3dDevice, &options, &prtMesh, &blockerMesh, pSettings ) ) )
	{
		goto LCleanup;
	}

	if ( FAILED ( hr = m_prtBuffer.RunIrradianceCalculator( pd3dDevice, &options, pSettings, &prtMesh, &vertexRefArray, irradianceArray, pSHCoeffsRed, pSHCoeffsGreen, pSHCoeffsBlue) ) )
	{
		goto LCleanup;
	}*/

	//Estimate the lighting coefficients, and then calculate the ourgoing irradiance
	if( FAILED( hr = RunPRTSimulator( pd3dDevice, &options, &prtMesh, &blockerMesh, pSettings ) ) )
	{
		goto LCleanup;
	}

	world = *g_Camera.GetWorldMatrix();

	if(  FAILED( hr = RunVertexRefRecorder( pd3dDevice, &options, pSettings, &prtMesh, vertexRefArray, &world ) ) )
	{
		goto LCleanup;
	}

	//output the transfered coeffcients for MATLAB optimization
	m_prtBuffer.ExtractCompressedData(&options, &prtMesh, &vertexRefArray);
	envMapCal.ExtractPRTData( &options, &prtMesh, &vertexRefArray );

	//Run the following code only after the optimization has been done and irradiance is calculated with the help of MATLAB
	
	// This code will save a cubemap texture that represents the SH projection of the light probe if you want it
	/*in.open("coeffs.txt", std::ios::in);
	for( UINT i = 0; i < options.dwOrder * options.dwOrder; i++){
		in >> pSHCoeffsRed[i];
		 pSHCoeffsRed[i] =  pSHCoeffsRed[i] / 1e3;
	}
	for( UINT i = 0; i < options.dwOrder * options.dwOrder; i++){
		in >> pSHCoeffsGreen[i];
		pSHCoeffsGreen[i] =  pSHCoeffsGreen[i] / 1e3;
	}
	for( UINT i = 0; i < options.dwOrder * options.dwOrder; i++){
		in >> pSHCoeffsBlue[i];
		pSHCoeffsBlue[i] =  pSHCoeffsBlue[i] / 1e3;
	}
	
	
	LPDIRECT3DCUBETEXTURE9 m_pSHEnvironmentMap;
	// reconstruct the prefiltered lighting environment into a cube map
	V( D3DXCreateCubeTexture( pd3dDevice, 256, 1, 0, D3DFMT_A16B16G16R16F,
		D3DPOOL_MANAGED, &m_pSHEnvironmentMap ) );
	SHCubeProj projData;
	projData.Init( pSHCoeffsRed, pSHCoeffsGreen, pSHCoeffsBlue );
	V( D3DXFillCubeTexture( m_pSHEnvironmentMap, SHCubeFill, &projData ) );	
	V( D3DXSaveTextureToFile( L"projection_dino.dds", D3DXIFF_DDS, m_pSHEnvironmentMap, NULL ) ); 
	*/

	irraIn.open("irraMatlab.txt", std::ios::in);
	for( UINT i = 0; i < prtMesh.pMesh->GetNumVertices(); i++ )
	{
		D3DCOLORVALUE value;
		irraIn >> value.r >> value.g >> value.b;
		irradianceArray.Add(value);
	}

	if ( FAILED( hr = RunMetricCalculator( pSettings, &vertexRefArray, &irradianceArray, fObjectRadius ) ) )
	{
		goto LCleanup;
	}

LCleanup:

	// Cleanup
	optFile.FreeOptions( &options );
	for( int i = 0; i < prtMesh.materialArray.GetSize(); i++ )
		SAFE_DELETE_ARRAY( prtMesh.materialArray[i].pTextureFilename );
	SAFE_RELEASE( prtMesh.pMesh );
	for( int i = 0; i < blockerMesh.materialArray.GetSize(); i++ )
		SAFE_DELETE_ARRAY( blockerMesh.materialArray[i].pTextureFilename );
	SAFE_RELEASE( blockerMesh.pMesh );

	return hr;
}


//--------------------------------------------------------------------------------------
IDirect3DDevice9* CreateNULLRefDevice()
{
	HRESULT hr;
	IDirect3D9* pD3D = Direct3DCreate9( D3D_SDK_VERSION );
	if( NULL == pD3D )
		return NULL;

	D3DDISPLAYMODE Mode;
	pD3D->GetAdapterDisplayMode( 0, &Mode );

	D3DPRESENT_PARAMETERS pp;
	ZeroMemory( &pp, sizeof( D3DPRESENT_PARAMETERS ) );
	pp.BackBufferWidth = 1;
	pp.BackBufferHeight = 1;
	pp.BackBufferFormat = Mode.Format;
	pp.BackBufferCount = 1;
	pp.SwapEffect = D3DSWAPEFFECT_COPY;
	pp.Windowed = TRUE;

	IDirect3DDevice9* pd3dDevice;
	hr = pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, GetConsoleWindow(),
		D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pd3dDevice );
	SAFE_RELEASE( pD3D );
	if( FAILED( hr ) || pd3dDevice == NULL )
		return NULL;

	return pd3dDevice;
}


//--------------------------------------------------------------------------------------
HRESULT LoadMeshes( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions,
				   CONCAT_MESH* pPRTMesh, CONCAT_MESH* pBlockerMesh, SETTINGS* pSettings )
{
	HRESULT hr;
	ID3DXBuffer* pMaterialBuffer = NULL;
	DWORD dwNumMaterials = 0;
	D3DXMATERIAL* pMaterials = NULL;

	CGrowableArray <ID3DXMesh*> blockerMeshArray;
	CGrowableArray <D3DXMATRIX> blockerMatrixArray;

	CGrowableArray <ID3DXMesh*> prtMeshArray;
	CGrowableArray <D3DXMATRIX> prtMatrixArray;

	pPRTMesh->pMesh = NULL;
	pPRTMesh->dwNumMaterials = 0;
	pBlockerMesh->pMesh = NULL;
	pBlockerMesh->dwNumMaterials = 0;

	DWORD iMesh;
	CPlyLoader plyLoader;

	for( iMesh = 0; iMesh < pOptions->dwNumMeshes; iMesh++ )
	{
		if( pSettings->bVerbose )
			wprintf( L"Looking for mesh: %s\n", pOptions->pInputMeshes[iMesh].strMeshFile );

		ID3DXMesh* pMesh = NULL;
		WCHAR str[MAX_PATH];
		WCHAR strMediaDir[MAX_PATH];
		if( FAILED( hr = DXUTFindDXSDKMediaFileCch( str, MAX_PATH, pOptions->pInputMeshes[iMesh].strMeshFile ) ) )
		{
			wprintf( L"Can not find mesh %s\n", str );
			return hr;
		}

		wprintf( L"Reading mesh: %s\n", str );

		// Store the directory where the mesh was found
		wcscpy_s( strMediaDir, MAX_PATH, str );
		WCHAR* pch = wcsrchr( strMediaDir, L'\\' );
		DXUTSetMediaSearchPath( strMediaDir );

		WCHAR* pModelType = wcsrchr( strMediaDir, L'.' );

		if( 0 == wcscmp( pModelType, L".x" ) )
		{
			// Load mesh from *.x file
			if( FAILED( hr = D3DXLoadMeshFromX( str, D3DXMESH_SYSTEMMEM, pd3dDevice, NULL,
				&pMaterialBuffer, NULL, &dwNumMaterials, &pMesh ) ) )
				return hr;

			V( AdjustMeshDecl( pd3dDevice, &pMesh ) );

			// Compact & attribute sort mesh
			DWORD* rgdwAdjacency = NULL;
			rgdwAdjacency = new DWORD[pMesh->GetNumFaces() * 3];
			if( rgdwAdjacency == NULL )
				return E_OUTOFMEMORY;
			V_RETURN( pMesh->GenerateAdjacency( 1e-6f, rgdwAdjacency ) );
			V_RETURN( pMesh->OptimizeInplace( D3DXMESHOPT_COMPACT | D3DXMESHOPT_ATTRSORT,
				rgdwAdjacency, NULL, NULL, NULL ) );
			delete []rgdwAdjacency;

			DWORD dwNumAttribs = 0;
			V( pMesh->GetAttributeTable( NULL, &dwNumAttribs ) );
			assert( dwNumAttribs == dwNumMaterials );

			// Loop over D3D materials in mesh and store them
			pMaterials = ( D3DXMATERIAL* )pMaterialBuffer->GetBufferPointer();
			UINT iMat;
			for( iMat = 0; iMat < dwNumMaterials; iMat++ )
			{
				D3DXMATERIAL mat;
				mat.MatD3D = pMaterials[iMat].MatD3D;
				if( pMaterials[iMat].pTextureFilename )
				{
					mat.pTextureFilename = new CHAR[MAX_PATH];
					if( mat.pTextureFilename )
					{
						WCHAR strTextureTemp[MAX_PATH];
						WCHAR strFullPath[MAX_PATH];
						CHAR strFullPathA[MAX_PATH];
						MultiByteToWideChar( CP_ACP, 0, pMaterials[iMat].pTextureFilename, -1, strTextureTemp, MAX_PATH );
						strTextureTemp[MAX_PATH - 1] = 0;
						DXUTFindDXSDKMediaFileCch( strFullPath, MAX_PATH, strTextureTemp );
						WideCharToMultiByte( CP_ACP, 0, strFullPath, -1, strFullPathA, MAX_PATH, NULL, NULL );
						strcpy_s( mat.pTextureFilename, MAX_PATH, strFullPathA );
					}
				}
				else
				{
					mat.pTextureFilename = NULL;
				}

				// handle if there's not enough SH materials specified for this mesh 
				D3DXSHMATERIAL shMat;
				if( pOptions->pInputMeshes[iMesh].dwNumSHMaterials == 0 )
				{
					shMat.Diffuse = D3DXCOLOR( 2.00f, 2.00f, 2.00f, 1.0f );
					shMat.Absorption = D3DXCOLOR( 0.0030f, 0.0030f, 0.0460f, 1.0f );
					shMat.bSubSurf = FALSE;
					shMat.RelativeIndexOfRefraction = 1.3f;
					shMat.ReducedScattering = D3DXCOLOR( 1.00f, 1.00f, 1.00f, 1.0f );
				}
				else
				{
					DWORD iSH = iMat;
					if( iSH >= pOptions->pInputMeshes[iMesh].dwNumSHMaterials )
						iSH = pOptions->pInputMeshes[iMesh].dwNumSHMaterials - 1;
					shMat = pOptions->pInputMeshes[iMesh].pSHMaterials[iSH];
				}

				if( pOptions->pInputMeshes[iMesh].bIsBlockerMesh )
				{
					pBlockerMesh->materialArray.Add( mat );
					pBlockerMesh->shMaterialArray.Add( shMat );
				}
				else
				{
					pPRTMesh->materialArray.Add( mat );
					pPRTMesh->shMaterialArray.Add( shMat );
				}
			}
			SAFE_RELEASE( pMaterialBuffer );
		}

		// load mesh from *.ply file, without a material nor texture
		else if( 0 == wcscmp( pModelType, L".ply" ) )
		{
			// Load mesh from *.ply file, compact and sort the mesh after loading
			plyLoader.Create(pd3dDevice, str);
			pMesh = plyLoader.GetMesh();

			V( AdjustMeshDecl( pd3dDevice, &pMesh ) );
			// Set the material of the ply mesh file

			dwNumMaterials = 1;
			if(plyLoader.GetNumMaterials() != 1)
			{
				wprintf( L"Error in the material of the ply model %s\n", str );
				return hr;
			}
			else{
				D3DXMATERIAL materials;
				Material* pMat = plyLoader.GetMaterial(0);

				if( pMat != NULL)
				{
					//Ambient
					materials.MatD3D.Ambient.r = pMat->vAmbient.x;
					materials.MatD3D.Ambient.g = pMat->vAmbient.y;
					materials.MatD3D.Ambient.b = pMat->vAmbient.z;
					materials.MatD3D.Ambient.a = pMat->fAlpha;

					//Diffuse
					materials.MatD3D.Diffuse.r = pMat->vDiffuse.x;
					materials.MatD3D.Diffuse.g = pMat->vDiffuse.y;
					materials.MatD3D.Diffuse.b = pMat->vDiffuse.z;
					materials.MatD3D.Diffuse.a = pMat->fAlpha;

					//Specular
					materials.MatD3D.Specular.r = pMat->vSpecular.x;
					materials.MatD3D.Specular.g = pMat->vSpecular.y;
					materials.MatD3D.Specular.b = pMat->vSpecular.z;
					materials.MatD3D.Specular.a = pMat->fAlpha;

					//Emissive
					materials.MatD3D.Emissive.r = 0;
					materials.MatD3D.Emissive.g = 0;
					materials.MatD3D.Emissive.b = 0;
					materials.MatD3D.Emissive.a = 0;

					//Power
					materials.MatD3D.Power = ( float )pMat->nShininess;

					materials.pTextureFilename = NULL;

				}

				// handle if there's not enough SH materials specified for this mesh 
				D3DXSHMATERIAL shMat;
				if( pOptions->pInputMeshes[iMesh].dwNumSHMaterials == 0 )
				{
					shMat.Diffuse = D3DXCOLOR( 2.00f, 2.00f, 2.00f, 1.0f );
					shMat.Absorption = D3DXCOLOR( 0.0030f, 0.0030f, 0.0460f, 1.0f );
					shMat.bSubSurf = FALSE;
					shMat.RelativeIndexOfRefraction = 1.3f;
					shMat.ReducedScattering = D3DXCOLOR( 1.00f, 1.00f, 1.00f, 1.0f );
				}
				else
				{
					shMat = pOptions->pInputMeshes[iMesh].pSHMaterials[0];
				}

				if( pOptions->pInputMeshes[iMesh].bIsBlockerMesh )
				{
					pBlockerMesh->materialArray.Add( materials );
					pBlockerMesh->shMaterialArray.Add( shMat );
				}
				else
				{
					pPRTMesh->materialArray.Add( materials );
					pPRTMesh->shMaterialArray.Add( shMat );
				}
			}
		}
		else
		{
			wprintf( L"Unknown type of model\n");
			return hr;
		}

		// Build world matrix from settings in options file
		D3DXMATRIX mScale, mTranslate, mRotate, mWorld;
		D3DXMatrixScaling( &mScale, pOptions->pInputMeshes[iMesh].vScale.x, pOptions->pInputMeshes[iMesh].vScale.y,
			pOptions->pInputMeshes[iMesh].vScale.z );
		D3DXMatrixRotationYawPitchRoll( &mRotate, pOptions->pInputMeshes[iMesh].fYaw,
			pOptions->pInputMeshes[iMesh].fPitch, pOptions->pInputMeshes[iMesh].fRoll );
		D3DXMatrixTranslation( &mTranslate, pOptions->pInputMeshes[iMesh].vTranslate.x,
			pOptions->pInputMeshes[iMesh].vTranslate.y,
			pOptions->pInputMeshes[iMesh].vTranslate.z );
		mWorld = mScale * mRotate * mTranslate;

		// Record data in arrays
		if( pOptions->pInputMeshes[iMesh].bIsBlockerMesh )
		{
			pBlockerMesh->dwNumMaterials += dwNumMaterials;
			pBlockerMesh->numMaterialsArray.Add( dwNumMaterials );
			blockerMeshArray.Add( pMesh );
			blockerMatrixArray.Add( mWorld );
		}
		else
		{
			pPRTMesh->dwNumMaterials += dwNumMaterials;
			pPRTMesh->numMaterialsArray.Add( dwNumMaterials );
			prtMeshArray.Add( pMesh );
			prtMatrixArray.Add( mWorld );
		}
	}

	ID3DXMesh** ppMeshes;
	DWORD dwNumMeshes;
	D3DXMATRIX* pGeomXForms;

	// Concat blocker meshes from arrays
	dwNumMeshes = blockerMeshArray.GetSize();
	if( dwNumMeshes > 0 )
	{
		LONGLONG dwNumVerts = 0;
		LONGLONG dwNumFaces = 0;
		for( DWORD iMesh = 0; iMesh < dwNumMeshes; iMesh++ )
		{
			dwNumVerts += blockerMeshArray[iMesh]->GetNumVertices();
			dwNumFaces += blockerMeshArray[iMesh]->GetNumFaces();
		}

		DWORD dwFlags = D3DXMESH_SYSTEMMEM;
		if( dwNumVerts > 0xFFFF || dwNumFaces > 0xFFFF )
			dwFlags |= D3DXMESH_32BIT;

		ppMeshes = blockerMeshArray.GetData();
		pGeomXForms = blockerMatrixArray.GetData();
		V_RETURN( D3DXConcatenateMeshes( ppMeshes, dwNumMeshes, dwFlags, pGeomXForms, NULL, NULL,
			pd3dDevice, &pBlockerMesh->pMesh ) );
		for( int i = 0; i < blockerMeshArray.GetSize(); i++ )
			SAFE_RELEASE( blockerMeshArray[i] );

		NormalizeNormals( pBlockerMesh->pMesh );
	}

	// Concat prt meshes from arrays
	dwNumMeshes = prtMeshArray.GetSize();
	if( dwNumMeshes > 0 )
	{
		LONGLONG dwNumVerts = 0;
		LONGLONG dwNumFaces = 0;
		for( DWORD iMesh = 0; iMesh < dwNumMeshes; iMesh++ )
		{
			dwNumVerts += prtMeshArray[iMesh]->GetNumVertices();
			dwNumFaces += prtMeshArray[iMesh]->GetNumFaces();
		}

		DWORD dwFlags = D3DXMESH_SYSTEMMEM;
		if( dwNumVerts > 0xFFFF || dwNumFaces > 0xFFFF )
			dwFlags |= D3DXMESH_32BIT;

		CGrowableArray <D3DXMATRIX> uvMatrixArray;
		for( int i = 0; i < prtMatrixArray.GetSize(); i++ )
		{
			D3DXMATRIX mat;
			D3DXMatrixIdentity( &mat );
			uvMatrixArray.Add( mat );
		}
		D3DXMATRIX* pUVXForms = uvMatrixArray.GetData();

		ppMeshes = prtMeshArray.GetData();
		pGeomXForms = prtMatrixArray.GetData();
		V_RETURN( D3DXConcatenateMeshes( ppMeshes, dwNumMeshes, dwFlags, pGeomXForms, pUVXForms, NULL,
			pd3dDevice, &pPRTMesh->pMesh ) );
		for( int i = 0; i < prtMeshArray.GetSize(); i++ )
			SAFE_RELEASE( prtMeshArray[i] );

		DWORD dwNumAttribs = 0;
		V( pPRTMesh->pMesh->GetAttributeTable( NULL, &dwNumAttribs ) );
		assert( dwNumAttribs == ( DWORD )pPRTMesh->materialArray.GetSize() );
		assert( dwNumAttribs > 0 );

		NormalizeNormals( pPRTMesh->pMesh );
	}



	return S_OK;
}


//--------------------------------------------------------------------------------------
void NormalizeNormals( ID3DXMesh* pMesh )
{
	HRESULT hr;
	BYTE* pV = NULL;

	D3DVERTEXELEMENT9 decl[MAXD3DDECLLENGTH + 1];
	pMesh->GetDeclaration( decl );
	int nNormalOffset = -1;
	for( int di = 0; di < MAX_FVF_DECL_SIZE; di++ )
	{
		if( decl[di].Usage == D3DDECLUSAGE_NORMAL )
		{
			nNormalOffset = decl[di].Offset;
			break;
		}
		if( decl[di].Stream == 255 )
			break;
	}
	if( nNormalOffset < 0 )
		return;

	V( pMesh->LockVertexBuffer( 0, ( void** )&pV ) );
	UINT uStride = pMesh->GetNumBytesPerVertex();
	BYTE* pNormals = pV + nNormalOffset;
	for( UINT uVert = 0; uVert < pMesh->GetNumVertices(); uVert++ )
	{
		D3DXVECTOR3* pCurNormal = ( D3DXVECTOR3* )pNormals;
		D3DXVec3Normalize( pCurNormal, pCurNormal );
		pNormals += uStride;
	}
	pMesh->UnlockVertexBuffer();
}


//--------------------------------------------------------------------------------------
void DisplayUsage()
{
	wprintf( L"\n" );
	wprintf( L"PRTCmdLine - a command line PRT simulator tool\n" );
	wprintf( L"\n" );
	wprintf( L"Usage: PRTCmdLine.exe [/s] [filename1] [filename2] ...\n" );
	wprintf( L"\n" );
	wprintf( L"where:\n" );
	wprintf( L"\n" );
	wprintf( L"  [/v]\t\tVerbose output.  Useful for debugging\n" );
	wprintf( L"  [/s]\t\tSearches in the specified directory and all subdirectoies of\n" );
	wprintf( L"  \t\teach filename\n" );
	wprintf( L"  [filename*]\tSpecifies the directory and XML files to read.  Wildcards are\n" );
	wprintf( L"  \t\tsupported.\n" );
	wprintf( L"  \t\tSee options.xml for an example options XML file\n" );
}

bool DoesMeshHaveUsage( ID3DXMesh* pMesh, BYTE Usage )
{
	D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
	pMesh->GetDeclaration( decl );

	for( int di = 0; di < MAX_FVF_DECL_SIZE; di++ )
	{
		if( decl[di].Usage == Usage )
			return true;
		if( decl[di].Stream == 255 )
			return false;
	}

	return false;
}

HRESULT AdjustMeshDecl( IDirect3DDevice9* pd3dDevice, ID3DXMesh** ppMesh )
{
	HRESULT hr;
	LPD3DXMESH pInMesh = *ppMesh;
	LPD3DXMESH pOutMesh = NULL;

	D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE] =
	{
		{0,  0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
		{0,  12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
		{0,  24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},

		{0,  32, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0},
		{0,  36, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 1},
		{0,  52, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 2},
		{0,  68, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 3},
		{0,  84, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 4},
		{0, 100, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 5},
		{0, 116, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 6},
		D3DDECL_END()
	};

	// To do CPCA, we need to store (g_dwNumPCAVectors + 1) scalers per vertex, so 
	// make the mesh have a known decl to store this data.  Since we can't do 
	// skinning and PRT at once, we use D3DDECLUSAGE_BLENDWEIGHT[0] 
	// to D3DDECLUSAGE_BLENDWEIGHT[6] to store our per vertex data needed for PRT.
	// Notice that D3DDECLUSAGE_BLENDWEIGHT[0] is a float1, and
	// D3DDECLUSAGE_BLENDWEIGHT[1]-D3DDECLUSAGE_BLENDWEIGHT[6] are float4.  This allows 
	// up to 24 PCA weights and 1 float that gives the vertex shader 
	// an index into the vertex's cluster's data
	V( pInMesh->CloneMesh( pInMesh->GetOptions(), decl, pd3dDevice, &pOutMesh ) );

	// Make sure there are normals which are required for lighting
	if( !DoesMeshHaveUsage( pInMesh, D3DDECLUSAGE_NORMAL ) )
		V( D3DXComputeNormals( pOutMesh, NULL ) );

	SAFE_RELEASE( pInMesh );

	*ppMesh = pOutMesh;

	return S_OK;
}

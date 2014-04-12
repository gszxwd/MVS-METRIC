//--------------------------------------------------------------------------------------
// File: EnvMapCal.cpp
//
// Desc: Extract the PRT for MATLAB, to calculate the lighting coeffcients
//--------------------------------------------------------------------------------------
#include "DXUT.h"
#include "SDKmisc.h"
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include <conio.h>
#include "config.h"
#include "PRTSim.h"
#include "VertexRef.h"
#include "EnvMapCal.h"

using namespace std;

HRESULT CEnvMapCal::ExtractPRTData( SIMULATOR_OPTIONS* pOptions, CONCAT_MESH *pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray )
{
	HRESULT hr;
	m_pMesh = pPRTMesh->pMesh;
	m_dwOrder = pOptions->dwOrder;
	m_materialDiffuse = pPRTMesh->materialArray.GetAt(0).MatD3D.Diffuse;
	m_vertexRefArray = vertexRefArray;

	if(  FAILED( hr = D3DXLoadPRTBufferFromFile( pOptions->strOutputPRTBuffer, &m_pPRTBuffer ) ) )
	{
		WCHAR sz[256];
		swprintf_s( sz, 256, L"\nError: Failed loading %s", pOptions->strOutputPRTBuffer );
		wprintf( sz );
		return S_FALSE;
	}

	UINT dwNumSamples = m_pPRTBuffer->GetNumSamples();
	UINT dwNumCoeffs = m_pPRTBuffer->GetNumCoeffs();
	UINT dwNumChannels = m_pPRTBuffer->GetNumChannels();
	BOOL bIsTexture = m_pPRTBuffer->IsTexture();

	wofstream fout("PRT.txt");
	float *buffer;
	V( m_pPRTBuffer->LockBuffer(0, dwNumSamples, &buffer) );
	for( int i = 0; i < dwNumSamples * dwNumCoeffs * dwNumChannels; i++ )
		fout << buffer[i] << L"\n";
	fout.close();

	return S_OK;
}


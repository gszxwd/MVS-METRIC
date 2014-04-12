//--------------------------------------------------------------------------------------
// File: EnvMapCalculator.cpp
//
// Desc: To calculate the lighting coeffcients with the alglib optimization library.
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
#include "EnvMapCalculator.h"


struct VERTEXREF_STATE
{
	CRITICAL_SECTION cs;
	bool bUserAbort;
	bool bStopRecorder;
	bool bRunning;
	bool bFailed;
	bool bProgressMode;
	DWORD nNumPasses;
	DWORD nCurPass;
	float fPercentDone;
	WCHAR strCurPass[256];
};


//-----------------------------------------------------------------------------
// static helper function
//-----------------------------------------------------------------------------
DWORD WINAPI StaticEnvMapUIThreadProc( LPVOID lpParameter )
{
	VERTEXREF_STATE* pPRTState = ( VERTEXREF_STATE* )lpParameter;
	WCHAR sz[256] = {0};
	bool bStop = false;
	float fLastPercent = -1.0f;
	double fLastPercentAnnounceTime = 0.0f;

	DXUTGetGlobalTimer()->Start();

	while( !bStop )
	{
		EnterCriticalSection( &pPRTState->cs );

		if( pPRTState->bProgressMode )
		{
			if( pPRTState->fPercentDone < 0.0f || DXUTGetGlobalTimer()->GetTime() < fLastPercentAnnounceTime + 15.0f )
				swprintf_s( sz, 256, L"." );
			else
			{
				swprintf_s( sz, 256, L"%0.0f%%%%.", pPRTState->fPercentDone * 100.0f );
				fLastPercent = pPRTState->fPercentDone;
				fLastPercentAnnounceTime = DXUTGetGlobalTimer()->GetTime();
			}
			wprintf( sz );
		}

		while( _kbhit() )
		{
			int nChar = _getch();
			if( nChar == VK_ESCAPE )
			{
				wprintf( L"\n\nEscape key pressed. Aborting!\n" );
				pPRTState->bStopRecorder = true;
				pPRTState->bUserAbort = true;
				break;
			}
		}

		bStop = pPRTState->bStopRecorder;
		LeaveCriticalSection( &pPRTState->cs );

		if( !bStop )
			Sleep( 1000 );
	}

	return 0;
}

HRESULT CCOMPRTBUFFER::ExtractCompressedData( SIMULATOR_OPTIONS* pOptions, CONCAT_MESH *pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray )
{
	HRESULT hr;
	m_pMesh = pPRTMesh->pMesh;
	m_dwOrder = pOptions->dwOrder;
	m_materialDiffuse = pPRTMesh->materialArray.GetAt(0).MatD3D.Diffuse;
	m_vertexRefArray = vertexRefArray;

	if(  FAILED( hr = D3DXLoadPRTCompBufferFromFile( pOptions->strOutputCompPRTBuffer, &m_pPRTCompBuffer ) ) )
	{
		WCHAR sz[256];
		swprintf_s( sz, 256, L"\nError: Failed loading %s", pOptions->strOutputCompPRTBuffer );
		wprintf( sz );
		return S_FALSE;
	}

	BOOL bIsTexture = m_pPRTCompBuffer->IsTexture();
	// First call ID3DXPRTCompBuffer::NormalizeData.  
	// but it makes it easier to use data formats that have little presision.
	// It normalizes the PCA weights so that they are between [-1,1]
	// and modifies the basis vectors accordingly.  
	V( m_pPRTCompBuffer->NormalizeData() );

	UINT dwNumSamples = m_pPRTCompBuffer->GetNumSamples();
	UINT dwNumCoeffs = m_pPRTCompBuffer->GetNumCoeffs();
	UINT dwNumChannels = m_pPRTCompBuffer->GetNumChannels();
	UINT dwNumClusters = m_pPRTCompBuffer->GetNumClusters();
	UINT dwNumPCA = m_pPRTCompBuffer->GetNumPCA();

	// With clustered PCA, each vertex is assigned to a cluster.  To figure out 
	// which vertex goes with which cluster, call ID3DXPRTCompBuffer::ExtractClusterIDs.
	// This will return a cluster ID for every vertex.  Simply pass in an array of UINTs 
	// that is the size of the number of vertices (which also equals the number of samples), and 
	// the cluster ID for vertex N will be at puClusterIDs[N].
	UINT* pClusterIDs = new UINT[ dwNumSamples ];
	assert( pClusterIDs );
	if( pClusterIDs == NULL )
		return S_FALSE;
	V( m_pPRTCompBuffer->ExtractClusterIDs( pClusterIDs ) );

	D3DVERTEXELEMENT9 declCur[MAX_FVF_DECL_SIZE];
	m_pMesh->GetDeclaration( declCur );

	// Now use this cluster ID info to store a value in the mesh in the 
	// D3DDECLUSAGE_BLENDWEIGHT[0] which is declared in the vertex decl to be a float1
	// This value will be passed into the vertex shader to allow the shader 
	// use this number as an offset into an array of shader constants.  
	// The value we set per vertex is based on the cluster ID and the stride 
	// of the shader constants array.  
	BYTE* pV = NULL;
	V( m_pMesh->LockVertexBuffer( 0, ( void** )&pV ) );
	UINT uStride = m_pMesh->GetNumBytesPerVertex();
	BYTE* pClusterID = pV + 32; // 32 == D3DDECLUSAGE_BLENDWEIGHT[0] offset
	for( UINT uVert = 0; uVert < dwNumSamples; uVert++ )
	{
		float fArrayOffset = ( float )( pClusterIDs[uVert] * ( 1 + 3 * ( dwNumPCA / 4 ) ) );
		memcpy( pClusterID, &fArrayOffset, sizeof( float ) );
		pClusterID += uStride;
	}
	m_pMesh->UnlockVertexBuffer();
	SAFE_DELETE_ARRAY( pClusterIDs );

	// Now we also need to store the per vertex PCA weights.  Earilier when
	// the mesh was loaded, we changed the vertex decl to make room to store these
	// PCA weights.  In this sample, we will use D3DDECLUSAGE_BLENDWEIGHT[1] to 
	// D3DDECLUSAGE_BLENDWEIGHT[6].  Using D3DDECLUSAGE_BLENDWEIGHT intead of some other 
	// usage was an arbritatey decision.  Since D3DDECLUSAGE_BLENDWEIGHT[1-6] were 
	// declared as float4 then we can store up to 6*4 PCA weights per vertex.  They don't
	// have to be declared as float4, but its a reasonable choice.  So for example, 
	// if dwNumPCAVectors=16 the function will write data to D3DDECLUSAGE_BLENDWEIGHT[1-4]
	V( m_pPRTCompBuffer->ExtractToMesh( dwNumPCA, D3DDECLUSAGE_BLENDWEIGHT, 1, m_pMesh ) );

	// Extract the cluster bases into a large array of floats.  
	// ID3DXPRTCompBuffer::ExtractBasis will extract the basis 
	// for a single cluster.  
	//
	// A single cluster basis is an array of
	// (NumPCA+1)*NumCoeffs*NumChannels floats
	// The "1+" is for the cluster mean.
	int nClusterBasisSize = ( dwNumPCA + 1 ) * dwNumCoeffs * dwNumChannels;
	int nBufferSize = nClusterBasisSize * dwNumClusters;

	SAFE_DELETE_ARRAY( m_aPRTClusterBases );
	m_aPRTClusterBases = new float[nBufferSize];
	assert( m_aPRTClusterBases );

	for( DWORD iCluster = 0; iCluster < dwNumClusters; iCluster++ )
	{
		// ID3DXPRTCompBuffer::ExtractBasis() extracts the basis for a single cluster at a time.
		V( m_pPRTCompBuffer->ExtractBasis( iCluster, &m_aPRTClusterBases[iCluster * nClusterBasisSize] ) );
	}

	SAFE_DELETE_ARRAY( m_aPRTConstants );
	m_aPRTConstants = new float[dwNumClusters * ( 4 + dwNumChannels * dwNumPCA )];
	assert( m_aPRTConstants );

	std::wofstream fout("Weights.txt");
	std::wofstream out("CompressedPRT.txt");
	if(fout == NULL)
		return S_FALSE;
	fout << L"NumSamples " << dwNumSamples << L"\n";
	fout << L"NumCoeffs " << dwNumCoeffs << L"\n";
	fout << L"NumChannels " << dwNumChannels << L"\n";
	fout << L"NumClusters " << dwNumClusters << L"\n";
	fout << L"NumPCA " << dwNumPCA << L"\n\n";

	float* pVertex = NULL;
	V( m_pMesh->LockVertexBuffer( 0, ( void** )&pVertex ) );
	for( UINT uVert = 0; uVert < dwNumSamples; uVert++ )
	{
		fout << L"Index " << uVert << L"\n";
		fout << L"Position " << pVertex[33*uVert] << L" "
			<< pVertex[33*uVert+1] << L" "
			<< pVertex[33*uVert+2] << L"\n";
		fout << L"Normal " << pVertex[33*uVert+3] << L" "
			<< pVertex[33*uVert+4] << L" "
			<< pVertex[33*uVert+5] << L"\n";
		fout << "Offset " << pVertex[33*uVert+8] << L"\n";
		fout << "Weight ";
		for( int i = 0; i < dwNumPCA; i++ )
			fout << pVertex[33*uVert+9+i] << L" ";
		fout << L"\n\n\n";

		DWORD dwBasisStride = dwNumCoeffs * dwNumChannels * ( dwNumPCA + 1 );
		for( int k = 0; k < dwNumChannels; k++ ){
			for( int i = 0; i < dwNumCoeffs; i++) 
			{
				float temp;
				UINT offset = pVertex[33*uVert+8] / (1 + dwNumChannels * (dwNumPCA / 4));
				temp = m_aPRTClusterBases[offset*dwBasisStride + k * dwNumCoeffs + i];
				for( int j = 0; j < dwNumPCA; j++ ) 
				{
					int nOffset = offset * dwBasisStride + ( j + 1 ) * dwNumCoeffs * dwNumChannels;
					temp += pVertex[33*uVert+9+j] * m_aPRTClusterBases[nOffset + k * dwNumCoeffs +i];
				}
				out << temp << L"\n";
			}
		}

	}
	out.close();
	fout.close();

	fout.open("PRTClusterBases.txt", std::ios::out);
	for( UINT i = 0; i < nBufferSize; i++)
		fout << m_aPRTClusterBases[i] << L"\n";

	m_pMesh->UnlockVertexBuffer();
	fout.close();

	return S_OK;
}


//--------------------------------------------------------------------------------------
void CCOMPRTBUFFER::ComputeShaderConstants( float* pSHCoeffsRed, float* pSHCoeffsGreen, float* pSHCoeffsBlue )
{
	UINT dwNumCoeffs = m_pPRTCompBuffer->GetNumCoeffs();
	UINT dwOrder = m_dwOrder;
	UINT dwNumChannels = m_pPRTCompBuffer->GetNumChannels();
	UINT dwNumClusters = m_pPRTCompBuffer->GetNumClusters();
	UINT dwNumPCA = m_pPRTCompBuffer->GetNumPCA();

	//
	// With compressed PRT, a single diffuse channel is caluated by:
	//       R[p] = (M[k] dot L') + sum( w[p][j] * (B[k][j] dot L');
	// where the sum runs j between 0 and # of PCA vectors
	//       R[p] = exit radiance at point p
	//       M[k] = mean of cluster k 
	//       L' = source radiance approximated with SH coefficients
	//       w[p][j] = the j'th PCA weight for point p
	//       B[k][j] = the j'th PCA basis vector for cluster k
	//
	// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
	// these values are passed as constants using the array m_aPRTConstants.   
	// 
	// So we compute an array of floats, m_aPRTConstants, here.
	// This array is the L' dot M[k] and L' dot B[k][j].
	// The source radiance is the lighting environment in terms of spherical
	// harmonic coefficients which can be computed with D3DXSHEval* or D3DXSHProjectCubeMap.  
	// M[k] and B[k][j] are also in terms of spherical harmonic basis coefficients 
	// and come from ID3DXPRTCompBuffer::ExtractBasis().
	//
	DWORD dwClusterStride = dwNumChannels * dwNumPCA + 4;
	DWORD dwBasisStride = dwNumCoeffs * dwNumChannels * ( dwNumPCA + 1 );

	for( DWORD iCluster = 0; iCluster < dwNumClusters; iCluster++ )
	{
		// For each cluster, store L' dot M[k] per channel, where M[k] is the mean of cluster k
		m_aPRTConstants[iCluster * dwClusterStride + 0] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[iCluster *
			dwBasisStride + 0 * dwNumCoeffs], pSHCoeffsRed );
		m_aPRTConstants[iCluster * dwClusterStride + 1] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[iCluster *
			dwBasisStride + 1 * dwNumCoeffs], pSHCoeffsGreen );
		m_aPRTConstants[iCluster * dwClusterStride + 2] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[iCluster *
			dwBasisStride + 2 * dwNumCoeffs], pSHCoeffsBlue );
		m_aPRTConstants[iCluster * dwClusterStride + 3] = 0.0f;

		// Then per channel we compute L' dot B[k][j], where B[k][j] is the jth PCA basis vector for cluster k
		float* pPCAStart = &m_aPRTConstants[iCluster * dwClusterStride + 4];
		for( DWORD iPCA = 0; iPCA < dwNumPCA; iPCA++ )
		{
			int nOffset = iCluster * dwBasisStride + ( iPCA + 1 ) * dwNumCoeffs * dwNumChannels;

			pPCAStart[0 * dwNumPCA + iPCA] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[nOffset + 0 * dwNumCoeffs],
				pSHCoeffsRed );
			pPCAStart[1 * dwNumPCA + iPCA] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[nOffset + 1 * dwNumCoeffs],
				pSHCoeffsGreen );
			pPCAStart[2 * dwNumPCA + iPCA] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[nOffset + 2 * dwNumCoeffs],
				pSHCoeffsBlue );
		}
	}
}

void CCOMPRTBUFFER::GetPRTDiffuse(const real_1d_array &red, const real_1d_array &green, const real_1d_array &blue,  CGrowableArray<D3DCOLORVALUE> &irradianceArray)
{
	UINT dwOrder = m_dwOrder;

	float *pSHCoeffsRed, *pSHCoeffsGreen, *pSHCoeffsBlue;
	pSHCoeffsRed = new float[dwOrder * dwOrder];
	pSHCoeffsGreen = new float[dwOrder * dwOrder];
	pSHCoeffsBlue = new float[dwOrder * dwOrder];
	for( UINT i = 0; i < dwOrder*dwOrder; i++){
		pSHCoeffsRed[i] = red[i];
		pSHCoeffsGreen[i] = green[i];
		pSHCoeffsBlue[i] = blue[i];
	}
	ComputeShaderConstants( pSHCoeffsRed, pSHCoeffsGreen, pSHCoeffsBlue );

	float* pV = NULL;
	m_pMesh->LockVertexBuffer( 0, ( void** )&pV );
	DWORD dwNumVertices = m_pMesh->GetNumVertices();
	for( UINT uVert = 8; uVert < dwNumVertices * 33; uVert = uVert + 33 )
	{
		int iClusterOffset = (int)(pV[uVert]);

		DWORD NUM_PCA = m_pPRTCompBuffer->GetNumPCA();
		float *vPCAWeights = new float[NUM_PCA];
		for(UINT i = 0; i < NUM_PCA; i++)
		{
			vPCAWeights[i] = pV[uVert + 1 + i];
		}
		// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
		// these values are passed in as the array aPRTConstants. 
		float vAccumR[4];
		float vAccumG[4];
		float vAccumB[4];
		// For each channel, multiply and sum all the vPCAWeights[j] by aPRTConstants[x] 
		// where: vPCAWeights[j] is w[p][j]
		//		  aPRTConstants[x] is the value of (B[k][j] dot L') that was
		//		  calculated on the CPU and passed in as a shader constant
		// Note this code is multipled and added 4 floats at a time since each 
		// register is a 4-D vector, and is the reason for using (NUM_PCA/4)
		for (UINT j=0; j < NUM_PCA/4; j++) 
		{
			for(int i = 0; i < 4; i++)
			{
				vAccumR[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*0+4*j+i];
				vAccumG[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*1+4*j+i];
				vAccumB[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*2+4*j+i];
			}

		} 

		// Now for each channel, sum the 4D vector and add aPRTConstants[x] 
		// where: aPRTConstants[x] which is the value of (M[k] dot L') and
		//		  was calculated on the CPU and passed in as a shader constant.
		D3DCOLORVALUE value;
		value.r = m_aPRTConstants[4 * iClusterOffset];
		value.g = m_aPRTConstants[4 * iClusterOffset + 1];
		value.b = m_aPRTConstants[4 * iClusterOffset + 2];
		value.a = m_aPRTConstants[4 * iClusterOffset + 3];
		for(int i = 0; i < 4 ; i++){
			value.r += vAccumR[i];
			value.g += vAccumG[i];
			value.b += vAccumB[i];
		}
		// For spectral simulations the material properity is baked into the transfer coefficients.
		// If using nonspectral, then you can modulate by the diffuse material properity here.

		value.r *= m_materialDiffuse.r;
		value.g *= m_materialDiffuse.g;
		value.b *= m_materialDiffuse.b;

		irradianceArray.Add(value);

	}
	m_pMesh->UnlockVertexBuffer();

}


void CCOMPRTBUFFER::GetPRTDiffuse(float* pSHCoeffsRed, float* pSHCoeffsGreen, float* pSHCoeffsBlue,  CGrowableArray<D3DCOLORVALUE> &irradianceArray)
{

	ComputeShaderConstants( pSHCoeffsRed, pSHCoeffsGreen, pSHCoeffsBlue );

	float* pV = NULL;
	m_pMesh->LockVertexBuffer( 0, ( void** )&pV );
	DWORD dwNumVertices = m_pMesh->GetNumVertices();
	for( UINT uVert = 8; uVert < dwNumVertices * 33; uVert = uVert + 33 )
	{
		int iClusterOffset = (int)(pV[uVert]);

		DWORD NUM_PCA = m_pPRTCompBuffer->GetNumPCA();
		float *vPCAWeights = new float[NUM_PCA];
		for(UINT i = 0; i < NUM_PCA; i++)
		{
			vPCAWeights[i] = pV[uVert + 1 + i];
		}
		// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
		// these values are passed in as the array aPRTConstants. 
		float vAccumR[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		float vAccumG[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		float vAccumB[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		// For each channel, multiply and sum all the vPCAWeights[j] by aPRTConstants[x] 
		// where: vPCAWeights[j] is w[p][j]
		//		  aPRTConstants[x] is the value of (B[k][j] dot L') that was
		//		  calculated on the CPU and passed in as a shader constant
		// Note this code is multipled and added 4 floats at a time since each 
		// register is a 4-D vector, and is the reason for using (NUM_PCA/4)
		for (UINT j=0; j < NUM_PCA/4; j++) 
		{
			for(int i = 0; i < 4; i++)
			{
				vAccumR[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*0+4*j+i];
				vAccumG[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*1+4*j+i];
				vAccumB[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*2+4*j+i];
			}

		} 

		// Now for each channel, sum the 4D vector and add aPRTConstants[x] 
		// where: aPRTConstants[x] which is the value of (M[k] dot L') and
		//		  was calculated on the CPU and passed in as a shader constant.
		D3DCOLORVALUE value;
		value.r = m_aPRTConstants[4 * iClusterOffset];
		value.g = m_aPRTConstants[4 * iClusterOffset + 1];
		value.b = m_aPRTConstants[4 * iClusterOffset + 2];
		value.a = m_aPRTConstants[4 * iClusterOffset + 3];
		for(int i = 0; i < 4 ; i++){
			value.r += vAccumR[i];
			value.g += vAccumG[i];
			value.b += vAccumB[i];
		}
		// For spectral simulations the material properity is baked into the transfer coefficients.
		// If using nonspectral, then you can modulate by the diffuse material properity here.

		value.r *= m_materialDiffuse.r;
		value.g *= m_materialDiffuse.g;
		value.b *= m_materialDiffuse.b;

		irradianceArray.Add(value);

	}
	m_pMesh->UnlockVertexBuffer();

}


void CCOMPRTBUFFER::ComputeRedConstants( float* pSHCoeffsRed )
{
	UINT dwNumCoeffs = m_pPRTCompBuffer->GetNumCoeffs();
	UINT dwOrder = m_dwOrder;
	UINT dwNumChannels = m_pPRTCompBuffer->GetNumChannels();
	UINT dwNumClusters = m_pPRTCompBuffer->GetNumClusters();
	UINT dwNumPCA = m_pPRTCompBuffer->GetNumPCA();

	//
	// With compressed PRT, a single diffuse channel is caluated by:
	//       R[p] = (M[k] dot L') + sum( w[p][j] * (B[k][j] dot L');
	// where the sum runs j between 0 and # of PCA vectors
	//       R[p] = exit radiance at point p
	//       M[k] = mean of cluster k 
	//       L' = source radiance approximated with SH coefficients
	//       w[p][j] = the j'th PCA weight for point p
	//       B[k][j] = the j'th PCA basis vector for cluster k
	//
	// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
	// these values are passed as constants using the array m_aPRTConstants.   
	// 
	// So we compute an array of floats, m_aPRTConstants, here.
	// This array is the L' dot M[k] and L' dot B[k][j].
	// The source radiance is the lighting environment in terms of spherical
	// harmonic coefficients which can be computed with D3DXSHEval* or D3DXSHProjectCubeMap.  
	// M[k] and B[k][j] are also in terms of spherical harmonic basis coefficients 
	// and come from ID3DXPRTCompBuffer::ExtractBasis().
	//
	DWORD dwClusterStride = dwNumChannels * dwNumPCA + 4;
	DWORD dwBasisStride = dwNumCoeffs * dwNumChannels * ( dwNumPCA + 1 );

	for( DWORD iCluster = 0; iCluster < dwNumClusters; iCluster++ )
	{
		// For each cluster, store L' dot M[k] per channel, where M[k] is the mean of cluster k
		m_aPRTConstants[iCluster * dwClusterStride + 0] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[iCluster *
			dwBasisStride + 0 * dwNumCoeffs], pSHCoeffsRed );
		m_aPRTConstants[iCluster * dwClusterStride + 3] = 0.0f;

		// Then per channel we compute L' dot B[k][j], where B[k][j] is the jth PCA basis vector for cluster k
		float* pPCAStart = &m_aPRTConstants[iCluster * dwClusterStride + 4];
		for( DWORD iPCA = 0; iPCA < dwNumPCA; iPCA++ )
		{
			int nOffset = iCluster * dwBasisStride + ( iPCA + 1 ) * dwNumCoeffs * dwNumChannels;

			pPCAStart[0 * dwNumPCA + iPCA] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[nOffset + 0 * dwNumCoeffs],
				pSHCoeffsRed );
		}
	}
}

void CCOMPRTBUFFER::ComputeGreenConstants( float* pSHCoeffsGreen )
{
	UINT dwNumCoeffs = m_pPRTCompBuffer->GetNumCoeffs();
	UINT dwOrder = m_dwOrder;
	UINT dwNumChannels = m_pPRTCompBuffer->GetNumChannels();
	UINT dwNumClusters = m_pPRTCompBuffer->GetNumClusters();
	UINT dwNumPCA = m_pPRTCompBuffer->GetNumPCA();

	//
	// With compressed PRT, a single diffuse channel is caluated by:
	//       R[p] = (M[k] dot L') + sum( w[p][j] * (B[k][j] dot L');
	// where the sum runs j between 0 and # of PCA vectors
	//       R[p] = exit radiance at point p
	//       M[k] = mean of cluster k 
	//       L' = source radiance approximated with SH coefficients
	//       w[p][j] = the j'th PCA weight for point p
	//       B[k][j] = the j'th PCA basis vector for cluster k
	//
	// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
	// these values are passed as constants using the array m_aPRTConstants.   
	// 
	// So we compute an array of floats, m_aPRTConstants, here.
	// This array is the L' dot M[k] and L' dot B[k][j].
	// The source radiance is the lighting environment in terms of spherical
	// harmonic coefficients which can be computed with D3DXSHEval* or D3DXSHProjectCubeMap.  
	// M[k] and B[k][j] are also in terms of spherical harmonic basis coefficients 
	// and come from ID3DXPRTCompBuffer::ExtractBasis().
	//
	DWORD dwClusterStride = dwNumChannels * dwNumPCA + 4;
	DWORD dwBasisStride = dwNumCoeffs * dwNumChannels * ( dwNumPCA + 1 );

	for( DWORD iCluster = 0; iCluster < dwNumClusters; iCluster++ )
	{
		// For each cluster, store L' dot M[k] per channel, where M[k] is the mean of cluster k
		m_aPRTConstants[iCluster * dwClusterStride + 1] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[iCluster *
			dwBasisStride + 1 * dwNumCoeffs],
			pSHCoeffsGreen );
		m_aPRTConstants[iCluster * dwClusterStride + 3] = 0.0f;

		// Then per channel we compute L' dot B[k][j], where B[k][j] is the jth PCA basis vector for cluster k
		float* pPCAStart = &m_aPRTConstants[iCluster * dwClusterStride + 4];
		for( DWORD iPCA = 0; iPCA < dwNumPCA; iPCA++ )
		{
			int nOffset = iCluster * dwBasisStride + ( iPCA + 1 ) * dwNumCoeffs * dwNumChannels;

			pPCAStart[1 * dwNumPCA + iPCA] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[nOffset + 1 * dwNumCoeffs],
				pSHCoeffsGreen );
		}
	}
}

void CCOMPRTBUFFER::ComputeBlueConstants( float* pSHCoeffsBlue )
{
	UINT dwNumCoeffs = m_pPRTCompBuffer->GetNumCoeffs();
	UINT dwOrder = m_dwOrder;
	UINT dwNumChannels = m_pPRTCompBuffer->GetNumChannels();
	UINT dwNumClusters = m_pPRTCompBuffer->GetNumClusters();
	UINT dwNumPCA = m_pPRTCompBuffer->GetNumPCA();

	//
	// With compressed PRT, a single diffuse channel is caluated by:
	//       R[p] = (M[k] dot L') + sum( w[p][j] * (B[k][j] dot L');
	// where the sum runs j between 0 and # of PCA vectors
	//       R[p] = exit radiance at point p
	//       M[k] = mean of cluster k 
	//       L' = source radiance approximated with SH coefficients
	//       w[p][j] = the j'th PCA weight for point p
	//       B[k][j] = the j'th PCA basis vector for cluster k
	//
	// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
	// these values are passed as constants using the array m_aPRTConstants.   
	// 
	// So we compute an array of floats, m_aPRTConstants, here.
	// This array is the L' dot M[k] and L' dot B[k][j].
	// The source radiance is the lighting environment in terms of spherical
	// harmonic coefficients which can be computed with D3DXSHEval* or D3DXSHProjectCubeMap.  
	// M[k] and B[k][j] are also in terms of spherical harmonic basis coefficients 
	// and come from ID3DXPRTCompBuffer::ExtractBasis().
	//
	DWORD dwClusterStride = dwNumChannels * dwNumPCA + 4;
	DWORD dwBasisStride = dwNumCoeffs * dwNumChannels * ( dwNumPCA + 1 );

	for( DWORD iCluster = 0; iCluster < dwNumClusters; iCluster++ )
	{
		// For each cluster, store L' dot M[k] per channel, where M[k] is the mean of cluster k
		m_aPRTConstants[iCluster * dwClusterStride + 2] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[iCluster *
			dwBasisStride + 2 * dwNumCoeffs], pSHCoeffsBlue );
		m_aPRTConstants[iCluster * dwClusterStride + 3] = 0.0f;

		// Then per channel we compute L' dot B[k][j], where B[k][j] is the jth PCA basis vector for cluster k
		float* pPCAStart = &m_aPRTConstants[iCluster * dwClusterStride + 4];
		for( DWORD iPCA = 0; iPCA < dwNumPCA; iPCA++ )
		{
			int nOffset = iCluster * dwBasisStride + ( iPCA + 1 ) * dwNumCoeffs * dwNumChannels;
			pPCAStart[2 * dwNumPCA + iPCA] = D3DXSHDot( dwOrder, &m_aPRTClusterBases[nOffset + 2 * dwNumCoeffs],
				pSHCoeffsBlue );
		}
	}
}

double redfunc_ptr( const real_1d_array &red, CCOMPRTBUFFER* comPRTBuffer)
{
	double func = 0.0;

	UINT dwOrder = comPRTBuffer->GetOrders();
	ID3DXMesh* m_pMesh = comPRTBuffer->GetMesh();
	ID3DXPRTCompBuffer* m_pPRTCompBuffer = comPRTBuffer->GetPRTCompBuffer();
	float* m_aPRTConstants = comPRTBuffer->GetPRTConstants();
	CGrowableArray<VERTEXREF> *m_vertexRefArray = comPRTBuffer->GetVertexRef();
	D3DCOLORVALUE m_materialDiffuse = comPRTBuffer->GetMaterial();

	float *pSHCoeffsRed;
	pSHCoeffsRed = new float[dwOrder * dwOrder];
	for( UINT i = 0; i < dwOrder*dwOrder; i++)
		pSHCoeffsRed[i] = red[i];
	comPRTBuffer->ComputeRedConstants( pSHCoeffsRed );

	float* pV = NULL;
	m_pMesh->LockVertexBuffer( 0, ( void** )&pV );
	DWORD dwNumVertices = m_pMesh->GetNumVertices();
	for( UINT uVert = 8; uVert < dwNumVertices * 33; uVert = uVert + 33 )
	{
		int iClusterOffset = (int)(pV[uVert]);

		DWORD NUM_PCA = m_pPRTCompBuffer->GetNumPCA();
		float *vPCAWeights = new float[NUM_PCA];
		for(UINT i = 0; i < NUM_PCA; i++)
		{
			vPCAWeights[i] = pV[uVert + 1 + i];
		}
		// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
		// these values are passed in as the array aPRTConstants. 
		float vAccumR[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		// For each channel, multiply and sum all the vPCAWeights[j] by aPRTConstants[x] 
		// where: vPCAWeights[j] is w[p][j]
		//		  aPRTConstants[x] is the value of (B[k][j] dot L') that was
		//		  calculated on the CPU and passed in as a shader constant
		// Note this code is multipled and added 4 floats at a time since each 
		// register is a 4-D vector, and is the reason for using (NUM_PCA/4)
		for (UINT j=0; j < NUM_PCA/4; j++) 
		{
			for(int i = 0; i < 4; i++)
			{
				vAccumR[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*0+4*j+i];
			}

		} 
		float diffuse = m_aPRTConstants[4 * iClusterOffset];
		for(int i = 0; i < 4 ; i++)
			diffuse += vAccumR[i];
		// For spectral simulations the material properity is baked into the transfer coefficients.
		// If using nonspectral, then you can modulate by the diffuse material properity here.

		diffuse *= m_materialDiffuse.r;
		// Now for each channel, sum the 4D vector and add aPRTConstants[x] 
		// where: aPRTConstants[x] which is the value of (M[k] dot L') and
		//		  was calculated on the CPU and passed in as a shader constant.
		for(int i = 0; i < m_vertexRefArray->GetSize(); i++)
		{
			for(int j = 0; j < m_vertexRefArray->GetAt(i).intensityArray.GetSize(); j++)
			{
				func += abs( diffuse - m_vertexRefArray->GetAt(i).intensityArray.GetAt(j).iRed );
			}
		}
	}
	m_pMesh->UnlockVertexBuffer();

	return func;

}

void red_func( const real_1d_array &red, double &func, void *ptr)
{
	func = redfunc_ptr( red, (CCOMPRTBUFFER*)ptr);
}

double greenfunc_ptr( const real_1d_array &green, CCOMPRTBUFFER* comPRTBuffer)
{
	double func = 0.0;

	UINT dwOrder = comPRTBuffer->GetOrders();
	ID3DXMesh* m_pMesh = comPRTBuffer->GetMesh();
	ID3DXPRTCompBuffer* m_pPRTCompBuffer = comPRTBuffer->GetPRTCompBuffer();
	float* m_aPRTConstants = comPRTBuffer->GetPRTConstants();
	CGrowableArray<VERTEXREF> *m_vertexRefArray = comPRTBuffer->GetVertexRef();
	D3DCOLORVALUE m_materialDiffuse = comPRTBuffer->GetMaterial();

	float *pSHCoeffsGreen;
	pSHCoeffsGreen = new float[dwOrder * dwOrder];
	for( UINT i = 0; i < dwOrder*dwOrder; i++)
		pSHCoeffsGreen[i] = green[i];
	comPRTBuffer->ComputeGreenConstants( pSHCoeffsGreen );

	float* pV = NULL;
	m_pMesh->LockVertexBuffer( 0, ( void** )&pV );
	DWORD dwNumVertices = m_pMesh->GetNumVertices();
	for( UINT uVert = 8; uVert < dwNumVertices * 33; uVert = uVert + 33 )
	{
		int iClusterOffset = (int)(pV[uVert]);

		DWORD NUM_PCA = m_pPRTCompBuffer->GetNumPCA();
		float *vPCAWeights = new float[NUM_PCA];
		for(UINT i = 0; i < NUM_PCA; i++)
		{
			vPCAWeights[i] = pV[uVert + 1 + i];
		}
		// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
		// these values are passed in as the array aPRTConstants. 
		float vAccumG[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		// For each channel, multiply and sum all the vPCAWeights[j] by aPRTConstants[x] 
		// where: vPCAWeights[j] is w[p][j]
		//		  aPRTConstants[x] is the value of (B[k][j] dot L') that was
		//		  calculated on the CPU and passed in as a shader constant
		// Note this code is multipled and added 4 floats at a time since each 
		// register is a 4-D vector, and is the reason for using (NUM_PCA/4)
		for (UINT j=0; j < NUM_PCA/4; j++) 
		{
			for(int i = 0; i < 4; i++)
			{
				vAccumG[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*1+4*j+i];
			}

		} 
		float diffuse = m_aPRTConstants[4 * iClusterOffset + 1];
		for(int i = 0; i < 4 ; i++)
			diffuse += vAccumG[i];
		// For spectral simulations the material properity is baked into the transfer coefficients.
		// If using nonspectral, then you can modulate by the diffuse material properity here.

		diffuse *= m_materialDiffuse.g;
		// Now for each channel, sum the 4D vector and add aPRTConstants[x] 
		// where: aPRTConstants[x] which is the value of (M[k] dot L') and
		//		  was calculated on the CPU and passed in as a shader constant.
		for(int i = 0; i < m_vertexRefArray->GetSize(); i++)
		{
			for(int j = 0; j < m_vertexRefArray->GetAt(i).intensityArray.GetSize(); j++)
			{
				func += abs( diffuse - m_vertexRefArray->GetAt(i).intensityArray.GetAt(j).iGreen );
			}
		}
	}
	m_pMesh->UnlockVertexBuffer();

	return func;

}

void green_func( const real_1d_array &green, double &func, void *ptr)
{
	func = greenfunc_ptr(green, (CCOMPRTBUFFER*)ptr);
}
double bluefunc_ptr(const real_1d_array &blue, CCOMPRTBUFFER* comPRTBuffer)
{
	double func = 0.0;

	UINT dwOrder = comPRTBuffer->GetOrders();
	ID3DXMesh* m_pMesh = comPRTBuffer->GetMesh();
	ID3DXPRTCompBuffer* m_pPRTCompBuffer = comPRTBuffer->GetPRTCompBuffer();
	float* m_aPRTConstants = comPRTBuffer->GetPRTConstants();
	CGrowableArray<VERTEXREF> *m_vertexRefArray = comPRTBuffer->GetVertexRef();
	D3DCOLORVALUE m_materialDiffuse = comPRTBuffer->GetMaterial();

	float *pSHCoeffsBlue;
	pSHCoeffsBlue = new float[dwOrder * dwOrder];
	for( UINT i = 0; i < dwOrder*dwOrder; i++)
		pSHCoeffsBlue[i] = blue[i];
	comPRTBuffer->ComputeBlueConstants( pSHCoeffsBlue );

	float* pV = NULL;
	m_pMesh->LockVertexBuffer( 0, ( void** )&pV );
	DWORD dwNumVertices = m_pMesh->GetNumVertices();
	for( UINT uVert = 8; uVert < dwNumVertices * 33; uVert = uVert + 33 )
	{
		int iClusterOffset = (int)(pV[uVert]);

		DWORD NUM_PCA = m_pPRTCompBuffer->GetNumPCA();
		float *vPCAWeights = new float[NUM_PCA];
		for(UINT i = 0; i < NUM_PCA; i++)
		{
			vPCAWeights[i] = pV[uVert + 1 + i];
		}
		// Note: since both (M[k] dot L') and (B[k][j] dot L') can be computed on the CPU, 
		// these values are passed in as the array aPRTConstants. 
		float vAccumB[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		// For each channel, multiply and sum all the vPCAWeights[j] by aPRTConstants[x] 
		// where: vPCAWeights[j] is w[p][j]
		//		  aPRTConstants[x] is the value of (B[k][j] dot L') that was
		//		  calculated on the CPU and passed in as a shader constant
		// Note this code is multipled and added 4 floats at a time since each 
		// register is a 4-D vector, and is the reason for using (NUM_PCA/4)
		for (UINT j=0; j < NUM_PCA/4; j++) 
		{
			for(int i = 0; i < 4; i++)
			{
				vAccumB[i] += vPCAWeights[j*4 + i] * m_aPRTConstants[4*iClusterOffset+4+(NUM_PCA)*2+4*j+i];
			}

		} 
		float diffuse = m_aPRTConstants[4 * iClusterOffset + 2];
		for(int i = 0; i < 4 ; i++)
			diffuse += vAccumB[i];
		// For spectral simulations the material properity is baked into the transfer coefficients.
		// If using nonspectral, then you can modulate by the diffuse material properity here.

		diffuse *= m_materialDiffuse.b;
		// Now for each channel, sum the 4D vector and add aPRTConstants[x] 
		// where: aPRTConstants[x] which is the value of (M[k] dot L') and
		//		  was calculated on the CPU and passed in as a shader constant.
		for(int i = 0; i < m_vertexRefArray->GetSize(); i++)
		{
			for(int j = 0; j < m_vertexRefArray->GetAt(i).intensityArray.GetSize(); j++)
			{
				func += abs( diffuse - m_vertexRefArray->GetAt(i).intensityArray.GetAt(j).iBlue );
			}
		}
	}
	m_pMesh->UnlockVertexBuffer();

	return func;
}
void blue_func( const real_1d_array &blue, double &func, void *ptr )
{
	func = bluefunc_ptr(blue, (CCOMPRTBUFFER*)ptr);
}

HRESULT CCOMPRTBUFFER::RunEnvMapCalculator( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions,  SETTINGS* pSettings, CONCAT_MESH* pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray, CGrowableArray<D3DCOLORVALUE>& irradianceArray )
{
	DWORD dwResult;
	DWORD dwThreadId;
	HRESULT hr;

	VERTEXREF_STATE prtState;
	ZeroMemory( &prtState, sizeof( VERTEXREF_STATE ) );
	InitializeCriticalSection( &prtState.cs );
	prtState.bStopRecorder = false;
	prtState.bRunning = true;
	prtState.nNumPasses = 4;
	prtState.nCurPass = 1;
	prtState.fPercentDone = -1.0f;
	prtState.bProgressMode = true;

	wprintf( L"\nStarting recoder vertex reference.  Press ESC to abort\n" );

	HANDLE hThreadId = CreateThread( NULL, 0, StaticEnvMapUIThreadProc, ( LPVOID )&prtState, 0, &dwThreadId );

	//Stage 1:extract compressed data
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Extract compressed data..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = -1.0f;
	LeaveCriticalSection( &prtState.cs );

	V ( ExtractCompressedData( pOptions, pPRTMesh, vertexRefArray ) );

	//Stage 2: optimization, calculate the coefficients of the environment map
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Optimization: calculate the coefficients of the environment map..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	double epsg = 0.0000000001;
	double epsf = 0;
	double epsx = 0;
	double diffstep = 1.0e-6;
	ae_int_t maxits = 0;

	char str[256];
	UINT i = 0;
	str[i] = '[';
	for( i = 1; i < 2 * pOptions->dwOrder * pOptions->dwOrder; i= i + 2){
		str[i] = '0';
		str[i+1] = ',';
	}
	str[i-1] = ']';
	str[i] = '\0';

	real_1d_array red = str;
	mincgstate redstate;
	mincgreport redrep;
	mincgcreatef(red, diffstep, redstate);
	mincgsetcond(redstate, epsg, epsf, epsx, maxits);
	alglib::mincgoptimize(redstate, red_func, NULL, this);
	mincgresults(redstate, red, redrep);

	real_1d_array green = str;
	mincgstate greenstate;
	mincgreport greenrep;
	mincgcreatef(green, diffstep, greenstate);
	mincgsetcond(greenstate, epsg, epsf, epsx, maxits);
	alglib::mincgoptimize(greenstate, green_func, NULL, this);
	mincgresults(greenstate, green, greenrep);

	real_1d_array blue = str;
	mincgstate bluestate;
	mincgreport bluerep;
	mincgcreatef(blue, diffstep, bluestate);
	mincgsetcond(bluestate, epsg, epsf, epsx, maxits);
	alglib::mincgoptimize(bluestate, blue_func, NULL, this);
	mincgresults(bluestate, blue, bluerep);


	//Stage 3: Calculate the estimated irradiance for each vertex
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Calculate the estimated irradiance for each vertex..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	GetPRTDiffuse(red, green, blue, irradianceArray);

	//Stage 4: Save all the data
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Save all the data..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	std::wofstream wofOutputEnvMap("EnvMap.txt");
	if( wofOutputEnvMap == NULL )
	{
		wprintf( L"Can not save the vertex Reference\n");
		goto LEarlyExit;
	}
	wofOutputEnvMap << L"red    :" ;
	for( i = 1; i < m_dwOrder * m_dwOrder; i++)
		wofOutputEnvMap << red[i] << L" ";
	wofOutputEnvMap << L"\n\n";

	wofOutputEnvMap << L"green  :" ;
	for( i = 1; i < m_dwOrder * m_dwOrder; i++)
		wofOutputEnvMap << green[i] << L" ";
	wofOutputEnvMap << L"\n\n";

	wofOutputEnvMap << L"blue  :" ;
	for( i = 1; i < m_dwOrder * m_dwOrder; i++)
		wofOutputEnvMap << blue[i] << L" ";
	wofOutputEnvMap << L"\n\n";

	float* pV = NULL;
	pPRTMesh->pMesh->LockVertexBuffer( 0, ( void** )&pV );
	DWORD dwNumVertices = pPRTMesh->pMesh->GetNumVertices();
	for( UINT uVert = 0; uVert < dwNumVertices; uVert++ )
	{
		wofOutputEnvMap << L"position   " << pV[uVert*33] << L" " << pV[uVert*33+1] << L" " << pV[uVert*33+2] << L"\n";
		wofOutputEnvMap << L"irradiance " << irradianceArray.GetAt(uVert).r << L" "
			<< irradianceArray.GetAt(uVert).g << L" "
			<< irradianceArray.GetAt(uVert).b << L"\n\n";
	}
	pPRTMesh->pMesh->UnlockVertexBuffer();
	wofOutputEnvMap.close();

LEarlyExit:

	// Usually fails becaused user stoped the simulator
	EnterCriticalSection( &prtState.cs );
	prtState.bRunning = false;
	prtState.bStopRecorder = true;
	if( FAILED( hr ) )
		prtState.bFailed = true;
	pSettings->bUserAbort = prtState.bUserAbort;
	LeaveCriticalSection( &prtState.cs );

	if( SUCCEEDED( hr ) )
		wprintf( L"\n\nDone!\n" );

	// Wait for it to close
	dwResult = WaitForSingleObject( hThreadId, 10000 );
	if( dwResult == WAIT_TIMEOUT )
		return E_FAIL;

	DeleteCriticalSection( &prtState.cs );


	// It returns E_FAIL if the simulation was aborted from the callback
	if( hr == E_FAIL )
		return E_FAIL;

	if( FAILED( hr ) )
	{
		DXTRACE_ERR( TEXT( "EnvMapCalculator" ), hr );
		return hr;
	}

	return S_OK;
}





//Under known lighting conditions, calculate the irradiance
HRESULT CCOMPRTBUFFER::RunIrradianceCalculator( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions, SETTINGS* pSettings, CONCAT_MESH* pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray, 
											   CGrowableArray<D3DCOLORVALUE>& irradianceArray, float* pSHCoeffsRed, float* pSHCoeffsGreen, float* pSHCoeffsBlue)
{
	DWORD dwResult;
	DWORD dwThreadId;
	HRESULT hr;

	VERTEXREF_STATE prtState;
	ZeroMemory( &prtState, sizeof( VERTEXREF_STATE ) );
	InitializeCriticalSection( &prtState.cs );
	prtState.bStopRecorder = false;
	prtState.bRunning = true;
	prtState.nNumPasses = 3;
	prtState.nCurPass = 1;
	prtState.fPercentDone = -1.0f;
	prtState.bProgressMode = true;

	wprintf( L"\nStarting calculating the irradiance for each vertex.  Press ESC to abort\n" );

	HANDLE hThreadId = CreateThread( NULL, 0, StaticEnvMapUIThreadProc, ( LPVOID )&prtState, 0, &dwThreadId );

	//Stage 1:extract compressed data
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Extract compressed data..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = -1.0f;
	LeaveCriticalSection( &prtState.cs );

	V ( ExtractCompressedData( pOptions, pPRTMesh, vertexRefArray ) );

	//Stage 2: optimization, calculate the coefficients of the environment map
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Optimization: calculate the irradiance of each vertex..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	GetPRTDiffuse(pSHCoeffsRed, pSHCoeffsGreen, pSHCoeffsBlue, irradianceArray);

	//Stage 4: Save all the data
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Save all the data..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	std::wofstream wofOutputEnvMap("EnvMap.txt");
	if( wofOutputEnvMap == NULL )
	{
		wprintf( L"Can not save the irradiance\n");
		goto LEarlyExit;
	}
	for( int i = 0; i < m_dwOrder * m_dwOrder; i++)
		wofOutputEnvMap << pSHCoeffsRed[i] << L" ";
	wofOutputEnvMap << L"\n";
	for( int i = 0; i < m_dwOrder * m_dwOrder; i++)
		wofOutputEnvMap << pSHCoeffsGreen[i] << L" ";
	wofOutputEnvMap << L"\n";
	for( int i = 0; i < m_dwOrder * m_dwOrder; i++)
		wofOutputEnvMap << pSHCoeffsBlue[i] << L" ";
	wofOutputEnvMap << L"\n";
	float* pV = NULL;
	pPRTMesh->pMesh->LockVertexBuffer( 0, ( void** )&pV );
	DWORD dwNumVertices = pPRTMesh->pMesh->GetNumVertices();
	for( UINT uVert = 0; uVert < dwNumVertices; uVert++ )
	{
		wofOutputEnvMap << L"position   " << pV[uVert*33] << L" " << pV[uVert*33+1] << L" " << pV[uVert*33+2] << L"\n";
		wofOutputEnvMap << L"irradiance " << irradianceArray.GetAt(uVert).r << L" "
			<< irradianceArray.GetAt(uVert).g << L" "
			<< irradianceArray.GetAt(uVert).b << L"\n\n";
	}
	pPRTMesh->pMesh->UnlockVertexBuffer();
	wofOutputEnvMap.close();

LEarlyExit:

	// Usually fails becaused user stoped the simulator
	EnterCriticalSection( &prtState.cs );
	prtState.bRunning = false;
	prtState.bStopRecorder = true;
	if( FAILED( hr ) )
		prtState.bFailed = true;
	pSettings->bUserAbort = prtState.bUserAbort;
	LeaveCriticalSection( &prtState.cs );

	if( SUCCEEDED( hr ) )
		wprintf( L"\n\nDone!\n" );

	// Wait for it to close
	dwResult = WaitForSingleObject( hThreadId, 10000 );
	if( dwResult == WAIT_TIMEOUT )
		return E_FAIL;

	DeleteCriticalSection( &prtState.cs );


	// It returns E_FAIL if the simulation was aborted from the callback
	if( hr == E_FAIL )
		return E_FAIL;

	if( FAILED( hr ) )
	{
		DXTRACE_ERR( TEXT( "EnvMapCalculator" ), hr );
		return hr;
	}

	return S_OK;
}





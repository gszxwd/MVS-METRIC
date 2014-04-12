//--------------------------------------------------------------------------------------
// File: EnvMapCalculator.h
//
//
//--------------------------------------------------------------------------------------
#include "ALGLIB\stdafx.h"
#include "ALGLIB\optimization.h"

using namespace alglib;


class CCOMPRTBUFFER{
public:
	HRESULT ExtractCompressedData( SIMULATOR_OPTIONS* pOptions, CONCAT_MESH *pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray );
	void ComputeRedConstants( float* pSHCoeffsRed );
	void ComputeGreenConstants( float* pSHCoeffsGreen );
	void ComputeBlueConstants( float* pSHCoeffsBlue );
	HRESULT RunEnvMapCalculator( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions,  SETTINGS* pSettings, CONCAT_MESH* pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray, CGrowableArray<D3DCOLORVALUE>& irradianceArray );
	void ComputeShaderConstants( float* pSHCoeffsRed, float* pSHCoeffsGreen, float* pSHCoeffsBlue );
	void GetPRTDiffuse(const real_1d_array &red, const real_1d_array &green, const real_1d_array &blue,  CGrowableArray<D3DCOLORVALUE> &irradianceArray);
	void GetPRTDiffuse(float* pSHCoeffsRed, float* pSHCoeffsGreen, float* pSHCoeffsBlue,  CGrowableArray<D3DCOLORVALUE> &irradianceArray);
	HRESULT RunIrradianceCalculator( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions,  SETTINGS* pSettings, CONCAT_MESH* pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray, 
											   CGrowableArray<D3DCOLORVALUE>& irradianceArray, float* pSHCoeffsRed, float* pSHCoeffsGreen, float* pSHCoeffsBlue);

	CCOMPRTBUFFER()
	{
		 m_pPRTCompBuffer = NULL;
		 m_aPRTClusterBases = NULL;
		 m_aPRTConstants = NULL;
		 m_pMesh = NULL;
		 m_dwOrder = 0;
	}
	
	UINT GetOrders()
	{
		return m_dwOrder;
	}

	CGrowableArray<VERTEXREF>* GetVertexRef()
	{
		return m_vertexRefArray;
	}

	ID3DXPRTCompBuffer* GetPRTCompBuffer()
	{
		return m_pPRTCompBuffer;
	}

	float* GetPRTConstants()
	{
		return m_aPRTConstants;
	}

	ID3DXMesh* GetMesh()
	{
		return m_pMesh;
	}

	D3DCOLORVALUE GetMaterial()
	{
		return m_materialDiffuse;
	}


protected:

    ID3DXPRTCompBuffer* m_pPRTCompBuffer;
    // The basis buffer is a large array of floats where 
    // Call ID3DXPRTCompBuffer::ExtractBasis() to extract the basis 
    // for every cluster.  The basis for a cluster is an array of
    // (NumPCAVectors+1)*(NumChannels*Order^2) floats. 
    // The "1+" is for the cluster mean.
    float* m_aPRTClusterBases;
    // m_aPRTConstants stores the incident radiance dotted with the transfer function.
    // Each cluster has an array of floats which is the size of 
    // 4+MAX_NUM_CHANNELS*NUM_PCA_VECTORS. This number comes from: there can 
    // be up to 3 channels (R,G,B), and each channel can 
    // have up to NUM_PCA_VECTORS of PCA vectors.  Each cluster also has 
    // a mean PCA vector which is described with 4 floats (and hence the +4).
    float* m_aPRTConstants;

	ID3DXMesh* m_pMesh;

	UINT m_dwOrder;
	D3DCOLORVALUE m_materialDiffuse;

	CGrowableArray<VERTEXREF> *m_vertexRefArray;

};
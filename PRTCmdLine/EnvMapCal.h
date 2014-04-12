//--------------------------------------------------------------------------------------
// File: EnvMapCal.h
//
//
//--------------------------------------------------------------------------------------

#include "ALGLIB\stdafx.h"
#include "ALGLIB\optimization.h"

using namespace alglib;


class CEnvMapCal{

public:

	HRESULT ExtractPRTData( SIMULATOR_OPTIONS* pOptions, CONCAT_MESH *pPRTMesh, CGrowableArray<VERTEXREF> *vertexRefArray );

	CEnvMapCal()
	{
		 m_pPRTBuffer = NULL;
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

	ID3DXPRTBuffer* GetPRTBuffer()
	{
		return m_pPRTBuffer;
	}

	ID3DXMesh* GetMesh()
	{
		return m_pMesh;
	}

	D3DCOLORVALUE GetMaterial()
	{
		return m_materialDiffuse;
	}

private:

    ID3DXPRTBuffer* m_pPRTBuffer;
	ID3DXMesh* m_pMesh;
	UINT m_dwOrder;
	D3DCOLORVALUE m_materialDiffuse;
	CGrowableArray<VERTEXREF> *m_vertexRefArray;
};
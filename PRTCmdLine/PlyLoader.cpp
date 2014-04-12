//--------------------------------------------------------------------------------------
// File: PlyLoader.cpp
//
// Wrapper class for ID3DXMesh interface. Handles loading mesh data from an *.ply file
// and resource management for material textures.
//
//--------------------------------------------------------------------------------------
#include "DXUT.h"
#include "SDKmisc.h"
#pragma warning(disable: 4995)
#include "PlyLoader.h"
#include <fstream>
using namespace std;
#pragma warning(default: 4995)


// Vertex declaration
D3DVERTEXELEMENT9 VERTEX_DECL[] =
{
    { 0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT,  D3DDECLUSAGE_POSITION, 0},
    D3DDECL_END()
};


//--------------------------------------------------------------------------------------
CPlyLoader::CPlyLoader()
{
    m_pd3dDevice = NULL;
    m_pMesh = NULL;

    ZeroMemory( m_strMediaDir, sizeof( m_strMediaDir ) );
}


//--------------------------------------------------------------------------------------
CPlyLoader::~CPlyLoader()
{
   // Destroy();
}


//--------------------------------------------------------------------------------------
void CPlyLoader::Destroy()
{
    for( int iMaterial = 0; iMaterial < m_Materials.GetSize(); iMaterial++ )
    {
        Material* pMaterial = m_Materials.GetAt( iMaterial );

        // Avoid releasing the same texture twice
        for( int x = iMaterial + 1; x < m_Materials.GetSize(); x++ )
        {
            Material* pCur = m_Materials.GetAt( x );
            if( pCur->pTexture == pMaterial->pTexture )
                pCur->pTexture = NULL;
        }

        SAFE_RELEASE( pMaterial->pTexture );
        SAFE_DELETE( pMaterial );
    }

    m_Materials.RemoveAll();
    m_Vertices.RemoveAll();
    m_Indices.RemoveAll();
    m_Attributes.RemoveAll();

    SAFE_RELEASE( m_pMesh );
    m_pd3dDevice = NULL;
}


//--------------------------------------------------------------------------------------
HRESULT CPlyLoader::Create( IDirect3DDevice9* pd3dDevice, const WCHAR* strFilename )
{
    HRESULT hr;
    WCHAR str[ MAX_PATH ] = {0};

    // Start clean
    Destroy();

    // Store the device pointer
    m_pd3dDevice = pd3dDevice;

    // Load the vertex buffer, index buffer, and subset information from a file. In this case, 
    // an .obj file was chosen for simplicity, but it's meant to illustrate that ID3DXMesh objects
    // can be filled from any mesh file format once the necessary data is extracted from file.
    V_RETURN( LoadGeometryFromPly( strFilename ) );

    // Set the current directory based on where the mesh was found
    WCHAR wstrOldDir[MAX_PATH] = {0};
    GetCurrentDirectory( MAX_PATH, wstrOldDir );
    SetCurrentDirectory( m_strMediaDir );

    // Load material textures
    for( int iMaterial = 0; iMaterial < m_Materials.GetSize(); iMaterial++ )
    {
        Material* pMaterial = m_Materials.GetAt( iMaterial );
        if( pMaterial->strTexture[0] )
        {
            // Avoid loading the same texture twice
            bool bFound = false;
            for( int x = 0; x < iMaterial; x++ )
            {
                Material* pCur = m_Materials.GetAt( x );
                if( 0 == wcscmp( pCur->strTexture, pMaterial->strTexture ) )
                {
                    bFound = true;
                    pMaterial->pTexture = pCur->pTexture;
                    break;
                }
            }

            // Not found, load the texture
            if( !bFound )
            {
                V_RETURN( DXUTFindDXSDKMediaFileCch( str, MAX_PATH, pMaterial->strTexture ) );
                V_RETURN( D3DXCreateTextureFromFile( pd3dDevice, pMaterial->strTexture,
                                                     &( pMaterial->pTexture ) ) );
            }
        }
    }

    // Restore the original current directory
    SetCurrentDirectory( wstrOldDir );

    // Create the encapsulated mesh
    ID3DXMesh* pMesh = NULL;
    V_RETURN( D3DXCreateMesh( m_Indices.GetSize() / 3, m_Vertices.GetSize(),
                              D3DXMESH_MANAGED | D3DXMESH_32BIT, VERTEX_DECL,
                              pd3dDevice, &pMesh ) );

    // Copy the vertex data
    VERTEX* pVertex;
    V_RETURN( pMesh->LockVertexBuffer( 0, ( void** )&pVertex ) );
    memcpy( pVertex, m_Vertices.GetData(), m_Vertices.GetSize() * sizeof( VERTEX ) );
    pMesh->UnlockVertexBuffer();
    m_Vertices.RemoveAll();

    // Copy the index data
    DWORD* pIndex;
    V_RETURN( pMesh->LockIndexBuffer( 0, ( void** )&pIndex ) );
    memcpy( pIndex, m_Indices.GetData(), m_Indices.GetSize() * sizeof( DWORD ) );
    pMesh->UnlockIndexBuffer();
    m_Indices.RemoveAll();

    // Copy the attribute data
    DWORD* pSubset;
    V_RETURN( pMesh->LockAttributeBuffer( 0, &pSubset ) );
    memcpy( pSubset, m_Attributes.GetData(), m_Attributes.GetSize() * sizeof( DWORD ) );
    pMesh->UnlockAttributeBuffer();
    m_Attributes.RemoveAll();

    // Reorder the vertices according to subset and optimize the mesh for this graphics 
    // card's vertex cache. When rendering the mesh's triangle list the vertices will 
    // cache hit more often so it won't have to re-execute the vertex shader.
    DWORD* aAdjacency = new DWORD[pMesh->GetNumFaces() * 3];
    if( aAdjacency == NULL )
        return E_OUTOFMEMORY;

    V( pMesh->GenerateAdjacency( 1e-6f, aAdjacency ) );
    V( pMesh->OptimizeInplace( D3DXMESHOPT_ATTRSORT | D3DXMESHOPT_VERTEXCACHE, aAdjacency, NULL, NULL, NULL ) );

    SAFE_DELETE_ARRAY( aAdjacency );
    m_pMesh = pMesh;

    return S_OK;
}


//--------------------------------------------------------------------------------------
HRESULT CPlyLoader::LoadGeometryFromPly( const WCHAR* strFileName )
{
    WCHAR wstr[MAX_PATH];
    char str[MAX_PATH];
    HRESULT hr;

    // Find the file
    V_RETURN( DXUTFindDXSDKMediaFileCch( wstr, MAX_PATH, strFileName ) );
    WideCharToMultiByte( CP_ACP, 0, wstr, -1, str, MAX_PATH, NULL, NULL );

    // Store the directory where the mesh was found
    wcscpy_s( m_strMediaDir, MAX_PATH - 1, wstr );
    WCHAR* pch = wcsrchr( m_strMediaDir, L'\\' );
    if( pch )
        *pch = NULL;

    // Create temporary storage for the input data. Once the data has been loaded into
    // a reasonable format we can create a D3DXMesh object and load it with the mesh data.
    CGrowableArray <D3DXVECTOR3> Positions;

    // The first subset uses the default material
    Material* pMaterial = new Material();
    if( pMaterial == NULL )
        return E_OUTOFMEMORY;

    InitMaterial( pMaterial );
    wcscpy_s( pMaterial->strName, MAX_PATH - 1, L"default" );
    m_Materials.Add( pMaterial );

    DWORD dwCurSubset = 0;

    // File input from the binary ply file
    char strCommand[256] = {0};
	int iVertexNum, iFaceNum;
	ifstream InFile( str, ios::binary | ios::in);
    if( !InFile )
        return DXTRACE_ERR( L"ifstream::open", E_FAIL );

    while( !InFile.eof())
	{
        InFile >> strCommand;

        if( 0 == strcmp( strCommand, "ply" ) )
        {
            //header
        }
		else if( 0 == strcmp( strCommand, "format" ))
		{
			//format
		}
        else if( 0 == strcmp( strCommand, "comment" ) )
        {
			//comment
        }
        else if( 0 == strcmp( strCommand, "element" ) )
        {
			char strElementType[256];
			InFile >> strElementType;
			if(0 == strcmp( strElementType, "vertex") )
				InFile >> iVertexNum;
			else if(0 == strcmp( strElementType, "face") )
				InFile >> iFaceNum;
        }
        else if( 0 == strcmp( strCommand, "property" ) )
        {
            // for the ply file generated by the PossionRecon,
			// the vertex property is 
			// property float x
			// property float y
			// property float z
			// the face property is 
			// property list uchar int vertex_indices
        }
        else if( 0 == strcmp( strCommand, "end_header" ) )
        {
            // Read in the vertex and face data
			InFile.ignore( 1000, '\n' );
			for(int i = 0; i < iVertexNum; i++)
			{
				// Vertex Position
				 float x, y, z;
				 InFile.read( (char*)(&x), sizeof( x ) );
				 InFile.read( (char*)(&y), sizeof( y ) );
				 InFile.read( (char*)(&z), sizeof( z ) );
				 //bunny
				 /*x = x * 10;
				 y = (y - 0.1) * 10;
				 z = z * 10;*/
				 Positions.Add( D3DXVECTOR3( x, y, z ) );
			}
			for(int i = 0; i < iFaceNum; i++)
			{
				char num;
				InFile.read( &num, sizeof( num ));
				UINT iPosition;
				VERTEX vertex;
				for(int j = 0; j < 3; j++)
				{
					ZeroMemory( &vertex, sizeof( VERTEX ) );

					InFile.read((char*)(&iPosition), sizeof(iPosition));

					vertex.position = Positions[ iPosition ];

					DWORD index = AddVertex( iPosition, &vertex );
					if ( index == (DWORD)-1 )
						return E_OUTOFMEMORY;

					m_Indices.Add( index );
				}
				m_Attributes.Add( dwCurSubset );
			}
        }
        else
        {
            // Unimplemented or unrecognized command
        }

        InFile.ignore( 1000, '\n' );
    }

    // Cleanup
    InFile.close();
    DeleteCache();

    return S_OK;
}


//--------------------------------------------------------------------------------------
DWORD CPlyLoader::AddVertex( UINT hash, VERTEX* pVertex )
{
    // If this vertex doesn't already exist in the Vertices list, create a new entry.
    // Add the index of the vertex to the Indices list.
    bool bFoundInList = false;
    DWORD index = 0;

    // Since it's very slow to check every element in the vertex list, a hashtable stores
    // vertex indices according to the vertex position's index as reported by the OBJ file
    if( ( UINT )m_VertexCache.GetSize() > hash )
    {
        CacheEntry* pEntry = m_VertexCache.GetAt( hash );
        while( pEntry != NULL )
        {
            VERTEX* pCacheVertex = m_Vertices.GetData() + pEntry->index;

            // If this vertex is identical to the vertex already in the list, simply
            // point the index buffer to the existing vertex
            if( 0 == memcmp( pVertex, pCacheVertex, sizeof( VERTEX ) ) )
            {
                bFoundInList = true;
                index = pEntry->index;
                break;
            }

            pEntry = pEntry->pNext;
        }
    }

    // Vertex was not found in the list. Create a new entry, both within the Vertices list
    // and also within the hashtable cache
    if( !bFoundInList )
    {
        // Add to the Vertices list
        index = m_Vertices.GetSize();
        m_Vertices.Add( *pVertex );

        // Add this to the hashtable
        CacheEntry* pNewEntry = new CacheEntry;
        if( pNewEntry == NULL )
            return (DWORD)-1;

        pNewEntry->index = index;
        pNewEntry->pNext = NULL;

        // Grow the cache if needed
        while( ( UINT )m_VertexCache.GetSize() <= hash )
        {
            m_VertexCache.Add( NULL );
        }

        // Add to the end of the linked list
        CacheEntry* pCurEntry = m_VertexCache.GetAt( hash );
        if( pCurEntry == NULL )
        {
            // This is the head element
            m_VertexCache.SetAt( hash, pNewEntry );
        }
        else
        {
            // Find the tail
            while( pCurEntry->pNext != NULL )
            {
                pCurEntry = pCurEntry->pNext;
            }

            pCurEntry->pNext = pNewEntry;
        }
    }

    return index;
}


//--------------------------------------------------------------------------------------
void CPlyLoader::DeleteCache()
{
    // Iterate through all the elements in the cache and subsequent linked lists
    for( int i = 0; i < m_VertexCache.GetSize(); i++ )
    {
        CacheEntry* pEntry = m_VertexCache.GetAt( i );
        while( pEntry != NULL )
        {
            CacheEntry* pNext = pEntry->pNext;
            SAFE_DELETE( pEntry );
            pEntry = pNext;
        }
    }

    m_VertexCache.RemoveAll();
}


//--------------------------------------------------------------------------------------
void CPlyLoader::InitMaterial( Material* pMaterial )
{
    ZeroMemory( pMaterial, sizeof( Material ) );

    pMaterial->vAmbient = D3DXVECTOR3( 0.0f, 0.0f, 0.0f );
    pMaterial->vDiffuse = D3DXVECTOR3( 1.0f, 1.0f, 1.0f );
    pMaterial->vSpecular = D3DXVECTOR3( 0.0f, 0.0f, 0.0f );
    pMaterial->nShininess = 1;
    pMaterial->fAlpha = 1.0f;
    pMaterial->bSpecular = false;
    pMaterial->pTexture = NULL;
}

//-----------------------------------------------------------------------------
// File: VertexRef.cpp
//
// Desc: 
//-----------------------------------------------------------------------------

#include "DXUT.h"
#include "SDKmisc.h"
#include <stdio.h>
#include <wchar.h>
#include <conio.h>
#include <fstream>
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include "config.h"
#include "PRTSim.h"
#include "VertexRef.h"
using namespace std;

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
// Function-prototypes
//-----------------------------------------------------------------------------
HRESULT RunVertexRefRecorder( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions, CONCAT_MESH* pPRTMesh, DWORD dwNumImages, CGrowableArray<VERTEXREF> &vertexRefArray);
HRESULT WINAPI StaticVertexRefRecorderCB( float fPercentDone, LPVOID pParam );
BOOL ComputeHitDistance( CONCAT_MESH* pPRTMesh, D3DXVECTOR3 vRayOrigin, D3DXVECTOR3 vRayDirection,  float &fDistanceToCollision, DWORD &pCountOfHits, LPD3DXBUFFER &ppAllHits, D3DXMATRIX* pWorld );
BOOL HasIndex( CGrowableArray<UINT>* indiceArray, UINT iCheckIndex);

//-----------------------------------------------------------------------------
// static helper function
//-----------------------------------------------------------------------------
DWORD WINAPI StaticUserInterThreadProc( LPVOID lpParameter )
{
	VERTEXREF_STATE* pPRTState = ( VERTEXREF_STATE* )lpParameter;
	WCHAR sz[256] = {0};
	bool bStop = false;
	float fLastPercent = -1.0f;
	double fLastPercentAnnounceTime = 0.0f;

	DXUTGetGlobalTimer()->Start();

	while( !bStop )
	{
		//EnterCriticalSection( &pPRTState->cs );	// error?

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

//-----------------------------------------------------------------------------
HRESULT RunVertexRefRecorder( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions, SETTINGS* pSettings, CONCAT_MESH* pPRTMesh, CGrowableArray<VERTEXREF> &vertexRefArray,  D3DXMATRIX* pWorld)
{
	DWORD dwResult;
	DWORD dwThreadId;
	HRESULT hr;

	DWORD dwNumImages = NUMIMAGES;

	VERTEXREF_STATE prtState;
	ZeroMemory( &prtState, sizeof( VERTEXREF_STATE ) );
	InitializeCriticalSection( &prtState.cs );
	prtState.bStopRecorder = false;
	prtState.bRunning = true;
	prtState.nNumPasses = 3;
	prtState.nCurPass = 1;
	prtState.fPercentDone = -1.0f;
	prtState.bProgressMode = true;

	wofstream wofOutputVertexRef;
	wofstream wofIndex;

	DWORD dwNumBytesPerVertex = pPRTMesh->pMesh->GetNumBytesPerVertex();
	DWORD dwNumVertices = pPRTMesh->pMesh->GetNumVertices();
	DWORD dwNumFaces = pPRTMesh->pMesh->GetNumFaces();

	wprintf( L"\nStarting recoder vertex reference.  Press ESC to abort\n" );

	HANDLE hThreadId = CreateThread( NULL, 0, StaticUserInterThreadProc, ( LPVOID )&prtState, 0, &dwThreadId );

	//Stage 1:
	EnterCriticalSection( &prtState.cs );
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Computer and record the image intensity..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	float* pBufferIndices;
	LPDIRECT3DVERTEXBUFFER9 m_VB;
	pPRTMesh->pMesh->GetVertexBuffer(&m_VB);
	if( m_VB == NULL)
	{
		wprintf( L"Error in Vertex Buffer");
		goto LEarlyExit;
	}

	V( m_VB->Lock( 0, dwNumVertices * sizeof(BYTE) * dwNumBytesPerVertex,(VOID**)&pBufferIndices, D3DLOCK_READONLY ) );
	for(UINT j = 0; j < dwNumVertices; j++) 
	{
		D3DXVECTOR3 vVertexPos = D3DXVECTOR3( pBufferIndices[j*(dwNumBytesPerVertex/sizeof(float))], pBufferIndices[j*(dwNumBytesPerVertex/sizeof(float))+1], pBufferIndices[j*(dwNumBytesPerVertex/sizeof(float))+2]);
		VERTEXREF vertexRef;
		ZeroMemory(&vertexRef, sizeof(VERTEXREF));
		vertexRef.vVertexPosition = vVertexPos;
		for( UINT i = 0; i < dwNumImages; i++ )
		{
			WCHAR str[MAX_PATH];
			WCHAR strCameraMatrix[MAX_PATH], strCameraPosition[MAX_PATH], strImage[MAX_PATH], strMask[MAX_PATH];
			if( i < 10 && i >= 0 )
				wcscpy_s( str, L"000" );
			else if( i >= 10 && i < 100 )
				wcscpy_s( str, L"00" );
			else
				wcscpy_s( str, L"0" );

			//Read in the ith camera position
			wsprintf(strCameraPosition, L"%s%s%dPos.txt", L"Media\\PRT Demo\\txtPos\\",str, i);
			wifstream wifCameraPosition(strCameraPosition);
			if( wifCameraPosition == NULL)
			{
				wprintf( L"Can not find camera position\n");
				continue;
			}
			D3DXVECTOR3 vCameraPos;
			wifCameraPosition >> vCameraPos.x >> vCameraPos.y >> vCameraPos.z;
			wifCameraPosition.close();

			//test whether the camera can see the vertex
			float fDistanceToCollion = 0.0;
			D3DXVECTOR3 vRayDirection;
			DWORD dwNumHits = 0;
			BOOL bHit = 0;
			LPD3DXBUFFER ppAllHits;

			vRayDirection = vCameraPos - vVertexPos;
			D3DXMATRIX* mWorld = pWorld; 
			bHit = ComputeHitDistance( pPRTMesh, vVertexPos, vRayDirection, fDistanceToCollion, dwNumHits, ppAllHits, mWorld );

			if( dwNumHits > 0)
			{
				D3DXINTERSECTINFO* info = (D3DXINTERSECTINFO*) ppAllHits->GetBufferPointer();

				fDistanceToCollion = 0.0;

				for(UINT k = 0; k < ppAllHits->GetBufferSize() / sizeof(D3DXINTERSECTINFO); k++)
				{
					if(info[k].Dist > fDistanceToCollion)
						fDistanceToCollion = info[k].Dist;
				}
			}

			if( bHit == 0 || bHit == 1 &&  fDistanceToCollion <= 1e-6f)
			{
				//the vertex can be viewed by the ith camera,
				//then compute its projection

				//Read in the projection matrix
				wsprintf(strCameraMatrix, L"%s%s%d.txt", L"Media\\PRT Demo\\txt\\",str, i);
				wifstream wifCameraMatrix( strCameraMatrix );
				if( wifCameraMatrix == NULL)
				{
					wprintf( L"Can not find camera matrix\n");
					continue;
				}
				D3DXMATRIX matCameraMatrix;
				wifCameraMatrix.ignore(1000, '\n');
				wifCameraMatrix >> matCameraMatrix._11 >> matCameraMatrix._12 >> matCameraMatrix._13 >> matCameraMatrix._14;
				wifCameraMatrix >> matCameraMatrix._21 >> matCameraMatrix._22 >> matCameraMatrix._23 >> matCameraMatrix._24;
				wifCameraMatrix >> matCameraMatrix._31 >> matCameraMatrix._32 >> matCameraMatrix._33 >> matCameraMatrix._34;
				wifCameraMatrix.close();

				float fD = 0.0;
				fD = matCameraMatrix._31 * vVertexPos.x + matCameraMatrix._32 * vVertexPos.y + matCameraMatrix._33 * vVertexPos.z + matCameraMatrix._34;

				if( fD == 0 )
				{
					wprintf( L"Can not divide by zero\n");
					continue;
				}
				int iU, iV;
				iU = (int)(( matCameraMatrix._11 * vVertexPos.x + matCameraMatrix._12 * vVertexPos.y + matCameraMatrix._13 * vVertexPos.z + matCameraMatrix._14) / fD);
				iV = (int)(( matCameraMatrix._21 * vVertexPos.x + matCameraMatrix._22 * vVertexPos.y + matCameraMatrix._23 * vVertexPos.z + matCameraMatrix._24) / fD);

				//Test whether the projection is in the foreground areas
				//with the mask,
				wsprintf( strMask, L"%s%s%d.pgm", L"Media\\PRT Demo\\masks\\",str, i );
				char cstrMask[MAX_PATH];
				WideCharToMultiByte( CP_ACP, 0, strMask, MAX_PATH, cstrMask, MAX_PATH, NULL, NULL );
				IplImage* pgmImg = cvLoadImage(cstrMask, -1);
				if( pgmImg == NULL)
				{
					iU++;
				}
				if( iU >= 0 && iU < pgmImg->width && iV >=0 && iV < pgmImg->height )
				{
					CvScalar value;
					value = cvGet2D( pgmImg, iV, iU);

					if(value.val[0] <= 128)
					{
						//foregound pixel

						wsprintf( strImage, L"%s%s%d.jpg", L"Media\\PRT Demo\\visualize\\",str, i );
						char cstrImage[MAX_PATH];
						WideCharToMultiByte( CP_ACP, 0, strImage, MAX_PATH, cstrImage, MAX_PATH, NULL, NULL );
						IplImage* pImg = cvLoadImage( cstrImage, -1 );
						
						if( pImg == NULL)
						{
							iU++;;
						}

						//I(x,y)blue
						UINT blue = ((uchar*)(pImg->imageData + pImg->widthStep*iV))[iU*3];
						//I(x,y)green 
						UINT green = ((uchar*)(pImg->imageData + pImg->widthStep*iV))[iU*3+1];
						//I(x,y)red 
						UINT red =((uchar*)(pImg->imageData + pImg->widthStep*iV))[iU*3+2];


						//Record the right intensity
						INTENSITY inten;
						inten.iVertexIndex = j;
						inten.iImageIndex = i;
						inten.iBlue = blue;
						inten.iGreen = green;
						inten.iRed = red;

						for(int i = (-1) * WINDOWSIZE / 2; i <= WINDOWSIZE / 2; i++ )
							for(int j = (-1) * WINDOWSIZE / 2; j <= WINDOWSIZE / 2; j++)
							{
								if( iU + i >= 0 && iU + i < pgmImg->width && iV + j >=0 && iV + j < pgmImg->height ){
									CvScalar value;
									value = cvGet2D( pgmImg, iV + j, iU + i);
									if(value.val[0] <= 128) {
										inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][0] = ((uchar*)(pImg->imageData + pImg->widthStep*(iV+j)))[(iU+i)*3+2];
										inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][1] = ((uchar*)(pImg->imageData + pImg->widthStep*(iV+j)))[(iU+i)*3+1];
										inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][2] = ((uchar*)(pImg->imageData + pImg->widthStep*(iV+j)))[(iU+i)*3];
									}
									else{
										inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][0] = 300;
										inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][1] = 300;
										inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][2] = 300;
									}
								}
								else{
									inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][0] = 300;
									inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][1] = 300;
									inten.matNeighbourIntensity[(i+WINDOWSIZE/2)*WINDOWSIZE+j+WINDOWSIZE/2][2] = 300;
								}

							}
						vertexRef.intensityArray.Add(inten);
												
						cvReleaseImage(&pImg);
					}
				}
				cvReleaseImage(&pgmImg);
			}
		}
		vertexRefArray.Add(vertexRef);
	}
	if( m_VB != NULL)
		m_VB->Release();

	
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Computer the neighbour of each vertex..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	wofIndex.open("indices.txt", ios::out);
	WORD* pIndexBuffer;
	LPDIRECT3DINDEXBUFFER9 m_IB;
	pPRTMesh->pMesh->GetIndexBuffer(&m_IB);
	if(m_IB == NULL)
	{
		wprintf( L"Error in Index Buffer");
		goto LEarlyExit;
	}
	V( m_IB->Lock( 0, dwNumFaces * sizeof(WORD) * 3, (VOID**)&pIndexBuffer, D3DLOCK_READONLY ) );
	for(UINT j = 0; j < dwNumFaces * 3; j = j + 3 )
	{
		wofIndex << L"3\t";
		for( UINT k = 0; k < 3; k++ )
		{
			UINT iVertexIndex = pIndexBuffer[j + k];
			wofIndex << iVertexIndex << L"\t";
			vertexRefArray.GetAt( iVertexIndex ).faceArray.Add( j / 3 );
		}
		wofIndex << L"\n";
		UINT iVertexIndex, iNeighbourIndex;
		iVertexIndex = pIndexBuffer[j];
		iNeighbourIndex = pIndexBuffer[j+1];
		if( !HasIndex( &vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray, iNeighbourIndex ) )
			vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray.Add( iNeighbourIndex );
		iNeighbourIndex = pIndexBuffer[j+2];
		if( !HasIndex( &vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray, iNeighbourIndex ) )
			vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray.Add(iNeighbourIndex);

		iVertexIndex = pIndexBuffer[j + 1];
		iNeighbourIndex = pIndexBuffer[j];
		if( !HasIndex( &vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray, iNeighbourIndex ) )
			vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray.Add(iNeighbourIndex);
		iNeighbourIndex = pIndexBuffer[j+2];
		if( !HasIndex( &vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray, iNeighbourIndex ) )
			vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray.Add(iNeighbourIndex);

		iVertexIndex = pIndexBuffer[j + 2];
		iNeighbourIndex = pIndexBuffer[j];
		if( !HasIndex( &vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray, iNeighbourIndex ) )
			vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray.Add(iNeighbourIndex);
		iNeighbourIndex = pIndexBuffer[j+1];
		if( !HasIndex( &vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray, iNeighbourIndex ) )
			vertexRefArray.GetAt(iVertexIndex).vertexNeighbourArray.Add(iNeighbourIndex);
	}
	m_IB->Unlock();
	if( m_IB != NULL)
		m_IB->Release();
	wofIndex.close();

	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Save the vertex reference..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	wofOutputVertexRef.open("VertexRef.txt", ios::out);
	if( wofOutputVertexRef == NULL )
	{
		wprintf( L"Can not save the vertex Reference\n");
		goto LEarlyExit;
	}
	wofOutputVertexRef << L"window size " << WINDOWSIZE << L"\n"; 
	for( int i = 0; i < vertexRefArray.GetSize(); i++ )
	{
		wofOutputVertexRef << L"position " << vertexRefArray.GetAt(i).vVertexPosition.x << L" "
			<< vertexRefArray.GetAt(i).vVertexPosition.y << L" "
			<< vertexRefArray.GetAt(i).vVertexPosition.z << L"\n";
		for( int j = 0; j < vertexRefArray.GetAt(i).intensityArray.GetSize(); j++ )
		{
			wofOutputVertexRef << L"intensity " << vertexRefArray.GetAt(i).intensityArray.GetAt(j).iVertexIndex << L" "
				<< vertexRefArray.GetAt(i).intensityArray.GetAt(j).iImageIndex << L" "
				<< vertexRefArray.GetAt(i).intensityArray.GetAt(j).iRed << L" "
				<< vertexRefArray.GetAt(i).intensityArray.GetAt(j).iGreen << L" "
				<< vertexRefArray.GetAt(i).intensityArray.GetAt(j).iBlue << L"\n";
			wofOutputVertexRef << L"window" << L"\n";
			for(int k = 0; k < WINDOWSIZE * WINDOWSIZE; k++)
			{
				wofOutputVertexRef << vertexRefArray.GetAt(i).intensityArray.GetAt(j).matNeighbourIntensity[k][0] << L" "
					<< vertexRefArray.GetAt(i).intensityArray.GetAt(j).matNeighbourIntensity[k][1] << L" "
					<< vertexRefArray.GetAt(i).intensityArray.GetAt(j).matNeighbourIntensity[k][2] << L"\n";
			}
		}
		wofOutputVertexRef << L"neighbour " << vertexRefArray.GetAt(i).vertexNeighbourArray.GetSize() << L" ";
		for( int j = 0; j < vertexRefArray.GetAt(i).vertexNeighbourArray.GetSize(); j++ )
		{
			wofOutputVertexRef << vertexRefArray.GetAt(i).vertexNeighbourArray.GetAt(j) << L" ";
		}
		wofOutputVertexRef << L"\n";
		wofOutputVertexRef << L"face " << vertexRefArray.GetAt(i).faceArray.GetSize() << L" ";
		for( int j = 0; j < vertexRefArray.GetAt(i).faceArray.GetSize(); j++ )
		{
			wofOutputVertexRef<< vertexRefArray.GetAt(i).faceArray.GetAt(j) << L" ";
		}
		wofOutputVertexRef << L"\n\n\n";
		/*wofOutputVertexRef << vertexRefArray.GetAt(i).vertexNeighbourArray.GetSize() << L"\n";
		wofOutputVertexRef <<vertexRefArray.GetAt(i).faceArray.GetSize() << L"\n";*/
	}
	wofOutputVertexRef.close();


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
	//dwResult = WaitForSingleObject( hThreadId, 10000 );
	//if( dwResult == WAIT_TIMEOUT )
	//	return E_FAIL;

	DeleteCriticalSection( &prtState.cs );


	// It returns E_FAIL if the simulation was aborted from the callback
	if( hr == E_FAIL )
		return E_FAIL;

	if( FAILED( hr ) )
	{
		DXTRACE_ERR( TEXT( "VertexRefRecorder" ), hr );
		return hr;
	}

	return S_OK;
}


//-----------------------------------------------------------------------------
// static helper function
//-----------------------------------------------------------------------------
HRESULT WINAPI StaticVertexRefRecorderCB( float fPercentDone, LPVOID pParam )
{
	VERTEXREF_STATE* pPRTState = ( VERTEXREF_STATE* )pParam;

	EnterCriticalSection( &pPRTState->cs );
	pPRTState->fPercentDone = fPercentDone;

	// In this callback, returning anything except S_OK will stop the simulator
	HRESULT hr = S_OK;
	if( pPRTState->bStopRecorder )
		hr = E_FAIL;

	LeaveCriticalSection( &pPRTState->cs );

	return hr;
}

BOOL ComputeHitDistance( CONCAT_MESH* pPRTMesh, D3DXVECTOR3 vRayOrigin, D3DXVECTOR3 vRayDirection,  float &fDistanceToCollision, DWORD &pCountOfHits, LPD3DXBUFFER &ppAllHits, D3DXMATRIX* pWorld )
{
	// Use inverse of matrix
	D3DXMATRIX matInverse;
	D3DXMatrixInverse( &matInverse, NULL, pWorld );


	// Transform ray origin and direction by inv matrix
	D3DXVECTOR3 vRayObjOrigin, vRayObjDirection;

	D3DXVec3TransformCoord( &vRayObjOrigin, &vRayOrigin, &matInverse );
	D3DXVec3TransformNormal( &vRayObjDirection, &vRayDirection, &matInverse );
	D3DXVec3Normalize( &vRayObjDirection, &vRayObjDirection );

	BOOL bHit;
	DWORD dwFaceIndex;
	float pv, pu;
	D3DXIntersectSubset( pPRTMesh->pMesh, 0, &vRayObjOrigin, &vRayObjDirection, &bHit, &dwFaceIndex, &pu, &pv, &fDistanceToCollision, &ppAllHits, &pCountOfHits );

	return bHit;
}

BOOL HasIndex( CGrowableArray<UINT>* indiceArray, UINT iCheckIndex)
{
	for(int i = 0; i < indiceArray->GetSize(); i ++)
	{
		if( indiceArray->GetAt(i) == iCheckIndex)
			return 1;
	}
	return 0;
}
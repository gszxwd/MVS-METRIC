//----------------------------------------------------------------------------
// File: MetricCal.cpp
//
// Desc: Compute the gradient metric
//-----------------------------------------------------------------------------
#include "DXUT.h"
#include "SDKmisc.h"
#include <fstream>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>
#include <conio.h>
#include "config.h"
#include "PRTSim.h"
#include "VertexRef.h"
#include "EnvMapCalculator.h"
#include "MetricCal.h"

using namespace std;

#define NOSHAREDCAMERAS 1e8
#define EXCEED -1
#define SAMEPOINT -2

#define INVISIBLE -1
#define SINGLEVIEW -2

struct PRT_STATE
{
    CRITICAL_SECTION cs;
    bool bUserAbort;
    bool bStopSimulator;
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
HRESULT RunMetricCalculator( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions, CONCAT_MESH* pPRTMesh,
                         CONCAT_MESH* pBlockerMesh );
HRESULT WINAPI StaticMetricCalculator( float fPercentDone, LPVOID pParam );
void ComputeNeighbourDiff(VERTEXREF *cur, VERTEXREF *neigh, UINT curIndex, UINT neighIndex, CGrowableArray<D3DCOLORVALUE> *irradianceArray, float fObjectRadius,
						  float& red, float& green, float& blue);
void ComputeNCCScore( INTENSITY inten1, INTENSITY inten2, float &red, float &green, float &blue );
//-----------------------------------------------------------------------------
// static helper function
//-----------------------------------------------------------------------------
DWORD WINAPI StaticMetricUIThreadProc( LPVOID lpParameter )
{
    PRT_STATE* pPRTState = ( PRT_STATE* )lpParameter;
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
            if( pPRTState->fPercentDone < 0.0f || DXUTGetGlobalTimer()->GetTime() < fLastPercentAnnounceTime + 5.0f )
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
                pPRTState->bStopSimulator = true;
                pPRTState->bUserAbort = true;
                break;
            }
        }

        bStop = pPRTState->bStopSimulator;
        LeaveCriticalSection( &pPRTState->cs );

        if( !bStop )
            Sleep( 1000 );
    }

    return 0;
}

//-----------------------------------------------------------------------------
HRESULT RunMetricCalculator(  SETTINGS* pSettings, CGrowableArray <VERTEXREF> * vertexRefArray, CGrowableArray<D3DCOLORVALUE> *irradianceArray, float fObjectRadius)
{
    DWORD dwResult;
    DWORD dwThreadId;
	HRESULT hr;

	CGrowableArray<float> redGradientArray;
	CGrowableArray<float> greenGradientArray;
	CGrowableArray<float> blueGradientArray;
	CGrowableArray<float> redNCCArray;
	CGrowableArray<float> greenNCCArray;
	CGrowableArray<float> blueNCCArray;


    PRT_STATE prtState;
    ZeroMemory( &prtState, sizeof( PRT_STATE ) );
    InitializeCriticalSection( &prtState.cs );
    prtState.bStopSimulator = false;
    prtState.bRunning = true;
    prtState.nNumPasses = 3;
    prtState.nCurPass = 1;
    prtState.fPercentDone = -1.0f;
    prtState.bProgressMode = true;

	wofstream wofOutputMetric;

	wifstream ifInputMetric, iInputMetric;
	WCHAR buffer[256];

    wprintf( L"\nStarting calculating the gradient and NCC score.  Press ESC to abort\n" );

	//Stage 1: Compute the gradient metric of each vertex
    swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Computing the gradient metric of each vertex..", prtState.nCurPass,
                     prtState.nNumPasses );
    wprintf( prtState.strCurPass );

	HANDLE hThreadId = CreateThread( NULL, 0, StaticMetricUIThreadProc, ( LPVOID )&prtState, 0, &dwThreadId );

	ifInputMetric.open("VertexRef.txt", ios::in);
	ifInputMetric >> buffer;
	int uVert = -1;

	while( !ifInputMetric.eof() )
	{
		if( wcscmp( L"window", buffer) == 0)
			ifInputMetric.getline(buffer, 256);
		else if( wcscmp( L"position", buffer) == 0){
			uVert++;
			ifInputMetric.getline(buffer, 256);
		}
		else if( wcscmp( L"intensity", buffer) == 0)
		{
			INTENSITY inten;
			ifInputMetric >> inten.iVertexIndex >> inten.iImageIndex >> inten.iRed >> inten.iGreen >> inten.iBlue;
			ifInputMetric.getline(buffer, 256);
			ifInputMetric.getline(buffer, 256);
			for( int i = 0; i < WINDOWSIZE * WINDOWSIZE; i++)
				ifInputMetric >> inten.matNeighbourIntensity[i][0] >> inten.matNeighbourIntensity[i][1] >> inten.matNeighbourIntensity[i][2];
			vertexRefArray->GetAt(uVert).intensityArray.Add(inten);
		}
		else if( wcscmp( L"neighbour", buffer) == 0) 
		{
			UINT neighNum;
			UINT neighIndex;
			ifInputMetric >> neighNum;
			for( int i = 0; i < neighNum; i++){
				ifInputMetric >> neighIndex;
				vertexRefArray->GetAt(uVert).vertexNeighbourArray.Add(neighIndex);
			}
		}
		else if( wcscmp( L"face", buffer) == 0)
		{
			UINT faceNum;
			UINT faceIndex;
			ifInputMetric >> faceNum;
			for( int i = 0; i < faceNum; i++){
				ifInputMetric >> faceIndex;
				vertexRefArray->GetAt(uVert).faceArray.Add(faceIndex);
			}
		}
		else{}
		ifInputMetric >> buffer;
	}

	for( int i =0; i < vertexRefArray->GetSize(); i++ )
	{
		VERTEXREF curVertex, neighVertex;
		curVertex = vertexRefArray->GetAt(i);
		float redGradient = 0.0;
		float blueGradient = 0.0;
		float greenGradient = 0.0;
		bool breakFlag = false;
		for( int j = 0; j < curVertex.vertexNeighbourArray.GetSize(); j++ )
		{
			UINT neighIndex = curVertex.vertexNeighbourArray.GetAt(j);
			neighVertex = vertexRefArray->GetAt(neighIndex);
			float fRedGrad = 0.0;
			float fGreenGrad = 0.0;
			float fBlueGrad = 0.0;
			ComputeNeighbourDiff(&curVertex, &neighVertex, i, neighIndex, irradianceArray, fObjectRadius, fRedGrad, fGreenGrad, fBlueGrad);
			if( fRedGrad == NOSHAREDCAMERAS ){
				redGradient = NOSHAREDCAMERAS;
				greenGradient = NOSHAREDCAMERAS;
				blueGradient = NOSHAREDCAMERAS;
				breakFlag = true;
				break;
			}
			else if( fRedGrad == SAMEPOINT){
				redGradient = SAMEPOINT;
				greenGradient = SAMEPOINT;
				blueGradient = SAMEPOINT;
				breakFlag = true;
				break;
			}
			else
			{
				redGradient += fRedGrad;
				blueGradient += fBlueGrad;
				greenGradient += fGreenGrad;
			}		
		}
		if(breakFlag == false && redGradient >= NOSHAREDCAMERAS)
			redGradient = EXCEED;
		if(breakFlag == false && greenGradient >= NOSHAREDCAMERAS)
			greenGradient = EXCEED;
		if(breakFlag == false && blueGradient >= NOSHAREDCAMERAS)
			blueGradient = EXCEED;
		redGradientArray.Add( redGradient );
		greenGradientArray.Add( greenGradient );
		blueGradientArray.Add( blueGradient );
	}


	//Stage 2: Computing the NCC score of each vertex..
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Computing the NCC score of each vertex..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	for( int i = 0; i < vertexRefArray->GetSize(); i++ )
	{
		if( vertexRefArray->GetAt(i).intensityArray.GetSize() == 0 ){
			redNCCArray.Add( INVISIBLE );
			greenNCCArray.Add( INVISIBLE );
			blueNCCArray.Add( INVISIBLE );
		}
		else if( vertexRefArray->GetAt(i).intensityArray.GetSize() == 1 ){
			redNCCArray.Add( SINGLEVIEW );
			greenNCCArray.Add( SINGLEVIEW );
			blueNCCArray.Add( SINGLEVIEW );
		}
		else{
			float redNCC, greenNCC, blueNCC;
			float red, green, blue;
			redNCC = 0.0;
			greenNCC = 0.0;
			blueNCC = 0.0;
			for( int k = 0; k < vertexRefArray->GetAt(i).intensityArray.GetSize() - 1; k++ ) {
				for( int j = k + 1; j < vertexRefArray->GetAt(i).intensityArray.GetSize(); j++) {
					INTENSITY cur, next;
					cur = vertexRefArray->GetAt(i).intensityArray.GetAt(k);
					next = vertexRefArray->GetAt(i).intensityArray.GetAt(j);
					ComputeNCCScore( cur, next, red, green, blue);
					redNCC += red;
					greenNCC += green;
					blueNCC += blue;
				}
			}
			int size = vertexRefArray->GetAt(i).intensityArray.GetSize();
			redNCC /= ((size * (size + 1)) / 2);
			greenNCC /= ((size * (size + 1)) / 2);
			blueNCC /= ((size * (size + 1)) / 2);
			redNCCArray.Add(redNCC);
			greenNCCArray.Add(greenNCC);
			blueNCCArray.Add(blueNCC);
		}

	}


	//Stage 3: Save all the data
	EnterCriticalSection( &prtState.cs );
	prtState.nCurPass++;
	swprintf_s( prtState.strCurPass, 256, L"\nStage %d of %d: Save all the data..", prtState.nCurPass,
		prtState.nNumPasses );
	wprintf( prtState.strCurPass );
	prtState.fPercentDone = 0.0f;
	LeaveCriticalSection( &prtState.cs );

	wofOutputMetric.open("metric.txt", ios::out);
	if( wofOutputMetric == NULL){
		wprintf( L"Can not save the metric\n");
		goto LEarlyExit;
	}

	for( int i = 0; i < vertexRefArray->GetSize(); i++ )
	{
		wofOutputMetric << L"position " << vertexRefArray->GetAt(i).vVertexPosition.x << L" "
			<< vertexRefArray->GetAt(i).vVertexPosition.y << L" "
			<< vertexRefArray->GetAt(i).vVertexPosition.z << L"\n";
		wofOutputMetric << L"gradient " << redGradientArray.GetAt(i) << L" "
			<< greenGradientArray.GetAt(i) << L" "
			<< blueGradientArray.GetAt(i) << L"\n";
		wofOutputMetric << L"ncc score "  << redNCCArray.GetAt(i) << L" "
			<< greenNCCArray.GetAt(i) << L" "
			<< blueNCCArray.GetAt(i) << L"\n";
		wofOutputMetric << L"\n";
	}



  

LEarlyExit:

    // Usually fails becaused user stoped the simulator
    EnterCriticalSection( &prtState.cs );
    prtState.bRunning = false;
    prtState.bStopSimulator = true;
	hr = 1;
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
        DXTRACE_ERR( TEXT( "ID3DXPRTEngine" ), hr );
        return hr;
    }

    return S_OK;
}

void ComputeNeighbourDiff(VERTEXREF* cur, VERTEXREF *neigh, UINT curIndex, UINT neighIndex, CGrowableArray<D3DCOLORVALUE> *irradianceArray, float fObjectRadius,
						  float& red, float& green, float& blue)
{
	red = green = blue = 0.0;
	float redGradIrrad = 0.0;
	float blueGradIrrad = 0.0;
	float greenGradIrrad = 0.0;
	float redGradInten = 0.0;
	float greenGradInten = 0.0;
	float blueGradInten = 0.0;
	int sharedCameraNum = 0;

	float redDiff1 = 0.0;
	float redDiff2 = 0.0;
	float greenDiff1 = 0.0;
	float greenDiff2 = 0.0;
	float blueDiff1 = 0.0;
	float blueDiff2 = 0.0;

	for( int i = 0; i < cur->intensityArray.GetSize(); i++ )
	{
		for( int j = 0; j < neigh->intensityArray.GetSize(); j++ )
		{
			if( neigh->intensityArray.GetAt(j).iImageIndex == cur->intensityArray.GetAt(i).iImageIndex ){
				sharedCameraNum++;
				redGradInten = (float)cur->intensityArray.GetAt(i).iRed - (float)neigh->intensityArray.GetAt(j).iRed;
				greenGradInten = (float)cur->intensityArray.GetAt(i).iGreen - (float)neigh->intensityArray.GetAt(j).iGreen;
				blueGradInten = (float)cur->intensityArray.GetAt(i).iBlue - (float)neigh->intensityArray.GetAt(j).iBlue;
				redGradIrrad = irradianceArray->GetAt(curIndex).r - irradianceArray->GetAt(neighIndex).r;
				greenGradIrrad = irradianceArray->GetAt(curIndex).g - irradianceArray->GetAt(neighIndex).g;
				blueGradIrrad = irradianceArray->GetAt(curIndex).b - irradianceArray->GetAt(neighIndex).b;
				/*red += (redGradIrrad - redGradInten) * (redGradIrrad - redGradInten);
				blue += (blueGradIrrad - blueGradInten) * (blueGradIrrad - blueGradInten);
				green += (greenGradIrrad - greenGradInten) * (greenGradIrrad - greenGradInten);
				break;
				redDiff1 = (float)cur->intensityArray.GetAt(i).iRed - irradianceArray->GetAt(curIndex).r;
				redDiff2 = (float)neigh->intensityArray.GetAt(j).iRed - irradianceArray->GetAt(neighIndex).r;
				greenDiff1 = (float)cur->intensityArray.GetAt(i).iGreen - irradianceArray->GetAt(curIndex).g;
				greenDiff2 = (float)neigh->intensityArray.GetAt(j).iGreen -  irradianceArray->GetAt(neighIndex).g;
				blueDiff1 = (float)cur->intensityArray.GetAt(i).iBlue -  irradianceArray->GetAt(curIndex).b;
				blueDiff2 = (float)neigh->intensityArray.GetAt(j).iBlue - irradianceArray->GetAt(neighIndex).b;
				red += (redDiff1 - redDiff2) * (redDiff1 - redDiff2);
				green += (greenDiff1 - greenDiff2) * (greenDiff1 - greenDiff2);
				blue += (blueDiff1 - blueDiff2) * (blueDiff1 - blueDiff2);*/
				red += redGradIrrad * redGradIrrad;
				green += greenGradIrrad * greenGradIrrad;
				blue += blueGradIrrad * blueGradIrrad;
				break;
			}
		}
	}
	if(sharedCameraNum == 0)
		red = NOSHAREDCAMERAS;
	else{
		float verticeVector[3];
		verticeVector[0] = cur->vVertexPosition.x - neigh->vVertexPosition.x;
		verticeVector[1] = cur->vVertexPosition.y - neigh->vVertexPosition.y;
		verticeVector[2] = cur->vVertexPosition.z - neigh->vVertexPosition.z;
		float verticeDis = 0;
		verticeDis = sqrt( verticeVector[0]*verticeVector[0] + verticeVector[1]*verticeVector[1] + verticeVector[2]*verticeVector[2]);
		if(verticeDis < 1e-6 && (red != 0 || green != 0 || blue != 0))
			red = SAMEPOINT;
		else{
			red =  red/ ((verticeDis) / fObjectRadius);
			green =  green/ ((verticeDis) / fObjectRadius);
			blue =  blue/ ((verticeDis) / fObjectRadius);
			red /= sharedCameraNum;
			green /= sharedCameraNum;
			blue /= sharedCameraNum;
		}
	}
}

void ComputeNCCScore( INTENSITY inten1, INTENSITY inten2, float &red, float &green, float &blue )
{
	int iNumPixels = 0; 
	float meanred1 = 0.0;
	float meangreen1 = 0.0;
	float meanblue1 = 0.0;
	float meanred2 = 0.0;
	float meangreen2 = 0.0;
	float meanblue2 = 0.0;
	float tempRed1 = 0.0;
	float tempRed2 = 0.0;
	float tempRed3 = 0.0;
	float tempGreen3 = 0.0;
	float tempGreen1 = 0.0;
	float tempGreen2 = 0.0;
	float tempBlue1 = 0.0;
	float tempBlue2 = 0.0;
	float tempBlue3 = 0.0;

	for( int i = 0; i < WINDOWSIZE * WINDOWSIZE; i++ )
	{
		if( inten1.matNeighbourIntensity[i][0] != 300 && inten2.matNeighbourIntensity[i][0] != 300 )
		{
			iNumPixels++;
			meanred1 += inten1.matNeighbourIntensity[i][0];
			meangreen1 += inten1.matNeighbourIntensity[i][1];
			meanblue1 += inten1.matNeighbourIntensity[i][2];
			meanred2 += inten2.matNeighbourIntensity[i][0];
			meangreen2 += inten2.matNeighbourIntensity[i][1];
			meanblue2 += inten2.matNeighbourIntensity[i][2];
		}
	}
	meanred1 /= iNumPixels;
	meangreen1 /= iNumPixels;
	meanblue1 /= iNumPixels;
	meanred2 /= iNumPixels;
	meangreen2 /= iNumPixels;
	meanblue2 /= iNumPixels;

	for( int i = 0; i < WINDOWSIZE * WINDOWSIZE; i++ )
	{
		if( inten1.matNeighbourIntensity[i][0] != 300 && inten2.matNeighbourIntensity[i][0] != 300 )
		{
			tempRed1 +=  (inten1.matNeighbourIntensity[i][0] - meanred1) * (inten2.matNeighbourIntensity[i][0] - meanred2);
			tempGreen1 +=  (inten1.matNeighbourIntensity[i][1] - meangreen1) * (inten2.matNeighbourIntensity[i][1] - meangreen2);
			tempBlue1 +=  (inten1.matNeighbourIntensity[i][2] - meanblue1) * (inten2.matNeighbourIntensity[i][2] - meanblue2);

			tempRed2 += (inten1.matNeighbourIntensity[i][0] - meanred1) * (inten1.matNeighbourIntensity[i][0] - meanred1);
			tempGreen2 += (inten1.matNeighbourIntensity[i][1] - meangreen1) * (inten1.matNeighbourIntensity[i][1] - meangreen1);
			tempBlue2 += (inten1.matNeighbourIntensity[i][2] - meanblue1) * (inten1.matNeighbourIntensity[i][2] - meanblue1);

			tempRed3 += (inten2.matNeighbourIntensity[i][0] - meanred2) * (inten2.matNeighbourIntensity[i][0] - meanred2);
			tempGreen3 += (inten2.matNeighbourIntensity[i][1] - meangreen2) * (inten2.matNeighbourIntensity[i][1] - meangreen2);
			tempBlue3 += (inten2.matNeighbourIntensity[i][2] - meanblue2) * (inten2.matNeighbourIntensity[i][2] - meanblue2);
		}
	}

	red = tempRed1 / sqrt( tempRed2 * tempRed3 );
	green = tempGreen1 / sqrt( tempGreen2 * tempGreen3 );
	blue = tempBlue1 / sqrt( tempBlue2 * tempBlue3 );

}
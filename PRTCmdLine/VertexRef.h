//-----------------------------------------------------------------------------
// File: VertexRef.h
//
//-----------------------------------------------------------------------------

#ifndef _VERTEXREF_H_
#define _VERTEXREF_H_
#pragma once

#define WINDOWSIZE 7
#define NUMIMAGES 12 //bunny 21
//dinoSparseRing 16
//texdata 12
//hall 61
struct INTENSITY
{
	UINT iVertexIndex;
	UINT iImageIndex;
	UINT iRed;
	UINT iGreen;
	UINT iBlue;
	UINT matNeighbourIntensity[WINDOWSIZE*WINDOWSIZE][3];
};

struct VERTEXREF
{
	D3DXVECTOR3 vVertexPosition;
	CGrowableArray<INTENSITY> intensityArray;
	CGrowableArray<UINT> vertexNeighbourArray;
	CGrowableArray<UINT> faceArray;
};

//-----------------------------------------------------------------------------
// Function-prototypes
//-----------------------------------------------------------------------------

HRESULT RunVertexRefRecorder( IDirect3DDevice9* pd3dDevice, SIMULATOR_OPTIONS* pOptions, SETTINGS* pSettings, CONCAT_MESH* pPRTMesh, CGrowableArray<VERTEXREF> &vertexRefArray,  D3DXMATRIX* pWorld);



#endif // _VERTEXREF_H_
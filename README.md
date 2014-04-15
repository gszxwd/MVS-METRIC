MVS-METRIC
==========

A new metric for multi-view stereo reconstruction

Prepare:

1. install vs2008 (e.g. Express Version)

2. install dxsdk (aug 2010) and config the project (include & lib path)

3. install opencv (>=2.3.1) and config the project (include & lib path)

4. put the reconstructed model, images and calibrated images into fold "PRT demo"

5. modify the image numbers Macro at file "VertexRef.h"

6. modify the input option file "option.xml" and add it to the command argment

7. run the PRTcmdline project

8. install matlab 2010a (There is a bug in 2013a version) and make sure the outputs of the PRTcmdline project are put into the matlab Optimization folder

9. run the Untitled2.m file

10.recompute the metric.txt

Solved bugs:

Ln326, VertexRef.cpp		sizeof(DWORD)->sizeof(WORD)

Ln318, VertexRef.cpp		DOWRD* pIndexBuffer->WORD* pIndexBuffer

Ln44, main.cpp			add semicolon(;) at end of the line

ln125, VertexRef.cpp		add EnterCriticalSection( &prtState.cs );

ln325, MetricCal.cpp 		add hr=1;

ln57, VertexRef.cpp		//EnterCriticalSection( &pPRTState->cs );

Ln72, Untitled2.m		change to for i = 1: size(RedList,1)

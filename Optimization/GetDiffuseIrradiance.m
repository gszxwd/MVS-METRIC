function [RedList, GreenList, BlueList] = GetDiffuseIrradiance(pSHCoeffsRed, pSHCoeffsGreen, pSHCoeffsBlue)
fid = fopen('Weights.txt'); 
tline = fgets(fid);
tline = strrep(tline, 'NumSamples', '');
NumSamples = sscanf(tline, '%d', 1);
tline = fgets(fid);
tline = strrep(tline, 'NumCoeffs', '');
NumCoeffs = sscanf(tline, '%d', 1);
tline = fgets(fid);
tline = strrep(tline, 'NumChannels', '');
NumChannels = sscanf(tline, '%d', 1);
tline = fgets(fid);
tline = strrep(tline, 'NumClusters', '');
NumClusters = sscanf(tline, '%d', 1);
tline = fgets(fid);
tline = strrep(tline, 'NumPCA', '');
NumPCA = sscanf(tline, '%d', 1);
VertexList(NumSamples, 1) = struct('Index', [], 'Position', [], 'Normal', [],'Offset', [], 'Weight', [] );
RedList = zeros(NumSamples, 1);
GreenList = zeros(NumSamples, 1);
BlueList = zeros(NumSamples, 1);
tline = fgets(fid);
clear VertexList;
i = 1;
while ~feof(fid)
    tline = fgets(fid);
     if (findstr(tline, 'Index') > 0)
        tline = strrep(tline, 'Index', '');
        index = sscanf(tline, '%d', 1);
        VertexList(i, 1).Index = index;
     elseif (findstr(tline, 'Position') > 0)
        tline = strrep(tline, 'Position', '');
        pos = sscanf(tline, '%f%f%f', 3);
        VertexList(i, 1).Position = pos;
     elseif (findstr(tline, 'Normal') > 0)
        tline = strrep(tline, 'Normal', '');
        normal = sscanf(tline, '%f%f%f', 3);
        VertexList(i, 1).Normal = normal;
     elseif (findstr(tline, 'Offset') > 0)
        tline = strrep(tline, 'Offset', '');
        offset = sscanf(tline, '%d', 1);
        VertexList(i, 1).Offset = offset;
     elseif (findstr(tline, 'Weight') > 0)
        tline = strrep(tline, 'Weight', '');
        weight = sscanf(tline, '%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f%f', 24);
        VertexList(i, 1).Weight = weight;
        i = i + 1;
     elseif (strcmp(tline, '\n') == 0)
     end
end
fclose(fid);

 PRTClusterBases = load('PRTClusterBases.txt');

IrradianceList = zeros(NumSamples, 1);
PRTConstants = zeros(NumClusters * ( 4 + NumChannels * NumPCA ), 1);
dwClusterStride = NumChannels * NumPCA + 4;
dwBasisStride = NumCoeffs * NumChannels * ( NumPCA + 1 );
dwOrder = 6;
for iCluster = 0 : NumClusters - 1
    % For each cluster, store L' dot M[k] per channel, where M[k] is the mean of cluster k
    PRTConstants(iCluster * dwClusterStride + 1 + 0, 1) = Dot( dwOrder, iCluster * dwBasisStride + 0 * NumCoeffs, PRTClusterBases, pSHCoeffsRed );
    PRTConstants(iCluster * dwClusterStride + 1 + 1, 1) = Dot( dwOrder, iCluster * dwBasisStride + 1 * NumCoeffs, PRTClusterBases, pSHCoeffsGreen );
    PRTConstants(iCluster * dwClusterStride + 1 + 2, 1) = Dot( dwOrder, iCluster * dwBasisStride + 2 * NumCoeffs, PRTClusterBases, pSHCoeffsBlue );
    PRTConstants(iCluster * dwClusterStride + 1 + 3, 1) = 0.0;
    % Then per channel we compute L' dot B[k][j], where B[k][j] is the jth PCA basis vector for cluster k
    for  iPCA = 0 : NumPCA - 1
        nOffset = iCluster * dwBasisStride + ( iPCA + 1 ) * NumCoeffs * NumChannels;        
        PRTConstants(iCluster * dwClusterStride + 4 + 0 * NumPCA + iPCA + 1, 1) = Dot( dwOrder, nOffset + 0 * NumCoeffs, PRTClusterBases, pSHCoeffsRed );
        PRTConstants(iCluster * dwClusterStride + 4 + 1 * NumPCA + iPCA + 1, 1) = Dot( dwOrder, nOffset + 1 * NumCoeffs, PRTClusterBases, pSHCoeffsGreen );
        PRTConstants(iCluster * dwClusterStride + 4 + 2 * NumPCA + iPCA + 1, 1) = Dot( dwOrder, nOffset + 2 * NumCoeffs, PRTClusterBases, pSHCoeffsBlue );
    end			
end

for uVert = 1 : NumSamples
    iClusterOffset = VertexList(uVert,1).Offset;
 
    vPCAWeights = zeros(NumPCA, 1);
 
    vPCAWeights = VertexList(uVert, 1).Weight;

    vAccumR = zeros(4, 1);
    vAccumG = zeros(4, 1);
    vAccumB = zeros(4, 1);
    for j = 0 : NumPCA/4 - 1
        for i = 1 : 4
            vAccumR(i,1) = vAccumR(i,1) + vPCAWeights(j*4 + i ,1) * PRTConstants(4*iClusterOffset+4+(NumPCA)*0+4*j+i ,1);
            vAccumG(i,1) = vAccumG(i,1) + vPCAWeights(j*4 + i ,1) * PRTConstants(4*iClusterOffset+4+(NumPCA)*1+4*j+i, 1);
            vAccumB(i,1) = vAccumB(i,1) + vPCAWeights(j*4 + i ,1) * PRTConstants(4*iClusterOffset+4+(NumPCA)*2+4*j+i, 1);
        end
    end
    red  = PRTConstants(4 * iClusterOffset + 1,1);
    green = PRTConstants(4 * iClusterOffset + 2,1);
    blue = PRTConstants(4 * iClusterOffset + 3,1);
	for i = 1 : 4 
        red = red + vAccumR(i,1);
        green = green + vAccumG(i,1);
        blue = blue + vAccumB(i,1);
    end
    RedList(uVert, 1) = red;
    GreenList(uVert, 1) = green;
    BlueList(uVert, 1) = blue;
end

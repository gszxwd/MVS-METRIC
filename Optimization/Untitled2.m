Order = 6;
NumChannels = 3;
NumSamples = 41624;
NumCoeffs = Order * Order;
Pixels = zeros(NumSamples, NumChannels);
IntensityList = zeros(1,5);
mat = load('CompressedPRT.txt');
 
fid = fopen('VertexRef.txt'); 
tline = fgets(fid);
tline = strrep(tline, 'window size', '');
windowsize = sscanf(tline, '%d', 1);
uVert = 0; 
while ~feof(fid)
    tline = fgets(fid);
    if (findstr(tline, 'position') > 0)
        uVert = uVert + 1;
    elseif(findstr(tline, 'intensity') > 0)
       tline = strrep(tline, 'intensity', ''); 
       intensity = sscanf(tline, '%d%d%d%d%d', 5);
       IntensityList = [IntensityList; intensity'];
    else
        ;
    end         
end
IntensityList = IntensityList( 2 : size(IntensityList, 1) , 1: 5);

uVert = IntensityList(1, 1);
inten = [0, 0, 0];
num = 0; 
Pixels = zeros(NumSamples, NumChannels);
for i = 1 : size(IntensityList, 1) 
    if( IntensityList(i, 1) == uVert)
        if( i <  size(IntensityList, 1))
            inten(1) = inten(1) + IntensityList(i, 3);
            inten(2) = inten(2) + IntensityList(i, 4);
            inten(3) = inten(3) + IntensityList(i, 5);
            num = num + 1;
        else
            inten(1) = inten(1) + IntensityList(i, 3);
            inten(2) = inten(2) + IntensityList(i, 4);
            inten(3) = inten(3) + IntensityList(i, 5);
            num = num + 1;
            inten = inten / num;
            Pixels(uVert + 1, 1:3) = inten;
        end
    else
        inten = inten / num;
        Pixels(uVert + 1, 1:3) = inten;
        num = 1; 
        inten = zeros(1, 3); 
        uVert = IntensityList(i, 1);
        inten(1) = inten(1) + IntensityList(i, 3);
        inten(2) = inten(2) + IntensityList(i, 4);
        inten(3) = inten(3) + IntensityList(i, 5);
    end
end
%optimization
options = optimset('LargeScale', 'off', 'GradObj', 'on', 'FinDiffType', 'central', 'DerivativeCheck', 'on', 'Display', 'iter', 'MaxFunEvals', 1e1000000, 'MaxIter', 1e1000);
red0 = 200 * ones(36,1)
[red1,fval,exitflag,output,grad] = fminunc(@(red) RedFunc(red, mat, IntensityList, Pixels, NumCoeffs, NumChannels),red0,options)

options = optimset('LargeScale', 'off', 'GradObj', 'on', 'FinDiffType', 'central', 'DerivativeCheck', 'on', 'Display', 'iter', 'MaxFunEvals', 1e1000000, 'MaxIter', 1e1000);
green0 = 200 * ones(36,1)
[green1,fval,exitflag,output,grad] = fminunc(@(green) GreenFunc(green, mat, IntensityList, Pixels, NumCoeffs, NumChannels),green0,options)

options = optimset('LargeScale', 'off', 'GradObj', 'on', 'FinDiffType', 'central', 'DerivativeCheck', 'on', 'Display', 'iter', 'MaxFunEvals', 1e1000000, 'MaxIter', 1e1000);
blue0 = 200 * ones(36,1)
[blue1,fval,exitflag,output,grad] = fminunc(@(blue) BlueFunc(blue, mat, IntensityList, Pixels, NumCoeffs, NumChannels),blue0,options)

[RedList, GreenList, BlueList] = GetDiffuseIrradiance(red1, green1, blue1);
for i = 1 : size(RedList,1)     %size(Pixels, 1)
    RedList(i) = RedList(i) * Pixels(i, 1);
    GreenList(i) = GreenList(i) * Pixels(i, 2);
    BlueList(i) = BlueList(i) * Pixels(i, 3);
end
irradianceMatlab = [RedList, GreenList, BlueList];
save('D:\\irraMatlab.txt', 'irradianceMatlab', '-ascii')

coeffs = [red1; green1; blue1]
save('D:\\coeffs.txt', 'coeffs', '-ascii');
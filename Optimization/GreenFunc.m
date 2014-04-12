function [Sum Gradient] = GreenFunc(green, LightCoeffs, IntensityList, Pixels, NumCoeffs, NumChannels)
%UNTITLED3 Summary of this function goes here
%   Detailed explanation goes here
Sum = 0;
Gradient = 0; 
for i = 1 : size(IntensityList, 1)
    uVert = IntensityList(i, 1) + 1;
    greenCoeffs = LightCoeffs( (uVert - 1) * NumCoeffs * NumChannels +  1 * NumCoeffs + 1 : (uVert - 1) * NumCoeffs * NumChannels + 2 * NumCoeffs, 1);
    Sum = Sum + abs(greenCoeffs' * green * Pixels(uVert, 2) - IntensityList(i, 4));
end
if nargout > 1
    Gradient = zeros(NumCoeffs,1);
    for i = 1 : size(IntensityList,1)
         uVert = IntensityList(i, 1) + 1;
         greenCoeffs = LightCoeffs( (uVert - 1) * NumCoeffs * NumChannels +  1 * NumCoeffs + 1 : (uVert - 1) * NumCoeffs * NumChannels + 2 * NumCoeffs, 1);
        for j = 1 : 36
            Gradient(j) = Gradient(j) + Pixels(uVert, 2) * greenCoeffs(j,1) * sign(Pixels(uVert, 2) * greenCoeffs'* green - IntensityList(i, 4));
        end
    end
end


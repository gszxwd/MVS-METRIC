function [Sum Gradient] = RedFunc(red, LightCoeffs, IntensityList, Pixels, NumCoeffs, NumChannels)
%UNTITLED3 Summary of this function goes here
%   Detailed explanation goes here
Sum = 0;
Gradient = 0;
for i = 1 : size(IntensityList, 1)
    uVert = IntensityList(i, 1) + 1;
    redCoeffs = LightCoeffs( (uVert - 1) * NumCoeffs * NumChannels + 1 : (uVert - 1) * NumCoeffs * NumChannels + 1 * NumCoeffs, 1);
    Sum = Sum + abs(redCoeffs' * red * Pixels(uVert, 1) - IntensityList(i, 3));
end 
if nargout > 1
    Gradient = zeros(NumCoeffs,1);
    for i = 1 : size(IntensityList,1)
         uVert = IntensityList(i, 1) + 1;
         redCoeffs = LightCoeffs( (uVert - 1) * NumCoeffs * NumChannels + 1 : (uVert - 1) * NumCoeffs * NumChannels + 1 * NumCoeffs, 1);
        for j = 1 : 36
            Gradient(j) = Gradient(j) + Pixels(uVert, 1) * redCoeffs(j,1) * sign(Pixels(uVert, 1) * redCoeffs'* red - IntensityList(i, 3));
        end
    end
end


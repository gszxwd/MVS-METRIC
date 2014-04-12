function [Sum Gradient] = BlueFunc(blue, LightCoeffs, IntensityList, Pixels, NumCoeffs, NumChannels)
%UNTITLED3 Summary of this function goes here
%   Detailed explanation goes here
Sum = 0;
Gradient = 0;
for i = 1 : size(IntensityList, 1)
    uVert = IntensityList(i, 1) + 1;
    blueCoeffs = LightCoeffs( (uVert - 1) * NumCoeffs * NumChannels +  2 * NumCoeffs + 1 : uVert * NumCoeffs * NumChannels, 1);
    Sum = Sum + abs(blueCoeffs' * blue * Pixels(uVert, 3) - IntensityList(i, 5));
end
if nargout > 1
    Gradient = zeros(NumCoeffs,1);
    for i = 1 : size(IntensityList,1)
         uVert = IntensityList(i, 1) + 1;
         blueCoeffs = LightCoeffs( (uVert - 1) * NumCoeffs * NumChannels +  2 * NumCoeffs + 1 : uVert * NumCoeffs * NumChannels, 1);
        for j = 1 : 36
            Gradient(j) = Gradient(j) + Pixels(uVert, 3) * blueCoeffs(j,1) * sign(Pixels(uVert, 3) * blueCoeffs'* blue - IntensityList(i, 5));
        end
    end
end


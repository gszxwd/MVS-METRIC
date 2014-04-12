function dotResult = Dot( dwOrder, start, input1, input2 )
%UNTITLED2 Summary of this function goes here
%   Detailed explanation goes here
dotResult = 0;
for i = 1 : dwOrder * dwOrder
    dotResult = dotResult + input1(start+i, 1) * input2(i);
end


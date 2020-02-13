d=load('d:\tmp\rfid-rw\out.csv');

t = d(:,1);
ch1 = d(:,2);
ch2 = d(:,3);
clear d;

L1 = 25500; L2 = 58550;
smoothing = 10;

ch1 = interp(decimate(ch1,smoothing),smoothing);
ch2 = interp(decimate(ch2,smoothing),smoothing);
t = [zeros(smoothing-1,1);t];

t = t(L1:L2);
ch1 = ch1(L1:L2);
ch2 = ch2(L1:L2);

plotscope(t,ch2/5+0.1, -0.3,1.5,1.1)
hold on
plotscope(t,ch1/40-0.58, -0.3,1.5,1.1)






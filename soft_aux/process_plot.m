d=load('out.csv');

smoothing = 10;

t = d(:,1);
ch1 = d(:,2);
ch2 = d(:,3);
clear d;

ch1 = interp(decimate(ch1,smoothing),smoothing);
ch2 = interp(decimate(ch2,smoothing),smoothing);
t = [0;0;t];

p1 = plot(t,ch1*0.1-2, 'yellow'); 
hold on;
p2 = plot(t, ch2, 'green')
ax = gca;
ax.Color = 'black'

grid on
ax.GridColor = 'white'
ax.GridAlpha = 0.3
ax.GridLineStyle = '-.'

hold off;





function plotscope(x,y,brightness,contrast,focus)
% Plot 2D simulating an analog oscilloscope screen
% Usage: plotscope(x, y, brightness, contrast, focus)
% x, y: data
% brightness: -1 to 1, usually 0
% contrast: 1 to 10, usually 2
% focus: 1 to 2, usually 1.5

% Center the signal
x = x - mean(x);
x = x / max(abs(x));


BKCOLOR = [0.1490 0.2627 0.2431];
GRIDCOLOR = [255 255 100]/255;

% Create the color map
color0 = BKCOLOR;
color1 = [color0(1)  1  0.8];
color2 = [1  1  1];

grad_r_1 = linspace(color0(1),color1(1),100);
grad_g_1 = linspace(color0(2),color1(2),100);
grad_b_1 = linspace(color0(3),color1(3),100);

grad_r_2 = linspace(color1(1),color2(1),100);
grad_g_2 = linspace(color1(2),color2(2),100);
grad_b_2 = linspace(color1(3),color2(3),100);

mc = [grad_r_1'  grad_g_1' grad_b_1'];
mc = [mc; grad_r_2'  grad_g_2' grad_b_2'];
colormap(mc)

clear grad_r_1 grad_g_1 grad_b_1 grad_r_2 grad_g_2 grad_b_2 mc
clear color0 color1 color2


% Plot lines usng surface

% differentiate to get velocity
v = diff(y)./diff(x);

% interpolate to match plot points
v_x = linspace(mean([x(1) x(2)]), mean([x(end-1) x(end)]), length(v));
v = interp1(v_x,v,x);


% truncate limits where gradient is NaN
x = x(2:end-1);
y = y(2:end-1);
v = v(2:end-1);


% Brightness of the spot depends both the speed and the amplitude of the
% wave (area).
env = movmean(abs(v),40);
env = env / max(env);
vnorm = abs(v/quantile(abs(v),0.50));
vnorm(vnorm > 1) = 1;
b = 0.0*env + 0.8*vnorm;

% Map brightness to color using logistic function
%brightness = 0.2;
%contrast = 5.9;
%focus = 1.0;
z = 1-1./(1+exp(-contrast*(b-brightness))); 

X = [x(:) x(:)];
Y = [y(:) y(:)];
Z = [z(:) z(:)];

% Plot several lines to mimic screen blur
if focus == 0
    nblur = 1;
    focus = 1;
else
    nblur = 5;
end
for i = nblur:-0.5:1
    surf( ...
         'XData', X, ...
         'YData', Y, ...
         'ZData', Z * exp(1-i), ...
         'EdgeColor', 'interp', ...
         'LineWidth', i*focus, ...
         'EdgeAlpha', 1, ...
         'FaceColor', 'none', ...
         'AlignVertexCenters', 'on', ...
         'Marker', 'none', ...
         'MarkerSize', i);

    hold on
end

view(2);
hold off

grid on

% Color limit is not 1 to allow saturation
% Aspect ratio is 4:3
set(gca, ...
    'XAxisLocation', 'origin', ...
    'YAxisLocation', 'origin', ...
    'TickDir', 'both', ...
    'XTickLabel', [], ...
    'YTickLabel', [], ...
    'XMinorTick', 'on', ...
    'YMinorTick', 'on', ...
    'Color', BKCOLOR, ...
    'XColor', GRIDCOLOR/2, ...
    'YColor', GRIDCOLOR/2, ...
    'GridColor', GRIDCOLOR, ...
    'GridAlpha', 0.3, ...
    'GridLineStyle', '-', ...
    'Layer', 'top', ...
    'Box', 'on', ...
    'LineWidth', 0.6, ...
    'YLim', [-1.15 1.15], ...
    'XLim', [-1 1], ...
    'YMinorGrid', 'on', ...
    'XMinorGrid', 'on', ...
    'Position', [0.01 0 0.98 1], ...
    'DataAspectRatio', [1 1.75 1], ...
    'CLim', [0 0.8]);

% Grid, oscope grids are 10x8, 4 subticks
ax = gca;

lx = ax.XLim;
ax.XTick = linspace(lx(1), lx(2), 10+1);
ax.XAxis.MinorTickValues = linspace(lx(1), lx(2), 5*10+1);

ly = ax.YLim;
ax.YTick = linspace(ly(1), ly(2), 8+1);
ax.YAxis.MinorTickValues = linspace(ly(1), ly(2), 5*8+1);


f = gcf;
f.Color = BKCOLOR/1.5;


end
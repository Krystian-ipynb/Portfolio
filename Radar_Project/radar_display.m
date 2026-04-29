COM_PORT    = "COM6";
BAUD_RATE   = 9600;
MAX_DIST_CM = 6;
MAX_BLIPS   = 500;
FADE_SECS   = 3.0;   % seconds before a blip fully disappears
 
% ---- open serial port ----
s = serialport(COM_PORT, BAUD_RATE);
configureTerminator(s, "LF");
flush(s);
disp("Serial port opened: " + COM_PORT);
disp("Close the radar figure window to stop.");
 
% ---- set up the figure ----
fig = figure('Name', 'Ultrasonic Radar', ...
    'Color', [0.02 0.06 0.02], ...
    'Position', [100 100 800 600]);
ax = polaraxes('Parent', fig);
ax.Color             = [0.02 0.06 0.02];
ax.GridColor         = [0.0  0.4  0.0];
ax.GridAlpha         = 0.8;
ax.ThetaColor        = [0.0  0.6  0.0];
ax.RColor            = [0.0  0.6  0.0];
ax.ThetaLim          = [0 180];
ax.RLim              = [0 MAX_DIST_CM];
ax.ThetaZeroLocation = 'right';
ax.ThetaDir          = 'counterclockwise';
ax.FontSize          = 11;
ax.RTick             = [1.5 3.0 4.5 6.0];
ax.RTickLabel        = {'1.5cm','3.0cm','4.5cm','6.0cm'};
hold(ax, 'on');
 
% ---- sweep line ----
sweepLine = polarplot(ax, [0 pi/2], [0 MAX_DIST_CM], ...
    'Color', [0.0 1.0 0.3], 'LineWidth', 2);
 
% ---- blip scatter — initialise with NaN so nothing draws at startup ----
blipPlot = polarscatter(ax, NaN, NaN, 30, [0 1 0], 'filled');
colormap(ax, [linspace(0,0,256)', linspace(0.3,1,256)', linspace(0,0.3,256)']);
 
title(ax, 'RADAR SWEEP — HC-SR04 + SG90', ...
    'Color', [0.0 0.9 0.3], 'FontSize', 13, 'FontWeight', 'bold');
 
% ---- blip storage ----
% blipTimes stores the tic-time when each blip was created (0 = empty slot)
blipAngles = nan(1, MAX_BLIPS);
blipDists  = nan(1, MAX_BLIPS);
blipTimes  = zeros(1, MAX_BLIPS);   % wall-clock birth time via tic/toc
blipIdx    = 1;
prev_dist  = 0;
 
startTic = tic;   % master clock — toc(startTic) gives elapsed seconds
 
% ---- configure non-blocking serial timeout ----
s.Timeout = 0.05;   % readline waits at most 50 ms, then throws
 
% ---- main loop ----
disp("Scanning...");
while ishandle(fig)
    now = toc(startTic);   % current time in seconds
 
    % ---- age & expire blips based on WALL TIME, not scan steps ----
    ages    = now - blipTimes;              % seconds since each blip was born
    active  = blipTimes > 0;               % slot is occupied
    expired = active & (ages >= FADE_SECS);
 
    % Clear expired slots
    blipAngles(expired) = NaN;
    blipDists(expired)  = NaN;
    blipTimes(expired)  = 0;
 
    % ---- try to read one serial line (non-blocking via short timeout) ----
    try
        line  = readline(s);
        parts = strsplit(strtrim(line), ',');
 
        if numel(parts) == 2
            angle_deg = str2double(parts{1});
            dist_cm   = str2double(parts{2});
 
            if ~isnan(angle_deg) && ~isnan(dist_cm)
                dist_cm = min(dist_cm, MAX_DIST_CM);
 
                % ---- smoothing ----
                if prev_dist == 0
                    prev_dist = dist_cm;
                end
                dist_cm   = 0.6 * dist_cm + 0.4 * prev_dist;
                prev_dist = dist_cm;
 
                angle_rad = deg2rad(angle_deg);
 
                % ---- update sweep line ----
                set(sweepLine, ...
                    'ThetaData', [angle_rad angle_rad], ...
                    'RData',     [0 MAX_DIST_CM]);
 
                % ---- add new blip only on valid detection ----
                if dist_cm > 0 && dist_cm < MAX_DIST_CM
                    blipAngles(blipIdx) = angle_rad;
                    blipDists(blipIdx)  = dist_cm;
                    blipTimes(blipIdx)  = now;
                    blipIdx = mod(blipIdx, MAX_BLIPS) + 1;
                end
            end
        end
    catch ME
        % Timeout or read error — just continue so fading still updates
        if ~ishandle(fig)
            break;
        end
    end
 
    % ---- compute per-blip fade alpha from wall-clock age ----
    ages      = now - blipTimes;
    normAges  = ages / FADE_SECS;
    normAges(blipTimes == 0) = 1;          % empty slots → fully transparent
    fadeAlpha = max(0, (1 - normAges).^2);
 
    % ---- build colour + alpha: invisible slots get alpha=0 colour ----
    colours = [zeros(MAX_BLIPS,1), fadeAlpha(:), zeros(MAX_BLIPS,1)];
 
    % Replace NaN positions with a dummy value (0,0) so scatter doesn't
    % render a stray black marker; colour alpha is already 0 for these.
    drawAngles = blipAngles;
    drawDists  = blipDists;
    drawAngles(isnan(drawAngles)) = 0;
    drawDists(isnan(drawDists))   = 0;
 
    set(blipPlot, ...
        'ThetaData', drawAngles, ...
        'RData',     drawDists, ...
        'CData',     colours);
 
    drawnow limitrate
end
 
% ---- cleanup ----
clear s;
disp("Radar stopped. Serial port closed.");
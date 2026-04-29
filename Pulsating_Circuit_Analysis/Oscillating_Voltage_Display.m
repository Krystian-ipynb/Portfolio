%% Button-Triggered Oscillator Capture
clear; close all;

%% --- Settings ---
PORT = "COM6";
BAUD = 9600;
NUM_SAMPLES = 2000;
SAMPLE_RATE = 0.01;

%% --- Setup Serial ---
s = serialport(PORT, BAUD);
configureTerminator(s, "LF");
flush(s);

%% --- Setup Figure (time domain) ---
fig = figure('Name', 'Oscillator Capture', 'NumberTitle', 'off');
ax  = axes(fig);
xlabel(ax, 'Time (s)');
ylabel(ax, 'Voltage (V)');
title(ax, 'Waiting for button press...');
ylim(ax, [0 5]);
grid(ax, 'on');
ax.YMinorGrid = 'on';
hold(ax, 'on');

allRuns = {};
colors  = lines(20);
runNum  = 0;

disp("Waiting for button press. Close the figure to stop.");

%% --- Main Loop ---
while ishandle(fig)

    line = strtrim(char(readline(s)));
    if ~strcmp(line, "START"), continue; end

    runNum = runNum + 1;
    fprintf("Run %d started...\n", runNum);
    title(ax, sprintf('Run %d - collecting...', runNum));
    drawnow;

    voltageData = zeros(1, NUM_SAMPLES);
    i = 1;
    while i <= NUM_SAMPLES && ishandle(fig)
        line = strtrim(readline(s));
        if strcmp(line, "END"), break; end
        val = str2double(line);
        if ~isnan(val)
            voltageData(i) = val;
            i = i + 1;
        end
    end

    timeData = (0:NUM_SAMPLES-1) * SAMPLE_RATE;
    allRuns{runNum} = voltageData;

    % --- Time domain plot ---
    plot(ax, timeData, voltageData, ...
        'Color', colors(mod(runNum-1, 20)+1, :), ...
        'LineWidth', 1.5, ...
        'DisplayName', sprintf('Run %d', runNum));
    legend(ax, 'show', 'Location', 'best');
    title(ax, sprintf('Run %d complete - waiting for next press...', runNum));
    drawnow;

    % --- Power Engineering Analysis ---
    fs = 1 / SAMPLE_RATE;

    % RMS voltage
    Vrms = sqrt(mean(voltageData.^2));

    % Two-sided FFT using fftshift
    Y        = fftshift(fft(voltageData));
    Y_mag    = abs(Y / NUM_SAMPLES);
    f        = linspace(-fs/2, fs/2, NUM_SAMPLES);

    % One-sided values for analysis (positive frequencies only)
    halfN      = NUM_SAMPLES/2 + 1;
    Y_single   = Y_mag(NUM_SAMPLES/2+1:end);
    Y_single(2:end) = 2 * Y_single(2:end);
    f_single   = f(NUM_SAMPLES/2+1:end);

    % Dominant frequency
    [~, peakIdx] = max(Y_single(2:end));
    peakIdx      = peakIdx + 1;
    dominantFreq = f_single(peakIdx);
    period       = 1 / dominantFreq;

    % THD - first 5 harmonics
    harmonicPower = 0;
    for h = 2:5
        hIdx = round(h * dominantFreq / (fs / NUM_SAMPLES)) + 1;
        if hIdx <= length(Y_single)
            harmonicPower = harmonicPower + Y_single(hIdx)^2;
        end
    end
    THD = sqrt(harmonicPower) / Y_single(peakIdx) * 100;

    % Voltage sag and swell detection
    windowSize = 100;
    sagCount   = 0;
    swellCount = 0;
    for w = 1:windowSize:NUM_SAMPLES-windowSize
        windowRMS = sqrt(mean(voltageData(w:w+windowSize-1).^2));
        if windowRMS < 0.9 * Vrms
            sagCount = sagCount + 1;
        elseif windowRMS > 1.1 * Vrms
            swellCount = swellCount + 1;
        end
    end

    % Power factor simulation (50 sample phase shift)
    shiftedSignal = circshift(voltageData, 50);
    realPower     = mean(voltageData .* shiftedSignal);
    apparentPower = Vrms * sqrt(mean(shiftedSignal.^2));
    PF            = realPower / apparentPower;

    % --- Print results ---
    fprintf("\n--- Run %d Power Analysis ---\n", runNum);
    fprintf("  Dominant frequency : %.3f Hz\n", dominantFreq);
    fprintf("  Period             : %.3f s\n",  period);
    fprintf("  Vrms               : %.3f V\n",  Vrms);
    fprintf("  THD                : %.2f %%\n", THD);
    fprintf("  Power factor       : %.3f\n",    PF);
    fprintf("  Voltage sags       : %d windows\n",   sagCount);
    fprintf("  Voltage swells     : %d windows\n\n", swellCount);

    % --- Analysis figures ---
    analysisColor = colors(mod(runNum-1, 20)+1, :);

    % Two-sided FFT spectrum plot
    figFFT = figure('Name', sprintf('Run %d - FFT Spectrum', runNum));
    axFFT  = axes(figFFT);
    plot(axFFT, f, Y_mag, 'Color', analysisColor, 'LineWidth', 1.5);
    xlabel(axFFT, 'Frequency (Hz)');
    ylabel(axFFT, 'Amplitude (V)');
    title(axFFT, sprintf('Run %d - Two-sided FFT  |  Dominant: %.3f Hz', runNum, dominantFreq));
    xlim(axFFT, [-fs/2 fs/2]);
    grid(axFFT, 'on');

    % Power quality summary table
    figPQ = figure('Name', sprintf('Run %d - Power Quality', runNum));
    axPQ  = axes(figPQ);
    axis(axPQ, 'off');
    title(axPQ, sprintf('Run %d - Power Quality Metrics', runNum));

    tableData    = {Vrms; THD; PF};
    rowNames     = {'Vrms (V)'; 'THD (%)'; 'Power Factor'};
    colNames     = {'Value'};

    t = uitable(figPQ, ...
    'Data',                tableData, ...
    'RowName',             rowNames, ...
    'ColumnName',          colNames, ...
    'Units',               'Normalized', ...
    'Position',            [0.1 0.2 0.8 0.6], ...
    'ColumnWidth',         {150}, ...
    'FontSize',            14);

end

%% --- Cleanup ---
delete(s);
disp("Session ended.");
fprintf("Total runs captured: %d\n", runNum);

for k = 1:numel(allRuns)
    filename = sprintf('run_%02d.csv', k);
    writematrix(allRuns{k}', filename);
    fprintf("Saved %s\n", filename);
end
clc;
clearvars;
close all;

%ta files me ta trades prepei na einai sto idio path me ton kodika

files = ["moving_avg.txt","candlesticks.txt"];
symbols = ["AAPL" "AMZN" "TSLA" "BINANCE_ETHUSDT"];
time_candlestick = 6;
time_mov_avg = 3;
for i=1:size(files,2)
    if files(i) == "moving_avg.txt"
        time_col = time_mov_avg;
    else
        time_col = time_candlestick;
    end
    figure('Name',files(i));
    for j=1:size(symbols,2)
        file = sprintf('%s_%s',symbols(j),files(i));
        data = readmatrix(file);
        time = data(:,time_col);
        time = abs(time);  %get the absolute values
        subplot(4,1,j);
        %length(time)
        plot(time,LineStyle="--",LineWidth=2,Color="r");
        xlim([1 length(time)]);
        xlabel("minutes","FontSize",12,"FontWeight","bold")
        ylabel("delay (ms)","FontSize",12,"FontWeight","bold");
        grid on
        title(file,Interpreter="none",FontSize=14,FontWeight="bold");
    end
    
    
end

%%for the cpu idle

real_time = 2905*60 + 21.403;
user_time = 17*60 + 1.258;
sys_time = 7*60 + 1.161;

active_cpu_time = user_time + sys_time;
idle_time = real_time - active_cpu_time;
idle_percentage = (idle_time / real_time) * 100; 

fprintf('CPU Idle Time Percentage: %.2f%%\n', idle_percentage);
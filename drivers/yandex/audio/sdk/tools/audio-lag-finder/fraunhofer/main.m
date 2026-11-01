pkg load signal;

clear ;
close all;
dashboard = {};

%% Read input files
[mic,fs_mic] = audioread('mic.wav');
[spk, fs_spk] = audioread('spk.wav');
[lb, fs_lb] = audioread('lb.wav');

%% Delay estimation (mic to spk)
delay_est( mic,spk,fs_mic,"mic_spk_delay_est.png" );

%% Check if delay between far-end and near-end is negative
[xc,lag] = xcorr(mic(1:end,1),spk);
[~,I]    = max(abs(xc));
del      = lag(I);
warning(['Mic to Spk: Delay between far and near-end is approximately equal to ', num2str(del),' samples']);

%% Delay estimation (lb to spk)
delay_est( lb,spk,fs_lb,"lb_spk_delay_est.png" );

%% Check if delay between far-end and near-end is negative
[xc,lag] = xcorr(lb(1:end,1),spk);
[~,I]    = max(abs(xc));
del      = lag(I);
warning(['Lb to Spk: Delay between far and near-end is approximately equal to ', num2str(del),' samples']);

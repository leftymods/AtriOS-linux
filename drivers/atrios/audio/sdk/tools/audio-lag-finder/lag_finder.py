import os
import argparse
import numpy as np
import matplotlib.pyplot as plt

from voicetech.vqe.pylibs.audio_lag import calc_audio_lag


def draw_waveform(raw, name, sample_rate):
    signal = np.fromstring(raw, np.int16)
    time = np.linspace(0, len(signal) / sample_rate, num=len(signal))
    plt.figure(figsize=(20, 5))
    plt.title("Signal Wave...")
    plt.plot(time, signal)
    plt.savefig(name)


def main():
    parser = argparse.ArgumentParser(description='Audio Lag Finder.')
    parser.add_argument('--raw1', metavar='path', type=str, required=True, help='path to audio1.raw')
    parser.add_argument('--raw2', metavar='path', type=str, required=True, help='path to audio2.raw')
    parser.add_argument(
        '--dump-waveform', required=False, action='store_true', help='will dump waveforms as .png files'
    )
    parser.add_argument('--dump-correlation', required=False, action='store_true', help='dump correlation graph')

    args = parser.parse_args()

    raw1_path = args.raw1
    raw2_path = args.raw2

    raw1_size = os.path.getsize(raw1_path)
    raw2_size = os.path.getsize(raw2_path)
    with open(raw1_path, "rb") as f:
        raw1 = f.read(raw1_size)

    with open(raw2_path, "rb") as f:
        raw2 = f.read(raw2_size)

    sample_rate = 16000
    if args.dump_waveform:
        draw_waveform(raw1, 'raw1_wfm.png', sample_rate)
        draw_waveform(raw2, 'raw2_wfm.png', sample_rate)

    samples1 = np.frombuffer(raw1, dtype=np.int16).reshape(-1, 1)
    samples2 = np.frombuffer(raw2, dtype=np.int16).reshape(-1, 1)

    best_shift, best_corr, corrs = calc_audio_lag(samples1, samples2, sample_rate)
    best_corr = abs(best_corr)
    print(f"Found shift: {best_shift} frames. {best_shift/(1. * sample_rate)}s. Correlation: {best_corr}")

    if args.dump_correlation:
        plt.figure(figsize=(20, 20))
        plt.plot(corrs)
        plt.savefig('corrs.png')

import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal

# Define parameters
Fs = 48000  # Sampling frequency (Hz)
Fc = 5000  # Cutoff frequency (Hz)
N = 15  # Number of taps (must be odd for symmetry)
fc_norm = Fc / (Fs / 2)  # Normalize cutoff (0 to 1, Nyquist = 1)
print(fc_norm)

# Design the filter
# h = signal.firwin(N, fc_norm, window='hamming')
#
# # Plot impulse response
# plt.stem(h)
# plt.title("Impulse Response of FIR Low-Pass Filter")
# plt.xlabel("Samples")
# plt.ylabel("Amplitude")
# plt.show()
#
# # Plot frequency response

# Ok, so the ideal frequency response is the sinc function
# We need to apply a hamming window also.


M = (N - 1) / 2
h = [1/N for n in range(N)]


w, H = signal.freqz(h, worN=8000)
plt.plot(w / np.pi, 20 * np.log10(abs(H)))
plt.title("Magnitude Response of FIR Low-Pass Filter Moving Average")
plt.xlabel("Normalized Frequency (×π rad/sample)")
plt.ylabel("Magnitude (dB)")
plt.grid()
plt.show()

M = (N - 1) / 2
h = [np.sinc(2 * fc_norm * (n - M)) for n in range(N)]


w, H = signal.freqz(h, worN=8000)
plt.plot(w / np.pi, 20 * np.log10(abs(H)))
plt.title("Magnitude Response of FIR Low-Pass Filter")
plt.xlabel("Normalized Frequency (×π rad/sample)")
plt.ylabel("Magnitude (dB)")
plt.grid()
plt.show()


# Apply Hamming window
hamming_window = np.hamming(N)
h *= hamming_window  # Element-wise multiplication


w, H = signal.freqz(h, worN=8000)
plt.plot(w / np.pi, 20 * np.log10(abs(H)))
plt.title("Magnitude Response of FIR Low-Pass Filter With Hamming Window")
plt.xlabel("Normalized Frequency (×π rad/sample)")
plt.ylabel("Magnitude (dB)")
plt.grid()
plt.show()


# Ok, now I want to pre-compute both sinc(x) and hamming_window, to use them as lookup tables instead of computing them on the fly

sinc_range = 32
sinc_len = 512
# Generate sinc lookup table
sinc_x = np.linspace(0, sinc_range, sinc_len)  # 512 samples from 0 to 32
sinc_table = np.sinc(sinc_x)  # Compute sinc function once
print(sinc_table)

# Generate hamming lookup table
# 512 samples over the Hamming window range
hamming_table = np.hamming(sinc_len)  # Compute full Hamming window


# Compute filter coefficients using lookup tables
M = (N - 1) / 2

sinc_values = []
for n in range(N):
    x = 2 * fc_norm * (n - M)
    x = abs(x)  # sinc function is symmetric

    # Normalize x to lookup table range
    index_float = x / sinc_range * (sinc_len - 1)

    # Linear interpolation
    index_low = int(np.floor(index_float))
    index_high = min(index_low + 1, sinc_len - 1)  # Ensure it doesn't go out of bounds
    weight = index_float - index_low  # Fractional part

    # Interpolated sinc value
    sinc_val = (1 - weight) * sinc_table[index_low] + weight * sinc_table[index_high]
    sinc_values.append(sinc_val)

# sinc_indexes = np.array(sinc_indexes)
# sinc_values = sinc_table[sinc_indexes]

h = sinc_values * hamming_window  # Apply Hamming window

# Plot impulse response
plt.stem(h)
plt.title("Impulse Response of FIR Low-Pass Filter With sinc and hanning lookup table")
plt.xlabel("Samples")
plt.ylabel("Amplitude")
plt.show()

# Compute and plot frequency response
w, H = signal.freqz(h, worN=8000)
plt.plot(w / np.pi, 20 * np.log10(abs(H)))
plt.title("Magnitude Response of FIR Low-Pass Filter with sinc and hanning LUT")
plt.xlabel("Normalized Frequency (×π rad/sample)")
plt.ylabel("Magnitude (dB)")
plt.grid()
plt.show()

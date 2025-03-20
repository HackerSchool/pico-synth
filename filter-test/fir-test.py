import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal
import sounddevice as sd

# Define parameters
Fs = 44100  # Sampling frequency (Hz)
Fc = 5000  # Cutoff frequency (Hz)
N = 33  # Number of taps (must be odd for symmetry)
fc_norm = Fc / (Fs / 2)  # Normalize cutoff (0 to 1, Nyquist = 1)
print("fc_norm", fc_norm)


duration = 5.0  # 1-second audio
t = np.linspace(0, duration, int(Fs * duration), endpoint=False)

# Generate a 1000 Hz triangle wave
freq = 8000  # Hz
triangle_wave = signal.square(2 * np.pi * freq * t)  # width=0.5 makes it a triangle wave

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
h = [1 / N for n in range(N)]


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
print("M", M)
sinc_values = []
for n in range(N):
    n_minus_M = n - M
    print("n_minus_M", n_minus_M)
    x = 2 * fc_norm * (n_minus_M)
    x = abs(x)  # sinc function is symmetric
    print("x", x)

    # Normalize x to lookup table range
    index_float = x / sinc_range * (sinc_len - 1)

    print("index_float", index_float)

    # Linear interpolation
    index_low = int(np.floor(index_float))
    # Ensure it doesn't go out of bounds
    index_high = min(index_low + 1, sinc_len - 1)

    print("index_high", index_high)
    print("index_low", index_low)
    weight = index_float - index_low  # Fractional part
    print("weight", weight)

    # Interpolated sinc value
    sinc_val = (1 - weight) * sinc_table[index_low] + weight * sinc_table[index_high]
    print("sinc val", sinc_val)
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


h_pi = [
    0.000000,
    0.000260,
    0.001690,
    -0.001461,
    -0.008413,
    0.000507,
    0.021432,
    0.007907,
    -0.040330,
    -0.031117,
    0.061903,
    0.080958,
    -0.082586,
    -0.193264,
    0.097941,
    0.687198,
    1.000000,
    0.687198,
    0.097941,
    -0.193264,
    -0.082586,
    0.080958,
    0.061903,
    -0.031117,
    -0.040330,
    0.007907,
    0.021432,
    0.000507,
    -0.008413,
    -0.001461,
    0.001690,
    0.000260,
    0.00000,
]
# Compute and plot frequency response
w, H = signal.freqz(h_pi, worN=8000)
plt.plot(w / np.pi, 20 * np.log10(abs(H)))
plt.title("Magnitude Response of FIR Low-Pass Filter pi")
plt.xlabel("Normalized Frequency (×π rad/sample)")
plt.ylabel("Magnitude (dB)")
plt.grid()
plt.show()

# Compute the difference
h_diff = h - h_pi

# Create figure and axis
plt.figure(figsize=(10, 6))

# Plot h and h_pi on the same plot
plt.stem(np.arange(33), h, linefmt="b-", markerfmt="bo", basefmt="b", label="h")
plt.stem(np.arange(33), h_pi, linefmt="r-", markerfmt="ro", basefmt="r", label="h_pi")

# Plot their difference in a separate subplot
plt.stem(
    np.arange(33), h_diff, linefmt="g-", markerfmt="go", basefmt="g", label="h - h_pi"
)

# Add title and labels
plt.title(
    "Impulse Response of FIR Low-Pass Filter \nWith Sinc and Hanning Lookup Table"
)
plt.xlabel("Samples")
plt.ylabel("Amplitude")
plt.legend()

# Show plot
plt.show()


filtered_signal = np.convolve(triangle_wave, h, mode='same')

# Normalize the output to prevent clipping
filtered_signal = filtered_signal / np.max(np.abs(filtered_signal))

# Plot original and filtered signals
plt.figure(figsize=(10, 5))
plt.plot(t[:1000], triangle_wave[:1000], label="Original Triangle Wave", alpha=0.7)
plt.plot(t[:1000], filtered_signal[:1000], label="Filtered Signal", alpha=0.7)
plt.xlabel("Time (s)")
plt.ylabel("Amplitude")
plt.legend()
plt.title("1000Hz Triangle Wave Before and After Convolution")
plt.show()

# Play the filtered audio
sd.play(filtered_signal, samplerate=Fs)
sd.wait()  # Wait until audio playback finishes


sd.play(triangle_wave, samplerate=Fs)
sd.wait()  # Wait until audio playback finishes

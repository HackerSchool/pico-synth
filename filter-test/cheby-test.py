from scipy.signal import cheby1, sosfreqz, sosfilt
import numpy as np
import matplotlib.pyplot as plt

# Filter specifications
order = 8           # Filter order
ripple = 1          # dB of ripple in the passband
fs = 44100           # Sampling frequency in Hz
fc = 5000          # Cutoff frequency in Hz

# Design the digital Chebyshev Type I filter
sos = cheby1(order, ripple, fc, btype='low', fs=fs, output='sos')

# Frequency response
w, h = sosfreqz(sos, fs=fs)
plt.semilogx(w, 20 * np.log10(abs(h)))
plt.title('Chebyshev Type I Filter Frequency Response')
plt.xlabel('Frequency [Hz]')
plt.ylabel('Gain [dB]')
plt.grid()
plt.show()

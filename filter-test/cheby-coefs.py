import matplotlib.pyplot as plt
import numpy as np
from scipy import signal


def cheby_lpf_coeffs(n, epsilon, fs, fc):
    """
    Calculate coefficients for a Chebyshev lowpass filter

    Parameters:
    n (int): Filter order (must be even)
    epsilon (float): Ripple parameter [0,1]
    fs (float): Sampling frequency
    fc (float): Cutoff frequency

    Returns:
    tuple: (b, a) coefficients for the filter
    """
    if n % 2 != 0:
        raise ValueError("Filter order must be even")

    m = int(n / 2)
    a = np.tan(np.pi * fc / fs)
    a2 = a * a
    u = np.log((1.0 + np.sqrt(1.0 + epsilon * epsilon)) / epsilon)
    su = np.sinh(u / n)
    cu = np.cosh(u / n)

    # Initialize arrays
    A = np.zeros(m)
    d1 = np.zeros(m)
    d2 = np.zeros(m)

    # Calculate coefficients
    for i in range(m):
        b = np.sin(np.pi * (2.0 * i + 1.0) / (2.0 * n)) * su
        c = np.cos(np.pi * (2.0 * i + 1.0) / (2.0 * n)) * cu
        c = b * b + c * c
        s = a2 * c + 2.0 * a * b + 1.0
        A[i] = a2 / (4.0 * s)
        d1[i] = 2.0 * (1 - a2 * c) / s
        d2[i] = -(a2 * c - 2.0 * a * b + 1.0) / s

    # Convert to transfer function
    # The original code implements a cascade of second-order sections
    # We'll build the full transfer function for visualization
    b_sections = []
    a_sections = []

    for i in range(m):
        # Each section is A[i] * (1 + 2z^-1 + z^-2) / (1 - d1[i]z^-1 - d2[i]z^-2)
        b_sec = A[i] * np.array([1, 2, 1])
        a_sec = np.array([1, -d1[i], -d2[i]])
        b_sections.append(b_sec)
        a_sections.append(a_sec)

    # Combine all sections
    b_full = b_sections[0]
    a_full = a_sections[0]

    for i in range(1, m):
        b_full = np.convolve(b_full, b_sections[i])
        a_full = np.convolve(a_full, a_sections[i])

    # Apply normalization factor
    b_full = b_full * (2.0 / epsilon)

    return b_full, a_full


def plot_frequency_response(
    b, a, fs, title="Chebyshev Lowpass Filter Frequency Response"
):
    """
    Plot the frequency response of the filter

    Parameters:
    b (array): Numerator coefficients
    a (array): Denominator coefficients
    fs (float): Sampling frequency
    title (str): Plot title
    """
    w, h = signal.freqz(b, a, worN=8000)
    f = w * fs / (2 * np.pi)

    # Plot magnitude response
    plt.figure(figsize=(10, 6))

    # Magnitude response in dB
    plt.subplot(2, 1, 1)
    db = 20 * np.log10(np.maximum(np.abs(h), 1e-10))
    plt.plot(f, db)
    plt.title(title)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Magnitude (dB)")
    plt.grid(True)

    # Phase response
    plt.subplot(2, 1, 2)
    phase = np.unwrap(np.angle(h))
    plt.plot(f, phase)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Phase (radians)")
    plt.grid(True)

    plt.tight_layout()
    plt.show()


# Example usage
if __name__ == "__main__":
    # Example parameters
    n = 6  # Filter order (must be even)
    epsilon = 1  # Ripple parameter
    fs = 44100  # Sampling frequency in Hz
    fc = 5000  # Cutoff frequency in Hz

    # Calculate filter coefficients
    b, a = cheby_lpf_coeffs(n, epsilon, fs, fc)

    # Print coefficients
    print("Numerator coefficients (b):", b)
    print("Denominator coefficients (a):", a)

    # Plot frequency response
    plot_frequency_response(b, a, fs)

    # Compare with scipy's implementation
    b_scipy, a_scipy = signal.cheby1(
        n, 20 * np.log10(1 / epsilon), fc / (fs / 2), "low", analog=False
    )

    # Plot scipy's implementation for comparison
    plot_frequency_response(b_scipy, a_scipy, fs, "SciPy's Chebyshev Type I Filter")

    # Check if our implementation matches scipy's
    print("\nDifference between our implementation and scipy's:")
    print("Max difference in numerator coefficients:", np.max(np.abs(b - b_scipy)))
    print("Max difference in denominator coefficients:", np.max(np.abs(a - a_scipy)))

import numpy as np

def generate_zc_sequence(N, q):
    """
    Generates a Zadoff-Chu sequence of length N and root q.

    Parameters:
    N (int): Sequence length (must be a positive integer)
    q (int): Root index (must be an integer relatively prime to N)

    Returns:
    numpy.ndarray: Complex-valued Zadoff-Chu sequence of length N
    """
    n = np.arange(N)
    exponent = -1j * np.pi * q * n * (n + 1) / N
    zc_sequence = np.exp(exponent)
    return zc_sequence

def zc_sequence_to_binary(zc_sequence, method='real'):
    """
    Maps a complex Zadoff-Chu sequence to a binary sequence.

    Parameters:
    zc_sequence (numpy.ndarray): Complex-valued Zadoff-Chu sequence
    method (str): Mapping method ('real', 'imag', or 'phase')

    Returns:
    numpy.ndarray: Binary sequence (numpy array of 0s and 1s)
    """
    if method == 'real':
        values = np.real(zc_sequence)
    elif method == 'imag':
        values = np.imag(zc_sequence)
    elif method == 'phase':
        phases = np.angle(zc_sequence)
        values = np.cos(phases)  # or use phases directly
    else:
        raise ValueError("Invalid mapping method. Choose 'real', 'imag', or 'phase'.")

    binary_sequence = (values >= 0).astype(int)
    return binary_sequence

def check_run_length_constraints(binary_sequence, max_ones=3, max_zeros=2):
    """
    Checks if a binary sequence meets the run-length constraints.

    Parameters:
    binary_sequence (numpy.ndarray): Binary sequence to check
    max_ones (int): Maximum allowed consecutive ones
    max_zeros (int): Maximum allowed consecutive zeros

    Returns:
    bool: True if constraints are met, False otherwise
    """
    binary_str = ''.join(map(str, binary_sequence))
    ones_runs = max(len(s) for s in binary_str.split('0'))
    zeros_runs = max(len(s) for s in binary_str.split('1'))

    if ones_runs > max_ones or zeros_runs > max_zeros:
        return False
    else:
        return True

def generate_codewords(N, q_values, max_ones=3, max_zeros=2, mapping_method='real'):
    """
    Generates codewords for given q values that meet run-length constraints.

    Parameters:
    N (int): Sequence length
    q_values (list): List of root indices (q values)
    max_ones (int): Maximum allowed consecutive ones
    max_zeros (int): Maximum allowed consecutive zeros
    mapping_method (str): Method to map complex sequence to binary ('real', 'imag', 'phase')

    Returns:
    dict: Dictionary mapping q values to binary codewords
    """
    codewords = {}
    for q in q_values:
        # Ensure q is relatively prime to N
        if np.gcd(q, N) != 1:
            continue  # Skip q values not relatively prime to N

        # Generate the ZC sequence
        zc_seq = generate_zc_sequence(N, q)

        # Map to binary sequence
        binary_seq = zc_sequence_to_binary(zc_seq, method=mapping_method)

        # Check run-length constraints
        if check_run_length_constraints(binary_seq, max_ones, max_zeros):
            codewords[q] = binary_seq

    return codewords

# Example usage
if __name__ == "__main__":
    N = 17  # Sequence length
    q_values = range(1, 17)  # Root indices from 1 to 16

    # Generate codewords that meet the run-length constraints
    codewords = generate_codewords(N, q_values, max_ones=3, max_zeros=2, mapping_method='real')

    # Map codewords to 4-bit data words (assuming codewords are sufficient)
    data_words = [format(i, '04b') for i in range(16)]  # '0000' to '1111'

    # Assign codewords to data words
    assigned_codewords = {}
    for data_word, (q, binary_seq) in zip(data_words, codewords.items()):
        assigned_codewords[data_word] = binary_seq
        print(f"Data word: {data_word}, Root q: {q}")
        print("Binary sequence:")
        print(binary_seq)
        print("---------------")

    if len(assigned_codewords) < 16:
        print("Warning: Not enough codewords meet the run-length constraints to represent all 4-bit data words.")


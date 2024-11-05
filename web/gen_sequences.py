import re
from itertools import combinations

def has_invalid_runs(bit_string):
    """
    Checks if the binary string has invalid runs.

    Parameters:
    bit_string (str): The binary string to check.

    Returns:
    bool: True if invalid runs are found, False otherwise.
    """
    # Check for runs of '1's longer than 4
    if re.search(r'1{4,}', bit_string):
        return True
    # Check for runs of '0's longer than 2
    if re.search(r'0{3,}', bit_string):
        return True
    return False

def compute_correlation(original):
    """
    Computes the correlation of the original integer with its shifted versions.

    Parameters:
    original (int): The original 16-bit integer.

    Returns:
    tuple: Correlation values with +1 and -1 bit shifts.
    """
    correlations = []
    # Shift left by 1 bit, zero fill on the LSB side
    for i in range(1, 2):
        shifted_left = (original << i) & 0xFFFF  # Ensure 16-bit by masking
        xor_left = original ^ shifted_left
        num_ones_left = bin(xor_left).count('1')
        correlation_left = 16 - num_ones_left
        correlations.append(correlation_left)
    
    # Shift right by 1 bit, one fill on the MSB side
    for i in range(1, 2):
        shifted_right = (original >> i) | 0xFFFF  # One fill 
        xor_right = original ^ shifted_right
        num_ones_right = bin(xor_right).count('1')
        correlation_right = 16 - num_ones_right
        correlations.append(correlation_right)

    return correlations
    
def compute_inner_corr(number):
    byte_a = (number >> 8) & 0x00FF
    byte_b = (number & 0x00FF)
    xor_bytes = byte_a ^ byte_b
    num_ones = bin(xor_bytes).count('1')
    correlation = 8 - num_ones
    
    return correlation
   

def hamming_distance(a, b):
    """
    Computes the Hamming distance between two 16-bit integers.

    Parameters:
    a (int): First integer.
    b (int): Second integer.

    Returns:
    int: Hamming distance between a and b.
    """
    return bin(a ^ b).count('1')

def main():
    # First stage: collect all integers that meet initial criteria
    candidates = []
    for i in range(1, 65536, 2):  # Iterate over odd numbers only
        bit_string = format(i, '016b')  # Convert to 16-bit binary string
        if not has_invalid_runs(bit_string):
            # The integer passes the initial filters
            correlations = compute_correlation(i)
            # Eliminate integers with correlation of 7 or higher
            valid = True
            for c in correlations:
                if c > 9:
                    valid = False
            if valid:
                # The integer passes all filters
                candidates.append(i)

    print('First-stage candidates (low autocorrelation at low shifts): %d' % len(candidates))
    
    # Now filter out all of those numbers that don't have high redundancy
    next_candidates = []
    for c in candidates:
        inner_corr = compute_inner_corr(c)
        # For this stage, we want a *high* correlation (i.e., redundancy)
        if inner_corr > 7:
            next_candidates.append(c)

    print('Second-stage candidates (high inner correlation, MSB to LSB): %d' % len(next_candidates))
    if len(next_candidates) <= 20:
        for x in next_candidates:
            print(format(x, '016b'))

    # next stage: compute Hamming distances between all pairs
    pair_count = 0
    values = []
    distance_thresh = 8
    individuals = {}
    for a, b in combinations(next_candidates, 2):
        distance = hamming_distance(a, b)
        if distance > distance_thresh:
            pair_count += 1
            value = '%s, %s : %d' % (format(a, '016b'), format(b, '016b'), distance)
            values.append(value)
            key_a = format(a, '016b')
            key_b = format(b, '016b')
            if key_a not in individuals:
                individuals[key_a] = 1
            else:
                individuals[key_a] += 1
            if key_b not in individuals:
                individuals[key_b] = 1
            else:
                individuals[key_b] += 1
            

    # Print the count of pairs that meet the Hamming distance criterion
    print(f"Count of pairs with Hamming distance greater than {distance_thresh}: {pair_count}")
    
    if pair_count < 40:
        for x in values:
            print(x)
            
        print('Most common individual values in the filtered list:')
        for k, v in individuals.items():
            print('%s: %d' % (k, v))
        

if __name__ == "__main__":
    main()

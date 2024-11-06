from itertools import combinations

codewords_corrected = [
    int("1011001010110011", 2),  # RF_CODEWORD_0
    int("0100101001001010", 2),  # RF_CODEWORD_1
    int("1001010110010101", 2),  # RF_CODEWORD_2
    int("0101001101010011", 2),  # RF_CODEWORD_3
    int("0010010100100110", 2),  # RF_CODEWORD_4
    int("1110100111001101", 2),  # RF_CODEWORD_5 
    int("0110101100110100", 2),  # RF_CODEWORD_6
    int("1110011010101001", 2),   # RF_CODEWORD_7
    int("0000000000000000", 2),  # all zeros, as a check
    int("1111111111111111", 2),  # all ones, as a check
]

for i in codewords_corrected:
    print(i)
    

# Function to compute Hamming distance between two binary numbers
def hamming_distance(x, y):
    return bin(x ^ y).count('1')

# Re-calculate Hamming distances with the corrected codewords
distances_corrected = []
for (i, j) in combinations(range(len(codewords_corrected)), 2):
    distance = hamming_distance(codewords_corrected[i], codewords_corrected[j])
    distances_corrected.append(((i, j), distance))

# Sort by Hamming distance and get the lowest 10
worst_10_corrected = sorted(distances_corrected, key=lambda x: x[1])[:30]
for x in worst_10_corrected:
    print(x)

'''
Results for these values:
    int("1011001010110011", 2),  # RF_CODEWORD_0
    int("0100101001001010", 2),  # RF_CODEWORD_1
    int("1001010110010101", 2),  # RF_CODEWORD_2
    int("0101001101010011", 2),  # RF_CODEWORD_3
    int("0010010100100110", 2),  # RF_CODEWORD_4
    int("1110100111001101", 2),  # RF_CODEWORD_5 
    int("0110101100110100", 2),  # RF_CODEWORD_6
    int("1110011010101001", 2),   # RF_CODEWORD_7
    int("0000000000000000", 2),  # all zeros, as a check
    int("1111111111111111", 2),  # all ones, as a check
45747
19018
38293
21331
9510
59853
27444
59049
0
65535
((0, 7), 6)
((1, 3), 6)
((1, 8), 6)
((4, 6), 6)
((4, 8), 6)
((5, 9), 6)
((0, 2), 7)
((0, 3), 7)
((0, 9), 7)
((5, 7), 7)
((7, 9), 7)
((1, 5), 8)
((1, 6), 8)
((2, 3), 8)
((2, 4), 8)
((2, 5), 8)
((2, 8), 8)
((2, 9), 8)
((3, 6), 8)
((3, 8), 8)
((3, 9), 8)
((5, 6), 8)
((6, 8), 8)
((6, 9), 8)
((0, 4), 9)
((0, 6), 9)
((0, 8), 9)
((1, 7), 9)
((2, 7), 9)
((4, 7), 9)
'''
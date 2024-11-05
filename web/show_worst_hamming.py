from itertools import combinations

codewords_corrected = [
    int("1011001010110011", 2),  # RF_CODEWORD_0
    int("0100100001001010", 2),  # RF_CODEWORD_1
    int("1001010110010101", 2),  # RF_CODEWORD_2
    int("0101001101010011", 2),  # RF_CODEWORD_3
    int("0010010100100100", 2),  # RF_CODEWORD_4
    int("1110100111001101", 2),  # RF_CODEWORD_5 (corrected as per user feedback)
    int("0010101100110010", 2),  # RF_CODEWORD_6
    int("1110011010101001", 2)   # RF_CODEWORD_7
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

import numpy as np
import jpegio as jio
import sys
def merge_diff_jpeg(base_jpg_path, diff_jpg_path, output_merged_path):
    jpeg0 = jio.read(base_jpg_path)
    jpeg1 = jio.read(diff_jpg_path)
    BLOCK_SIZE = 8
    for component in range(len(jpeg0.coef_arrays)):
        coeffs0 = jpeg0.coef_arrays[component]
        coeffs1 = jpeg1.coef_arrays[component]
        num_blocks_h, num_blocks_w = [x // BLOCK_SIZE for x in coeffs1.shape]
        blocks1 = coeffs1.reshape(num_blocks_h, BLOCK_SIZE, num_blocks_w, BLOCK_SIZE).transpose(0, 2, 1, 3)
        blocks0 = coeffs0.reshape(num_blocks_h, BLOCK_SIZE, num_blocks_w, BLOCK_SIZE).transpose(0, 2, 1, 3)
        mask = np.any(blocks1, axis=(2, 3))
        blocks0[mask] = blocks1[mask]
    jio.write(jpeg0, output_merged_path)
if __name__ == '__main__':
    merge_diff_jpeg(sys.argv[1], sys.argv[2], sys.argv[3])

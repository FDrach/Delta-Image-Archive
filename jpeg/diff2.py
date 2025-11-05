import numpy as np
import jpegio as jio
import sys
def create_diff_jpeg(base_jpg_path, new_jpg_path, output_diff_path):
    jpeg0 = jio.read(base_jpg_path)
    jpeg1 = jio.read(new_jpg_path)
    BLOCK_SIZE = 8
    for component in range(len(jpeg0.coef_arrays)):
        coeffs0 = jpeg0.coef_arrays[component]
        coeffs1 = jpeg1.coef_arrays[component]
        full_mask = (coeffs0 != coeffs1)
        num_blocks_h, num_blocks_w = [x // BLOCK_SIZE for x in full_mask.shape]
        blocks = full_mask.reshape(num_blocks_h, BLOCK_SIZE, num_blocks_w, BLOCK_SIZE).transpose(0, 2, 1, 3)
        block_mask = np.any(blocks, axis=(2, 3))
        upscaled_block_mask = np.kron(block_mask, np.ones((BLOCK_SIZE, BLOCK_SIZE), dtype=bool))
        coeffs1 *= upscaled_block_mask
    jio.write(jpeg1, output_diff_path)
if __name__ == '__main__':
    create_diff_jpeg(sys.argv[1], sys.argv[2], sys.argv[3])

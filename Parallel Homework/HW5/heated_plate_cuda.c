// This program simulates the flow of heat through a two-dimensional plate.
// The number of grid cells used to model the plate as well as the number of
//  iterations to simulate can be specified on the command-line as follows:
//  ./heated_plate_sequential <columns> <rows> <iterations>
// For example, to execute with a 500 x 500 grid for 250 iterations, use:
//  ./heated_plate_sequential 500 500 250


#include <stdlib.h>
#include <stdio.h>
#include <time.h>


// Define the immutable boundary conditions and the inital cell value
#define TOP_BOUNDARY_VALUE 0.0
#define BOTTOM_BOUNDARY_VALUE 100.0
#define LEFT_BOUNDARY_VALUE 0.0
#define RIGHT_BOUNDARY_VALUE 100.0
#define INITIAL_CELL_VALUE 50.0


// Function prototypes
void print_cells(float **cells, int n_x, int n_y);
void initialize_cells(float **cells, int n_x, int n_y);
void create_snapshot(float **cells, int n_x, int n_y, int id);
float **allocate_cells(int n_x, int n_y);
void die(const char *error);


int main(int argc, char **argv) {
	// Record the start time of the program
	time_t start_time = time(NULL);
	
	// Extract the input parameters from the command line arguments
	// Number of columns in the grid (default = 1,000)
	int num_cols = (argc > 1) ? atoi(argv[1]) : 1000;
	// Number of rows in the grid (default = 1,000)
	int num_rows = (argc > 2) ? atoi(argv[2]) : 1000;
	// Number of iterations to simulate (default = 100)
	int iterations = (argc > 3) ? atoi(argv[3]) : 100;
	
	// Output the simulation parameters
	printf("Grid: %dx%d, Iterations: %d\n", num_cols, num_rows, iterations);
		
	// We allocate two arrays: one for the current time step and one for the next time step.
	// At the end of each iteration, we switch the arrays in order to avoid copying.
	// The arrays are allocated with an extra surrounding layer which contains
	//  the immutable boundary conditions (this simplifies the logic in the inner loop).
	float **cells[2];
	cells[0] = allocate_cells(num_cols + 2, num_rows + 2);
	cells[1] = allocate_cells(num_cols + 2, num_rows + 2);
	int cur_cells_index = 0, next_cells_index = 1;
	
	int N = ((num_rows + 2) * (num_cols + 2));
	
	// Initialize the interior (non-boundary) cells to their initial value.
	// Note that we only need to initialize the array for the current time
	//  step, since we will write to the array for the next time step
	//  during the first iteration.
	initialize_cells(cells[0], num_cols, num_rows);
	
	// Set the immutable boundary conditions in both copies of the array
	int x, y, i;
	for (x = 1; x <= num_cols; x++) cells[0][0][x] = cells[1][0][x] = TOP_BOUNDARY_VALUE;
	for (x = 1; x <= num_cols; x++) cells[0][num_rows + 1][x] = cells[1][num_rows + 1][x] = BOTTOM_BOUNDARY_VALUE;
	for (y = 1; y <= num_rows; y++) cells[0][y][0] = cells[1][y][0] = LEFT_BOUNDARY_VALUE;
	for (y = 1; y <= num_rows; y++) cells[0][y][num_cols + 1] = cells[1][y][num_cols + 1] = RIGHT_BOUNDARY_VALUE;
	
	//Start new GPU calls
	float **GPU_cells[2];
	int vector_size = N * 2 * sizeof(float);
	check_error(cudaMalloc((void **) &GPU_cells, vector_size));
	
	// Transfer the input vectors to GPU memory
	check_error(cudaMemcpy(GPU_cells, cells, vector_size, cudaMemcpyHostToDevice));
	
	// Determine the number of thread blocks in the x- and y-dimension
	// Note: we use a two-dimensional grid of thread blocks here because each dimension
	//  of the grid can only have up to 64K thread blocks; if we want to use more than
	//  64K thread blocks, we need to use a two-dimensional grid. This is slightly
	//  awkward, however, since the underlying problem is inherently one-dimensional
	int num_blocks = (N + threads_per_block - 1) / threads_per_block;
	int max_blocks_per_dimension = 65535;
	int num_blocks_y = (int) ((float) (num_blocks + max_blocks_per_dimension - 1) / (float) max_blocks_per_dimension);
	int num_blocks_x = (int) ((float) (num_blocks + num_blocks_y - 1) / (float) num_blocks_y);
	dim3 grid_size(num_blocks_x, num_blocks_y, 1);
	
	
	// Execute the kernel to compute the vector sum on the GPU
	long long kernel_start_time = start_timer();	
	for (int i = 0; i < iterations; i++)
	{	
		kernel <<< grid_size , threads_per_block >>> (GPU_cells, num_cols, num_rows, cur_cells_index);
		cur_cells_index = next_cells_index;
		next_cells_index = !cur_cells_index;
	}	
	cudaThreadSynchronize();  // This is only needed for timing and error-checking purposes	
	stop_timer(kernel_start_time, "\t Kernel execution");	
	// Check for kernel errors
	check_error(cudaGetLastError());
	
	
	// Transfer the result from the GPU to the CPU
	check_error(cudaMemcpy(cells, GPU_cells, vector_size, cudaMemcpyDeviceToHost));
	
	// Free the GPU memory
	check_error(cudaFree(GPU_cells));
	
	// Output a snapshot of the final state of the plate
	int final_cells = (iterations % 2 == 0) ? 0 : 1;
	create_snapshot(cells[final_cells], num_cols, num_rows, iterations);

	// Compute and output the execution time
	time_t end_time = time(NULL);
	printf("\nExecution time: %d seconds\n", (int) difftime(end_time, start_time));
	
	return 0;
}


//GPU Kernal
__global__ void kernal(float **cells[2], int num_cols, int num_rows, int index)
{
	int index_x = (blockDim.x * blockIdx.x) + threadIdx.x;
	int index_y = (blockDim.y * blockIdx.y) + threadIdx.y;
	int oldIndex = 0;
	if (index == 0) oldIndex = 1;
	// Make sure we do not go off the end of the array
	if (index_x <= num_cols && index_x >= 1) {
		if (index_y <= num_rows && index_y >= 1) {
		cells[index][index_y][index_x] = (cells[oldIndex][index_y][index_x + 1] +
											cells[oldIndex][index_y][index_x - 1] +
											cells[oldIndex][index_y + 1][index_x] +
											cells[oldIndex][index_y - 1][index_x]) * 0.25
	}
}

// Allocates and returns a pointer to a 2D array of floats
float **allocate_cells(int num_cols, int num_rows) {
	float **array = (float **) malloc(num_rows * sizeof(float *));
	if (array == NULL) die("Error allocating array!\n");
	
	array[0] = (float *) malloc(num_rows * num_cols * sizeof(float));
	if (array[0] == NULL) die("Error allocating array!\n");

	int i;
	for (i = 1; i < num_rows; i++) {
		array[i] = array[0] + (i * num_cols);
	}

	return array;
}


// Sets all of the specified cells to their initial value.
// Assumes the existence of a one-cell thick boundary layer.
void initialize_cells(float **cells, int num_cols, int num_rows) {
	int x, y;
	for (y = 1; y <= num_rows; y++) {
		for (x = 1; x <= num_cols; x++) {
			cells[y][x] = INITIAL_CELL_VALUE;
		}
	}
}


// Creates a snapshot of the current state of the cells in PPM format.
// The plate is scaled down so the image is at most 1,000 x 1,000 pixels.
// This function assumes the existence of a boundary layer, which is not
//  included in the snapshot (i.e., it assumes that valid array indices
//  are [1..num_rows][1..num_cols]).
void create_snapshot(float **cells, int num_cols, int num_rows, int id) {
	int scale_x, scale_y;
	scale_x = scale_y = 1;
	
	// Figure out if we need to scale down the snapshot (to 1,000 x 1,000)
	//  and, if so, how much to scale down
	if (num_cols > 1000) {
		if ((num_cols % 1000) == 0) scale_x = num_cols / 1000;
		else {
			die("Cannot create snapshot for x-dimensions >1,000 that are not multiples of 1,000!\n");
			return;
		}
	}
	if (num_rows > 1000) {
		if ((num_rows % 1000) == 0) scale_y = num_rows / 1000;
		else {
			printf("Cannot create snapshot for y-dimensions >1,000 that are not multiples of 1,000!\n");
			return;
		}
	}
	
	// Open/create the file
	char text[255];
	sprintf(text, "snapshot.%d.ppm", id);
	FILE *out = fopen(text, "w");
	// Make sure the file was created
	if (out == NULL) {
		printf("Error creating snapshot file!\n");
		return;
	}
	
	// Write header information to file
	// P3 = RGB values in decimal (P6 = RGB values in binary)
	fprintf(out, "P3 %d %d 100\n", num_rows / scale_y, num_cols / scale_x);
	
	// Precompute the value needed to scale down the cells
	float inverse_cells_per_pixel = 1.0 / ((float) scale_x * scale_y);
	
	// Write the values of the cells to the file
	int x, y, i, j;
	for (y = 1; y <= num_rows; y += scale_y) {
		for (x = 1; x <= num_cols; x += scale_x) {
			float sum = 0.0;
			for (j = y; j < y + scale_y; j++) {
				for (i = x; i < x + scale_x; i++) {
					sum += cells[j][i];
				}
			}
			// Write out the average value of the cells we just visited
			int average = (int) (sum * inverse_cells_per_pixel);
			fprintf(out, "%d 0 %d\t", average, 100 - average);
		}
		fwrite("\n", sizeof(char), 1, out);
	}
	
	// Close the file
	fclose(out);
}


// Prints the specified error message and then exits
void die(const char *error) {
	printf("%s", error);
	exit(1);
}

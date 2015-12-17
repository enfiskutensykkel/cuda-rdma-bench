#ifndef __BENCH_H__
#define __BENCH_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <sisci_api.h>
#include "translist.h"


/* Benchmark different functions */
#define BENCH_IS_DMA(mode) (!!((mode) & 0x10))      // Is the benchmark type DMA?
typedef enum {
    BENCH_DO_NOTHING                    = 0x00,     // Dummy benchmark type
    BENCH_DMA_PUSH_TO_REMOTE            = 0x10,     // Use DMA to push data to remote host
    BENCH_DMA_GLOBAL_PUSH_TO_REMOTE     = 0x11,     // Use global DMA to push data to remote host
    BENCH_DMA_PULL_FROM_REMOTE          = 0x12,     // Use DMA to pull data from remote host
    BENCH_DMA_GLOBAL_PULL_FROM_REMOTE   = 0x13,     // Use global DMA to pull data from remote host
    BENCH_SCIMEMWRITE_TO_REMOTE         = 0x20,     // Use SCIMemWrite to write data to remote host (PIO)
    BENCH_SCIMEMCPY_TO_REMOTE           = 0x30,     // Use SCIMemCpy to write data to remote host (PIO)
    BENCH_SCIMEMCPY_FROM_REMOTE         = 0x31,     // Use SCIMemCpy to read data from remote host (PIO)
    BENCH_WRITE_TO_REMOTE               = 0x40,     // Use regular memcpy to write data to remote host (PIO)
    BENCH_READ_FROM_REMOTE              = 0x41,     // Use regular memcpy to read data from remote host (PIO)
    BENCH_DATA_INTERRUPT                = 0xff      // Use data interrupts to send data to a remote host
} bench_mode_t;


/* Benchmark configuration */
typedef struct {
    bench_mode_t    benchmark_mode;         // Type of benchmark
    size_t          num_runs;               // Number of times to repeat the benchmark
    translist_t     transfer_list;          // Transfer list that describes what to transfer
} bench_t;


/* Benchmark result */
typedef struct {
    size_t          success_count;          // Number of times transfer was a success
    int             buffer_matches;         // If the remote and local buffer matched after benchmark
    size_t          total_size;             // Total number of bytes transfered
    uint64_t        total_runtime;          // Total runtime
    uint64_t        runtimes[0];            // Individual runtimes
} result_t;


/* Helper function
 *
 * Returns the current timestamp in microseconds (µs) 
 */
uint64_t ts_usecs();


/* Helper function
 *
 * Returns a pseudo-random byte value 
 */
uint8_t random_byte_value();


/* Run benchmarking server 
 * 
 * This will block until stop_server() is invoked asynchronously (i.e. from an signal handler)
 */
void server(unsigned adapter_no, int gpu_id, unsigned segment_id, size_t segment_size);


/* Stop the benchmarking server */
void stop_server();


/* Run benchmark 
 *
 * Returns 0 on success, and non-zero on error
 */
int client(unsigned adapter_no, const bench_t* benchmark, result_t* result);

#ifdef __cplusplus
}
#endif
#endif

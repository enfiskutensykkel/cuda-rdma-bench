#include <stdint.h>
#include <stdlib.h>
#include <sisci_api.h>
#include "translist.h"
#include "common.h"
#include "util.h"
#include "reporting.h"
#include "bench.h"
#include "gpu.h"
#include "ram.h"


static int verify_transfer(translist_desc_t* desc)
{
    sci_error_t err;
    sci_map_t remote_buf_map;

    volatile void* remote_ptr;
    remote_ptr = SCIMapRemoteSegment(desc->segment_remote, &remote_buf_map, 0, desc->segment_size, NULL, 0, &err);
    if (err != SCI_ERR_OK)
    {
        log_error("Failed to map remote segment: %s", SCIGetErrorString(err));
        return -1;
    }

    log_info("Comparing local and remote memory...");
    size_t bytes;
    if (desc->gpu_device_id != NO_GPU)
    {
        bytes = gpu_memcmp(desc->gpu_device_id, desc->buffer_ptr, remote_ptr, desc->segment_size);
    }
    else
    {
        bytes = ram_memcmp(desc->buffer_ptr, remote_ptr, desc->segment_size);
    }

    do
    {
        SCIUnmapSegment(remote_buf_map, 0, &err);
    }
    while (err == SCI_ERR_BUSY);
    
    if (err != SCI_ERR_OK)
    {
        log_error("Unexpected error: %s", SCIGetErrorString(err));
    }

    return bytes == desc->segment_size;
}


double dma(unsigned adapter, translist_t tl, translist_desc_t* tsd, unsigned flags, size_t repeat, double* runs)
{
    sci_error_t err;
    sci_dma_queue_t q;
    size_t veclen = translist_size(tl);

    // Create DMA queue
    SCICreateDMAQueue(tsd->sisci_desc, &q, adapter, 1, 0, &err);
    if (err != SCI_ERR_OK)
    {
        log_error("Failed to create DMA queue");
        return 0;
    }

    // Create DMA transfer vector
    dis_dma_vec_t vec[veclen];
    size_t total_size = 0;
    for (size_t i = 0; i < veclen; ++i)
    {
        translist_entry_t entry;
        translist_element(tl, i, &entry);
        
        vec[i].size = entry.size;
        vec[i].local_offset = entry.offset_local;
        vec[i].remote_offset = entry.offset_remote;
        vec[i].flags = 0;

        total_size += entry.size;
    }

    // Do DMA transfer
    log_debug("Performing DMA transfer of %lu-sized vector  %d times", veclen, repeat);
    uint64_t start = ts_usecs();
    for (size_t i = 0; i < repeat; ++i)
    {
        uint64_t before = ts_usecs();
        SCIStartDmaTransferVec(q, tsd->segment_local, tsd->segment_remote, veclen, vec, NULL, NULL, SCI_FLAG_DMA_WAIT | flags, &err);
        uint64_t after = ts_usecs();

        runs[i] = (double) total_size / (double) (after - before);

        if (err != SCI_ERR_OK)
        {
            log_error("DMA transfer failed %s", SCIGetErrorString(err));
            runs[i] = 0.0;
        }
    }
    uint64_t end = ts_usecs();

    SCIRemoveDMAQueue(q, 0, &err);
    if (err != SCI_ERR_OK)
    {
        log_error("Failed to remove DMA queue");
    }

    // Try to prevent overflowing
    double megabytes_per_sec = (double) total_size;
    megabytes_per_sec *= repeat;
    megabytes_per_sec /= end - start;

    return megabytes_per_sec;
}


double client(unsigned adapter, bench_mode_t mode, translist_t tl, size_t repeat, double* runs)
{
    translist_desc_t tl_desc = translist_desc(tl);

    // Fill local buffer with random byte
    uint8_t byte = random_byte_value();

    log_debug("Creating buffer and filling with random value %02x", byte);
    if (tl_desc.gpu_device_id != NO_GPU)
    {
        gpu_memset(tl_desc.gpu_device_id, tl_desc.buffer_ptr, tl_desc.segment_size, byte);
    }
    else
    {
        ram_memset(tl_desc.buffer_ptr, tl_desc.segment_size, byte);
    }

    // Initialize benchmark variables
    double total = 0.0;
    for (size_t i = 0; i < repeat; ++i)
    {
        runs[i] = 0.0;
    }

    // Do benchmark
    log_info("Executing benchmark...");
    unsigned sci_flags = 0;
    switch (mode)
    {
        case BENCH_SCI_DMA_GLOBAL_PUSH_TO_REMOTE:
            sci_flags |= SCI_FLAG_DMA_GLOBAL;
        case BENCH_SCI_DMA_PUSH_TO_REMOTE:
            total = dma(adapter, tl, &tl_desc, sci_flags, repeat, runs);
            break;

        case BENCH_SCI_DMA_GLOBAL_PULL_FROM_REMOTE:
            sci_flags |= SCI_FLAG_DMA_GLOBAL;
        case BENCH_SCI_DMA_PULL_FROM_REMOTE:
            sci_flags |= SCI_FLAG_DMA_READ;
            total = dma(adapter, tl, &tl_desc, sci_flags, repeat, runs);
            break;

        default:
            log_error("%s is not yet supported", bench_mode_name(mode));
            break;

        case BENCH_DO_NOTHING:
            log_error("No benchmarking operation is set");
            break;
    }
    log_info("Benchmark complete, verifying transfer.");

    // Verify transfer
    sci_error_t err;
    
    SCITriggerInterrupt(tl_desc.validate, 0, &err);
    if (err != SCI_ERR_OK)
    {
        log_error("Failed to trigger remote interrupt");
    }

    uint8_t value;
    if (tl_desc.gpu_device_id != NO_GPU)
    {
        gpu_memcpy_buffer_to_local(tl_desc.gpu_device_id, tl_desc.buffer_ptr, &value, 1);
    }
    else
    {
        value = *((uint8_t*) tl_desc.buffer_ptr);
    }

    fprintf(stderr, 
            "******* BUFFER *******\n"
            " Before transfer:  %02x\n"
            "  After transfer:  %02x\n"
            "**********************\n", 
            byte, value);

    if (verify_transfer(&tl_desc) != 1)
    {
        log_warn("Local and remote buffers differ");
    }
    else
    {
        log_debug("Local and remote buffers are equal");
    }

    return total;
}

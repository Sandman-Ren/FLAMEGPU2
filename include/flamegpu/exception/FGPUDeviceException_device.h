#ifndef INCLUDE_FLAMEGPU_EXCEPTION_FGPUDEVICEEXCEPTION_DEVICE_H_
#define INCLUDE_FLAMEGPU_EXCEPTION_FGPUDEVICEEXCEPTION_DEVICE_H_

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <cstring>

#ifndef NO_SEATBELTS
/**
 * This allows us to write DTHROW("My Error message: %d", 12); or similar to report an error in device code
 */
#define DTHROW DeviceException::create(__FILE__, __LINE__).setMessage

/**
 * This struct should exist in device memory per stream
 * It holds buffers for outputting a deconstructed format string
 */
struct DeviceExceptionBuffer {
    static const unsigned int MAX_ARGS = 20;
    static const unsigned int ARG_BUFF_LEN = 4096;
    static const unsigned int FORMAT_BUFF_LEN = 4096;
    static const unsigned int FILE_BUFF_LEN = 1024;
    static const unsigned int OUT_STRING_LEN = FORMAT_BUFF_LEN * 2;
    /**
     * The number of threads reporting error
     */
    unsigned int error_count;
    char file_path[FILE_BUFF_LEN];
    unsigned int line_no;
    unsigned int block_id[3];
    unsigned int thread_id[3];
    /**
     * The format string passed by the user to the printer
     */
    char format_string[FORMAT_BUFF_LEN];
    /**
     * The type size of each of the args passed to the printer
     */
    unsigned int format_args_sizes[MAX_ARGS];
    /**
     * A compact buffer of each of the args passed to the printer
     * Their size corresponds to the matching array above
     */
    char format_args[ARG_BUFF_LEN];
    /**
     * The total number of args passed to the printer
     */
    unsigned int arg_count;
    /**
     * The total space used by the args
     */
    unsigned int arg_offset;
};
/**
 * This class is used on device for reporting errors
 * It should be used indirectly via the DTHROW macro
 * e.g. DTHROW("MyError %d", 12);
 */
class DeviceException {
 public:
    /**
     * Create and return a new device exception
     */
    __device__ static DeviceException create(const char *file, const unsigned int line) {
        return {file, line};
    }
    /**
     * Construct the formatted message for the exception
     * This should be used like printf
     * It does not support '%s', all other format args should work
     */
    template<typename... Args>
    __device__ void setMessage(const char *format, Args... args) {
        extern __shared__ DeviceExceptionBuffer* buff[];
        // Only the thread which first reported error gets to output
        if (hasError) {
            // Only output once
            if (buff[0]->format_string[0])
                return;
            // Copy the format string
            unsigned int eos = 0;
            for (eos = 0; eos < DeviceExceptionBuffer::FORMAT_BUFF_LEN; ++eos)
                if (format[eos] == '\0')
                    break;
            memcpy(buff[0]->format_string, format, eos * sizeof(char));
            // Process args
            subformat_recurse(buff[0], args...);
        }
    }

 private:
    /**
     * Called by setMessage
     * This is required to process var args, it performs the processing of a single arg
     */
    template<typename T>
    __device__ void subformat(DeviceExceptionBuffer *buff, T t);
    /**
     * Called by setMessage
     * This is required to process var args, it performs recursion to scrape of each arg
     */
    template<typename T, typename... Args>
    __device__ void subformat_recurse(DeviceExceptionBuffer *buff, const T &t, Args... args) {
        // Call subformat with T
        subformat(buff, t);
        // Recurse with the rest of the list
        subformat_recurse(buff, args...);
    }
    __device__ void subformat_recurse(DeviceExceptionBuffer *buff) { }
    /**
     * strlen() implementation for use on device
     * The regular strlen function is not available on device
     */
    __device__ unsigned int strlen(const char *c) {
        unsigned int eos = 0;
        for (eos = 0; eos < DeviceExceptionBuffer::FORMAT_BUFF_LEN; ++eos)
            if (*(c + eos) == '\0' || eos >= DeviceExceptionBuffer::FORMAT_BUFF_LEN)
                break;
        return eos + 1;  // Include the terminating character
    }
    /**
     * Construct the DeviceException
     * Start by atomically incrementing the error count
     * If the previous value was 0, we own the error so report the error location
     */
    __device__ DeviceException(const char *file, const unsigned int line)
        : hasError(!getErrorCount()) {
        extern __shared__ DeviceExceptionBuffer* buff[];
        if (hasError) {
            // Copy file location
            const size_t file_len = strlen(file);
            memcpy(buff[0]->file_path, file, file_len);
            // Copy line no
            buff[0]->line_no = line;
            // Copy block/thread indices
            const uint3 bid3 = blockIdx;
            memcpy(buff[0]->block_id, &bid3, sizeof(unsigned int) * 3);
            const uint3 tid3 = threadIdx;
            memcpy(buff[0]->thread_id, &tid3, sizeof(unsigned int) * 3);
        }
    }
    /**
     * Atomically increments the error flag and returns the return value (previous value)
     */
    __device__ unsigned int getErrorCount();
    /**
     * True if we won the race to report the error
     */
    const bool hasError;
};
template<typename T>
__device__ void DeviceException::subformat(DeviceExceptionBuffer *buff, T t) {
    if (buff->arg_count < DeviceExceptionBuffer::MAX_ARGS) {
        if (buff->arg_offset + sizeof(T) <= DeviceExceptionBuffer::ARG_BUFF_LEN) {
            // Copy arg size
            buff->format_args_sizes[buff->arg_count] = sizeof(T);
            // Copy arg value
            memcpy(buff->format_args + buff->arg_offset, &t, sizeof(T));
            // Update offsets
            ++buff->arg_count;
            buff->arg_offset += sizeof(T);
        }
    }
}
#if defined(__CUDACC_RTC__) || defined(FGPUDEVICEEXCEPTION_CU)
template<>
__device__ void DeviceException::subformat(DeviceExceptionBuffer *buff, const char *t) {
    if (buff->arg_count < DeviceExceptionBuffer::MAX_ARGS) {
        const unsigned int string_length = strlen(t);
        if (buff->arg_offset + string_length <= DeviceExceptionBuffer::ARG_BUFF_LEN) {
            // Copy arg size
            buff->format_args_sizes[buff->arg_count] = string_length;
            // Copy arg value
            memcpy(buff->format_args + buff->arg_offset, t, string_length);
            // Update offsets
            ++buff->arg_count;
            buff->arg_offset += string_length;
        }
    }
}
__device__ unsigned int DeviceException::getErrorCount() {
    extern __shared__ DeviceExceptionBuffer* buff[];
    // Are we the first exception
    return atomicInc(&buff[0]->error_count, UINT_MAX);
}
#endif
#else
/**
 * Ignore the device error macro when NO_SEATBELTS is enabled
 * These checks are costly to performance
 */
#define DTHROW(nop)
#endif  // NO_SEATBELTS
#endif  // INCLUDE_FLAMEGPU_EXCEPTION_FGPUDEVICEEXCEPTION_DEVICE_H_

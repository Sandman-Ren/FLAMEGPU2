#ifndef INCLUDE_FLAMEGPU_RUNTIME_DEVICEAPI_H_
#define INCLUDE_FLAMEGPU_RUNTIME_DEVICEAPI_H_


#include <cassert>
#include <cstdint>
#include <limits>

// #include "flamegpu/gpu/CUDAErrorChecking.h"            // required for CUDA error handling functions
#ifndef __CUDACC_RTC__
#include "flamegpu/runtime/cuRVE/curve.h"
#else
#include "dynamic/curve_rtc_dynamic.h"
#endif  // !_RTC
// #include "flamegpu/exception/FGPUException.h"
#include "flamegpu/runtime/utility/AgentRandom.cuh"
#include "flamegpu/runtime/utility/DeviceEnvironment.cuh"
#include "flamegpu/gpu/CUDAScanCompaction.h"
#include "flamegpu/runtime/AgentFunction.h"
#include "flamegpu/runtime/AgentFunctionCondition.h"
#include "flamegpu/runtime/messaging_device.h"
#include "flamegpu/defines.h"

/**
 * @brief  FLAMEGPU_API is a singleton class for the device runtime
 *
 * \todo longer description
 */
class ReadOnlyDeviceAPI {
    // Friends have access to TID() & TS_ID()
    template<typename AgentFunctionCondition>
    friend __global__ void agent_function_condition_wrapper(
#if !defined(SEATBELTS) || SEATBELTS
        DeviceExceptionBuffer *error_buffer,
#endif
        Curve::NamespaceHash,
        Curve::NamespaceHash,
        const unsigned int,
        curandState *,
        unsigned int *);

 public:
    /**
     * @param instance_id_hash CURVE hash of the CUDASimulation's instance id
     * @param modelname_hash CURVE hash of the model's name
     */
    __device__ ReadOnlyDeviceAPI(
        const Curve::NamespaceHash &instance_id_hash,
        const Curve::NamespaceHash &agentfuncname_hash,
        curandState *&d_rng)
        : random(AgentRandom(&d_rng[getThreadIndex()]))
        , environment(DeviceEnvironment(instance_id_hash))
        , agent_func_name_hash(agentfuncname_hash) { }

    template<typename T, unsigned int N> __device__
    T getVariable(const char(&variable_name)[N]);
    template<typename T, unsigned int N, unsigned int M> __device__
    T getVariable(const char(&variable_name)[M], const unsigned int &index);
    /**
     * Returns the agent's unique identifier
     */
    __device__ id_t getID() {
        return getVariable<id_t>("_id");
    }

    /**
     * Provides access to random functionality inside agent functions
     * @note random state isn't stored within the object, so it can be const
     */
    const AgentRandom random;
    /**
     * Provides access to environment variables inside agent functions
     */
    const DeviceEnvironment environment;

    /**
     * Access the current stepCount
     * @return the current step count, 0 indexed unsigned.
     */
    __forceinline__ __device__ unsigned int getStepCounter() const {
        return environment.getProperty<unsigned int>("_stepCount");
    }

    /**
     * Returns the current CUDA thread of the agent
     * All agents execute in a unique thread, but their associated thread may change between agent functions
     * Thread indices begin at 0 and continue to 1 below the number of agents executing
     */
    __forceinline__ __device__ static unsigned int getThreadIndex() {
        /*
        // 3D version
        auto blockId = blockIdx.x + blockIdx.y * gridDim.x
        + gridDim.x * gridDim.y * blockIdx.z;
        auto threadId = blockId * (blockDim.x * blockDim.y * blockDim.z)
        + (threadIdx.z * (blockDim.x * blockDim.y))
        + (threadIdx.y * blockDim.x)
        + threadIdx.x;
        return threadId;*/
#ifdef SEATBELTS
        assert(blockDim.y == 1);
        assert(blockDim.z == 1);
        assert(gridDim.y == 1);
        assert(gridDim.z == 1);
#endif
        return blockIdx.x * blockDim.x + threadIdx.x;
    }

 protected:
    Curve::NamespaceHash agent_func_name_hash;
};

/** @brief    A flame gpu api class for the device runtime only
 *
 * This class provides access to model variables/state inside agent functions
 *
 * This class should only be used by the device and never created on the host. It is safe for each agent function to create a copy of this class on the device. Any singleton type
 * behaviour is handled by the curveInstance class. This will ensure that initialisation of the curve (C) library is done only once.
 * @tparam MsgIn Input message type (the form found in flamegpu/runtime/messaging.h, MsgNone etc)
 * @tparam MsgOut Output message type (the form found in flamegpu/runtime/messaging.h, MsgNone etc)
 */
template<typename MsgIn, typename MsgOut>
class DeviceAPI : public ReadOnlyDeviceAPI{
    // Friends have access to TID() & TS_ID()
    template<typename AgentFunction, typename _MsgIn, typename _MsgOut>
    friend __global__ void agent_function_wrapper(
#if !defined(SEATBELTS) || SEATBELTS
        DeviceExceptionBuffer *error_buffer,
#endif
        Curve::NamespaceHash,
        Curve::NamespaceHash,
        Curve::NamespaceHash,
        Curve::NamespaceHash,
        Curve::NamespaceHash,
        id_t*,
        const unsigned int,
        const void *,
        const void *,
        curandState *,
        unsigned int *,
        unsigned int *,
        unsigned int *);

 public:
    class AgentOut {
     public:
        __device__ AgentOut(const Curve::NamespaceHash &aoh, id_t *&d_agent_output_nextID, unsigned int *&scan_flag_agentOutput)
            : agent_output_hash(aoh)
            , scan_flag(scan_flag_agentOutput)
            , nextID(d_agent_output_nextID) { }
        /**
         * Sets a variable in a new agent to be output after the agent function has completed
         * @param variable_name The name of the variable
         * @param value The value to set the variable
         * @tparam T The type of the variable, as set within the model description hierarchy
         * @tparam N Variable name length, this should be ignored as it is implicitly set
         * @note Any agent variables not set will remain as their default values
         * @note Calling AgentOut::setVariable() or AgentOut::getID() will trigger agent output
         */
        template<typename T, unsigned int N>
        __device__ void setVariable(const char(&variable_name)[N], T value) const;
        /**
         * Sets an element of an array variable in a new agent to be output after the agent function has completed
         * @param variable_name The name of the array variable
         * @param index The index to set within the array variable
         * @param value The value to set the element of the array velement
         * @tparam T The type of the variable, as set within the model description hierarchy
         * @tparam N The length of the array variable, as set within the model description hierarchy
         * @tparam M Variable name length, this should be ignored as it is implicitly set
         * @note Any agent variables not set will remain as their default values
         * @note Calling AgentOut::setVariable() or AgentOut::getID() will trigger agent output
         */
        template<typename T, unsigned int N, unsigned int M>
        __device__ void setVariable(const char(&variable_name)[M], const unsigned int &index, T value) const;
        /**
         * Return the ID of the agent to be created
         * @note Calling AgentOut::setVariable() or AgentOut::getID() will trigger agent output
         */
        __device__ id_t getID() const;

     private:
        /**
         * Sets scan flag and id
         */
        __device__ void genID() const;
        /**
         * Curve hash used for accessing new agent variables
         */
        const Curve::NamespaceHash agent_output_hash;
        /**
         * Scan flag, defaults to 0, set to 1, to mark than agent is output
         */
        unsigned int* const scan_flag;
        /**
         * Agent id if set
         * @note mutable, because this object is always const
         */
        mutable id_t id = ID_NOT_SET;
        /**
         * Ptr to global address storing a counter to track the next available agent ID for the agent type being output
         */
        id_t *nextID;
    };
    /**
     * Constructs the device-only API class instance.
     * @param instance_id_hash CURVE hash of the CUDASimulation's instance id
     * @param agentfuncname_hash Combined CURVE hashes of agent name and func name
     * @param _agent_output_hash Combined CURVE hashes for agent output
     * @param d_rng Device pointer to curand state for this kernel, index 0 should for TID()==0
     * @param scanFlag_agentOutput Array for agent output scan flag
     * @param msg_in Input message handler
     * @param msg_out Output message handler
     */
    __device__ DeviceAPI(
        const Curve::NamespaceHash &instance_id_hash,
        const Curve::NamespaceHash &agentfuncname_hash,
        const Curve::NamespaceHash &_agent_output_hash,
        id_t *&d_agent_output_nextID,
        curandState *&d_rng,
        unsigned int *&scanFlag_agentOutput,
        typename MsgIn::In &&msg_in,
        typename MsgOut::Out &&msg_out)
        : ReadOnlyDeviceAPI(instance_id_hash, agentfuncname_hash, d_rng)
        , message_in(msg_in)
        , message_out(msg_out)
        , agent_out(AgentOut(_agent_output_hash, d_agent_output_nextID, scanFlag_agentOutput))
    { }
    /**
     * Sets a variable within the currently executing agent
     * @param variable_name The name of the variable
     * @param value The value to set the variable
     * @tparam T The type of the variable, as set within the model description hierarchy
     * @tparam N Variable name length, this should be ignored as it is implicitly set
     */
    template<typename T, unsigned int N>
    __device__ void setVariable(const char(&variable_name)[N], T value);
    /**
     * Sets an element of an array variable within the currently executing agent
     * @param variable_name The name of the array variable
     * @param index The index to set within the array variable
     * @param value The value to set the element of the array velement
     * @tparam T The type of the variable, as set within the model description hierarchy
     * @tparam N The length of the array variable, as set within the model description hierarchy
     * @tparam M Variable name length, this should be ignored as it is implicitly set
     */
    template<typename T, unsigned int N, unsigned int M>
    __device__ void setVariable(const char(&variable_name)[M], const unsigned int &index, const T &value);

    /**
     * Provides access to message read functionality inside agent functions
     */
    const typename MsgIn::In message_in;
    /**
     * Provides access to message write functionality inside agent functions
     */
    const typename MsgOut::Out message_out;
    /**
     * Provides access to agent output functionality inside agent functions
     */
    const AgentOut agent_out;
};


/******************************************************************************************************* Implementation ********************************************************/

// An example of how the getVariable should work
// 1) Given the string name argument use runtime hashing to get a variable unsigned int (call function in RuntimeHashing.h)
// 2) Call (a new local) getHashIndex function to check the actual index in the hash table for the variable name. Once found we have a pointer to the vector of data for that agent variable
// 3) Using the CUDA thread and block index (threadIdx.x) return the specific agent variable value from the vector
// Useful existing code to look at is CUDAAgentStateList setAgentData function
// Note that this is using the hashing to get a specific pointer for a given variable name. This is exactly what we want to do in the FLAME GPU API class

/**
 * \brief Gets an agent memory value
 * \param variable_name Name of memory variable to retrieve
 */
template<typename T, unsigned int N>
__device__ T ReadOnlyDeviceAPI::getVariable(const char(&variable_name)[N]) {
    // simple indexing assumes index is the thread number (this may change later)
    const unsigned int index = (blockDim.x * blockIdx.x) + threadIdx.x;

    // get the value from curve
    T value = Curve::getAgentVariable<T>(variable_name, agent_func_name_hash , index);

    // return the variable from curve
    return value;
}

/**
 * \brief Sets an agent memory value
 * \param variable_name Name of memory variable to set
 * \param value Value to set it to
 */
template<typename MsgIn, typename MsgOut>
template<typename T, unsigned int N>
__device__ void DeviceAPI<MsgIn, MsgOut>::setVariable(const char(&variable_name)[N], T value) {
    if (variable_name[0] == '_') {
        return;  // Fail silently
    }
    // simple indexing assumes index is the thread number (this may change later)
    const unsigned int index = (blockDim.x * blockIdx.x) + threadIdx.x;
    // set the variable using curve
    Curve::setAgentVariable<T>(variable_name, agent_func_name_hash,  value, index);
}
/**
 * \brief Gets an agent memory value
 * \param variable_name Name of memory variable to retrieve
 */
template<typename T, unsigned int N, unsigned int M>
__device__ T ReadOnlyDeviceAPI::getVariable(const char(&variable_name)[M], const unsigned int &array_index) {
    // simple indexing assumes index is the thread number (this may change later)
    const unsigned int index = (blockDim.x * blockIdx.x) + threadIdx.x;

    // get the value from curve
    T value = Curve::getAgentArrayVariable<T, N>(variable_name, agent_func_name_hash , index, array_index);

    // return the variable from curve
    return value;
}

/**
 * \brief Sets an agent memory value
 * \param variable_name Name of memory variable to set
 * \param value Value to set it to
 */
template<typename MsgIn, typename MsgOut>
template<typename T, unsigned int N, unsigned int M>
__device__ void DeviceAPI<MsgIn, MsgOut>::setVariable(const char(&variable_name)[M], const unsigned int &array_index, const T &value) {
    if (variable_name[0] == '_') {
        return;  // Fail silently
    }
    // simple indexing assumes index is the thread number (this may change later)
    const unsigned int index = (blockDim.x * blockIdx.x) + threadIdx.x;

    // set the variable using curve
    Curve::setAgentArrayVariable<T, N>(variable_name , agent_func_name_hash,  value, index, array_index);
}

template<typename MsgIn, typename MsgOut>
template<typename T, unsigned int N>
__device__ void DeviceAPI<MsgIn, MsgOut>::AgentOut::setVariable(const char(&variable_name)[N], T value) const {
    if (agent_output_hash) {
        if (variable_name[0] == '_') {
            return;  // Fail silently
        }
        if (agent_output_hash) {
            // simple indexing assumes index is the thread number (this may change later)
            const unsigned int index = (blockDim.x * blockIdx.x) + threadIdx.x;

            // set the variable using curve
            Curve::setNewAgentVariable<T>(variable_name, agent_output_hash, value, index);

            // Mark scan flag
            genID();
        }
#if !defined(SEATBELTS) || SEATBELTS
    } else {
        DTHROW("Agent output must be enabled per agent function when defining the model.\n");
#endif
    }
}
template<typename MsgIn, typename MsgOut>
template<typename T, unsigned int N, unsigned int M>
__device__ void DeviceAPI<MsgIn, MsgOut>::AgentOut::setVariable(const char(&variable_name)[M], const unsigned int &array_index, T value) const {
    if (agent_output_hash) {
        if (variable_name[0] == '_') {
            return;  // Fail silently
        }
        // simple indexing assumes index is the thread number (this may change later)
        const unsigned int index = (blockDim.x * blockIdx.x) + threadIdx.x;

        // set the variable using curve
        Curve::setNewAgentArrayVariable<T, N>(variable_name, agent_output_hash, value, index, array_index);

        // Mark scan flag
        genID();
#if !defined(SEATBELTS) || SEATBELTS
    } else {
        DTHROW("Agent output must be enabled per agent function when defining the model.\n");
#endif
    }
}

template<typename MsgIn, typename MsgOut>
__device__ id_t DeviceAPI<MsgIn, MsgOut>::AgentOut::getID() const {
    if (agent_output_hash) {
        genID();
        return this->id;
    }
#if !defined(SEATBELTS) || SEATBELTS
    DTHROW("Agent output must be enabled per agent function when defining the model.\n");
#endif
    return ID_NOT_SET;
}
#ifdef __CUDACC__
template<typename MsgIn, typename MsgOut>
__device__ void DeviceAPI<MsgIn, MsgOut>::AgentOut::genID() const {
    // Only assign id and scan flag once
    if (this->id == ID_NOT_SET) {
        this->id = atomicInc(this->nextID, std::numeric_limits<id_t>().max());
        const unsigned int index = (blockDim.x * blockIdx.x) + threadIdx.x;
        Curve::setNewAgentVariable<id_t>("_id", agent_output_hash, this->id, index);  // Can't use ID_VARIABLE_NAME inline, as it isn't of char[N] type
        this->scan_flag[index] = 1;
    }
}
#endif

#endif  // INCLUDE_FLAMEGPU_RUNTIME_DEVICEAPI_H_
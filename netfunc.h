/*
	NetFunc, a simple network transparent function call system.
	Author : Tyler Hundt
	Written : 4-22-17
*/

#ifndef NETWORKTRANSPARENTFUNCTIONCALL_H_
#define NETWORKTRANSPARENTFUNCTIONCALL_H_
#include "json/json.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <cstdint>
#include <thread>

namespace netfunc
{
	typedef void (*NetFuncType)(nlohmann::json const &args, nlohmann::json &result);
	typedef bool (*StringSerializationType)(std::string const &input, std::unique_ptr<char[]> &outBuffer, uint16_t &outSizeBytes);
	typedef bool (*StringDeserializationType)(std::unique_ptr<char[]> const &inBuffer, uint16_t inSizeBytes, std::string &output);

	enum class ErrorResult
	{
		Call_Ok,          // No Errors
		Func_Overwrite,   // A function with that name was taken and has been overwritten
		Listener_Started, // The function was not added because the listener has been started already
		Net_Error,        // There was an error with the underlying network
		Request_Timeout,  // The timeout has been reached
		Invalid_Address,  // No listener at that address
		Bad_String,       // The string serialization function returned bad
		Bad_Json,         // There was an exception when parsing the json
		Return_Error,     // Remote function executed but parsing the return failed
		No_Default,       // The default connection is not supported with the current configuration
	};

	class ConnectionBase
	{
	public:
		virtual ~ConnectionBase(){}

		// Sets up the port and gets it ready to either connect or listen.
		// port : the port to try to setup
		// return : true if setup was successful, false if not
		virtual bool Setup(uint16_t port) = 0;

		// Destroys the open connection.
		virtual void Stop(void) = 0;

		// Try to open connection to remote listener. This should block until the connection returns good or not.
		// address, port : location to try to connect to
		// return : true if successfully connected, false if not
		virtual bool Connect(std::string const &address, uint16_t port) = 0;

		// Set the connection to start listening.
		// acceptQueueSize : requested size of the accept queue
		// return : true if successfully listening, false if not
		virtual bool Listen(uint16_t acceptQueueSize) = 0;

		// Try to accept a new connection from a listening port. This function should not block.
		// return : true if listening port is still good
		// newConnection : return the new connection, or nullptr if there was no new connection
		virtual bool Accept(std::unique_ptr<ConnectionBase> &newConnection) = 0;

		// Sends the data, send all the data. The send is responsible for sending a way for receive to know when
		//    all of the data has been read.
		// inBuffer : buffer that has all the data to send
		// sizeBytes : size in bytes of the buffer
		// return : true if successfully sent, false if not
		virtual bool Send(std::unique_ptr<char[]> const &inBuffer, uint16_t sizeBytes) = 0;

		// Try to receive SizeBytes of data. This should be non-blocking until data starts coming in, then it
		//    should block until all the data is read.
		// return : true if the connection is still in a good state, false if not
		// outBuffer : the buffer with the read data in it, or nullptr if there was no data ready to read
		// outSizeBytes : size of the buffer returned
		virtual bool Recv(std::unique_ptr<char[]> &outBuffer, uint16_t &outSizeBytes) = 0;
	};

	class Listener
	{
		std::unique_ptr<ConnectionBase> listeningConnection = nullptr;
		StringSerializationType serializeFunction = nullptr;
		StringDeserializationType deserializeFunction = nullptr;
		uint32_t maxThreadCount = 0;
		float internalTimeout = 1.0f;
		std::atomic_bool running = ATOMIC_VAR_INIT(false);
		std::atomic_uint activeThreadCount = ATOMIC_VAR_INIT(0);
		std::atomic<ErrorResult> threadedError = ATOMIC_VAR_INIT(ErrorResult::Net_Error);

		std::map<std::string, NetFuncType> functions;
		NetFuncType defaultFunction = nullptr;
		
		ErrorResult HelperUpdate(float timeoutSeconds);
		void HelperUpdateThread(void);
		ErrorResult HelperWork(std::unique_ptr<ConnectionBase> &connection);
		void HelperWorkThread(std::unique_ptr<ConnectionBase> connection);
	public:
		Listener() = default;
		Listener(Listener&) = delete;
		Listener(Listener&&) = delete;
		void operator=(Listener&) = delete;
		void operator=(Listener&&) = delete;
		~Listener() { Stop(); }

		// Add a function to the listening system.
		ErrorResult AddFunction(std::string const &name, NetFuncType func)
		{
			if(running) return ErrorResult::Listener_Started;
			return (functions.emplace(name, func).second) ? ErrorResult::Call_Ok : ErrorResult::Func_Overwrite;
		}

		// Set the default function to call with the request when it doesn't match any of the other function names.
		ErrorResult SetDefaultFunc(NetFuncType func)
		{
			if(running) return ErrorResult::Listener_Started;
			defaultFunction = func;
			return ErrorResult::Call_Ok;
		}

		// Set the string serialization functions for this object, make sure they match the ones that the other side uses.
		ErrorResult SetStringSerializations(StringSerializationType serializeFunc, StringDeserializationType deserializeFunc)
		{
			if(running) return ErrorResult::Listener_Started;
			serializeFunction = serializeFunc; deserializeFunction = deserializeFunc;
			return ErrorResult::Call_Ok;
		}

		// Set the connection class to use.
		template <typename T>
		ErrorResult SetConnectionType(void)
		{
			if(running) return ErrorResult::Listener_Started;
			listeningConnection.reset(new T());
			return ErrorResult::Call_Ok;
		}

		// Starts the listener port and sets up the backend to start accepting and handling requests.
		// port : the port to setup and listen on
		// helperNum : maximum number of helper threads
		//    if this is 0, no threads will be created and all work will be done on the calls to Update
		//    if otherwise, a thread will be created for accepting and processing requests as needed
		// acceptQueueSize : size of the accept queue passed into Listen
		// timeoutSeconds : the maximum amount of time a connection should wait for data from the requester
		//    only used if helperNum is not 0
		ErrorResult Start(uint16_t port, uint16_t helperNum, uint16_t acceptQueueSize, float timeoutSeconds = 1.0f);

		// Stops the listener port and waits for all threads to finish.
		void Stop(void);

		// Trys to accept and process requests.
		// timeoutSeconds : the amount of time that should pass before the function stops accepting new requests and return
		ErrorResult Update(float timeoutSeconds);
	};

	class Request
	{
		std::unique_ptr<ConnectionBase> connection = nullptr;
		StringSerializationType serializeFunction = nullptr;
		StringDeserializationType deserializeFunction = nullptr;
	public:
		// The returned json from the remote function
		nlohmann::json result;

		// Set the string serialization functions for this object, make sure they match the ones that the other side uses.
		void SetStringSerializations(StringSerializationType serializeFunc, StringDeserializationType deserializeFunc)
		{
			serializeFunction = serializeFunc; deserializeFunction = deserializeFunc;
		}

		// Set the connection class to use.
		template <typename T>
		void SetConnectionType(void)
		{
			connection.reset(new T());
		}

		// Send a request to execute a function on a listening connection.
		// address, port : location to try to connect to
		// name : the name bound to the function on the listening connection
		// args : the arguments that are passed to the function
		// waitForResult : should the function block until the remote function has finished
		//    if true, function will block
		//    if false, function will spawn a detached thread that handles the function call. there will not be a result
		// timeoutSeconds : if waitForResult is true, this is the maximum amount of time that the function can take to execute
		ErrorResult Send(std::string const &address, uint16_t port, std::string const &name, nlohmann::json const &args, bool waitForResult, float timeoutSeconds);
	};
};

#endif

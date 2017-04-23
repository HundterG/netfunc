/*
	NetFunc, a simple network transparent function call system.
	Author : Tyler Hundt
	Written : 4-22-17
*/

#include "netfunc.h"
#include <chrono>

// default string serialization functions
namespace
{
	bool DefaultStringSerialization(std::string const &input, std::unique_ptr<char[]> &outBuffer, uint16_t &outSizeBytes)
	{
		outSizeBytes = 0;
		outBuffer.reset();

		try
		{
			uint32_t expectedSize = uint32_t(input.size());
			if (expectedSize > std::numeric_limits<uint16_t>::max())
				// input is too big
				return false;

			outBuffer.reset(new char[input.size()]);
			if (!outBuffer)
				// buffer did not allocate
				return false;

			std::memcpy(outBuffer.get(), input.c_str(), input.size());
			outSizeBytes = uint16_t(expectedSize);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	bool DefaultStringDeserialization(std::unique_ptr<char[]> const &inBuffer, uint16_t inSizeBytes, std::string &output)
	{
		output.clear();

		try
		{
			output.assign(inBuffer.get(), inSizeBytes);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
};

#if defined(__GNUC__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
// default connection class
namespace
{
	class DefaultConnection : public netfunc::ConnectionBase
	{
		int mySocket = -1;
	public:
		DefaultConnection() = default;
		DefaultConnection(int in) : mySocket(in) {}

		// Sets up the port and gets it ready to either connect or listen.
		// port : the port to try to setup
		// return : true if setup was successful, false if not
		virtual bool Setup(uint16_t port) override
		{
			mySocket = socket(AF_INET, SOCK_STREAM, 0);
			if(mySocket < 0)
				return false;
			sockaddr_in sockAddr;
			std::memset(&sockAddr, 0, sizeof(sockAddr));
			sockAddr.sin_family = AF_INET;
			sockAddr.sin_addr.s_addr = INADDR_ANY;
			sockAddr.sin_port = htons(port);
			if(bind(mySocket, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr)) < 0)
			{
				close(mySocket);
				mySocket = -1;
				return false;
			}
			return true;
		}

		// Destroys the open connection.
		virtual void Stop(void) override
		{
			close(mySocket);
		}

		// Try to open connection to remote listener. This should block until the connection returns good or not.
		// address, port : location to try to connect to
		// return : true if successfully connected, false if not
		virtual bool Connect(std::string const &address, uint16_t port) override
		{
			sockaddr_in target;
			std::memset(&target, 0, sizeof(target));
			target.sin_family = AF_INET;
			target.sin_addr.s_addr = inet_addr(address.c_str());
			target.sin_port = htons(port);
			
			if(connect(mySocket, reinterpret_cast<sockaddr*>(&target), sizeof(target)) < 0)
				return false;
			return true;
		}

		// Set the connection to start listening.
		// acceptQueueSize : requested size of the accept queue
		// return : true if successfully listening, false if not
		virtual bool Listen(uint16_t acceptQueueSize) override
		{
			if(listen(mySocket, acceptQueueSize) == 0)
			{
				// make listener non-blocking
				int flags = fcntl(mySocket, F_GETFL);
				fcntl(mySocket, F_SETFL, flags | O_NONBLOCK);
				return true;
			}
			return false;
		}

		// Try to accept a new connection from a listening port. This function should not block.
		// return : true if listening port is still good
		// newConnection : return the new connection, or nullptr if there was no new connection
		virtual bool Accept(std::unique_ptr<ConnectionBase> &newConnection) override
		{
			sockaddr_in newAddr;
			std::memset(&newAddr, 0, sizeof(newAddr));
			socklen_t addrLen = sizeof(newAddr);
			int newSocket = accept(mySocket, reinterpret_cast<sockaddr*>(&newAddr), &addrLen);
			if(newSocket == EAGAIN || newSocket == EWOULDBLOCK)
				return true;
			else if(newSocket < -1)
				return false;
			else
			{
				newConnection.reset(new DefaultConnection(newSocket));
				return true;
			}
		}

		// Sends the data, send all the data. The send is responsible for sending a way for receive to know when
		//    all of the data has been read.
		// inBuffer : buffer that has all the data to send
		// sizeBytes : size in bytes of the buffer
		// return : true if successfully sent, false if not
		virtual bool Send(std::unique_ptr<char[]> const &inBuffer, uint16_t sizeBytes) override
		{
			std::unique_ptr<char[]> tempBuff(new char[sizeBytes + sizeof(uint16_t)]);
			if(!tempBuff)
				return false;

			*reinterpret_cast<uint16_t*>(tempBuff.get()) = htons(sizeBytes);
			std::memcpy(tempBuff.get() + sizeof(uint16_t), inBuffer.get(), sizeBytes);

			if(write(mySocket, tempBuff.get(), sizeBytes + sizeof(uint16_t)) != int32_t(sizeBytes + sizeof(uint16_t)))
				return false;
			return true;
		}

		// Try to receive SizeBytes of data. This should be non-blocking until data starts coming in, then it
		//    should block until all the data is read.
		// return : true if the connection is still in a good state, false if not
		// outBuffer : the buffer with the read data in it, or nullptr if there was no data ready to read
		// outSizeBytes : size of the buffer returned
		virtual bool Recv(std::unique_ptr<char[]> &outBuffer, uint16_t &outSizeBytes) override
		{
			outBuffer.reset();
			outSizeBytes = 0;

			pollfd dataCheck;
			dataCheck.fd = mySocket;
			dataCheck.events = POLLIN;
			dataCheck.revents = 0;
			if(poll(&dataCheck, 1, 1) < 0)
				return false;
			if(dataCheck.revents != POLLIN)
				return true;
			
			{
				uint16_t tempSize;
				int thisRead = read(mySocket, reinterpret_cast<char*>(&tempSize), sizeof(uint16_t));
				if(thisRead != sizeof(uint16_t))
					return false;
				outSizeBytes = ntohs(tempSize);
			}

			int32_t readBytes = 0;
			outBuffer.reset(new char[outSizeBytes]);
			if(!outBuffer)
				return false;
			while(readBytes < outSizeBytes)
			{
				int thisRead = read(mySocket, outBuffer.get()+readBytes, outSizeBytes - readBytes);
				if(thisRead < 0)
					return false;
				readBytes += thisRead;
			}
			return true;
		}
	};
}
#endif


// netfunc Listener definitions
namespace netfunc
{
	// Starts the listener port and sets up the backend to start accepting and handling requests.
	// port : the port to setup and listen on
	// helperNum : maximum number of helper threads
	//    if this is 0, no threads will be created and all work will be done on the calls to Update
	//    if otherwise, a thread will be created for accepting and processing requests as needed
	// acceptQueueSize : size of the accept queue passed into Listen
	// timeoutSeconds : the maximum amount of time a connection should wait for data from the requester
	//    only used if helperNum is not 0
	ErrorResult Listener::Start(uint16_t port, uint16_t helperNum, uint16_t acceptQueueSize, float timeoutSeconds)
	{
		if(running)
			return ErrorResult::Listener_Started;
		
		// setup variables
		if(listeningConnection == nullptr)
#if defined(__GNUC__)
			listeningConnection.reset(new DefaultConnection());
#else
			return ErrorResult::No_Default;
#endif

		if(serializeFunction == nullptr || deserializeFunction == nullptr)
		{
			serializeFunction = DefaultStringSerialization;
			deserializeFunction = DefaultStringDeserialization;
		}
		
		maxThreadCount = helperNum;
		internalTimeout = timeoutSeconds;

		// start the listener socket
		if(!listeningConnection->Setup(port))
			return ErrorResult::Net_Error;
		if(!listeningConnection->Listen(acceptQueueSize))
		{
			listeningConnection->Stop();
			return ErrorResult::Net_Error;
		}
		
		running = true;
		
		// start a helper thread if helperNum is greater than 0
		if(maxThreadCount >= 1)
		{
			++activeThreadCount;
			std::thread t(&Listener::HelperUpdateThread, this);
			t.detach();
			threadedError = ErrorResult::Call_Ok;
		}
		
		return ErrorResult::Call_Ok;
	}
	
	// Stops the listener port and waits for all threads to finish.
	void Listener::Stop(void)
	{
		if(running)
		{
			running = false;
			
			// wait for threads
			while(activeThreadCount > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			
			listeningConnection->Stop();
		}
	}
	
	// Trys to accept and process requests.
	// timeoutSeconds : the amount of time that should pass before the function stops accepting new requests and return
	ErrorResult Listener::Update(float timeoutSeconds)
	{
		try
		{
			if(running)
			{
				if(maxThreadCount == 0)
					return HelperUpdate(timeoutSeconds);
				else
					return threadedError;
			}
			else
				return ErrorResult::Net_Error;
		}
		catch(...)
		{
			return ErrorResult::Net_Error;
		}
	}
	
	ErrorResult Listener::HelperUpdate(float timeoutSeconds)
	{
		std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
		for(;;)
		{
			// check for timeout
			std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
			if(std::chrono::duration_cast<std::chrono::duration<double>>(nowTime-startTime).count() > timeoutSeconds)
			{
				return ErrorResult::Call_Ok;
			}
			
			// try to get a connection
			std::unique_ptr<ConnectionBase> newConnection;
			if(!listeningConnection->Accept(newConnection))
				return ErrorResult::Net_Error;
			
			if(newConnection)
			{
				// make new thread for request if can, otherwise run in this thread
				if(activeThreadCount < maxThreadCount)
				{
					++activeThreadCount;
					std::thread t(&Listener::HelperWorkThread, this, std::move(newConnection));
					t.detach();
				}
				else
				{
					ErrorResult result = HelperWork(newConnection);
					newConnection->Stop();
					if(result != ErrorResult::Call_Ok)
						return result;
				}
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	
	void Listener::HelperUpdateThread(void)
	{
		try
		{
			while(running)
			{
				if(HelperUpdate(5) == ErrorResult::Net_Error)
				{
					threadedError = ErrorResult::Net_Error;
					break;
				}
			}
		}
		catch(...){}
		--activeThreadCount;
	}
	
	ErrorResult Listener::HelperWork(std::unique_ptr<ConnectionBase> &connection)
	{
		ErrorResult returnValue = ErrorResult::Call_Ok;

		// read the request
		std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
		std::unique_ptr<char[]> buffer;
		uint16_t sizeBytes = 0;
		for(;;)
		{
			// check for timeout
			std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
			if(std::chrono::duration_cast<std::chrono::duration<double>>(nowTime-startTime).count() > internalTimeout)
				return netfunc::ErrorResult::Request_Timeout;

			// get data
			if(!connection->Recv(buffer, sizeBytes))
				return netfunc::ErrorResult::Net_Error;
			if(buffer)
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// pass buffer to deserializer
		std::string jsonString;
		if(!deserializeFunction(buffer, sizeBytes, jsonString))
			return netfunc::ErrorResult::Bad_String;
		buffer.reset();
		sizeBytes = 0;

		// deserialize json
		nlohmann::json request;
		try
		{
			request = nlohmann::json::parse(jsonString.c_str());
		}
		catch(...)
		{
			return netfunc::ErrorResult::Bad_String;
		}

		// call function
		nlohmann::json result;
		{
			auto nameRef = request.find("name");
			auto argsRef = request.find("args");
			if(nameRef == request.end() || argsRef == request.end())
				return ErrorResult::Bad_Json;

			// look for function, call if found
			auto foundFunc = functions.find(*nameRef);
			if(foundFunc != functions.end())
				foundFunc->second(*argsRef, result);
			else
			{
				// if not found, try default. no default, were done here
				if(defaultFunction)
					defaultFunction(*argsRef, result);
				else
					return ErrorResult::Call_Ok;
			}
		}

		// serialize result
		try
		{
			jsonString = result.dump();
		}
		catch(...)
		{
			// if failed, send back an empty object instead
			jsonString = "{}";
			returnValue = ErrorResult::Return_Error;
		}

		if(!serializeFunction(jsonString, buffer, sizeBytes))
		{
			// if failed, return something
			buffer.reset(new char[1]);
			*buffer.get() = 0;
			sizeBytes = 1;
			returnValue = ErrorResult::Return_Error;
		}

		// send result
		if(!connection->Send(buffer, sizeBytes))
			return netfunc::ErrorResult::Net_Error;

		// wait for a half a second to let network do its thing
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		
		return returnValue;
	}
	
	void Listener::HelperWorkThread(std::unique_ptr<ConnectionBase> connection)
	{
		try
		{
			HelperWork(connection);
			connection->Stop();
		}
		catch(...){}
		--activeThreadCount;
	}
}



// helper functions for Request
namespace
{
	netfunc::ErrorResult HelperRequest(std::string const &address, uint16_t port, std::string const &name, 
		nlohmann::json const &args, nlohmann::json &result, float timeoutSeconds,
		std::unique_ptr<netfunc::ConnectionBase> &connection, 
		netfunc::StringSerializationType serializeFunction, netfunc::StringDeserializationType deserializeFunction)
	{
		nlohmann::json fullRequest;
		fullRequest.emplace("name", name);
		fullRequest.emplace("args", args);

		// create the json as a string
		std::string requestString;
		try
		{
			requestString = fullRequest.dump();
		}
		catch(...)
		{
			return netfunc::ErrorResult::Bad_Json;
		}

		// pass the string through the serializer
		std::unique_ptr<char[]> buffer;
		uint16_t sizeBytes = 0;
		if(!serializeFunction(requestString, buffer, sizeBytes))
			return netfunc::ErrorResult::Bad_String;

		// start the connection
		if(!connection->Setup(0))
			return netfunc::ErrorResult::Net_Error;
		if(!connection->Connect(address, port))
		{
			connection->Stop();
			return netfunc::ErrorResult::Net_Error;
		}

		// send the string
		if(!connection->Send(buffer, sizeBytes))
		{
			connection->Stop();
			return netfunc::ErrorResult::Net_Error;
		}
		buffer.reset();
		sizeBytes = 0;

		// wait for response
		std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
		for(;;)
		{
			// check for timeout
			std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
			if(std::chrono::duration_cast<std::chrono::duration<double>>(nowTime-startTime).count() > timeoutSeconds)
			{
				connection->Stop();
				return netfunc::ErrorResult::Request_Timeout;
			}
			
			// get data
			if(!connection->Recv(buffer, sizeBytes))
			{
				connection->Stop();
				return netfunc::ErrorResult::Net_Error;
			}
			if(buffer)
				break;
			
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// close connection
		connection->Stop();

		// pass buffer to deserializer
		std::string returnString;
		if(!deserializeFunction(buffer, sizeBytes, returnString))
			return netfunc::ErrorResult::Return_Error;

		// get string as json
		try
		{
			result = nlohmann::json::parse(returnString.c_str());
		}
		catch(...)
		{
			return netfunc::ErrorResult::Return_Error;
		}

		return netfunc::ErrorResult::Call_Ok;
	}

	void HelperRequestThread(std::string address, uint16_t port, std::string name, nlohmann::json args,
		float timeoutSeconds, std::unique_ptr<netfunc::ConnectionBase> connection, 
		netfunc::StringSerializationType serializeFunction, netfunc::StringDeserializationType deserializeFunction)
	{
		nlohmann::json result;
		try
		{
			HelperRequest(address, port, name, args, result, timeoutSeconds,
				connection, serializeFunction, deserializeFunction);
		}
		catch(...){}
	}
}

// definition for Request
namespace netfunc
{
	// Send a request to execute a function on a listening connection.
	// address, port : location to try to connect to
	// name : the name bound to the function on the listening connection
	// args : the arguments that are passed to the function
	// waitForResult : should the function block until the remote function has finished
	//    if true, function will block
	//    if false, function will spawn a detached thread that handles the function call. there will not be a result.
	// timeoutSeconds : this is the maximum amount of time that the function can take to execute
	ErrorResult Request::Send(std::string const &address, uint16_t port, std::string const &name, nlohmann::json const &args, 
		bool waitForResult, float timeoutSeconds)
	{
		try
		{
			if(connection == nullptr)
#if defined(__GNUC__)
				connection.reset(new DefaultConnection());
#else
				return ErrorResult::No_Default;
#endif

			if(serializeFunction == nullptr || deserializeFunction == nullptr)
			{
				serializeFunction = DefaultStringSerialization;
				deserializeFunction = DefaultStringDeserialization;
			}

			if (waitForResult)
			{
				// do things in this thread
				return HelperRequest(address, port, name, args, result, timeoutSeconds, 
					connection, serializeFunction, deserializeFunction);
			}
			else
			{
				// spawn helper thread
				std::thread t(HelperRequestThread, address, port, name, args, timeoutSeconds, std::move(connection), 
					serializeFunction, deserializeFunction);
				t.detach();
				return ErrorResult::Call_Ok;
			}
		}
		catch(...)
		{
			return ErrorResult::Net_Error;
		}
	}
}

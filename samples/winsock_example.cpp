/*
	This example is the same as the first exept it includes an example of how to overload the connection class.
*/

#include <iostream>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
#include "../netfunc.h"

namespace
{
	class WinsockConnection : public netfunc::ConnectionBase
	{
		SOCKET mySocket = INVALID_SOCKET;
	public:
		WinsockConnection() = default;
		WinsockConnection(SOCKET in) : mySocket(in) { /* make blocking */ u_long iMode=1; ioctlsocket(mySocket, FIONBIO, &iMode); }

		// Sets up the port and gets it ready to either connect or listen.
		// port : the port to try to setup
		// return : true if setup was successful, false if not
		virtual bool Setup(uint16_t port) override
		{
			mySocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, 0);
			if(mySocket == INVALID_SOCKET)
				return false;
			sockaddr_in sockAddr = {0};
			sockAddr.sin_family = AF_INET;
			sockAddr.sin_addr.s_addr = INADDR_ANY;
			sockAddr.sin_port = htons(port);
			if(bind(mySocket, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr)) == SOCKET_ERROR)
			{
				closesocket(mySocket);
				mySocket = INVALID_SOCKET;
				return false;
			}
			return true;
		}

		// Destroys the open connection.
		virtual void Stop(void) override
		{
			closesocket(mySocket);
		}

		// Try to open connection to remote listener. This should block until the connection returns good or not.
		// address, port : location to try to connect to
		// return : true if successfully connected, false if not
		virtual bool Connect(std::string const &address, uint16_t port) override
		{
			sockaddr_in target = {0};
			target.sin_family = AF_INET;
			target.sin_addr.s_addr = inet_addr(address.c_str());
			target.sin_port = htons(port);

			if(connect(mySocket, reinterpret_cast<sockaddr*>(&target), sizeof(target))  == SOCKET_ERROR)
				return false;
			return true;
		}

		// Set the connection to start listening.
		// acceptQueueSize : requested size of the accept queue
		// return : true if successfully listening, false if not
		virtual bool Listen(uint16_t acceptQueueSize) override
		{
			if(listen(mySocket, acceptQueueSize) == SOCKET_ERROR)
				return false;
			// make non-blocking
			u_long iMode=1;
			ioctlsocket(mySocket, FIONBIO, &iMode);
			return true;
		}

		// Try to accept a new connection from a listening port. This function should not block.
		// return : true if listening port is still good
		// newConnection : return the new connection, or nullptr if there was no new connection
		virtual bool Accept(std::unique_ptr<ConnectionBase> &newConnection) override
		{
			sockaddr_in newAddr = {0};
			int addrLen = sizeof(newAddr);
			SOCKET newSocket = accept(mySocket, reinterpret_cast<sockaddr*>(&newAddr), &addrLen);
			if(newSocket == INVALID_SOCKET)
			{
				switch(WSAGetLastError())
				{
				case WSAEWOULDBLOCK: return true;
				default: return false;
				}
			}

			newConnection.reset(new WinsockConnection(newSocket));
			return true;
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

			if(send(mySocket, tempBuff.get(), sizeBytes + sizeof(uint16_t), 0) != sizeBytes + sizeof(uint16_t))
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

			u_long count = 0;
			if(ioctlsocket(mySocket, FIONREAD, &count) == SOCKET_ERROR)
				return false;
			if(count == 0)
				return true;

			{
				uint16_t tempSize;
				int thisRead = recv(mySocket, reinterpret_cast<char*>(&tempSize), sizeof(uint16_t), 0);
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
				int thisRead = recv(mySocket, outBuffer.get()+readBytes, outSizeBytes - readBytes, 0);
				if(thisRead == SOCKET_ERROR)
					return false;
				readBytes += thisRead;
			}
			return true;
		}
	};

	class WinsockSingleton
	{
	public:
		WinsockSingleton() { WSADATA wsaData = {0}; WSAStartup(MAKEWORD(2,2), &wsaData); }
		~WinsockSingleton() { WSACleanup(); }
	} winsokSingleton;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace
{
	netfunc::Listener server;

	void Function(nlohmann::json const &args, nlohmann::json &result)
	{
		std::cout << "Function was called with " << args.size() << " arguments, returning 5\n";
		result.emplace("number", 5);
	}

	void DefaultFunction(nlohmann::json const &args, nlohmann::json &result)
	{
		std::cout << "DefaultFunction was called with " << args.size() << " arguments\n";
	}
}

int main(void)
{
	std::cout << "starting listener... ";
	if(server.AddFunction("foo", Function) != netfunc::ErrorResult::Call_Ok)
	{
		std::cout << "failed\n";
		return 1;
	}
	server.SetDefaultFunc(DefaultFunction);
	server.SetConnectionType<WinsockConnection>();
	if(server.Start(8000, 1, 10) != netfunc::ErrorResult::Call_Ok)
	{
		std::cout << "failed\n";
		return 1;
	}
	std::cout << "good\n\n";

	std::cout << "send request\n";
	{
		netfunc::Request request;
		nlohmann::json args;
		args.emplace("pi", 3.14159f);
		request.SetConnectionType<WinsockConnection>();
		if(request.Send("127.0.0.1", 8000, "foo", args, true, 0.6f) != netfunc::ErrorResult::Call_Ok)
		{
			std::cout << "failed first request\n";
		}
		else
		{
			auto numberRef = request.result.find("number");
			if(numberRef != request.result.end())
				std::cout << "first request has returned " << float(*numberRef) << "\n";
			else
				std::cout << "first request has no return\n";
		}
	}

	{
		netfunc::Request request;
		nlohmann::json args;
		args.emplace("pi", 3.14159f);
		request.SetConnectionType<WinsockConnection>();
		if(request.Send("127.0.0.1", 8000, "bar", args, true, 0.6f) != netfunc::ErrorResult::Call_Ok)
		{
			std::cout << "failed second request\n";
		}
		else
		{
			auto numberRef = request.result.find("number");
			if(numberRef != request.result.end())
				std::cout << "second request has returned " << float(*numberRef) << "\n";
			else
				std::cout << "second request has no return\n";
		}
	}

	server.Stop();
	return 0;
}

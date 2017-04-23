/*
	This example shows a call that will block until the remote function is finished running.
	This can be good for when you need a result from the remote function, and an asynchronous
	model doesn't make sense for you application.
*/

#include <iostream>
#include <thread>
#include "../netfunc.h"

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
		if(request.Send("127.0.0.1", 8000, "foo", args, true, 1.5f) != netfunc::ErrorResult::Call_Ok)
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
		if(request.Send("127.0.0.1", 8000, "bar", args, true, 1.5f) != netfunc::ErrorResult::Call_Ok)
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

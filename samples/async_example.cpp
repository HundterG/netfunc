/*
	This example shows a call that will not block or return anything in result.
	This can be good for when you just want to fire a function to do a thing and
	move on without a trace.
*/

#include <iostream>
#include <thread>
#include "../netfunc.h"

namespace
{
	netfunc::Listener server;

	void Function(nlohmann::json const &args, nlohmann::json &result)
	{
		std::cout << "Function";
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
		if(request.Send("127.0.0.1", 8000, "foo", args, false, 0.4f) != netfunc::ErrorResult::Call_Ok)
		{
			std::cout << "failed request\n";
			return 1;
		}
	}

	std::cout << "doing stuff and forgetting";
	for(int32_t i=0 ; i<500 ; ++i)
	{
		std::cout << ". ";
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	server.Stop();
	return 0;
}

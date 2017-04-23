/*
	This example shows how we can use std future to do other work while waiting for
	the remote function to finish.
*/

#include <iostream>
#include <thread>
#include <future>
#include "../netfunc.h"

namespace
{
	netfunc::Listener server;

	void Function(nlohmann::json const &args, nlohmann::json &result)
	{
		result.emplace("number", 5);
	}

	float AsyncReturn(void)
	{
		netfunc::Request request;
		nlohmann::json args;
		args.emplace("pi", 3.14159f);
		if(request.Send("127.0.0.1", 8000, "foo", args, true, 1.0f) != netfunc::ErrorResult::Call_Ok)
			return 0.0f;
		auto numberRef = request.result.find("number");
		if(numberRef != request.result.end())
			return float(*numberRef);
		else
			return 0.0f;
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
	std::future<float> asyncFuncRef(std::async(AsyncReturn));

	std::cout << "doing stuff while waiting";
	for(;;)
	{
		std::cout << ". ";
		if(asyncFuncRef.wait_for(std::chrono::milliseconds(5)) == std::future_status::ready)
			break;
	}

	std::cout << "request has returned with value " << asyncFuncRef.get() << "\n";
	server.Stop();
	return 0;
}

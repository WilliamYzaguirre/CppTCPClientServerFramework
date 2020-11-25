#include <iostream>
#include <net.h>

enum class CustomMsgTypes : uint32_t
{
	ServerAccept,
	ServerDeny,
	ServerPing,
	MessageAll,
	ServerMessage,
	ClientValidated
};

class CustomClient : public net::client_interface<CustomMsgTypes>
{
public:
	void PingServer()
	{
		net::message<CustomMsgTypes> msg;
		msg.header.id = CustomMsgTypes::ServerPing;

		// Caution. This could be a problem if server and client are
		// running on different systems. This is for my debugging
		std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();

		msg << timeNow;
		Send(msg);
	}

	void MessageAll()
	{
		net::message<CustomMsgTypes> msg;
		msg.header.id = CustomMsgTypes::MessageAll;
		Send(msg);
	}
};

int main()
{
	CustomClient c;
	c.Connect("127.0.0.1", 60000);

	bool key[3] = { false, false, false };
	bool old_key[3] = { false, false, false };

	bool bQuit = false;
	while (!bQuit)
	{
		if (GetForegroundWindow() == GetConsoleWindow())
		{
			key[0] = GetAsyncKeyState('1') & 0x8000;
			key[1] = GetAsyncKeyState('2') & 0x8000;
			key[2] = GetAsyncKeyState('3') & 0x8000;
		}

		// If 1 is pressed, ping server
		if (key[0] && !old_key[0])
		{
			c.PingServer();
		}

		// If 2 is pressed, message all
		if (key[1] && !old_key[1])
		{
			c.MessageAll();
		}

		// If 3 is pressed, quit
		if (key[2] && !old_key[2])
		{
			bQuit = true;
		}

		for (int i = 0; i < 3; ++i)
		{
			old_key[i] = key[i];
		}

		if (c.IsConnected())
		{
			if (!c.Incoming().empty())
			{
				auto msg = c.Incoming().pop_front().msg;

				switch (msg.header.id)
				{
				case CustomMsgTypes::ServerPing:
				{
					// Server responded to ping
					std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
					std::chrono::system_clock::time_point timeThen;
					msg >> timeThen;
					std::cout << "Ping: " << std::chrono::duration<double>(timeNow - timeThen).count() << "\n";
				}
				break;

				case CustomMsgTypes::ServerMessage:
				{
					// Server sent message to client
					uint32_t clientID;
					msg >> clientID;
					std::cout << "Hello from [" << clientID << "]\n";
				}
				break;

				case CustomMsgTypes::ServerAccept:
				{
					// Server has accepted this client
					std::cout << "Server Accepted Connection\n";
				}
				break;

				case CustomMsgTypes::ClientValidated:
				{
					// Server has validated this client, display ID
					uint32_t clientID;
					msg >> clientID;
					std::cout << "NOTICE: [" << clientID << "] Has Joined The Server\n";
				}
				break;

				}
			}
		}
		else
		{
			std::cout << "Server Down\n";
			bQuit = true;
		}
	}

	return 0;
}
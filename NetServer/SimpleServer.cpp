#include <iostream>
#include <net.h>

enum class CustomMsgTypes : uint32_t
{
	ServerAccept,
	ServerDeny,
	ServerPing,
	MessageAll,
	ServerMessage,
	ClientJoined,
};

class CustomServer : public net::server_interface<CustomMsgTypes>
{
public:
	CustomServer(uint16_t nPort) : net::server_interface<CustomMsgTypes>(nPort)
	{

	}

protected:
	virtual void OnClientValidated(std::shared_ptr<net::connection<CustomMsgTypes>> client)
	{
		// Construct new message and send it to all clients
		// alerting them this new client has joined
		net::message<CustomMsgTypes> msg;
		msg.header.id = CustomMsgTypes::ClientJoined;
		msg << client->GetID();
		MessageAllClients(msg, client);
	}

	virtual bool OnClientConnect(std::shared_ptr<net::connection<CustomMsgTypes>> client)
	{
		net::message<CustomMsgTypes> msg;
		msg.header.id = CustomMsgTypes::ServerAccept;
		std::cout << "Sending server Accept\n";
		client->Send(msg);
		return true;
	}

	// Called when client appears to have disconnected
	virtual void OnClientDisconnet(std::shared_ptr<net::connection<CustomMsgTypes>> client)
	{
		std::cout << "Removing client [" << client->GetID() << "]\n";
	}

	// Called when a message arrives
	virtual void OnMessage(std::shared_ptr<net::connection<CustomMsgTypes>> client, net::message<CustomMsgTypes>& msg)
	{
		switch (msg.header.id)
		{
		case CustomMsgTypes::ServerPing:
		{
			std::cout << "[" << client->GetID() << "]: Server Ping\n";

			// Bounce the message birdman
			client->Send(msg);
		}
		break;

		case CustomMsgTypes::MessageAll:
		{
			std::cout << "[" << client->GetID() << "]: Message All\n";

			//Construct new message and send it to all clients
			net::message<CustomMsgTypes> msg;
			msg.header.id = CustomMsgTypes::ServerMessage;
			msg << client->GetID();
			MessageAllClients(msg, client);
		}
		break;

		}
	}
};

int main()
{
	CustomServer server(60000);
	server.Start();
	while (1)
	{
		server.Update(-1, true);
	}
	return 0;
}
#pragma once

#include "net_common.h"
#include "net_tsqueue.h"
#include "net_message.h"
#include "net_connection.h"

namespace net
{
	template<typename T>
	class server_interface
	{
	public:

		server_interface(uint16_t port) 
			: m_asioAcceptor(m_asioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
		{
			
		}

		virtual ~server_interface()
		{
			Stop();
		}

		// Start the server
		bool Start()
		{
			try
			{
				// Give the server work to do before starting thread, so that it doesn't return immidietly
				WaitForClientConnection();

				m_threadContext = std::thread([this]() { m_asioContext.run(); });
			}
			catch (std::exception& e)
			{
				// Something happened, server can't start
				std::cerr << "[SERVER] Exception: " << e.what() << "\n";
				return false;
			}

			std::cout << "[SERVER] Started\n";
			return true;
		}

		// Stop the server
		void Stop()
		{
			// Request the context to close
			m_asioContext.stop();

			// Tidy up context thread
			if (m_threadContext.joinable())
			{
				m_threadContext.join();
			}

			// Inform anybody who's listening
			std::cout << "[SERVER] Stopped\n";

		}

		// ASYNC - instruct asio to wait for connection
		void WaitForClientConnection()
		{
			m_asioAcceptor.async_accept(
				[this](std::error_code ec, asio::ip::tcp::socket socket)
				{
					if (!ec)
					{
						std::cout << "[SERVER] New Connecton: " << socket.remote_endpoint() << "\n";

						// Create new connection to handle client
						std::shared_ptr<connection<T>> newConn = std::make_shared<connection<T>>(connection<T>::owner::server, 
							m_asioContext, std::move(socket), m_qMessagesIn);

						// Give the server a chance to deny connection
						if (OnClientConnect(newConn))
						{
							// Connection allowed, so add to container of new connections
							m_deqConnections.push_back(std::move(newConn));

							// Give connection new ID and the increment
							m_deqConnections.back()->ConnectToClient(this, nIDCounter++);

							std::cout << "[" << m_deqConnections.back()->GetID() << "] Connection Approved\n";
						}
						else
						{
							std::cout << "[-----] Connection Denied\n";
						}
					}
					else
					{
						// Error occurred during acceptance
						std::cout << "[SERVER] New Connection Error: " << ec.message() << "\n";
					}

					// Prime asio context with more work, waiting for another connection
					WaitForClientConnection();
				});
		}

		// Send a message to a specific client
		void MessageClient(std::shared_ptr<connection<T>> client, const message<T>& msg)
		{
			// Check client is legitimate
			if (client && client->IsConnected())
			{
				// If yes, just send it
				client->Send(msg);
			}
			else
			{
				// If we can't communicate with the client, might as well remove it
				onClientDisconnect(client);
				client.reset();

				// Remove it from container
				m_deqConnections.erase(
					std::remove(m_deqConnections.begin(), m_deqConnections.end(), client), m_deqConnections.end());
			}
		}

		// Send message to all clients
		void MessageAllClients(const message<T>& msg, std::shared_ptr<connection<T>> pIgnoreClient = nullptr)
		{
			bool bInvalidClientExists = false;

			for (auto& client : m_deqConnections)
			{
				// Check is client is connected
				if (client && client->IsConnected())
				{
					// Yup
					if (client != pIgnoreClient)
					{
						client->Send(msg);
					}
				}
				else
				{
					// If we can't communicate with the client, might as well remove it
					onClientDisconnect(client);
					client.reset();

					// Set flag to later on remove dead clients
					bInvalidClientExists = true;
				}
			}

			// Better to remove them now, so we don't invalidate container as we're going through it
			if (bInvalidClientExists)
			{
				m_deqConnections.erase(
					std::remove(m_deqConnections.begin(), m_deqConnections.end(), nullptr), m_deqConnections.end());
			}
		}

		// Called by user to explicitly process some messages in queue
		// Fore server side logic
		void Update(size_t nMaxMessages = -1, bool bWait = false)
		{
			if (bWait)
			{
				m_qMessagesIn.wait();
			}

			// Process as many messages as you can up to nMaxMessages
			size_t nMessageCount = 0;
			while (nMessageCount < nMaxMessages && !m_qMessagesIn.empty())
			{
				// Grab the front message
				auto msg = m_qMessagesIn.pop_front();

				// Pass to message handler
				OnMessage(msg.remote, msg.msg);

				nMessageCount++;
			}
		}


	protected:
		// Server class shouls override these

		// Called when a client connects, you can deny the connection by returning false
		virtual bool OnClientConnect(std::shared_ptr<connection<T>> client)
		{
			return false;
		}

		// Called when a client appears to have disconnected
		virtual void onClientDisconnect(std::shared_ptr<connection<T>> client)
		{

		}

		// Called when a message arrives
		virtual void OnMessage(std::shared_ptr<connection<T>> client, message<T>& msg)
		{

		}

	public:
		// Called when a client is validated
		virtual void OnClientValidated(std::shared_ptr<connection<T>> client)
		{

		}

	protected:
		// Thread safe queue for incoming message packets
		tsqueue<owned_message<T>> m_qMessagesIn;

		// Container of active validated connections
		std::deque<std::shared_ptr<connection<T>>> m_deqConnections;

		// keep this order, needs to be initialized like this
		asio::io_context m_asioContext;
		std::thread m_threadContext;

		// Need ports of connections
		asio::ip::tcp::acceptor m_asioAcceptor;

		// Clients will be identitfied via an ID
		uint32_t nIDCounter = 10000;

	};
}
#pragma once

#include "net_common.h"
#include "net_tsqueue.h"
#include "net_message.h"

namespace net
{
	// Forward Decare
	template<typename T>
	class server_interface;


	template<typename T>
	class connection : public std::enable_shared_from_this<connection<T>>
	{
	public:
		// A connection is owned by either a client or server, behaves slightly differently
		// depending on which one
		enum class owner
		{
			server,
			client
		};

		connection(owner parent, asio::io_context& asioContext, asio::ip::tcp::socket socket, tsqueue<owned_message<T>>& qIn)
			: m_asioContext(asioContext), m_socket(std::move(socket)), m_qMessagesIn(qIn)
		{
			m_nOwnerType = parent;

			// Get auth check data
			if (m_nOwnerType == owner::server)
			{
				// Client wishes to establish a connection. This value is used
				// as a challenge
				m_nHandshakeOut = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());

				// Calculate correct hashed value
				m_nHandshakeCheck = basicScrambler(m_nHandshakeOut);
			}
			else
			{
				// Don't need to challenge as a client
				m_nHandshakeIn = 0;
				m_nHandshakeOut = 0;
			}
		}

		virtual ~connection()
		{}

		// Unique ID given to all clients to identify each other
		uint32_t GetID() const
		{
			return id;
		}

	public:
		// Only called by server
		void ConnectToClient(net::server_interface<T>* server, uint32_t uid = 0)
		{
			if (m_nOwnerType == owner::server)
			{
				if (m_socket.is_open())
				{
					id = uid;
					// Was: ReadHeader();

					// A client has attempted to connect to the server. So send them
					// the handsahke_out to auth
					WriteValidation();

					// Then, we wait asynchronously for the auth to be sent back
					ReadValidation(server);
				}
			}
		}

		// Only called by clients
		void ConnectToServer(const asio::ip::tcp::resolver::results_type& endpoints)
		{
			// Only clients can connect to servers
			if (m_nOwnerType == owner::client)
			{
				// Request asio attempts to connect to an endpoint
				asio::async_connect(m_socket, endpoints,
					[this](std::error_code ec, asio::ip::tcp::endpoint endpoint)
					{
						if (!ec)
						{
							// Was: ReadHeader();

							// First thing server does is send packet to be
							// authed. Wait for that and respond
							ReadValidation();
						}
					});
			}
		}

		// Called by servers or clients
		void Disconnect()
		{
			if (IsConnected())
			{
				asio::post(m_asioContext, [this]() { m_socket.close(); });
			}
		}

		// Returns if the connection is valid, open, and currently active
		bool IsConnected() const
		{
			return m_socket.is_open();
		}

		// Prime the connection to wait for incoming messages
		void StartListening()
		{

		}

	public:
		void Send(const message<T>& msg)
		{
			asio::post(m_asioContext,
				[this, msg]()
				{
					// If the queue has messages in it, we assume it is in the process of
					// being written to. If there are none, we start writing the messages
					// at front of queue
					bool bWritingMessage = !m_qMessagesOut.empty();
					m_qMessagesOut.push_back(msg);
					if (!bWritingMessage)
					{
						WriteHeader();
					}
				});
		}

	private:
		// ASYNC - Prime context ready to read a message header
		void ReadHeader()
		{
			asio::async_read(m_socket, asio::buffer(&m_msgTemporaryIn.header, sizeof(message_header<T>)),
				[this](std::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						// Header has been read, check if it has body
						if (m_msgTemporaryIn.header.size > 0)
						{
							// If it does, allocate space in messages body vector and issue asio with
							// task to read body
							m_msgTemporaryIn.body.resize(m_msgTemporaryIn.header.size);
							ReadBody();
						}
						else
						{
							// If it doesn't, add decapitated message to connections incoming message queue
							AddToIncomingMessageQueue();
						}
					}
					else
					{
						std::cout << "[" << id << "] Read Header Fail.\n";
						m_socket.close();
					}
				});
		}

		// ASYNC - Prime conext ready to read a message body
		void ReadBody()
		{
			// Once a header has been read, this function is called to read body
			asio::async_read(m_socket, asio::buffer(m_msgTemporaryIn.body.data(), m_msgTemporaryIn.body.size()),
				[this](std::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						AddToIncomingMessageQueue();
					}
					else
					{
						std::cout << "[" << id << "] Read Body Fail.\n";
						m_socket.close();
					}
				});
		}

		// ASYNC - Prime context to write a message header
		void WriteHeader()
		{
			asio::async_write(m_socket, asio::buffer(&m_qMessagesOut.front().header, sizeof(message_header<T>)),
				[this](std::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						if (m_qMessagesOut.front().body.size() > 0)
						{
							WriteBody();
						}
						else
						{
							m_qMessagesOut.pop_front();

							// Now check is Queue is empty. If not, write header again
							if (!m_qMessagesOut.empty())
							{
								WriteHeader();
							}
						}
					}
					else
					{
						std::cout << "[" << id << "] Write Header Fail.\n";
						m_socket.close();
					}
				});
		}

		// ASYNC - Prime context to write a message body
		void WriteBody()
		{
			// If this function is called, a header has just been sent, and that header
			// indicated a body existed for this message. Fill a transmission buffer
			// with the body data, and send it
			asio::async_write(m_socket, asio::buffer(m_qMessagesOut.front().body.data(), m_qMessagesOut.front().body.size()),
				[this](std::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						// Sending was successful, so we are done with the message
						// and remove it from the queue
						m_qMessagesOut.pop_front();

						// If the queue still has messages in it, then issue the task to 
						// send the next messages' header.
						if (!m_qMessagesOut.empty())
						{
							WriteHeader();
						}
					}
					else
					{
						// Sending failed
						std::cout << "[" << id << "] Write Body Fail.\n";
						m_socket.close();
					}
				});
		}

		void AddToIncomingMessageQueue()
		{
			// If the message is going to a server, you need to tag it with the name of the
			// client who sent it
			if (m_nOwnerType == owner::server)
			{
				m_qMessagesIn.push_back({ this->shared_from_this(), m_msgTemporaryIn });
			}
			// If the message is going to a client, there's only one server, no need to tag
			else
			{
				m_qMessagesIn.push_back({ nullptr, m_msgTemporaryIn });
			}

			// Prime asio for more work
			ReadHeader();
		}

		// "Encrypt" data
		uint64_t basicScrambler(uint64_t nInput)
		{
			uint64_t out = nInput ^ 0x12345678ABCD1234;
			out = (out & 0xF0F0F0F0F0F0F0F0) >> 4 | (out & 0x0F0F0F0F0F0F0F0F) << 4;
			return out ^ 0x4321DCBA87654321;
		}

		// ASYNC - used by client and server to write auth packet
		void WriteValidation()
		{
			asio::async_write(m_socket, asio::buffer(&m_nHandshakeOut, sizeof(uint64_t)),
				[this](std::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						// Auth data sent, clients should sit and wait for a response
						if (m_nOwnerType == owner::client)
						{
							ReadHeader();
						}
					}
					else
					{
						// Writing failed
						std::cout << "[" << id << "] Write Validation Fail.\n";
						m_socket.close();
					}
				});
		}


		void ReadValidation(net::server_interface<T>* server = nullptr)
		{
			asio::async_read(m_socket, asio::buffer(&m_nHandshakeIn, sizeof(uint64_t)),
				[this, server](std::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						if (m_nOwnerType == owner::server)
						{
							// If you're server, data coming in is response from client
							if (m_nHandshakeIn == m_nHandshakeCheck)
							{
								// Client has sent correct auth, so connect
								std::cout << "Client Validated\n";
								server->OnClientValidated(this->shared_from_this());

								// Now, sit and wait to receive data. Good Anton
								ReadHeader();
							}
							else
							{
								std::cout << "Client Disconnected (Failed Auth)\n";
								m_socket.close();
							}
						}
						else
						{
							// If you are client, solve puzzle
							m_nHandshakeOut = basicScrambler(m_nHandshakeIn);

							// Write and send result
							WriteValidation();
						}
					}
					else
					{
						// Sending failed
						std::cout << "[" << id << "] Read Validation Fail.\n";
						m_socket.close();
					}
				});
		}
		

	protected:
		// Each connection has a unique socket to a remote
		asio::ip::tcp::socket m_socket;

		// This context is shared with the whole asio instance
		asio::io_context& m_asioContext;

		// This queue holds all messages to be sent to the remote side of connection
		tsqueue<message<T>> m_qMessagesOut;

		// This queue holds all messages that have been recieved from the remote
		// side of this connection. It is a reference as the "owner" of this connection
		// is expected to provide a queue
		tsqueue<owned_message<T>>& m_qMessagesIn;
		message<T> m_msgTemporaryIn;

		// The owner decides how some of the connection behaves
		owner m_nOwnerType = owner::server;
		uint32_t id = 0;

		// Authentification variables
		uint64_t m_nHandshakeOut = 0;
		uint64_t m_nHandshakeIn = 0;
		uint64_t m_nHandshakeCheck = 0;
	};
}

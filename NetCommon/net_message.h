#pragma once
#include "net_common.h"

namespace net
{
	// Message header is sent at start of all messages. The
	// Template allows the use of "enum class" to ensure 
	// messages are valid at compile time
	template <typename T>
	struct message_header
	{
		T id{};
		uint32_t size = 0;
	};

	template <typename T>
	struct message
	{
		message_header<T> header{};
		std::vector<uint8_t> body;

		// Return size of entire message packet in bytes
		size_t size() const
		{
			return body.size();
		}

		// Override for std::cout compatability. Pushes message onto output stream
		friend std::ostream& operator << (std::ostream& os, const message<T>& msg)
		{
			os << "ID: " << int(msg.header.id) << " Size: " << msg.header.size;
			return os;
		}

		// Pushes any POD-like data into the message buffer
		template<typename DataType> 
		friend message<T>& operator << (message<T>& msg, const DataType& data)
		{
			// Check that the type of the data being pushes is trivially copyable
			static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

			// Cache current size of vector, as this will be the point we insert the data
			size_t i = msg.body.size();

			// Make room for new data by resizing vector
			msg.body.resize(msg.body.size() + sizeof(DataType));

			// Physically copy the data into the newly allocated vector space
			std::memcpy(msg.body.data() + i, &data, sizeof(DataType));

			// Update size of the message in header
			msg.header.size = msg.size();

			// Return the target message so that it can be chained
			return msg;
		}

		template<typename DataType>
		friend message<T>& operator >> (message<T>& msg, DataType& data)
		{
			// Check that the type of the data is trivially copyable
			static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

			// Cache the location towards the end of the vector where the pulled data starts
			size_t i = msg.body.size() - sizeof(DataType);

			// Physically copy the data from the vector into the user variable
			std::memcpy(&data, msg.body.data() + i, sizeof(DataType));

			// Shrink Vector to remove read bytes and reset the end position
			msg.body.resize(i);

			// Recalculate the message size
			msg.header.size = msg.size();

			// Return the target message so that it can chained
			return msg;
		}
	};

	// Forward declare the connect
	template <typename T>
	class connection;

	// So the server knows from which client a message comes from, add a pointer to the
	// connection that the client it connected to
	template <typename T>
	struct owned_message
	{
		std::shared_ptr<connection<T>> remote = nullptr;
		message<T> msg;

		// Once more, a friendly little string maker
		friend std::ostream& operator << (std::ostream& os, const owned_message<T>& msg)
		{
			os << msg.msg;
			return os;
		}
	};
}
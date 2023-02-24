#include <iostream>

#define ASIO_STANDALONE
#include <asio.hpp>

#include <atomic_queue/atomic_queue.h>

using queue = atomic_queue::AtomicQueue2<std::string, 1024>;

std::vector<asio::ip::tcp::socket*> sockets;
std::vector<asio::ip::tcp::acceptor*> acceptors;

queue c2p_queue;
queue s2p_queue;

queue p2s_queue;
queue p2c_queue;

/*void read(asio::ip::tcp::socket& socket) {
    socket.async_read_some(asio::buffer(buffer.data(), buffer.size()),
        [&](std::error_code ec, std::size_t length) {
            if (!ec) {
                std::cout << "\n\nRead " << length << " bytes.\n\n";

                for (int i = 0; i < length; i++) {
                    std::cout << buffer[i];
                } 

                read(socket);
            } else {
                std::cout << ec << std::endl;
            }
        }
    );
}*/

std::string make_string(asio::streambuf& streambuf)
{
    return {asio::buffers_begin(streambuf.data()), asio::buffers_end(streambuf.data())};
}

uint32_t make_uint32(asio::streambuf& streambuf) {
    std::istream is(&streambuf);
    return ((uint32_t) is.get() << 24) 
         + ((uint32_t) is.get() << 16) 
         + ((uint32_t) is.get() <<  8) 
         + ((uint32_t) is.get());
}

void readBuf(asio::ip::tcp::socket &socket, queue &out) {
    asio::streambuf read_buffer;

    while (1) {
        asio::read(socket, read_buffer, asio::transfer_exactly(4));
        std::string length_s = make_string(read_buffer);
        uint32_t length = make_uint32(read_buffer);
        std::cout << "Header: " << std::hex << (int) length << "\n";

        asio::read(socket, read_buffer, asio::transfer_exactly(length - 4));
        std::string s = make_string(read_buffer);
        read_buffer.consume(length); // Remove data that was read.
        std::cout << "Read: " << s << std::endl;
        out.push(length_s + s);
    }
}

void writeBuf(asio::ip::tcp::socket &socket, queue &in) {
    while (1) {
        std::string s = in.pop();
        asio::write(socket, asio::buffer(s));
    }
}

void connectLocal(asio::io_context &context, asio::ip::tcp::socket* socket, asio::ip::address address, int port) {
    asio::ip::tcp::endpoint endpoint(address, port);
    asio::ip::tcp::acceptor* acceptor = new asio::ip::tcp::acceptor(context, endpoint.protocol());

    acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor->bind(endpoint);
    acceptors.push_back(acceptor);
    acceptor->listen();
    
    acceptor->accept(*socket);
    sockets.push_back(socket);
}

void client(asio::io_context &context) {
    asio::ip::tcp::socket* socket = new asio::ip::tcp::socket(context);
    std::cout << "Connecting to client\n";
    connectLocal(context, socket, asio::ip::make_address("127.0.0.1"), 2050);

    std::thread c2p(readBuf, std::ref(*socket), std::ref(c2p_queue));
    std::thread p2c(writeBuf, std::ref(*socket), std::ref(s2p_queue));
    c2p.join();
    p2c.join();
}

void connectRemote(asio::io_context &context, asio::ip::tcp::socket* socket, asio::ip::address address, int port) {
    asio::ip::tcp::endpoint endpoint(address, port);
    socket->connect(endpoint);
    sockets.push_back(socket);
}

void server(asio::io_context &context) {
    asio::ip::tcp::socket* socket = new asio::ip::tcp::socket(context);
    std::cout << "Connecting to server\n";
    connectRemote(context, socket, asio::ip::make_address("54.86.47.176"), 2050);

    std::thread s2p(readBuf, std::ref(*socket), std::ref(s2p_queue));
    std::thread p2s(writeBuf, std::ref(*socket), std::ref(c2p_queue));
    s2p.join();
    p2s.join();
}

int main()
{
    asio::io_context context;
    /*asio::error_code ec;
    asio::io_context context;
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1", ec), 2050);
    asio::ip::tcp::socket socket(context);
    socket.connect(endpoint, ec);

    if (!ec) {
        std::cout << "Connected" << std::endl;
    } else {
        std::cout << ec.message() << std::endl;
    }

    if (socket.is_open()) {
        read(socket);

        std::string request = 
            "GET /index.html HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Connection: close\r\n\r\n";

        socket.write_some(asio::buffer(request.data(), request.size()), ec);

        context.run();
    }*/
    signal(SIGINT, [](int) {
        std::cout << "Closing sockets" << std::endl;
        for (auto socket : sockets) {
            socket->shutdown(asio::ip::tcp::socket::shutdown_both);
            socket->close();
            delete socket; 
        }
        for (auto acceptor : acceptors) {
            acceptor->cancel();
            acceptor->close();
            delete acceptor;
        }
        std::exit(0);
    });
    std::thread clientThread(client, std::ref(context));
    std::thread serverThread(server, std::ref(context));
    clientThread.join();
    serverThread.join();
    return 0;
}
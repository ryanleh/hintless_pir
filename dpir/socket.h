//#include <sys/socket.h>
//#include <arpa/inet.h>
//#include <netdb.h>
//#include <unistd.h>
//#include <iostream>
//#include <string>
//#include <cstring>
//#include <unordered_map>
//#include <atomic>
//#include <functional>
//#include <exception>

#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>

using namespace std;

#define SERVER_SOCKET "/tmp/dpir_server.sock"
#define CLIENT_SOCKET "/tmp/dpir_client.sock"

class Socket {
public:
    int Connect(std::string name) {
        socket_name_ = std::move(name);
        
        unlink(socket_name_.c_str());
        int serverFD = socket(AF_UNIX, SOCK_STREAM, 0);
        if (serverFD == -1) {
            return -1;
        }
        sockaddr_un serverAddr;
        serverAddr.sun_family = AF_UNIX;
        strncpy(serverAddr.sun_path, socket_name_.c_str(), sizeof(serverAddr.sun_path) - 1);
        if (bind(serverFD, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
            return -1;
        }
        
        if (listen(serverFD, 1) == -1) {
            return -1;
        };
        
        sockaddr_un client;
        socklen_t clientSize = sizeof(client);
        clientFD_ = accept(serverFD, (sockaddr*)&client, &clientSize);
        return 0;
    }

    ~Socket() {
        unlink(socket_name_.c_str());
    }

    int SendBytes(std::vector<char>& bytes) {
        // First send the size
        uint64_t size = bytes.size();
        int n = send(clientFD_, &size, 8, 0);
        if (n != 8) {
            std::cerr << "Error reading size" << std::endl;
            return -1;
        }

        // Now write the message
        n = send(clientFD_, bytes.data(), bytes.size(), 0);
        if (n != bytes.size()) {
            std::cerr << "Error writing bytes" << std::endl;
            return -1;
        }
        return 0;
    }

    int SendUints(std::vector<uint32_t>& data) {
        vector<char> bytes;
        bytes.insert(bytes.end(), (char*)&(*data.begin()), (char*)&(*data.end()));
        return this->SendBytes(bytes);
    }

    std::vector<char> RecvBytes() {
        std::vector<char> result;

        // First receive the size of the message
        char buf[4096];
        int n = recv(clientFD_, buf, 8, 0);
        if (n != 8) {
            std::cerr << "Error reading size" << std::endl;
            return result;
        }
        uint64_t size = *(uint64_t*)(buf);
        result.reserve(size);
        std::cout << "Receiving " << size << " bytes" << std::endl;

        // Then receive all of the bytes
        while (size > 0) {
            n = recv(clientFD_, buf, min((int)size, 4096), 0);
            if (n == 0 || n == -1) {
                std::cerr << "Error reading msg" << std::endl;
                return result;
            }
            result.insert(result.end(), buf, buf + n);
            size -= n;
        }

        return result;
    }

    std::vector<uint32_t> RecvUints() {
        std::vector<uint32_t> result;
        
        auto result_bytes = this->RecvBytes();
        if (result_bytes.size() % 4 != 0) {
            std::cerr << "Invalid array size" << std::endl;
            return result;
        }

        result.insert(result.end(), (uint32_t*)&(*result_bytes.begin()), (uint32_t*)&(*result_bytes.end()));
        return result;
    }


private:
    int clientFD_;
    std::string socket_name_;
};

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <string>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <ctime>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <vector>

const int MAX_CLIENTS = 100;

class Client {
private:
    int socket_idx;
    const int timeout;
    const int PORT;
    time_t last_time_send;
    std::string name;

    int SendRaw(const char *data, int data_size) {
        int bytes_sent;
        int total = 0;
        while (data_size > 0) {
            bytes_sent = send(socket_idx, data + total, data_size, MSG_NOSIGNAL);
            if (bytes_sent <= 0)
                if (errno == EPIPE) {
                    return 0;
                } else {
                    return -1;
                }

            total += bytes_sent;
            data_size -= bytes_sent;
        }

        return 1;
    }

    int SendAll(const std::string &data) {
        ulong data_size = (data.size());
        int total_size = sizeof(ulong) + data.size();

        char *buffer = new char[total_size];
        memcpy(buffer, &data_size, sizeof(ulong));
        memcpy(buffer + sizeof(ulong), data.c_str(), data.size());

        return SendRaw(buffer, total_size);
    }

public:
    Client(const char *name, const int port, const int timeout) : name(name), PORT(port), timeout(timeout) {
    }

    ~Client() {
        close(socket_idx);
    }

    void Connect() {
        struct sockaddr_in server_address;

        socket_idx = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_idx < 0) {
            std::cerr << "socket failed\n";
            exit(EXIT_FAILURE);
        }
        upd_last_send_time();

        int flags = fcntl(socket_idx, F_GETFL, 0);
        if (flags == -1) {
            std::cerr << "fcntl error\n";
            exit(EXIT_FAILURE);
        }

        if (fcntl(socket_idx, F_SETFL, flags | O_NONBLOCK) < 0) {
            std::cerr << "fcntl error\n";
            exit(EXIT_FAILURE);
        }

        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(PORT);
        server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

        while (connect(socket_idx, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            if (errno == EINPROGRESS) {
                fd_set write_fds;
                FD_ZERO(&write_fds);
                FD_SET(socket_idx, &write_fds);

                if (select(socket_idx + 1, NULL, &write_fds, NULL, NULL) < 0) {
                    std::cerr << "select error\n";
                    exit(EXIT_FAILURE);
                }

                if (FD_ISSET(socket_idx, &write_fds)) {
                    int error = 0;
                    socklen_t error_len = sizeof(error);
                    getsockopt(socket_idx, SOL_SOCKET, SO_ERROR, &error, &error_len);
                    if (error != 0) {
                        std::cerr << "connect error\n";
                        exit(EXIT_FAILURE);
                    }
                    break;
                } else {
                    std::cerr << "connection timeout\n";
                    exit(EXIT_FAILURE);
                }
            } else {
                std::cerr << "connection error\n";
                exit(EXIT_FAILURE);
            }
        }
    }

    void upd_last_send_time() {
        last_time_send = time(nullptr);
    }

    bool timeout_reached() {
        time_t current_time = time(nullptr);
        return (current_time - last_time_send) >= timeout;
    }

    void SendIfTimeout() {
        if (timeout_reached()) {
            auto now = std::chrono::system_clock::now();
            std::time_t now_time = std::chrono::system_clock::to_time_t(now);
            std::tm *tm_now = std::localtime(&now_time);

            auto duration = now.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
            auto seconds_in_millis = seconds * 1000;
            std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());
            std::ostringstream oss;
            oss << "[" << std::put_time(tm_now, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3)
                << ms.count() % 1000 << "] ";

            std::string message = oss.str() + name;
            if (SendAll(message) < 0) {
                std::cerr << "send error\n";
                exit(EXIT_FAILURE);
            }
            upd_last_send_time();
        }
    }
};

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "Arguments must be 3\n";
        return EXIT_FAILURE;
    }

    Client client(argv[1], std::atoi(argv[2]), std::atoi(argv[3]));
    client.Connect();

    while (true) {
        client.SendIfTimeout();
    }

    return 0;
}

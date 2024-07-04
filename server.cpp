#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define DATA_SIZE 1024

class Server {
   private:
    int server_fd;
    std::vector<int> client_fds;
    std::vector<std::string> buffers_for_recv;
    struct sockaddr_in server_address;

    void acceptClient() {
        socklen_t addr_len = sizeof(sockaddr_in);
        sockaddr_in client_address;
        int new_socket = accept(server_fd, (sockaddr *)&client_address, &addr_len);
        if (new_socket < 0) {
            perror("accept error");
            exit(EXIT_FAILURE);
        }

        int flags = fcntl(new_socket, F_GETFL, 0);
        if (flags == -1) {
            std::cerr << "fcntl error\n";
            exit(EXIT_FAILURE);
        }

        if (fcntl(new_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
            std::cerr << "fcntl error\n";
            exit(EXIT_FAILURE);
        }

        client_fds.push_back(new_socket);
        buffers_for_recv.emplace_back();
        std::cout << "new client connected, socket " << client_fds.size() - 1 << '\n';
    }

    int recvRaw(int client_socket_idx) {
        int bytes_recv;
        constexpr int buffer_size = 1024;
        char buffer[buffer_size];

        while (true) {
            bytes_recv = recv(client_fds[client_socket_idx], buffer, buffer_size, MSG_NOSIGNAL);
            if (bytes_recv <= 0) {
                if (bytes_recv == 0 || errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN || errno == ECONNRESET)
                    return 0;
                else
                    return -1;
            } else {
                buffers_for_recv[client_socket_idx].append(buffer, bytes_recv);
                if (bytes_recv < buffer_size) {
                    return buffers_for_recv[client_socket_idx].size();
                }
            }
        }

        return 1;
    }

   public:
    Server(int port) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }

        int flags = fcntl(server_fd, F_GETFL, 0);
        if (flags == -1) {
            std::cerr << "fcntl error\n";
            exit(EXIT_FAILURE);
        }

        if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            std::cerr << "fcntl error\n";
            exit(EXIT_FAILURE);
        }

        int yes = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) < 0) {
            std::cerr << "setsockopt error\n";
            exit(EXIT_FAILURE);
        }

        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(port);
        server_address.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            std::cerr << "bind failed\n";
            exit(EXIT_FAILURE);
        }

        if (listen(server_fd, 1) < 0) {
            std::cerr << "listen failed\n";
            exit(EXIT_FAILURE);
        }
    }

    ~Server() {
        close(server_fd);
        for (int fd : client_fds) {
            close(fd);
        }
    }

    void run() {
        while (true) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_fd, &read_fds);
            int max_sd = server_fd;

            for (int fd : client_fds) {
                FD_SET(fd, &read_fds);
                if (fd > max_sd) {
                    max_sd = fd;
                }
            }

            int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

            if (activity < 0) {
                std::cerr << "select error\n";
                exit(EXIT_FAILURE);
            }

            if (FD_ISSET(server_fd, &read_fds)) {
                acceptClient();
            }

            for (int i = 0; i < client_fds.size(); ++i) {
                int sd = client_fds[i];
                if (FD_ISSET(sd, &read_fds)) {
                    int val_read = RecvAll(i);
                    if (val_read <= 0) {
                        if (val_read == 0) {
                            std::cout << "Disconnected, socket:" << i << '\n';
                            close(sd);
                            client_fds.erase(client_fds.begin() + i);
                            buffers_for_recv.erase(buffers_for_recv.begin() + i);
                        } else {
                            std::cerr << "recv error\n";
                            close(sd);
                            client_fds.erase(client_fds.begin() + i);
                            buffers_for_recv.erase(buffers_for_recv.begin() + i);
                        }
                        break;
                    }
                }
            }
        }
    }

	int RecvAll(int client_socket_idx) {
		ulong data_size = 0;
		int result;

		result = recvRaw(client_socket_idx);
		if (result <= 0) {
			return result;
		}
		auto data_size_1 = (std::size_t *)(buffers_for_recv[client_socket_idx].c_str());

		std::fstream file("log.txt", std::ios_base::app);

		while (buffers_for_recv[client_socket_idx].size() > sizeof(ulong)) {
			if (buffers_for_recv[client_socket_idx].size() >= sizeof(ulong) + (*data_size_1)) {
				if (file.is_open()) {
					file << std::string(buffers_for_recv[client_socket_idx].c_str() + sizeof(ulong), (*data_size_1)) << '\n';
				}
				file.close();

				std::cout << std::string(buffers_for_recv[client_socket_idx].c_str() + sizeof(ulong), (*data_size_1)) << '\n';
				buffers_for_recv[client_socket_idx].erase(0, sizeof(ulong) + (*data_size_1));
				data_size_1 = (std::size_t *)(buffers_for_recv[client_socket_idx].c_str());
			} else {
				return 0;
			}
		}
		return result;
	}

};

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Arguments must be 1\n";
        return EXIT_FAILURE;
    }

    Server server(std::atoi(argv[1]));
    server.run();

    return 0;
}

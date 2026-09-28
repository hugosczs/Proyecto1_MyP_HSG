#include <iostream>
#include <string>
#include <thread>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "json.hpp"


using json = nlohmann::json;

void recibir_mensajes(int conexion_cliente) {
    char buffer[1024];
    std::string mensaje_actual;

    while (true) {
        ssize_t bytes_leidos = read(conexion_cliente, buffer, sizeof(buffer) -1);
        if (bytes_leidos <= 0) {
            std::cout << "\nDesconectando del servidor...\n";
            exit(0);
        }

        buffer[bytes_leidos] = '\0';
        mensaje_actual += buffer;

        size_t posicion;
        while((posicion = mensaje_actual.find('\n')) != std::string::npos) {
            std::string linea = mensaje_actual.substr(0, posicion);
            mensaje_actual.erase(0, posicion + 1);

            if (linea.empty()) continue;

            try {
                json mensaje = json::parse(linea);
                std::cout << "\n[Servidor]: " << mensaje.dump(4) << "\n> ";
                std::cout.flush();
            } catch (const std::exception& e) {}
        }
    }
}

int main(int argc, char* argv[]) {
    std::string ip_servidor = "192.168.1.68";
    if (argc > 1) {
        ip_servidor = argv[1];
    }

    int enchufe = socket(AF_INET, SOCK_STREAM, 0);
    if(enchufe < 0) {
        std::cerr << "Error al crear el socket del cliente...\n";
        return 1;
    }

    struct sockaddr_in direccion_servidor{};
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_port = htons(8080);

    if(inet_pton(AF_INET, ip_servidor.c_str(), &direccion_servidor.sin_addr) <= 0) {
        std::cerr << "Dirección IP no válida...\n";
        return 1;
    }

    if (connect(enchufe, (struct sockaddr*)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        std::cerr << "Fallo en la conexión...\n";
        return 1;
    }

    std::cout << "Conectado al servidor " << ip_servidor << "\n";
    std::cout << "¡¡Por favor, escribe los mensajes en JSON!!\n";

    std::thread hilo_receptor(recibir_mensajes, enchufe);
    hilo_receptor.detach();

    std::string entrada;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, entrada);

        if (!entrada.empty()) {
            entrada += "\n";
            write(enchufe, entrada.c_str(), entrada.length());
        }
    }
    close(enchufe);
    return 0;
}
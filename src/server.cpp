#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "json.hpp"

using json = nlohmann::json;

struct InfoCliente {
    int conexion_cliente;
    std::string username;
    std::string status = "ACTIVE";
};

struct InfoSala {
    std::unordered_set<std::string> miembros;
    std::unordered_set<std::string> invitados;
};

bool nombre_sala_valido(const std::string& nombre_sala) {
    return nombre_sala.length() <= 16;
}

std::unordered_map<std::string, InfoCliente> clientes;
std::mutex mutex_clientes;

std::unordered_map<std::string, InfoSala> salas;
std::mutex mutex_salas;

void enviar_mensaje(int conexion_cliente, const json& mensaje) {
    std::string datos = mensaje.dump() + "\n";
    write(conexion_cliente, datos.c_str(), datos.length());
}

bool leer_linea(int conexion_cliente, std::string& linea, bool& mensaje_demasiado_grande) {
    linea.clear();
    mensaje_demasiado_grande = false;

    char caracter;
    ssize_t bytes_leidos;

    while ((bytes_leidos = read(conexion_cliente, &caracter, 1)) > 0) {
        if(caracter == '\n') return true;

        if(caracter != '\0') {
            linea += caracter;

            if(linea.length() > 1024 * 1024) {
                mensaje_demasiado_grande = true;
                return false;
            }
        }
    }
    return !linea.empty();
}

void desconectar_cliente(int conexion_cliente, const std::string& username, bool estaba_identificado) {
    if(estaba_identificado) {
        {
            std::lock_guard seguro_salas(mutex_salas);
            for(auto it = salas.begin(); it != salas.end(); ) {
                if(it->second.miembros.erase(username)) {
                    json mensaje_salida = {{"type", "LEFT_ROOM"}, {"roomname", it->first}, {"username", username}};
                    std::lock_guard seguro_clientes(mutex_clientes);
                    for(const auto& miembro : it->second.miembros) {
                        enviar_mensaje(clientes[miembro].conexion_cliente, mensaje_salida);
                    }
                }

                if(it->second.miembros.empty()) {
                    it = salas.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::lock_guard seguro_clientes(mutex_clientes);
        clientes.erase(username);

        json mensaje_desconexion = {{"type", "DISCONNECTED"}, {"username", username}};
        for(const auto& [usuario, info] : clientes) {
            enviar_mensaje(info.conexion_cliente, mensaje_desconexion);
        }
    }
    close(conexion_cliente);
}

void manejar_cliente(int conexion_cliente) {
    std::string cadena_buffer;
    bool esta_identificado = false;
    std::string usuario_actual;

    bool mensaje_demasiado_grande = false;

    while(leer_linea(conexion_cliente, cadena_buffer, mensaje_demasiado_grande)) {
        if(cadena_buffer.empty()) continue;

        try{
            json mensaje = json::parse(cadena_buffer);
            std::string tipo = mensaje.at("type");

            if(!esta_identificado) {
                if(tipo == "IDENTIFY") {
                    std::string usuario_solicitado = mensaje.at("username");
                    if(usuario_solicitado.length() > 8) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "IDENTIFY"},
                            {"result", "INVALID_USERNAME"}, {"extra", "USERNAME_TOO_LONG"}
                        });
                        break;
                    }

                    std::lock_guard seguro(mutex_clientes);
                    if(clientes.count(usuario_solicitado)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "IDENTIFY"},
                            {"result", "USER_ALREADY_EXISTS"}, {"extra", usuario_solicitado}
                        });
                    } else {
                        esta_identificado = true;
                        usuario_actual = usuario_solicitado;
                        clientes[usuario_actual] = {conexion_cliente, usuario_actual, "ACTIVE"};

                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "IDENTIFY"},
                            {"result", "SUCCESS"}, {"extra", usuario_actual}
                        });

                        json mensaje_nuevo_usuario = {{"type", "NEW_USER"}, {"username", usuario_actual}};
                        for(const auto& [usuario, info] : clientes) {
                            if (usuario != usuario_actual) enviar_mensaje(info.conexion_cliente, mensaje_nuevo_usuario);
                        }
                    }
                } else {
                    enviar_mensaje(conexion_cliente, {{"type", "RESPONSE"}, {"operation", "INVALID"}, {"result", "NOT_IDENTIFIED"}});
                    break;
                }
            } else {
                if (tipo == "STATUS") {
                    std::string nuevo_estado = mensaje.at("status");
                    if(nuevo_estado != "ACTIVE" && nuevo_estado != "AWAY" && nuevo_estado != "BUSY") throw std::runtime_error("Invalid status");

                    std::lock_guard seguro(mutex_clientes);
                    if(clientes[usuario_actual].status != nuevo_estado) {
                        clientes[usuario_actual].status = nuevo_estado;
                        json mensaje_estado = {{"type", "NEW_STATUS"}, {"username", usuario_actual}, {"status", nuevo_estado}};
                        for(const auto& [usuario, info] : clientes) {
                            if (usuario != usuario_actual) enviar_mensaje(info.conexion_cliente, mensaje_estado);
                        }
                    }
                }
                else if (tipo == "USERS") {
                    json lista_usuarios = {{"type", "USER_LIST"}, {"users", json::object()}};
                    std::lock_guard seguro(mutex_clientes);
                    for(const auto& [usuario, info] : clientes) {
                        lista_usuarios["users"][usuario] = info.status;
                    }
                    enviar_mensaje(conexion_cliente, lista_usuarios);
                }
                else if (tipo == "TEXT") {
                    std::string destinatario = mensaje.at("username");
                    std::string texto = mensaje.at("text");
                    std::lock_guard seguro(mutex_clientes);
                    if(clientes.count(destinatario)) {
                        enviar_mensaje(clientes[destinatario].conexion_cliente, {
                            {"type", "TEXT_FROM"}, {"username", usuario_actual}, {"text", texto}
                        });
                    } else {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "TEXT"},
                            {"result", "NO_SUCH_USER"}, {"extra", destinatario}
                        });
                    }
                }
                else if (tipo == "PUBLIC_TEXT") {
                    std::string texto = mensaje.at("text");
                    std::lock_guard seguro(mutex_clientes);
                    json mensaje_publico = {{"type", "PUBLIC_TEXT_FROM"}, {"username", usuario_actual}, {"text", texto}};
                    for (const auto& [usuario, info] : clientes) {
                        if (usuario != usuario_actual) enviar_mensaje(info.conexion_cliente, mensaje_publico);
                    }
                }
                else if (tipo == "NEW_ROOM") {
                    std::string nombre_sala = mensaje.at("roomname");

                    if (!nombre_sala_valido(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "NEW_ROOM"},
                            {"result", "INVALID_ROOMNAME"}, {"extra", "ROOMNAME_TOO_LONG"}
                        });
                        break;
                    }

                    std::lock_guard seguro(mutex_salas);

                    if(salas.count(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "NEW_ROOM"},
                            {"result", "ROOM_ALREADY_EXISTS"}, {"extra", nombre_sala}
                        });
                    } else {
                        salas[nombre_sala].miembros.insert(usuario_actual);
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "NEW_ROOM"},
                            {"result", "SUCCESS"}, {"extra", nombre_sala}
                        });
                    }
                }
                else if (tipo == "INVITE"){
                    std::string nombre_sala = mensaje.at("roomname");

                    if (!nombre_sala_valido(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "INVITE"},
                            {"result", "INVALID_ROOMNAME"}, {"extra", "ROOMNAME_TOO_LONG"}
                        });
                        break;
                    }

                    auto lista_invitados = mensaje.at("usernames");

                    std::lock_guard seguro_salas(mutex_salas);

                    if (!salas.count(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "INVITE"},
                            {"result", "NO_SUCH_ROOM"}, {"extra", nombre_sala}
                        });
                    } else if (!salas[nombre_sala].miembros.count(usuario_actual)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "INVITE"},
                            {"result", "NOT_MEMBER"}, {"extra", nombre_sala}
                        });

                    }
                    else {
                        for (const auto& nombre_usuario : lista_invitados) {
                            std::string usuario = nombre_usuario;

                            std::lock_guard seguro_clientes(mutex_clientes);
                            if (!clientes.count(usuario)){
                                enviar_mensaje(conexion_cliente, {
                                    {"type", "RESPONSE"}, {"operation", "INVITE"},
                                    {"result", "NO_SUCH_USER"}, {"extra", usuario}
                                });
                                break;
                            }
                            if (salas[nombre_sala].miembros.count(usuario) ||
                                salas[nombre_sala].invitados.count(usuario)) {
                                continue;
                            }
                            salas[nombre_sala].invitados.insert(usuario);

                            enviar_mensaje(clientes[usuario].conexion_cliente, {
                                {"type", "INVITATION"}, {"username", usuario_actual},
                                {"roomname", nombre_sala}
                            });
                        }
                    }
                }
                else if (tipo == "JOIN_ROOM") {
                    std::string nombre_sala = mensaje.at("roomname");

                    if (!nombre_sala_valido(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "JOIN_ROOM"},
                            {"result", "INVALID_ROOMNAME"}, {"extra", "ROOMNAME_TOO_LONG"}
                        });
                        break;
                    }


                    std::lock_guard seguro(mutex_salas);

                    if (!salas.count(nombre_sala)) {
                        enviar_mensaje(conexion_cliente , {
                            {"type", "RESPONSE"}, {"operation", "JOIN_ROOM"},
                            {"result", "NO_SUCH_ROOM"}, {"extra", nombre_sala}
                        });
                    }
                    else if (!salas[nombre_sala].invitados.count(usuario_actual)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "JOIN_ROOM"},
                            {"result", "NOT_INVITED"}, {"extra", nombre_sala}
                        });
                    }
                    else {
                        salas[nombre_sala].invitados.erase(usuario_actual);
                        salas[nombre_sala].miembros.insert(usuario_actual);

                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "JOIN_ROOM"},
                            {"result", "SUCCESS"}, {"extra", nombre_sala}
                        });

                        json mensaje_union = {
                            {"type", "JOINED_ROOM"}, {"username", usuario_actual},
                            {"roomname", nombre_sala}
                        };
                        std::lock_guard seguro_clientes(mutex_clientes);

                        for (const auto& miembro : salas[nombre_sala].miembros) {
                            if (miembro != usuario_actual) {
                                enviar_mensaje(clientes[miembro].conexion_cliente, mensaje_union);
                            }
                        }
                    }
                }
                else if (tipo == "ROOM_USERS") {
                    std::string nombre_sala = mensaje.at("roomname");

                    if (!nombre_sala_valido(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "ROOM_USERS"},
                            {"result", "INVALID_ROOMNAME"}, {"extra", "ROOMNAME_TOO_LONG"}
                        });
                        break;
                    }
                    std::lock_guard seguro(mutex_salas);

                    if (!salas.count(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "ROOM_USERS"},
                            {"result", "NO_SUCH_ROOM"}, {"extra", nombre_sala}
                        });
                    } else if (!salas[nombre_sala].miembros.count(usuario_actual)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "ROOM_USERS"},
                            {"result", "NOT_JOINED"}, {"extra", nombre_sala}
                        });
                    }
                    else {
                        json lista_usuarios = {
                            {"type", "ROOM_USER_LIST"}, {"roomname", nombre_sala}, 
                            {"users", json::object()}
                        };
                        std::lock_guard seguro_clientes(mutex_clientes);

                        for (const auto& miembro : salas[nombre_sala].miembros) {
                            lista_usuarios["users"][miembro] = clientes[miembro].status;
                        }
                        enviar_mensaje(conexion_cliente, lista_usuarios);
                    }
                }
                else if (tipo == "ROOM_TEXT") {
                    std::string nombre_sala = mensaje.at("roomname");
                    std::string texto = mensaje.at("text");

                    if (!nombre_sala_valido(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "ROOM_TEXT"},
                            {"result", "INVALID_ROOMNAME"}, {"extra", "ROOMNAME_TOO_LONG"}
                        });
                        break;
                    }

                    std::lock_guard seguro_salas(mutex_salas);

                    if(!salas.count(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "ROOM_TEXT"},
                            {"result", "NO_SUCH_ROOM"}, {"extra", nombre_sala}
                        });
                    }
                    else if (!salas[nombre_sala].miembros.count(usuario_actual)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "ROOM_TEXT"},
                            {"result", "NOT_JOINED"}, {"extra", nombre_sala}
                        });
                    }
                    else {
                        json mensaje_sala = {
                            {"type", "ROOM_TEXT_FROM"}, {"username", usuario_actual},
                            {"roomname", nombre_sala}, {"text", texto}
                        };

                        std::lock_guard seguro_clientes(mutex_clientes);
                        for (const auto& miembro : salas[nombre_sala].miembros) {
                            if (miembro != usuario_actual) {
                                enviar_mensaje(clientes[miembro].conexion_cliente, mensaje_sala);
                            }
                        }
                    }
                }
                else if (tipo == "LEAVE_ROOM") {
                    std::string nombre_sala = mensaje.at("roomname");

                    if (!nombre_sala_valido(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "LEAVE_ROOM"},
                            {"result", "INVALID_ROOMNAME"}, {"extra", "ROOMNAME_TOO_LONG"}
                        });
                        break;
                    }

                    std::lock_guard seguro_salas(mutex_salas);

                    if (!salas.count(nombre_sala)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "LEAVE_ROOM"},
                            {"result", "NO_SUCH_ROOM"}, {"extra", nombre_sala}
                        });
                    }
                    else if (!salas[nombre_sala].miembros.count(usuario_actual)) {
                        enviar_mensaje(conexion_cliente, {
                            {"type", "RESPONSE"}, {"operation", "LEAVE_ROOM"},
                            {"result", "NOT_JOINED"}, {"extra", nombre_sala}
                        });
                    }
                    else {
                        salas[nombre_sala].miembros.erase(usuario_actual);

                        json mensaje_salida = {
                            {"type", "LEFT_ROOM"}, {"username", usuario_actual},
                            {"roomname", nombre_sala}
                        };

                        std::lock_guard seguro_clientes(mutex_clientes);

                        for (const auto& miembro : salas[nombre_sala].miembros) {
                            enviar_mensaje(clientes[miembro].conexion_cliente, mensaje_salida);
                        }
                    }
                }
                else if (tipo == "DISCONNECT") {
                    break;
                }
                else {
                    enviar_mensaje(conexion_cliente, {
                        {"type", "RESPONSE"}, {"operation", "INVALID"},
                        {"result", "INVALID"}
                    });
                    break;
                }
            }
        } catch (const std::exception& e) {
            enviar_mensaje(conexion_cliente, {
                {"type", "RESPONSE"}, {"operation", "INVALID"}, {"result", "INVALID"}
            });
            break;
        }
    }

    if (mensaje_demasiado_grande) {
        enviar_mensaje(conexion_cliente, {
            {"type", "RESPONSE"}, {"operation", "INVALID"},
            {"result", "INVALID"}
        });
    }

    desconectar_cliente(conexion_cliente, usuario_actual, esta_identificado);
}

int main() {
    int socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        std::cerr << "Error al crear el socket\n";
        return 1;
    }

    int opciones = 1;
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opciones, sizeof(opciones));

    struct sockaddr_in direccion_servidor{};
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_addr.s_addr = INADDR_ANY;
    direccion_servidor.sin_port = htons(8080);

    if (bind(socket_servidor, (struct sockaddr*)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        std::cerr << "Error en bind\n";
        return 1;
    }

    if(listen(socket_servidor, 10) < 0) {
        std::cerr << "Error en listen\n";
        return 1;
    }

    std::cout << "El servidor está escuchando en el puerto 8080...\n";

    while (true) {
        struct sockaddr_in direccion_cliente;
        socklen_t longitud_cliente = sizeof(direccion_cliente);
        int socket_cliente = accept(socket_servidor, (struct sockaddr*)&direccion_cliente, &longitud_cliente);

        if(socket_cliente >= 0) {
            std::cout << "Nuevo cliente conectado.\n";
            std::thread(manejar_cliente, socket_cliente).detach();
        }
    }
    return 0;
}
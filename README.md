Proyecto 1 — Sistema cliente - Servidor
Hugo Sánchez García (319251054)

Requisitos:
Para compilar se requiere:

* C++17 o posterior.
* CMake 3.10 o posterior.

Dependencias:
El proyecto usa JSON for Modern C++.

Compilación:
Desde el directorio raíz del proyecto, ejecutar:

* cmake -S . -B build
* cmake --build build

Y se generan los ejecutables:

* build/server
* build/client

Ejecución:

1. Iniciar el servidor
Desde el directorio raíz:
./build/server

El servidor comenzará a escuchar conexiones TCP en el puerto 8080.

Se mostrará un mensaje similar a:

El servidor está escuchando en el puerto 8080...

2. Iniciar un cliente

En otra terminal:
./build/client

El cliente se conectará a la dirección configurada en client.cpp.

También es posible especificar explícitamente la dirección IP del servidor como argumento:

./build/client <IP_DEL_SERVIDOR>

Por ejemplo:

./build/client 192.168.1.68

Esto permite ejecutar el servidor y los clientes en diferentes equipos dentro de una misma red.

Prueba local

Para ejecutar servidor y cliente en la misma computadora, el cliente puede configurarse para utilizar:

127.0.0.1

Consiste en abrir dos terminales.

En la primera:
./build/server

En la segunda:
./build/client 127.0.0.1

Para probar entre varios clientes, se pueden ejecutar varias instancias del cliente en varias terminales.


Uso:
Una vez conectado el cliente, las operaciones se introducen como objetos JSON.

Identificación:
{"type":"IDENTIFY","username":"usuario"}

* El nombre de usuario puede tener como máximo 8 caracteres.

Cambio de estado:
{"type":"STATUS","status":"AWAY"}

Los estados disponibles son:
ACTIVE
AWAY
BUSY

Consultar usuarios:
{"type":"USERS"}

Mensaje privado:
{"type":"TEXT","username":"usuario2","text":"Hola"}

Mensaje público:
{"type":"PUBLIC_TEXT","text":"Hola a todos"}

Crear una sala:
{"type":"NEW_ROOM","roomname":"sala1"}

* El nombre de la sala puede tener como máximo 16 caracteres.

Invitar usuarios:
{
    "type":"INVITE",
    "roomname":"sala1",
    "usernames":["usuario2","usuario3"]
}

Entrar a una sala:
* El usuario debe haber recibido previamente una invitación.

{"type":"JOIN_ROOM","roomname":"sala1"}

Consultar los integrantes de una sala:
{"type":"ROOM_USERS","roomname":"sala1"}

Enviar un mensaje a una sala:
{
    "type":"ROOM_TEXT",
    "roomname":"sala1",
    "text":"Hola a todos"
}

Salir de una sala:
{"type":"LEAVE_ROOM","roomname":"sala1"}

Desconectarse:
{"type":"DISCONNECT"}


* Cada mensaje termina con un salto de línea (\n).

* Los mensajes que superan el límite máximo de tamaño son rechazados y provocan la terminación de la conexión.

Manejo de errores:
* Los mensajes que no cumplen con el formato esperado, contienen información incompleta o utilizan operaciones desconocidas son tratados como mensajes inválidos.
* Los clientes que todavía no se han identificado solamente pueden utilizar la operación IDENTIFY.

Archivos del proyecto:

src/server.cpp
Contiene la implementación del servidor.

src/client.cpp
Contiene la implementación del cliente.
# Control de motores por BLE

## Cableado

| Lado | IN1 | IN2 | Enable (PWM) |
|---|---|---|---|
| Izquierdo | GPIO32 | GPIO33 | GPIO22 |
| Derecho | GPIO25 | GPIO26 | GPIO23 |

Adelante: IN1 = 0 e IN2 = 1. Atras: IN1 = 1 e IN2 = 0.
Conecta los enable a ENA/ENB del L298N, retira sus jumpers y comparte GND
entre ESP32 y L298N. El PWM usa 20 kHz y 10 bits.

## Comandos

Dispositivo `agbot`, servicio `0xFFF0`, escritura con respuesta en `0xFFF1`.
Cada escritura contiene **un byte binario**, con el estado completo de los botones:

| Bit | Boton | Valor |
|---|---|---|
| 0 | Adelante | `0x01` |
| 1 | Atras | `0x02` |
| 2 | Izquierda | `0x04` |
| 3 | Derecha | `0x08` |

Ejemplos: `0x05` adelante/izquierda, `0x09` adelante/derecha,
`0x06` atras/izquierda, `0x0A` atras/derecha, `0x00` parada.
Los caracteres ASCII `0` y `1` ya no son comandos validos.

Enviar al pulsar/soltar y repetir **cada 100 ms** mientras se conduce.
Cada comando sustituye al anterior; no son eventos individuales de botones.
Bits desconocidos, botones opuestos o una longitud distinta de un byte solicitan parada.
Girar sin avanzar/retroceder no mueve el vehiculo.

## Movimiento

- Mantener adelante/atras aumenta el PWM base de 0 a 100 % en 3 segundos.
- El lado interior recibe el 50 % del PWM base; los cambios de giro se suavizan.
- Al retroceder, izquierda reduce el lado derecho y derecha el izquierdo.
- Soltar la aceleracion apaga ambos lados y reinicia la rampa.
- Cada inicio o inversion espera 100 ms con los motores deshabilitados.
- La desconexion, un error BLE o 500 ms sin comandos solicitan parada.
- El ciclo de control es de 20 ms; las paradas se aplican en la siguiente iteracion.
- Un fallo de GPIO/PWM bloquea el movimiento hasta reiniciar.

PWM porcentual no equivale a velocidad medida: no se utilizan encoders.
La parada deshabilita ENA/ENB; no aplica frenado activo.

## Estado

`0xFFF2` permite lectura y notificaciones. El firmware publica cada 100 ms
un paquete de **5 bytes**:

| Posicion | Contenido |
|---|---|
| 0 | Botones aceptados |
| 1 | PWM base, 0..100 % |
| 2 | PWM izquierdo, 0..100 % |
| 3 | PWM derecho, 0..100 % |
| 4 | Flags: bit 0 timeout, bit 1 pausa, bit 2 fallo de salida |

Suscribirse a las notificaciones y leer una vez al conectar.

## Ubicacion del codigo

- `main/agbot.c`: recepcion BLE y publicacion del estado.
- `main/motor_control.c`: pines, LEDC, cola de comandos y tarea de control.
- `main/motor_motion.c`: aceleracion, giros, inversion y timeout sin hardware.
- `main/motor_motion.h`: tiempos y porcentaje de giro ajustables.

## Verificacion

Pruebas de logica en el equipo de desarrollo:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain main/motor_motion.c tests/motor_motion_test.c -o /tmp/opencode/motor_motion_test
/tmp/opencode/motor_motion_test
```

Con ESP-IDF inicializado: `idf.py reconfigure` y `idf.py build`.
En placa, empezar con las ruedas levantadas y comprobar ambos sentidos,
giros, liberacion de botones, inversion y perdida de comunicacion.

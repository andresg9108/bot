# bot

Firmware **agbot** para controlar dos lados de un robot con un **ESP32 y un módulo L298N**, mediante Bluetooth Low Energy (BLE). El ESP32 recibe los botones, calcula la aceleración y los giros, y controla el sentido y el PWM de los motores.

> **Aplicación de control: [botcontrol](https://github.com/andresg9108/botcontrol).**
> La aplicación envía los datos de control al ESP32 por Bluetooth BLE y recibe su estado. Su instalación y ejecución se documentan en ese repositorio; este contiene el firmware.

## Documentación

- Esta guía: hardware, conexiones, compilación y puesta en marcha.
- [Control de motores por BLE](BLE_CONTROL.md): protocolo binario, ejemplos, telemetría y detalles del movimiento.
- [Código principal](main/agbot.c), [control de salidas](main/motor_control.c) y [lógica de movimiento](main/motor_motion.c).

## Hardware necesario

- Una placa **ESP32** con BLE y con los GPIO de la tabla disponibles. La asignación actual corresponde al ESP32 clásico; no asumir que sirve sin cambios para otras variantes.
- Un **módulo L298N** para los dos canales de motores.
- Motores de corriente continua para los lados izquierdo y derecho, con alimentación adecuada al conjunto motor/driver.
- Cableado, alimentación para la placa y cable USB para programarla.
- Un teléfono con BLE para utilizar botcontrol.

El repositorio no especifica el modelo de placa, los motores, la batería ni sus tensiones o corrientes. Comprueba sus especificaciones y las del módulo antes de alimentar el montaje; no alimentes los motores desde los GPIO del ESP32.

### Pines y conexiones

Asignaciones verificadas en [motor_control.c](main/motor_control.c) y correspondencia con el L298N descrita en [agbot.c](main/agbot.c). Los números son **GPIO**, no posiciones físicas del conector de una placa.

| GPIO del ESP32 | Entrada del L298N | Señal | Lado |
|---|---|---|---|
| GPIO32 | IN1 | Sentido, primera entrada | Izquierdo |
| GPIO33 | IN2 | Sentido, segunda entrada | Izquierdo |
| GPIO22 | ENA | Habilitación y PWM, canal LEDC 0 | Izquierdo |
| GPIO25 | IN3 | Sentido, primera entrada | Derecho |
| GPIO26 | IN4 | Sentido, segunda entrada | Derecho |
| GPIO23 | ENB | Habilitación y PWM, canal LEDC 1 | Derecho |
| GND | GND | Referencia de masa común | Ambos |

- **Retira los jumpers de ENA y ENB** para que GPIO22 y GPIO23 controlen esas entradas mediante PWM. Esta indicación no se refiere al jumper del regulador de alimentación del módulo.
- Comparte GND entre ESP32 y L298N. Verifica por separado el cableado de alimentación según tu módulo; el firmware no define una conexión de alimentación de 5 V, una batería ni la polaridad física de los motores.
- El PWM es de **20 kHz y 10 bits**, con valores de 0 a 1023.
- En avance, el firmware pone IN1/IN3 a 0 e IN2/IN4 a 1; en retroceso invierte esos niveles. El sentido físico depende de la conexión y orientación de los motores: compruébalo con las ruedas levantadas.
- La parada deshabilita ENA/ENB y pone las cuatro entradas de sentido a 0; **no aplica frenado activo**.
- BLE usa la radio interna del ESP32: no necesita un módulo Bluetooth externo ni GPIO adicionales.

## Requisitos y compilación

### Entorno y dependencia BLE

El proyecto usa **ESP-IDF**, no Arduino ni CMake independiente. Inicializa el entorno de ESP-IDF en la terminal antes de ejecutar `idf.py` (por ejemplo, `source /ruta/a/esp-idf/export.sh`, sustituyendo la ruta por tu instalación). El componente BLE externo indica **ESP-IDF 5.4.2 sobre `esp32`** como referencia; esto no sustituye una compilación y prueba de este firmware con tu entorno.

El componente BLE pertenece al repositorio **[ESP32-utilities](https://github.com/andresg9108/ESP32-utilities)**. El [CMake raíz](CMakeLists.txt) lo registra mediante `EXTRA_COMPONENT_DIRS` con la ruta `../ESP32-utilities/BLE/agBLE`.

**Para que este proyecto funcione con la configuración actual, ambos repositorios deben estar como carpetas hermanas dentro del mismo directorio:** `<mi-carpeta>/ESP32-utilities` y `<mi-carpeta>/<este-proyecto>`. En el ejemplo, este proyecto se llama `bot`:

```text
mi-carpeta/
├── bot/
│   ├── CMakeLists.txt
│   └── main/
└── ESP32-utilities/
    └── BLE/
        └── agBLE/
            ├── CMakeLists.txt
            └── include/agBLE.h
```

No coloques `ESP32-utilities` dentro de `bot/`, ni `bot/` dentro de `ESP32-utilities/`. Conserva el nombre `ESP32-utilities`, porque forma parte de la ruta usada por CMake. El nombre de la carpeta de este proyecto puede ser distinto de `bot`, siempre que ambos repositorios sigan siendo hermanos. Otra distribución requeriría modificar la ruta de integración.

Para una descarga nueva, con Git instalado (sustituye `/ruta/a/mi-carpeta` por el directorio común que quieras usar):

```bash
mkdir -p /ruta/a/mi-carpeta
cd /ruta/a/mi-carpeta
git clone https://github.com/andresg9108/ESP32-utilities.git ESP32-utilities
git clone https://github.com/andresg9108/bot.git bot
cd bot
```

Si ya tienes este proyecto, conserva tu copia y sus cambios: descarga únicamente `ESP32-utilities` en su directorio padre y vuelve a la carpeta del firmware. Antes de configurar ESP-IDF, comprueba que existe `../ESP32-utilities/BLE/agBLE/CMakeLists.txt` desde esa carpeta.

[main/CMakeLists.txt](main/CMakeLists.txt) declara las dependencias `agBLE`, `esp_driver_gpio`, `esp_driver_ledc`, `freertos` y `nvs_flash`. Una vez descargados los repositorios, inicializa el entorno ESP-IDF y continúa con los pasos siguientes desde `bot/` (o el nombre que tenga tu copia de este proyecto).

### Configurar, compilar y grabar

Desde la raíz de `bot/`, con ESP-IDF inicializado y la dependencia presente:

```bash
# Configuración inicial para el ESP32 clásico usado por este cableado.
idf.py set-target esp32
idf.py menuconfig
idf.py build

# Sustituye PUERTO por el puerto serie de tu placa.
idf.py -p PUERTO flash monitor
```

`set-target` es un paso de configuración inicial o de cambio de chip, no un requisito para cada compilación; puede regenerar la configuración local. Comprueba antes cualquier ajuste propio de tu placa.

En `menuconfig`, comprueba Bluetooth con **NimBLE** y los roles **periférico**, **emisor de anuncios (broadcaster)** y **servidor GATT**. En **Component config → AG BLE configuration**, debe estar habilitado `CONFIG_AGBLE_SERVER_ENABLED`. El [sdkconfig.defaults](sdkconfig.defaults) activa Bluetooth/NimBLE y varios roles; verifica la configuración efectiva, especialmente si reutilizas un `sdkconfig` anterior. El firmware actúa como servidor BLE; el cliente es la aplicación.

La copia de trabajo inspeccionada contiene un `sdkconfig` local para `esp32`, pero [.gitignore](.gitignore) lo excluye: no dependas de recibirlo al obtener el proyecto. Ajusta también el puerto y los parámetros de la placa real.

## Puesta en marcha con botcontrol

1. Con la alimentación desconectada, revisa el cableado, la masa común y los jumpers ENA/ENB. Levanta las ruedas antes de probar movimiento.
2. Compila, graba y enciende el ESP32. El firmware inicializa las salidas apagadas y anuncia el nombre **`agbot`**.
3. Instala y ejecuta [botcontrol](https://github.com/andresg9108/botcontrol) siguiendo su README. Activa Bluetooth, concede los permisos y conecta con `agbot` desde la aplicación.
4. Mantén adelante o atrás y combina con izquierda o derecha. El giro por sí solo no mueve el robot.
5. Comprueba avance, retroceso, giros, parada al soltar y pérdida de comunicación. Haz estas pruebas en condiciones seguras antes de apoyar las ruedas.

### Funcionamiento y seguridad

- La potencia base sube de 0 a 100 % en **3 segundos de rampa**, después de la pausa inicial. Los cambios de giro se suavizan.
- Cada inicio o inversión introduce una pausa configurada de **100 ms** con los motores deshabilitados.
- Soltar avance/retroceso apaga ambos lados y reinicia la rampa.
- La desconexión, un error BLE o **500 ms sin comandos** solicitan parada. El control procesa las salidas cada **20 ms**; no es una parada instantánea ni un mecanismo de seguridad independiente.
- Un fallo de GPIO/PWM bloquea el movimiento hasta reiniciar e intenta apagar las salidas.
- Los porcentajes informados son PWM calculado, **no velocidad medida**: no hay encoders en esta implementación.

### Resumen del protocolo BLE

| Elemento | Valor y uso |
|---|---|
| Nombre | `agbot` |
| Servicio | `0xFFF0` |
| Comandos | `0xFFF1`, escritura con respuesta |
| Estado | `0xFFF2`, lectura y notificaciones |

Cada comando contiene **uno o dos bytes binarios**, no texto ASCII:

- Primer byte: estado completo de botones; adelante `0x01`, atrás `0x02`, izquierda `0x04`, derecha `0x08`. Se combinan mediante OR; `0x00` solicita parada.
- Segundo byte opcional: intensidad del giro entre 0 y 100 %. Si falta, se usa **50 %**. Se aplica al comando actual, no configura los siguientes. El formato de un byte descrito por botcontrol sigue siendo aceptado.
- Envía el estado al pulsar/soltar y repítelo **cada 100 ms** mientras conduces. Cada comando sustituye al anterior.
- Ejemplos hexadecimales: `05` es adelante/izquierda al 50 %; `05 19` es adelante/izquierda al 25 %; `00` es parada.
- Bits desconocidos, botones opuestos, intensidad superior a 100 o longitud distinta de uno o dos bytes solicitan parada.

El estado se publica cada **100 ms** en cinco bytes: botones aceptados, PWM base (%), PWM izquierdo (%), PWM derecho (%) y flags (bit 0: timeout; bit 1: pausa; bit 2: fallo de salida). Consulta [BLE_CONTROL.md](BLE_CONTROL.md) para ejemplos y reglas completas.

## Organización del código

| Archivo | Responsabilidad |
|---|---|
| [main/agbot.c](main/agbot.c) | `app_main`, inicialización NVS/BLE, recepción de comandos y publicación del estado |
| [main/motor_control.c](main/motor_control.c) | GPIO, PWM LEDC, cola del último comando y tarea de control |
| [main/motor_control.h](main/motor_control.h) | Interfaz del controlador y formato del estado |
| [main/motor_motion.c](main/motor_motion.c) | Lógica de aceleración, giros, inversión y timeout sin acceso al hardware |
| [main/motor_motion.h](main/motor_motion.h) | Máscaras de botones, temporizaciones y constantes de movimiento |
| [tests/motor_motion_test.c](tests/motor_motion_test.c) | Pruebas de lógica en el equipo de desarrollo |

## Comprobaciones

Con un compilador C instalado, desde la raíz del proyecto se pueden ejecutar las pruebas de lógica sin ESP32 ni ESP-IDF:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain main/motor_motion.c tests/motor_motion_test.c -o /tmp/agbot_motor_motion_test
/tmp/agbot_motor_motion_test
```

Estas pruebas cubren comandos, aceleración, liberación, giros, inversión, timeout e intensidad variable. No validan el cableado, las salidas GPIO/PWM ni la comunicación con el teléfono: esos puntos requieren pruebas en placa. Las instrucciones de esta guía se contrastaron con el código; no implican que se haya ejecutado aquí la compilación o la prueba física.

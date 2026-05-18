/*
 * TP2.c
 *
 * Created: 16/5/2026 00:59:55
 * Author : Lucila Juri y Santiago Robaldi
 */ 

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stddef.h>
#include "lcd.h"

// DEFINICIONES DE ESTADOS Y EVENTOS

typedef enum {
	ESTADO_REPOSO,            // Esperando ingreso (00:00)
	ESTADO_INGRESO,           // Usuario tipeando
	ESTADO_COCCION,           // Magnetrón y Luz ON, descontando
	ESTADO_PAUSA,             // Magnetrón OFF, Luz ON
	ESTADO_ALARMA,            // 00:00, parpadea alarma 5 seg
	ESTADO_PUERTA_AB_REPOSO,  // Abrió desde 00:00
	ESTADO_PUERTA_AB_INGRESO, // Abrió mientras tecleaba el tiempo
	ESTADO_PUERTA_AB_COCCION, // Abrió mientras calentaba (frena y resume al cerrar)
	ESTADO_PUERTA_AB_PAUSA    // Abrió mientras estaba pausado (frena y se queda en pausa al cerrar)
} Estado_t;

typedef enum {
    EV_TECLA_NUMERO,
    EV_TECLA_START,   // 'A'
    EV_TECLA_STOP,    // 'B'
    EV_TECLA_ADD30,   // 'C'
    EV_TECLA_DOOR,    // 'D'
    EV_TIEMPO_1SEG,   // Paso 1 seg en coccion
    EV_FIN_ALARMA,    // Pasaron 5 seg de alarma
    EV_NADA
} Evento_t;

// ===================================================================
// 2. VARIABLES GLOBALES DE CONTROL
// ===================================================================

volatile uint8_t flag_timer_10ms = 0;
volatile uint8_t flag_timer_1seg = 0;

static Estado_t estado_actual = ESTADO_REPOSO;
static uint8_t digitos[4] = {0, 0, 0, 0};
static uint8_t cant_digitos = 0;
static uint16_t tiempo_segundos = 0;
static uint8_t tecla_actual_numerica = 0;
static uint8_t flag_actualizar_lcd = 1;
static uint8_t contador_alarma_segundos = 0;

const uint8_t keypad_matrix[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};



// CONFIGURACIoN DE HARDWARE

void Setup_Hardware(void) {
	// --- LEDs como salidas, inician apagados ---
	DDRB |= (1 << DDB5);
	DDRC |= (1 << DDC4) | (1 << DDC5);
	PORTB &= ~(1 << PORTB5);
	PORTC &= ~((1 << PORTC4) | (1 << PORTC5));

	// --- Teclado: Filas como salidas (PB4, PB3, PB0, PD7) ---
	DDRB |= (1 << DDB4) | (1 << DDB3) | (1 << DDB0);
	DDRD |= (1 << DDD7);
	PORTB |= (1 << PORTB4) | (1 << PORTB3) | (1 << PORTB0);
	PORTD |= (1 << PORTD7);

	// --- Teclado: Columnas como entradas (PD3, PD5, PD4, PD2) ---
	DDRD &= ~((1 << DDD3) | (1 << DDD5) | (1 << DDD4) | (1 << DDD2));
	
	// Activamos las resistencias Pull-up
	PORTD |= (1 << PORTD3) | (1 << PORTD5) | (1 << PORTD4) | (1 << PORTD2);

	LCDinit();
	LCDclr();
}


// LECTURA DEL TECLADO

uint8_t KeypadUpdate(void) {
	uint8_t fila, columna;

	for (fila = 0; fila < 4; fila++) {
		// 1. Ponemos TODAS las filas en ALTO (1)
		PORTB |= (1 << PORTB4) | (1 << PORTB3) | (1 << PORTB0);
		PORTD |= (1 << PORTD7);
		
		// 2. Bajamos a CERO (0) solo la fila actual
		switch(fila) {
			case 0: PORTB &= ~(1 << PORTB4); break; // Fila 0 (1, 2, 3, A)
			case 1: PORTB &= ~(1 << PORTB3); break; // Fila 1 (4, 5, 6, B)
			case 2: PORTB &= ~(1 << PORTB0); break; // Fila 2 (7, 8, 9, C)
			case 3: PORTD &= ~(1 << PORTD7); break; // Fila 3 (*, 0, #, D)
		}

		__asm__ __volatile__ ("nop");
		__asm__ __volatile__ ("nop");

		// 3. Leemos las columnas
		for (columna = 0; columna < 4; columna++) {
			uint8_t bit_leido = 1;
			switch(columna) {
				case 0: bit_leido = (PIND & (1 << PIND3)); break; 
				case 1: bit_leido = (PIND & (1 << PIND5)); break; 
				case 2: bit_leido = (PIND & (1 << PIND4)); break; 
				case 3: bit_leido = (PIND & (1 << PIND2)); break; 
			}
			
			if (bit_leido == 0) {
				// Restauramos filas antes de salir
				PORTB |= (1 << PORTB4) | (1 << PORTB3) | (1 << PORTB0);
				PORTD |= (1 << PORTD7);
				
				return keypad_matrix[fila][columna];
			}
		}
	}
	
	// Si no hay nada presionado, dejamos todo en alto
	PORTB |= (1 << PORTB4) | (1 << PORTB3) | (1 << PORTB0);
	PORTD |= (1 << PORTD7);
	return 0xFF;
}

// Anti-robote de la catedra
uint8_t KEYPAD_Scan(uint8_t *pkey) {
	static uint8_t Old_key = 0xFF;
	static uint8_t Last_valid_key = 0xFF;
	uint8_t Key = KeypadUpdate();

	if (Key == 0xFF) {
		Old_key = 0xFF;
		Last_valid_key = 0xFF;
		return 0;
	}
	if (Key == Old_key) {
		if (Key != Last_valid_key) {
			*pkey = Key;
			Last_valid_key = Key;
			return 1;
		}
	}
	Old_key = Key;
	return 0;
}

// ACCIONES 

void Accion_ReiniciarTodo(void) {
    digitos[0] = 0; digitos[1] = 0; digitos[2] = 0; digitos[3] = 0;
    cant_digitos = 0;
    tiempo_segundos = 0;
    PORTB &= ~(1 << PORTB5); 
    PORTC &= ~((1 << PORTC4) | (1 << PORTC5)); 
    flag_actualizar_lcd = 1;
}

void Accion_IngresarDigito(void) {
    if (cant_digitos < 4) {
        digitos[0] = digitos[1];
        digitos[1] = digitos[2];
        digitos[2] = digitos[3];
        digitos[3] = tecla_actual_numerica - '0';
        if(!((cant_digitos==0)&&(tecla_actual_numerica=='0'))){cant_digitos++; flag_actualizar_lcd = 1;}
    }
}

uint16_t Convertir_Digitos_A_Segundos(void) {
    uint8_t minutos = (digitos[0] * 10) + digitos[1];
    uint8_t segundos = (digitos[2] * 10) + digitos[3];
    return (uint16_t)(minutos * 60) + segundos;
}

void Accion_IniciarCoccion(void) {
    tiempo_segundos = Convertir_Digitos_A_Segundos();
    if (tiempo_segundos > 0) {
        PORTB |= (1 << PORTB5); 
        PORTC |= (1 << PORTC4); 
        PORTC &= ~(1 << PORTC5); 
        flag_actualizar_lcd = 1;
    }
}

void Accion_ReanudarCoccion(void) {
    PORTB |= (1 << PORTB5); 
    PORTC |= (1 << PORTC4); 
    flag_actualizar_lcd = 1;
}

void Accion_Agregar30s(void) {
    tiempo_segundos += 30;
    if (tiempo_segundos > 3599) tiempo_segundos = 3599;
    PORTB |= (1 << PORTB5); 
    PORTC |= (1 << PORTC4); 
    flag_actualizar_lcd = 1;
}

void Accion_Pausar(void) {
    PORTB &= ~(1 << PORTB5); 
    PORTC |= (1 << PORTC4);  
    flag_actualizar_lcd = 1;
}

void Accion_DispararFin(void) {
    PORTB &= ~(1 << PORTB5); 
    PORTC &= ~(1 << PORTC4); 
    contador_alarma_segundos = 0;
    PORTC |= (1 << PORTC5);  
    flag_actualizar_lcd = 1;
}

void Accion_ApagarLuz_MantenerDatos(void) {
	PORTB &= ~(1 << PORTB5); 
	PORTC &= ~(1 << PORTC4);
	flag_actualizar_lcd = 1;
}

void Accion_Cancelar_MantenerLuz(void) {
	digitos[0] = 0; digitos[1] = 0; digitos[2] = 0; digitos[3] = 0;
	cant_digitos = 0;
	tiempo_segundos = 0;
	
	PORTB &= ~(1 << PORTB5); 
	PORTC |= (1 << PORTC4); 
	PORTC &= ~(1 << PORTC5); 
	
	flag_actualizar_lcd = 1;
}


// TABLA DE TRANSICIONES

typedef void (*Accion_t)(void);

typedef struct {
    Estado_t estado_actual;
    Evento_t evento_detectado;
    Estado_t proximo_estado;
    Accion_t funcion_accion;
} Transicion_t;

const Transicion_t tabla_mef[] = {
	// --- ESTADOS NORMALES ---
	{ ESTADO_REPOSO,             EV_TECLA_NUMERO,    ESTADO_INGRESO,             Accion_IngresarDigito },
	{ ESTADO_REPOSO,             EV_TECLA_ADD30,     ESTADO_COCCION,             Accion_Agregar30s     },
	{ ESTADO_REPOSO,             EV_TECLA_DOOR,      ESTADO_PUERTA_AB_REPOSO,    Accion_Pausar         }, 

	{ ESTADO_INGRESO,            EV_TECLA_NUMERO,    ESTADO_INGRESO,             Accion_IngresarDigito },
	{ ESTADO_INGRESO,            EV_TECLA_START,     ESTADO_COCCION,             Accion_IniciarCoccion },
	{ ESTADO_INGRESO,            EV_TECLA_STOP,      ESTADO_REPOSO,              Accion_ReiniciarTodo  },
	{ ESTADO_INGRESO,            EV_TECLA_ADD30,     ESTADO_COCCION,             Accion_Agregar30s     },
	{ ESTADO_INGRESO,            EV_TECLA_DOOR,      ESTADO_PUERTA_AB_INGRESO,   Accion_Pausar         }, 

	{ ESTADO_COCCION,            EV_TECLA_STOP,      ESTADO_PAUSA,               Accion_Pausar         },
	{ ESTADO_COCCION,            EV_TECLA_DOOR,      ESTADO_PUERTA_AB_COCCION,   Accion_Pausar         }, 
	{ ESTADO_COCCION,            EV_TECLA_ADD30,     ESTADO_COCCION,             Accion_Agregar30s     },
	{ ESTADO_COCCION,            EV_TIEMPO_1SEG,     ESTADO_ALARMA,              Accion_DispararFin    },

	{ ESTADO_PAUSA,              EV_TECLA_START,     ESTADO_COCCION,             Accion_ReanudarCoccion},
	{ ESTADO_PAUSA,              EV_TECLA_STOP,      ESTADO_REPOSO,              Accion_ReiniciarTodo  },
	{ ESTADO_PAUSA,              EV_TECLA_ADD30,     ESTADO_COCCION,             Accion_Agregar30s     },
	{ ESTADO_PAUSA,              EV_TECLA_DOOR,      ESTADO_PUERTA_AB_PAUSA,     Accion_Pausar         }, 

	{ ESTADO_ALARMA,             EV_FIN_ALARMA,      ESTADO_REPOSO,              Accion_ReiniciarTodo  },
	{ ESTADO_ALARMA,             EV_TECLA_STOP,      ESTADO_REPOSO,              Accion_ReiniciarTodo  },
	{ ESTADO_ALARMA,             EV_TECLA_DOOR,      ESTADO_PUERTA_AB_REPOSO,    Accion_Pausar         }, 

	// ESTADOS POR PUERTA ABIERTA
	
	// 1. Abierta desde Reposo
	{ ESTADO_PUERTA_AB_REPOSO,   EV_TECLA_DOOR,      ESTADO_REPOSO,              Accion_ReiniciarTodo  }, 
	{ ESTADO_PUERTA_AB_REPOSO,   EV_TECLA_NUMERO,    ESTADO_PUERTA_AB_INGRESO,   Accion_IngresarDigito }, 
	
	// 2. Abierta desde Ingreso
	{ ESTADO_PUERTA_AB_INGRESO,  EV_TECLA_DOOR,      ESTADO_INGRESO,             Accion_ApagarLuz_MantenerDatos }, 
	{ ESTADO_PUERTA_AB_INGRESO,  EV_TECLA_STOP,      ESTADO_PUERTA_AB_REPOSO,    Accion_Cancelar_MantenerLuz }, 
	{ ESTADO_PUERTA_AB_INGRESO,  EV_TECLA_NUMERO,    ESTADO_PUERTA_AB_INGRESO,   Accion_IngresarDigito },
	
	// 3. Abierta desde Cocción
	{ ESTADO_PUERTA_AB_COCCION,  EV_TECLA_DOOR,      ESTADO_COCCION,             Accion_ReanudarCoccion}, 
	{ ESTADO_PUERTA_AB_COCCION,  EV_TECLA_STOP,      ESTADO_PUERTA_AB_REPOSO,    Accion_Cancelar_MantenerLuz }, 
	
	// 4. Abierta desde Pausa
	{ ESTADO_PUERTA_AB_PAUSA,    EV_TECLA_DOOR,      ESTADO_PAUSA,               Accion_Pausar         }, 
	{ ESTADO_PUERTA_AB_PAUSA,    EV_TECLA_STOP,      ESTADO_PUERTA_AB_REPOSO,    Accion_Cancelar_MantenerLuz }  
		
};

#define NUM_TRANSICIONES (sizeof(tabla_mef) / sizeof(Transicion_t))

void MEF_ProcesarEvento(Evento_t evento_nuevo) {
    if (evento_nuevo == EV_NADA) return;
    
    for (uint8_t i = 0; i < NUM_TRANSICIONES; i++) {
        if (tabla_mef[i].estado_actual == estado_actual && 
            tabla_mef[i].evento_detectado == evento_nuevo) {
            
            if (tabla_mef[i].funcion_accion != NULL) {
                tabla_mef[i].funcion_accion();
            }
			
            // Si el START en ingreso da tiempo 0, evitamos pasar a cocciOn
            if (evento_nuevo == EV_TECLA_START && estado_actual == ESTADO_INGRESO && tiempo_segundos == 0) {
                break;
            }
            estado_actual = tabla_mef[i].proximo_estado;
            break;
        }
    }
}


// RUTINAS DE INTERRUPCION


ISR(TIMER1_COMPA_vect) {
    flag_timer_10ms = 1;
    static uint16_t contador_ticks = 0;
    contador_ticks++;
    if (contador_ticks >= 100) {
        contador_ticks = 0;
        flag_timer_1seg = 1;
    }
}

Evento_t Leer_Eventos(void) {
    if (flag_timer_10ms == 1) {
        flag_timer_10ms = 0;
        uint8_t tecla_detectada;
        if (KEYPAD_Scan(&tecla_detectada) == 1) {
            if (tecla_detectada >= '0' && tecla_detectada <= '9') {
                tecla_actual_numerica = tecla_detectada;
                return EV_TECLA_NUMERO;
            }
            switch(tecla_detectada) {
                case 'A': return EV_TECLA_START;
                case 'B': return EV_TECLA_STOP;
                case 'C': return EV_TECLA_ADD30;
                case 'D': return EV_TECLA_DOOR;
            }
        }
    }
    
    if (flag_timer_1seg == 1) {
        flag_timer_1seg = 0;
        if (estado_actual == ESTADO_COCCION) {
            if (tiempo_segundos > 0) {
                tiempo_segundos--;
                flag_actualizar_lcd = 1;
                if (tiempo_segundos == 0) return EV_TIEMPO_1SEG;
            }
        }
        if (estado_actual == ESTADO_ALARMA) {
            contador_alarma_segundos++;
            PORTC ^= (1 << PORTC5); 
            flag_actualizar_lcd = 1;
            if (contador_alarma_segundos >= 5) return EV_FIN_ALARMA;
        }
    }
    return EV_NADA;
}


// LCD


void Mostrar_Tiempo_LCD(uint16_t tiempo_tot_seg) {
    char buffer_texto[6];
	
	if (tiempo_tot_seg > 3599) 
	{
		tiempo_tot_seg = 3599;
		tiempo_segundos=3599;
	}
	uint8_t min = tiempo_tot_seg / 60;
    uint8_t seg = tiempo_tot_seg % 60;
    
    buffer_texto[0] = '0' + (min / 10);
    buffer_texto[1] = '0' + (min % 10);
    buffer_texto[2] = ':';
    buffer_texto[3] = '0' + (seg / 10);
    buffer_texto[4] = '0' + (seg % 10);
    buffer_texto[5] = '\0';
    
    LCDGotoXY(5, 0); 
    LCDstring((uint8_t*)buffer_texto, 5);
}

void Actualizar_Pantalla_LCD(void) {
    if (flag_actualizar_lcd == 0) return;
    flag_actualizar_lcd = 0;
    
    LCDclr();
    switch (estado_actual) {
        case ESTADO_REPOSO:
            Mostrar_Tiempo_LCD(0);
            LCDGotoXY(0, 1);
            LCDstring((uint8_t*)" INGRESE TIEMPO ", 16);
            break;
        case ESTADO_INGRESO:
            Mostrar_Tiempo_LCD(Convertir_Digitos_A_Segundos());
            LCDGotoXY(0, 1);
            LCDstring((uint8_t*)"A:START B:BORRAR", 16);
            break;
        case ESTADO_COCCION:
            Mostrar_Tiempo_LCD(tiempo_segundos);
            break;
        case ESTADO_PAUSA:
            Mostrar_Tiempo_LCD(tiempo_segundos);
            LCDGotoXY(0, 1);
            LCDstring((uint8_t*)"PAUSA  A:REANUD ", 16);
            break;
        case ESTADO_ALARMA:
            if (contador_alarma_segundos % 2 == 0) {
                Mostrar_Tiempo_LCD(0);
                LCDGotoXY(0, 1);
                LCDstring((uint8_t*)"      FIN       ", 16);
            }
            break;
		case ESTADO_PUERTA_AB_REPOSO:
			Mostrar_Tiempo_LCD(0);
			LCDGotoXY(0, 1);
			LCDstring((uint8_t*)" PUERTA ABIERTA ", 16);
			break;
		case ESTADO_PUERTA_AB_INGRESO:
			Mostrar_Tiempo_LCD(Convertir_Digitos_A_Segundos());
			LCDGotoXY(0, 1);
			LCDstring((uint8_t*)" PUERTA ABIERTA ", 16);
			break;
		case ESTADO_PUERTA_AB_COCCION:
		case ESTADO_PUERTA_AB_PAUSA:
			Mostrar_Tiempo_LCD(tiempo_segundos);
			LCDGotoXY(0, 1);
			LCDstring((uint8_t*)" PUERTA ABIERTA ", 16);
		break;
    }
}


void Setup_Timer(void) {
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
    OCR1A = 2499;
    TIMSK1 = (1 << OCIE1A);
}

int main(void) {
    Setup_Hardware();
    Setup_Timer();
    sei();
    
    Accion_ReiniciarTodo();
    
    while (1) {
        Evento_t evento_actual = Leer_Eventos();
        MEF_ProcesarEvento(evento_actual);
        Actualizar_Pantalla_LCD();
    }
    
    return 0;
}
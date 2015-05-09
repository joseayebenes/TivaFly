#include<stdbool.h>
#include<stdint.h>

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"
#include "driverlib/adc.h"
#include "driverlib/timer.h"
#include "utils/uartstdio.h"
#include "drivers/buttons.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "utils/cpu_usage.h"
#include "inc/hw_adc.h"
#include "FreeRTOS/source/include/event_groups.h"

#include "drivers/rgb.h"
#include "usb_dev_serial.h"
#include "protocol.h"

#include <cmath>


#define LED1TASKPRIO 1
#define LED1TASKSTACKSIZE 128
#define PITCH 0
#define ROLL 1
#define YAW 2

//Globales

uint32_t g_ui32CPUUsage;
uint32_t g_ulSystemClock;

QueueHandle_t potQueue;
QueueHandle_t velocidadQueue;
SemaphoreHandle_t SendSemaphore=NULL;
SemaphoreHandle_t EjesSemaphore=NULL;
SemaphoreHandle_t  PilotoQuitarSemaphore= NULL;

TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t consumoTaskHandle = NULL;
TaskHandle_t PilautTaskHandle = NULL;

EventGroupHandle_t xTrazaEventGroup;
#define TrazaBit	( 1 << 0 )


extern void vUARTTask( void *pvParameters );

int16_t ejes[3];
float  velocidad=60;
double combustible=100;
uint32_t hora=0;
double  altitud=3000;
int tiempoSim=1;

uint32_t color[3];

bool pilotoAutomatico=true;



//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, unsigned long ulLine)
{
}

#endif

//*****************************************************************************
//
// Aqui incluimos los "ganchos" a los diferentes eventos del FreeRTOS
//
//*****************************************************************************

//Esto es lo que se ejecuta cuando el sistema detecta un desbordamiento de pila
//
void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName)
{
	//
	// This function can not return, so loop forever.  Interrupts are disabled
	// on entry to this function, so no processor interrupts will interrupt
	// this loop.
	//
	while(1)
	{
	}
}

//Esto se ejecuta cada Tick del sistema. LLeva la estadistica de uso de la CPU (tiempo que la CPU ha estado funcionando)
void vApplicationTickHook( void )
{
	static unsigned char count = 0;

	if (++count == 10)
	{
		g_ui32CPUUsage = CPUUsageTick();
		count = 0;
	}
	//return;
}

//Esto se ejecuta cada vez que entra a funcionar la tarea Idle
void vApplicationIdleHook (void)
{
	SysCtlSleep();
}


//Esto se ejecuta cada vez que entra a funcionar la tarea Idle
void vApplicationMallocFailedHook (void)
{
	while(1);
}



//*****************************************************************************
//
// A continuacion van las tareas...
//
//*****************************************************************************

// El codigo de esta tarea esta definida en el fichero command.c, es la que se encarga de procesar los comandos del interprete a traves
// del terminal serie (puTTY)
//Aqui solo la declaramos para poderla referenciar en la funcion main
extern void vUARTTask( void *pvParameters );



static portTASK_FUNCTION(PilAuto,pvParameters)
{

	unsigned char frame[MAX_FRAME_SIZE];
	int num_datos;


	ADCSequenceDisable(ADC0_BASE,0);


	num_datos=create_frame(frame, COMANDO_AUTOMATICO, &pilotoAutomatico, sizeof(pilotoAutomatico), MAX_FRAME_SIZE);
	if (num_datos>=0){
		send_frame(frame, num_datos);
	}



	while(1)
	{
		vTaskDelay(configTICK_RATE_HZ);

		if(!pilotoAutomatico){
			num_datos=create_frame(frame, COMANDO_AUTOMATICO, &pilotoAutomatico, sizeof(pilotoAutomatico), MAX_FRAME_SIZE);
			if (num_datos>=0){
				send_frame(frame, num_datos);
			}
			ADCSequenceEnable(ADC0_BASE,0);
			vTaskDelete(NULL);
		}

		xSemaphoreTake(EjesSemaphore, portMAX_DELAY);
		ejes[PITCH]=0;
		ejes[ROLL]=0;
		ejes[YAW]=0;
		velocidad=100;
		num_datos=create_frame(frame, COMANDO_EJES, &ejes, sizeof(ejes), MAX_FRAME_SIZE);
			if (num_datos>=0){
				send_frame(frame, num_datos);
			}
		num_datos=create_frame(frame, COMANDO_SPEED, &velocidad, sizeof(velocidad), MAX_FRAME_SIZE);
			if (num_datos>=0){
				send_frame(frame, num_datos);
			}
		xSemaphoreGive(EjesSemaphore);


	}
}



static portTASK_FUNCTION(HighTask,pvParameters)
{

	unsigned char frame[MAX_FRAME_SIZE];
	int num_datos;


	while(1)
	{
		vTaskDelay(configTICK_RATE_HZ);

		if(altitud>0){

			xSemaphoreTake(EjesSemaphore, portMAX_DELAY);
			altitud += sin((ejes[PITCH]*3.14f)/180) *(velocidad*(1000/(60/tiempoSim))); //pasar a de minutos a horas y km a m
			xSemaphoreGive(EjesSemaphore);

			if(altitud>99999){
				altitud=99999;
			}

			num_datos=create_frame(frame, COMANDO_HIGH, &altitud, sizeof(altitud), MAX_FRAME_SIZE);
			if (num_datos>=0){
				send_frame(frame, num_datos);
			}

			if(combustible==0){
				color[BLUE]=0xFFFF;
				if(altitud<=800){
					color[RED]=0xFFFF;
					color[GREEN]=0x0;
				}
				RGBColorSet(color);
				if(altitud<2000){
					RGBBlinkRateSet((float)(1000/(altitud+1)));
				}

				velocidad+=9.8*(60*tiempoSim)/1000;
				num_datos=create_frame(frame, COMANDO_SPEED, &velocidad, sizeof(velocidad), MAX_FRAME_SIZE);
				if (num_datos>=0){
					send_frame(frame, num_datos);
				}
			}

		}else{
			//BLOQUEAR TIVA
			velocidad=0;
			altitud=0;
			if(combustible!=0) ADCSequenceDisable(ADC0_BASE,0);
			num_datos=create_frame(frame, COMANDO_COLISION,NULL, 0, MAX_FRAME_SIZE);
			if (num_datos>=0){
				send_frame(frame, num_datos);
			}
			vTaskSuspendAll ();
			vTaskDelete(NULL);
		}




	}
}

static portTASK_FUNCTION(SensorTask,pvParameters)
		{

	uint32_t potenciometros[3];
	uint32_t lastPotenciometros[3];

	int16_t lastEjes[3]={0,0,0};

	unsigned char frame[MAX_FRAME_SIZE];
	int num_datos;
	int incrementoPITCH, incrementoROLL;

	ejes[PITCH]=0;
	ejes[ROLL]=0;
	ejes[YAW]=0;
	num_datos=create_frame(frame, COMANDO_EJES, ejes, sizeof(ejes), MAX_FRAME_SIZE);
	if (num_datos>=0){
	send_frame(frame, num_datos);
	}

	while(1)
	{
		xQueueReceive(potQueue,potenciometros,portMAX_DELAY);

		/*eje[PITCH]=(potenciometros[PITCH]*90)/4096-45;
		eje[ROLL]=(potenciometros[ROLL]*60)/4096-30;
		eje[YAW]=(potenciometros[YAW]*360)/4096;

		incrementoPITCH=eje[PITCH]-eje[PITCH];
		incrementoROLL=eje[ROLL]-eje[ROLL];*/

		if(potenciometros[PITCH]>=0 && potenciometros[PITCH]<682){
			ejes[PITCH]-=1;
		}else if(potenciometros[PITCH]>=585 && potenciometros[PITCH]<1170){
			ejes[PITCH]-=2;
		}else if(potenciometros[PITCH]>=1170 && potenciometros[PITCH]<1754){
			ejes[PITCH]-=1;
		}else if(potenciometros[PITCH]>=1754 && potenciometros[PITCH]<2340){
			//SECCION CENTRAL

		}else if(potenciometros[PITCH]>=2340 && potenciometros[PITCH]<2925){
			ejes[PITCH]+=1;
		}else if(potenciometros[PITCH]>=2925 && potenciometros[PITCH]<3510){
			ejes[PITCH]+=2;
		}else if(potenciometros[PITCH]>=3510 && potenciometros[PITCH]<4096){
			ejes[PITCH]+=3;
		}

		if(potenciometros[ROLL]>=0 && potenciometros[ROLL]<682){
			ejes[ROLL]-=5;
		}else if(potenciometros[ROLL]>=585 && potenciometros[ROLL]<1170){
			ejes[ROLL]-=3;
		}else if(potenciometros[ROLL]>=1170 && potenciometros[ROLL]<1754){
			ejes[ROLL]-=1;
		}else if(potenciometros[ROLL]>=1754 && potenciometros[ROLL]<2340){
			//SECCION CENTRAL

		}else if(potenciometros[ROLL]>=2340 && potenciometros[ROLL]<2925){
			ejes[ROLL]+=1;
		}else if(potenciometros[ROLL]>=2925 && potenciometros[ROLL]<3510){
			ejes[ROLL]+=3;
		}else if(potenciometros[ROLL]>=3510 && potenciometros[ROLL]<4096){
			ejes[ROLL]+=5;
		}

		if (ejes[PITCH]>45){
			ejes[PITCH]=45;
		}else if (ejes[PITCH]<-45){
			ejes[PITCH]=-45;
		}

		if (ejes[ROLL]>30){
			ejes[ROLL]=30;
		}else if (ejes[ROLL]<-30){
			ejes[ROLL]=-30;
		}


		if(ejes[PITCH]!=lastEjes[PITCH] || ejes[ROLL]!=lastEjes[ROLL] || ejes[YAW]!=lastEjes[YAW]){
			num_datos=create_frame(frame, COMANDO_EJES, ejes, sizeof(ejes), MAX_FRAME_SIZE);
			if (num_datos>=0){
			send_frame(frame, num_datos);
			}
			lastEjes[PITCH]=ejes[PITCH];
			lastEjes[ROLL]=ejes[ROLL];
			lastEjes[YAW]=ejes[YAW];
		}
	}

}

static portTASK_FUNCTION(TimeTask,pvParameters)
{
	unsigned char frame[MAX_FRAME_SIZE];
	int num_datos;

	while(1)
	{
		vTaskDelay(configTICK_RATE_HZ);
		hora+=tiempoSim;
		if(hora>1440){
			hora=0;
		}

		num_datos=create_frame(frame, COMANDO_TIME, &hora, sizeof(hora), MAX_FRAME_SIZE);
		if (num_datos>=0){
			send_frame(frame, num_datos);
		}



	}
}

static portTASK_FUNCTION(ConsumoTask,pvParameters)
{

	double consumo=0.0374*exp(0.02*((velocidad*100)/240));


	TickType_t tiempo_ant =xTaskGetTickCount(  );


	unsigned char frame[MAX_FRAME_SIZE];
	int num_datos;

	while(1)
	{

		if(xQueueReceive(velocidadQueue,&velocidad, configTICK_RATE_HZ)){
			color[BLUE]=0xFFFF;
			RGBSet(color,((float)velocidad)/241);
		}

		if((xTaskGetTickCount(  )-tiempo_ant)>=configTICK_RATE_HZ*(60/tiempoSim) && combustible!=0){

			combustible -= 0.5*exp(0.02*(velocidad*100/240)) ;
			if(combustible<=20){
				color[GREEN]=0xFFFF;
				RGBColorSet(color);
			}

			if(combustible<=0){
				combustible=0;
				velocidad=0;
				pilotoAutomatico=false;
				ADCSequenceDisable(ADC0_BASE,0);

			}

			num_datos=create_frame(frame, COMANDO_FUEL, &combustible, sizeof(combustible), MAX_FRAME_SIZE);
			if (num_datos>=0){
				send_frame(frame, num_datos);
			}
			tiempo_ant =xTaskGetTickCount(  );
		}

		xSemaphoreTake(EjesSemaphore, portMAX_DELAY);
		if(ejes[PITCH]>-45 && combustible==0 && altitud>0){

			ejes[PITCH]--;
			num_datos=create_frame(frame, COMANDO_EJES, ejes, sizeof(ejes), MAX_FRAME_SIZE);
			if (num_datos>=0){
				send_frame(frame, num_datos);
			}
		}
		xSemaphoreGive(EjesSemaphore);


	}
}

// Codigo para procesar los comandos recibidos a traves del canal USB
static portTASK_FUNCTION( CommandProcessingTask, pvParameters ){



	unsigned char frame[MAX_FRAME_SIZE];	//Ojo, esto hace que esta tarea necesite bastante pila
	int numdatos;
	unsigned int errors=0;
	unsigned char command;
	EventBits_t bits;



	/* The parameters are not used. */
	( void ) pvParameters;


	for(;;)
	{
		numdatos=receive_frame(frame,MAX_FRAME_SIZE);
		if (numdatos>0)
		{	//Si no hay error, proceso la trama que ha llegado.
			numdatos=destuff_and_check_checksum(frame,numdatos);
			if (numdatos<0)
			{
				//Error de checksum (PROT_ERROR_BAD_CHECKSUM), ignorar el paquete
				errors++;
				// Procesamiento del error (TODO)
			}
			else
			{
				//El paquete esta bien, luego procedo a tratarlo.
				command=decode_command_type(frame,0);
				bits=xEventGroupGetBits(xTrazaEventGroup);
				switch(command)
				{
				case COMANDO_PING :

					if(bits & TrazaBit == TrazaBit){
						UARTprintf("Comando PING\n ");
					}

					//A un comando de ping se responde con el propio comando
					numdatos=create_frame(frame,command,0,0,MAX_FRAME_SIZE);
					if (numdatos>=0)
					{
						send_frame(frame,numdatos);
					}else{
						//Error de creacion de trama: determinar el error y abortar operacion
						errors++;
						// Procesamiento del error (TODO)
						// Esto de aqui abajo podria ir en una funcion "createFrameError(numdatos)  para evitar
						// tener que copiar y pegar todo en cada operacion de creacion de paquete
						switch(numdatos){
						case PROT_ERROR_NOMEM:
							// Procesamiento del error NO MEMORY (TODO)
							break;
						case PROT_ERROR_STUFFED_FRAME_TOO_LONG:
							// Procesamiento del error STUFFED_FRAME_TOO_LONG (TODO)
							break;
						case PROT_ERROR_COMMAND_TOO_LONG:
							// Procesamiento del error COMMAND TOO LONG (TODO)
							break;
						}
					}
					break;
				case COMANDO_START:         // Comando de ejemplo: eliminar en la aplicacion final
				{

					if(bits & TrazaBit == TrazaBit){
						UARTprintf("Comando START\n ");
					}


					if(sensorTaskHandle == NULL){


						if((xTaskCreate(ConsumoTask, (signed portCHAR *)"Consumo", LED1TASKSTACKSIZE,NULL,tskIDLE_PRIORITY + 1, &consumoTaskHandle)!= pdTRUE))
						{
							while(1);
						}

						if((xTaskCreate(SensorTask, (signed portCHAR *)"Sensor", LED1TASKSTACKSIZE,NULL,tskIDLE_PRIORITY + 1, &sensorTaskHandle) != pdTRUE))
						{
							while(1);
						}
						if((xTaskCreate(HighTask, (signed portCHAR *)"Altitud", LED1TASKSTACKSIZE,NULL,tskIDLE_PRIORITY + 1, NULL) != pdTRUE))
						{
							while(1);
						}

					}

				}
				break;
				case COMANDO_STOP:         // Comando de ejemplo: eliminar en la aplicacion final
				{

					if(bits & TrazaBit == TrazaBit){
						UARTprintf("Comando STOP\n ");
					}
					if(combustible>0){
					vTaskDelete(sensorTaskHandle);
					vTaskDelete( consumoTaskHandle );
					}

				}
				break;
				case COMANDO_SPEED:         // Comando de ejemplo: eliminar en la aplicacion final
				{

					if(bits & TrazaBit == TrazaBit){
						UARTprintf("Comando SPEED\n ");
					}
					uint32_t  velocidad;


					extract_packet_command_param(frame,sizeof(velocidad),&velocidad);
					xQueueSend( velocidadQueue,&velocidad,portMAX_DELAY);


				}
				break;
				case COMANDO_TIME:
				{

					if(bits & TrazaBit == TrazaBit){
						UARTprintf("Comando TIME\n ");
					}

					extract_packet_command_param(frame,sizeof(hora),&hora);
					if(xTaskCreate(TimeTask, (portCHAR *)"Time",512, NULL, tskIDLE_PRIORITY + 1, NULL) != pdTRUE)
					{
						while(1);
					}

				}
				break;
				default:
				{
					PARAM_COMANDO_NO_IMPLEMENTADO parametro;
					parametro.command=command;
					//El comando esta bien pero no esta implementado
					numdatos=create_frame(frame,COMANDO_NO_IMPLEMENTADO,&parametro,sizeof(parametro),MAX_FRAME_SIZE);
					if (numdatos>=0)
					{
						send_frame(frame,numdatos);
					}
					break;
				}
				}
			}
		}else{ // if (numdatos >0)
			//Error de recepcion de trama(PROT_ERROR_RX_FRAME_TOO_LONG), ignorar el paquete
			errors++;
			// Procesamiento del error (TODO)
		}
	}
}




// Rutinas de Interrupcion

void ADCIntHandler();

//Funciones de Configuracion

void confSys();
void confUART();
void confGPIO();
void confTasks();

void confQueue();
void confADC();
void confTimer();
void ButtonHandler();


//*****************************************************************************
//
// Funcion main(), Inicializa los perifericos, crea las tareas, etc... y arranca el bucle del sistema
//
//*****************************************************************************
int main(void)
{


	color[0]=0x0;
	color[1]=0x0;
	color[2]=0x0;


	confSys();
	confUART();
	confGPIO();
	confADC();
	confQueue();


	SendSemaphore = xSemaphoreCreateMutex();
	EjesSemaphore =	 xSemaphoreCreateMutex();

	xTrazaEventGroup = xEventGroupCreate();


	//
	// Mensaje de bienvenida inicial.
	//
	UARTprintf("\n\nBienvenido a la aplicacion Simulador Vuelo (curso 2014/15)!\n");
	UARTprintf("\nAutores: Anabel Ramirez y Jose Antonio Yebenes ");



	confTasks();
	//
	// Arranca el  scheduler.  Pasamos a ejecutar las tareas que se hayan activado.
	//
	confTimer();
	vTaskStartScheduler();	//el RTOS habilita las interrupciones al entrar aqui, asi que no hace falta habilitarlas

	//De la funcion vTaskStartScheduler no se sale nunca... a partir de aqui pasan a ejecutarse las tareas.
	while(1)
	{
		//Si llego aqui es que algo raro ha pasado
	}
}

void confSys(){
	//
	// Set the clocking to run at 40 MHz from the PLL.
	//
	ROM_SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ |
			SYSCTL_OSC_MAIN);	//Ponermos el reloj principal a 40 MHz (200 Mhz del Pll dividido por 5)


	// Get the system clock speed.
	g_ulSystemClock = SysCtlClockGet();


	//Habilita el clock gating de los perifericos durante el bajo consumo --> perifericos que se desee activos en modo Sleep
	//                                                                        deben habilitarse con SysCtlPeripheralSleepEnable
	ROM_SysCtlPeripheralClockGating(true);

	// Inicializa el subsistema de medida del uso de CPU (mide el tiempo que la CPU no esta dormida)
	// Para eso utiliza un timer, que aqui hemos puesto que sea el TIMER3 (ultimo parametro que se pasa a la funcion)
	// (y por tanto este no se deberia utilizar para otra cosa).
	CPUUsageInit(g_ulSystemClock, configTICK_RATE_HZ/10, 3);
}
void confUART(){
	//
	// Inicializa la UARTy la configura a 115.200 bps, 8-N-1 .
	//se usa para mandar y recibir mensajes y comandos por el puerto serie
	// Mediante un programa terminal como gtkterm, putty, cutecom, etc...
	//
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	ROM_GPIOPinConfigure(GPIO_PA0_U0RX);
	ROM_GPIOPinConfigure(GPIO_PA1_U0TX);
	ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	UARTStdioConfig(0,115200,SysCtlClockGet());

	ROM_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_UART0);	//La UART tiene que seguir funcionando aunque el micro este dormido
	ROM_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOA);	//La UART tiene que seguir funcionando aunque el micro este dormido

}
void confGPIO(){
	//Inicializa el puerto F (LEDs) --> No hace falta si usamos la libreria RGB
	 //   ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	 // ROM_GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3);
	//ROM_GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3, 0);	//LEDS APAGADOS

	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
	GPIOPinTypeGPIOInput(GPIO_PORTE_BASE, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOE);



	//Inicializa los LEDs usando libreria RGB --> usa Timers 0 y 1 (eliminar si no se usa finalmente)
	RGBInit(1);
	SysCtlPeripheralSleepEnable(GREEN_TIMER_PERIPH);
	SysCtlPeripheralSleepEnable(BLUE_TIMER_PERIPH);
	RGBEnable();


	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	ButtonsInit();
	GPIOIntClear(GPIO_PORTF_BASE, GPIO_INT_PIN_0|GPIO_INT_PIN_4);
	GPIOIntRegister(GPIO_PORTF_BASE,ButtonHandler);
	GPIOIntTypeSet(GPIO_PORTF_BASE,GPIO_INT_PIN_0|GPIO_INT_PIN_4, GPIO_RISING_EDGE);
	GPIOIntEnable(GPIO_PORTF_BASE, GPIO_INT_PIN_0|GPIO_INT_PIN_4);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOF);


}
void confTasks(){
	/**Creacion de tareas **/

	// Crea la tarea que gestiona los comandos UART (definida en el fichero commands.c)
	//
	if((xTaskCreate(vUARTTask, (portCHAR *)"Uart", 512,NULL,tskIDLE_PRIORITY + 1, NULL) != pdTRUE))
	{
		while(1);
	}

	UsbSerialInit(32,32);	//Inicializo el  sistema USB

	if(xTaskCreate(CommandProcessingTask, (portCHAR *)"usbser",512, NULL, tskIDLE_PRIORITY + 2, NULL) != pdTRUE)
	{
		while(1);
	}

}

void confADC(){



	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);   // Habilita ADC0
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_ADC0);
	ADCSequenceDisable(ADC0_BASE,0); // Deshabilita el secuenciador 1 del ADC0 para su configuracion


	HWREG(ADC0_BASE + ADC_O_PC) = (ADC_PC_SR_500K);	// usar en lugar de SysCtlADCSpeedSet
	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_TIMER, 0);// Disparo de muestreo por instrucciones de Timer
	ADCHardwareOversampleConfigure(ADC0_BASE, 64); //SobreMuestreo de 64 muestras

	// Configuramos los 4 conversores del secuenciador 1 para muestreo del sensor de temperatura
	ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH0); //Sequencer Step 0: Samples Channel PE3
	ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH1); //Sequencer Step 1: Samples Channel PE2
	ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH2 | ADC_CTL_IE | ADC_CTL_END); //Sequencer Step 2: Samples Channel PE1

	IntPrioritySet(INT_ADC0SS0,5<<5);
	// Tras configurar el secuenciador, se vuelve a habilitar
	ADCSequenceEnable(ADC0_BASE, 0);
	//Asociamos la funcion a la interrupcion
	ADCIntRegister(ADC0_BASE, 0,ADCIntHandler);

	//Activamos las interrupciones
	ADCIntEnable(ADC0_BASE,0);


}

void confTimer(){
	// Configuracion TIMER0
	// Habilita periferico Timer0

	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER2);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_TIMER2);
	TimerControlStall(TIMER2_BASE,TIMER_A,true);
	// Configura el Timer0 para cuenta periodica de 32 bits (no lo separa en TIMER0A y TIMER0B)
	TimerConfigure(TIMER2_BASE, TIMER_CFG_PERIODIC);
	uint32_t ui32Period = SysCtlClockGet() *0.1;
	// Carga la cuenta en el Timer0A
	TimerLoadSet(TIMER2_BASE, TIMER_A, ui32Period -1);
	//Configuramos el Timer como el TRIGGER del ADC
	TimerControlTrigger(TIMER2_BASE,TIMER_A,true);
	// Activa el Timer0A (empezara a funcionar)
	TimerEnable(TIMER2_BASE, TIMER_A);
}

void confQueue(){
		uint32_t potenciometros[3];
		potQueue = xQueueCreate( 1, sizeof(potenciometros) );

		velocidadQueue=xQueueCreate(1,sizeof(uint32_t));
}

void ADCIntHandler(){

	ADCIntClear(ADC0_BASE, 0); // Limpia el flag de interrupcion del ADC
	uint32_t potenciometros[3];
	BaseType_t xHigherPriorityTaskWoken;
	xHigherPriorityTaskWoken = pdFALSE;
	// leemos los datos del secuenciador
	 ADCSequenceDataGet(ADC0_BASE, 0, potenciometros);

	 //Enviamos el dato a la tarea
	 xQueueSendFromISR( potQueue,potenciometros,&xHigherPriorityTaskWoken);
	 if(xHigherPriorityTaskWoken == pdTRUE){
		 vPortYieldFromISR();
	 }

}

void ButtonHandler(){
	uint32_t mask=GPIOIntStatus(GPIO_PORTF_BASE,false);

	if(mask & GPIO_PIN_4){
		//Boton izquierdo
		if((xTaskCreate(PilAuto, (signed portCHAR *)"Piloto Auto", LED1TASKSTACKSIZE,NULL,tskIDLE_PRIORITY + 1, &PilautTaskHandle) != pdTRUE))
		{
			while(1);
		}
		pilotoAutomatico=true;
	}

	if(mask & GPIO_PIN_0){
		//boton derecho
		pilotoAutomatico=false;
	}

	GPIOIntClear(GPIO_PORTF_BASE,GPIO_PIN_0|GPIO_PIN_4);
}

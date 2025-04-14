#include <stm32f3xx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adc.h"
#include "leds.h"
#include "usart.h"
#include "systick_delay.h"
#include "PLL_Config.h"
#include "pwm.h"



//------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------
//ELEC 344 EXAMPLE CODE FOR MPPT
//BY STUART MACVEIGH		14/02/2023
//------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------

//REMEMBER TO TO KEEP ALL FILES TOGETHER, WHEN TRANSPORTING FILES BETWEEN PCs KEEP THEM ALL IN THE FOLDER!!


//ANALOG INPUTS - the two analogue input signals should be connected to PA0 and PA1
//call the "convert_PA0" and "convert_PA1" functions to read analogue values from PA0 and PA1 respectively
//an example is given in the loop below

//PWM OUTPUT - 
//signal is output from PB0
//The pwm frequency is set by parsing the value in Hz into the "init_pwm" function as shown in the init section below
//to control the pwm output, call the "output_pwm" function and parse the desired duty cycle in percent from 0 to 100.

//ON-BOARD LED - call the "led_on" and "led_off" functions to turn the led on and off respectively

//DELAYS - call the "delay_nms" function and parse the number of milliseconds to delay

//TERMINAL OUTPUT - call the "print_terminal" function and parse a string to print it to the PC terminal 
//numeric values can be formatted to a string using the "sprintf" function (an example is shown in the loop below)


//Variables/////////////////////////////////////////////////////////////
#define BUFFER_SIZE 128
enum stage eSwitch;
enum stage eSwitchPrev;
float flVolt = 0;
float flCurr = 0;
float flPower = 0;
float flPowerPrev = 0;
float flVoltPrev = 0;
float flCurrPrev = 0;
float flVref = 75;
float flVrefPrev = 0;
int xPositivePower = 0;
float flStep = 0.25;
int iIncrement = 0;

struct stPOWER{ //Buffer strcutre.
	float volt;
	float curr;
};

enum stage{
	CHECK = 0,
	CHANGE,
	P_INCREASE,
	P_DECREASE,
	V_INCREASE,
	V_DECREASE};

struct stPOWER stPowerBuffer[BUFFER_SIZE]; //Create a buffer of BUFFER_SIZE

//Functions/////////////////////////////////////////////////////////////////
float fxADCVolt (float adcVolt){
	return ((adcVolt / 4095) * 3.3 * 7.8);
}


float fxADCAmp (float adcAmp){
	return ((adcAmp / 4095) * 3.3 / 11.0 );
}


//Cals the power
float fxPower(float flVolt, float flAmp){
	return flVolt * flAmp;
}


//Main////////////////////////////////////////////////////////////////////
int main(void)
{
	//Setup///////////////////////////////////////////
	PLL_Config();
	SystemCoreClockUpdate();
	SysTick_Init();
	init_usart(115200);		//set to use 115200 baud
	

	ADC_Init();
	init_led();
	init_pwm(100000);
	
	//Variables//////////////////////////////////////

	unsigned short n=0;
	char term_msg[128];
	char term_msg_prev[128];
	char vref_msg[128];
	char state_msg[128];
	
	float flAverageVolt = 0;
	float flAverageCurr = 0;
	
	//Init the buffer with the first round of samples. 
	for(int i = 0; i < BUFFER_SIZE; i++){
		stPowerBuffer[i].volt = fxADCVolt(convert_PA0);
		flAverageVolt += stPowerBuffer[i].volt;
		
		stPowerBuffer[i].curr = fxADCAmp(convert_PA1);
		flCurr += stPowerBuffer[i].curr;
	}
	
	int j=0;
	int print = 0;
	

	
	while(1)
	{
		delay_nms(1);
		led_on();
		//--------------------------------------------
		//MPPT ALGORITHM BEGIN
		
		//Remove the old data stored in j from average
		flAverageVolt -= stPowerBuffer[j].volt;
		flAverageCurr -= stPowerBuffer[j].curr;
		
		if (flAverageCurr < 0) {flAverageCurr = 0;}
		
		
		//Store the next value in j
		stPowerBuffer[j].volt = fxADCVolt(convert_PA0);
		stPowerBuffer[j].curr = fxADCAmp(convert_PA1);
		
		//Add the data to the average
		flAverageVolt += stPowerBuffer[j].volt;
		flAverageCurr += stPowerBuffer[j].curr;
		
		//Increment for next store
		j++; 
		
		//Reset the element to 0 to start again
		if(j == BUFFER_SIZE){
			j = 0; 
		}
		
		//Average the values
		flVolt = flAverageVolt / BUFFER_SIZE;
		flCurr = flAverageCurr / BUFFER_SIZE;
		
		//Remove the dc error from signals.
		flVolt -= 0.23;
		flCurr -= 0.00006;
		
		//Ensure that the values are not negative. 
		if (flVolt < 0) {flVolt = 0.0f;}
		if (flCurr < 0) {flCurr = 0.0f;}
		
		//Calculate the power.
		flPower = fxPower(flVolt, flCurr);
					
		///////////////////////////////////////////
		switch (eSwitch) {
			
				case CHECK:
		
						if (flPower == 0){ //If zero then recheck. 
							flVref = 76;
							eSwitch = CHECK;
						} else if (fabs (flPower - flPowerPrev) < 0.0005){
								eSwitch = CHECK;
						} else {
								eSwitch = CHANGE;
						}
						break;

				case CHANGE:
						// Check if power increased or decreased
						if (flPower - flPowerPrev > 0) {
							eSwitch = P_INCREASE;
						} else {
								eSwitch = P_DECREASE;
						}
						break;

				case P_INCREASE:
						// Power increased, check if voltage also increased
						if (flVolt - flVoltPrev > 0){
								eSwitch = V_INCREASE; // Increase Vref
						} else {
								eSwitch = V_DECREASE; // Decrease Vref
						}
						break;

				case P_DECREASE:
						// Power decreased, check if voltage increased
						if (flVolt - flVoltPrev > 0){
								eSwitch = V_DECREASE; // Decrease Vref
						} else {
								eSwitch = V_INCREASE; // Increase Vref
						}
						break;

				case V_INCREASE:
						flVref += flStep; //Duty --
						if (flVref > 95) flVref = 95;	
						eSwitch = CHECK;
						break;

				case V_DECREASE:

						flVref -= flStep; //Duty ++
						if (flVref < 20) flVref = 20;		
						eSwitch = CHECK;
						break;

				default:
						eSwitch = CHECK;
						break;
		}
	

		//PRINT TERMINAL 
		float flDuty = 100 - flVref;
		delay_nms(10);
		output_pwm(flDuty);
		print++;
		
		if(print == 1){
			print = 0;
			sprintf(term_msg, "Duty: %.1f%% | P: %.6f W | Iin: %.5f A Vin: %2.2f V\n\r", flDuty, flPower, flCurr, flVolt); 
			print_terminal(term_msg);
		}
		
		
		flPowerPrev = flPower;
		flVoltPrev = flVolt;
		flCurrPrev = flCurr;

		//--------------------------------------------
		//MPPT ALGORITHM END
		led_off();
	
	
	}//end while
}

#include "Arduino.h"
#include <ArduinoBLE.h>
#include "BT2Reader.h"
#include "Renogy-BT2-Reader.h"

/**	Adafruit nrf52 code for communicating with Renogy DCC series MPPT solar controllers and DC:DC converters
 * These include:
 * Renogy DCC30s - https://www.renogy.com/dcc30s-12v-30a-dual-input-dc-dc-on-board-battery-charger-with-mppt/
 * Renogy DCC50s - https://www.renogy.com/dcc50s-12v-50a-dc-dc-on-board-battery-charger-with-mppt/ 
 * 
 * This library can read operating parameters.  While it is possible with some effort to modify this code to
 * write parameters, I have not exposed this here, as it is a security risk.  USE THIS AT YOUR OWN RISK!
 * 
 * Copyright Neil Shepherd 2022
 * Released under GPL license
 * 
 * Thanks go to Wireshark for allowing me to read the bluetooth packets used 
 */


char * RENOGY_DEVICE_NAME = "BT-TH-66F94E1C    ";
BLEDevice myDevice;
BT2Reader bt2Reader;
long lastChecked=0;
int commandSequenceToSend=0;


void setup() 
{
	Serial.begin(115200);
	delay(3000);
	Serial.println("ArduinoBLE (via ESP32-S3) connecting to Renogy BT-2 code example");
	Serial.println("-----------------------------------------------------\n");
	if (!BLE.begin()) 
	{
		Serial.println("starting Bluetooth® Low Energy module failed!");
		while (1) {delay(1000);};
	}
	BLE.setDeviceName("BT2 Reader");

	//Set callback functions
	BLE.setEventHandler(BLEDiscovered, scanCallback);
	BLE.setEventHandler(BLEConnected, connectCallback);
	BLE.setEventHandler(BLEDisconnected, disconnectCallback);	

	bt2Reader.setLoggingLevel(BT2READER_VERBOSE);
	bt2Reader.addTargetBT2Device(RENOGY_DEVICE_NAME);
	bt2Reader.begin();

	// start scanning for peripherals
	delay(2000);
	Serial.println("About to start scanning");	
	BLE.scan();  //scan with dupliates
}


/** Scan callback method.  
 */
void scanCallback(BLEDevice peripheral) 
{
	bt2Reader.scanCallback(peripheral);
}


/** Connect callback method.  Exact order of operations is up to the user.  if bt2Reader attempted a connection
 * it returns true (whether or not it succeeds).  Usually you can then skip any other code in this connectCallback
 * method, because it's unlikely to be relevant for other possible connections, saving CPU cycles
 */ 
void connectCallback(BLEDevice peripheral) 
{
	if (bt2Reader.connectCallback(peripheral)) 
	{
		myDevice = peripheral;
		Serial.print("Connected to BT2 device ");
		Serial.println(peripheral.localName());
	}
}

void disconnectCallback(BLEDevice peripheral) 
{
	if (bt2Reader.disconnectCallback(peripheral)) 
	{
		//myDevice = NULL;
		Serial.print("Disconnected to BT2 device ");
		Serial.println(peripheral.localName());
	}
}

void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic)
{
	if (bt2Reader.notifyCallback(peripheral,characteristic)) 
	{
		Serial.print("Characteristic notify from ");
		Serial.println(peripheral.localName());
	}
}

void loop() 
{
	BLE.poll();

	if (myDevice && millis()>lastChecked+5000) 
	{
		lastChecked=millis();

		uint16_t startRegister = bt2Commands[commandSequenceToSend].startRegister;
		uint16_t numberOfRegisters = bt2Commands[commandSequenceToSend].numberOfRegisters;
		uint32_t sendReadCommandTime = millis();
		bt2Reader.sendReadCommand(myDevice, startRegister, numberOfRegisters);
		commandSequenceToSend++;
		if(commandSequenceToSend>7)  commandSequenceToSend=0;

		while (!bt2Reader.getIsNewDataAvailable(myDevice) && (millis() - sendReadCommandTime < 5000)) 
		{
			BLE.poll();
			delay(2);
		}

		if (millis() - sendReadCommandTime >= 5000) 
		{
			Serial.println("Timeout error; did not receive response from BT2 within 5 seconds");
		} 
		else 
		{
			Serial.printf("Received response for %d registers 0x%04X - 0x%04X in %dms: ", 
					numberOfRegisters,
					startRegister,
					startRegister + numberOfRegisters - 1,
					(millis() - sendReadCommandTime)
			);
			bt2Reader.printHex(bt2Reader.getDevice(myDevice)->dataReceived, bt2Reader.getDevice(myDevice)->dataReceivedLength, false);

			
			for (int i = 0; i < numberOfRegisters; i++) 
			{
				Serial.printf("Register 0x%04X contains %d\n",
					startRegister + i,
					bt2Reader.getRegister(myDevice, startRegister + i)->value
				);
			}

			for (int i = 0; i < numberOfRegisters; i++) 
			{
				bt2Reader.printRegister(myDevice, startRegister + i);
			}
		}
	}
}
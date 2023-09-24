#include "BT2Reader.h"

/**	Code for communicating with Renogy DCC series MPPT solar controllers and DC:DC converters using ArduinoBLE.
 * 
 * These include:
 * Renogy DCC30s - https://www.renogy.com/dcc30s-12v-30a-dual-input-dc-dc-on-board-battery-charger-with-mppt/
 * Renogy DCC50s - https://www.renogy.com/dcc50s-12v-50a-dc-dc-on-board-battery-charger-with-mppt/ 
 * 
 * This library can read operating parameters.  While it is possible with some effort to modify this code to
 * write parameters, I have not exposed this here, as it is a security risk.  USE THIS AT YOUR OWN RISK!
 * 
 * Other devices that use the BT-2 could likely use this library too, albeit with additional register lookups
 * Currently this library only supports connecting to one BT2 device, future versions will have support for 
 * multiple connected devices  
 * 
 * Written by Neil Shepherd 2022, ported to ArduinoBLE by Mark Wilkie 2023
 * Released under GPL license
 * 
 * Thanks go to Wireshark for allowing me to read the bluetooth packets used 
 */

extern void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic);


void BT2Reader::addTargetBT2Device(char * peerName) 
{
	memcpy(peripheryName, peerName, strlen(peerName));
	log("Added target %s to device %s\n", peerName,peripheryName);
}

void BT2Reader::addTargetBT2Device(uint8_t * peerAddress) 
{
	memcpy(peripheryAddress, peerAddress, 6);
	log("Added target peer Address ");
	for (int i = 0; i < 6; i++) { logprintf("%02X ",peerAddress[i]); }
	logprintf("to device %s\n",peripheryAddress);
}


void BT2Reader::begin() 
{
	//Init our register data
	registerDescriptionSize = sizeof(registerDescription) / sizeof(registerDescription[0]);
	registerValueSize = 0;
	for (int i = 0; i < registerDescriptionSize; i++) { registerValueSize += (registerDescription[i].bytesUsed / 2); }

	log("Register Description is %d entries, registerValue is %d entries\n", registerDescriptionSize, registerValueSize);

	dataReceivedLength = 0;
	dataError = false;
	registerExpected = 0;
	newDataAvailable = false;

	for (int j = 0; j < registerDescriptionSize; j++) {
		int registerLength = registerDescription[j].bytesUsed / 2;
		int registerAddress = registerDescription[j].address;

		for (int k = 0; k < registerLength; k++) {
			registerValues[j+k].lastUpdateMillis = 0;
			registerValues[j+k].value = 0;
			registerValues[j+k].registerAddress = registerAddress;
			registerAddress++;
		}
	}

	invalidRegister.lastUpdateMillis = 0;
	invalidRegister.value = 0;
}


boolean BT2Reader::scanCallback(BLEDevice peripheral) 
{
    // discovered a peripheral, print out address, local name, and advertised service
	log("Found device %s at %s with uuuid %s\n",peripheral.localName().c_str(),peripheral.address().c_str(),peripheral.advertisedServiceUuid().c_str());

	if (peripheral.localName() == peripheryName || (memcmp(peripheral.address().c_str(),peripheryAddress, 6) == 0))
	{
		log("Found targeted BT2 device, attempting connection\n");
		peripheral.connect();
		return true;
	} 
				
	//If we got here, something went wrong
	return false;
}


boolean BT2Reader::connectCallback(BLEDevice myDevice) 
{
	bt2Device = myDevice;
	if(myDevice.discoverService(TX_SERVICE_UUID))
	{
		log("Renogy Tx service %s discovered\n",TX_SERVICE_UUID);
		txDeviceCharateristic=myDevice.characteristic(TX_CHARACTERISTIC_UUID);
		if(txDeviceCharateristic)
		{
			logerror("Renogy Tx characteristic not discovered, disconnecting\n");
			myDevice.disconnect();
			connected=false;
			return true;
		}
	} 
	else 
	{
		logerror("Renogy Tx service not discovered, disconnecting\n");
		myDevice.disconnect();
		return true;
	}

	if(myDevice.discoverService(RX_SERVICE_UUID))
	{
		log("Renogy Rx service %s discovered\n",RX_SERVICE_UUID);
		if(rxDeviceCharateristic=myDevice.characteristic(RX_CHARACTERISTIC_UUID))
		{
			if(rxDeviceCharateristic.canSubscribe() && rxDeviceCharateristic.subscribe())
			{
				rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
			}
			else
			{
				logerror("Renogy Rx characteristic not discovered or cannot be subscribed to, disconnecting\n");
				myDevice.disconnect();
				connected=false;
				return true;
			} 
		}
	} 
	else 
	{
		logerror("Renogy Rx service not discovered, disconnecting\n");
		myDevice.disconnect();
		return true;
	}

	connected=true;
	return true;
}

void BT2Reader::disconnectCallback(BLEDevice myDevice) 
{
	connected=false;
	memset(peripheryAddress, 0, 6);
	memset(peripheryName, 0, 20);

}

boolean BT2Reader::notifyCallback(BLEDevice myDevice, BLECharacteristic characteristic) 
{
	if (dataError) { return false; }									// don't append anything if there's already an error

	//Read data
	dataError = !appendRenogyPacket(characteristic);		// append second or greater packet

	if (!dataError && dataReceivedLength == getExpectedLength(dataReceived)) 
	{
		if (getIsReceivedDataValid(dataReceived)) 
		{
			//Serial.printf("Complete datagram of %d bytes, %d registers (%d packets) received:\n", 
			//	device->dataReceivedLength, device->dataReceived[2], device->dataReceivedLength % 20 + 1);
			//printHex(device->dataReceived, device->dataReceivedLength);
			processDataReceived();

			uint8_t bt2Response[21] = "main recv data[XX] [";
			for (int i = 0; i < dataError; i+= 20) 
			{
				bt2Response[15] = HEX_LOWER_CASE[(dataReceived[i] / 16) & 0x0F];
				bt2Response[16] = HEX_LOWER_CASE[(dataReceived[i]) & 0x0F];
				Serial.printf("Sending response #%d to BT2: %s\n", i, bt2Response);
				txDeviceCharateristic.writeValue(bt2Response, 20);
			}
		} 
		else 
		{
			Serial.printf("Checksum error: received is 0x%04X, calculated is 0x%04X\n", 
				getProvidedModbusChecksum(dataReceived), getCalculatedModbusChecksum(dataReceived));

			return false;
		}
	} 

	return true;
}


/** Appends received data.  Returns false if there's potential for buffer overrun, true otherwise
 */
boolean BT2Reader::appendRenogyPacket(BLECharacteristic characteristic) 
{
	int dataLen=characteristic.valueLength();
	if(dataLen<0)
		return true;

	if (dataLen + dataReceivedLength >= DEFAULT_DATA_BUFFER_LENGTH -1) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}

	characteristic.readValue(&dataReceived[dataReceivedLength],dataLen);
	dataReceivedLength += dataLen;
	if (getExpectedLength(dataReceived) < dataReceivedLength) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}
	return true;
}

void BT2Reader::processDataReceived() {

	int registerOffset = 0;
	int registersProvided = dataReceived[2] / 2;
	
	while (registerOffset < registersProvided) {
		int registerIndex = getRegisterValueIndex(registerExpected + registerOffset);
		if (registerIndex >= 0) {
			uint8_t msb = dataReceived[registerOffset * 2 + 3];
			uint8_t lsb = dataReceived[registerOffset * 2 + 4];
			registerValues[registerIndex].value = msb * 256 + lsb;
			registerValues[registerIndex].lastUpdateMillis = millis();
		}
		registerOffset++;
	}	
	newDataAvailable = true;
}


void BT2Reader::sendReadCommand(uint16_t startRegister, uint16_t numberOfRegisters) 
{
	uint8_t command[20];
	command[0] = 0xFF;
	command[1] = 0x03;
	command[2] = (startRegister >> 8) & 0xFF;
	command[3] = startRegister & 0xFF;
	command[4] = (numberOfRegisters >> 8) & 0xFF;
	command[5] = numberOfRegisters & 0xFF;
	uint16_t checksum = getCalculatedModbusChecksum(command, 0, 6);
	command[6] = checksum & 0xFF;
	command[7] = (checksum >> 8) & 0xFF;

	log("Sending command sequence: ");
	for (int i = 0; i < 8; i++) { logprintf("%02X ", command[i]); }
	logprintf("\n");

	txDeviceCharateristic.writeValue(command, 8);
	registerExpected = startRegister;
	dataReceivedLength = 0;
	dataError = false;
	newDataAvailable = false;
}


int BT2Reader::getRegisterValueIndex(uint16_t registerAddress) {
	int left = 0;
	int right = registerValueSize - 1;
	while (left <= right) {
		int mid = (left + right) / 2;
		if (registerValues[mid].registerAddress == registerAddress) { return mid; }
		if (registerValues[mid].registerAddress < registerAddress) { 
			left = mid + 1;
		} else {
			right = mid -1;
		}
	}
	return -1;
}

int BT2Reader::getRegisterDescriptionIndex(uint16_t registerAddress) {
	int left = 0;
	int right = registerDescriptionSize - 1;
	while (left <= right) {
		int mid = (left + right) / 2;
		if (registerDescription[mid].address == registerAddress) { return mid; }
		if (registerDescription[mid].address < registerAddress) { 
			left = mid + 1;
		} else {
			right = mid -1;
		}
	}
	return -1;
}


int BT2Reader::getExpectedLength(uint8_t * data) { return data[2] + 5; }

REGISTER_VALUE * BT2Reader::getRegister(uint16_t registerAddress) 
{
	int registerValueIndex = getRegisterValueIndex(registerAddress);
	if (registerValueIndex < 0) { return &invalidRegister; }
	return (&(registerValues[registerValueIndex]));
	}

boolean BT2Reader::getIsNewDataAvailable() 
{
	boolean isNewDataAvailable = newDataAvailable;
	newDataAvailable = false;
	return (isNewDataAvailable);
}

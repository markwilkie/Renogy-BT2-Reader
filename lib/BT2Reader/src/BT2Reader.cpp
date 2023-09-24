#include "BT2Reader.h"

BT2Reader * BT2Reader::_pointerToBT2ReaderClass = NULL;
extern void mainNotifyCallback(BLEDevice peripheral, BLECharacteristic characteristic);


int BT2Reader::setDeviceTableSize(int i) 
{
	deviceTableSize = min(max(1, i), MAXIMUM_BT2_DEVICES);
	//deviceTable = new DEVICE[deviceTableSize];
	for (int i = 0; i < deviceTableSize; i++) 
	{
		memset(deviceTable[i].peerName, 0, 20);
		memset(deviceTable[i].peerAddress, 0, 6);
		deviceTable[i].slotNamed = false;
		//deviceTable[i].device = NULL;
	}
	log("deviceTable is %d entries long\n", deviceTableSize);
	return deviceTableSize;
}


boolean BT2Reader::addTargetBT2Device(char * peerName) 
{
	if (deviceTableSize == 0) { setDeviceTableSize(1); }
	for (int i = 0; i < deviceTableSize; i++) 
	{
		if (!deviceTable[i].slotNamed) 
		{
			memcpy(deviceTable[i].peerName, peerName, strlen(peerName));
			deviceTable[i].slotNamed = true;
			log("added device %s to deviceTable\n", peerName);
			return true;
		}	
	}
	return false;
}

boolean BT2Reader::addTargetBT2Device(uint8_t * peerAddress) 
{
	if (deviceTableSize == 0) { setDeviceTableSize(1); }
	for (int i = 0; i < deviceTableSize; i++) 
	{
		if (!deviceTable[i].slotNamed) 
		{
			memcpy(deviceTable[i].peerAddress, peerAddress, 6);
			deviceTable[i].slotNamed = true;
			log("Added target peer Address ");
			for (int i = 0; i < 6; i++) { logprintf("%02X ",peerAddress[i]); }
			logprintf("to deviceTable\n");
			return true;
		}
	}
	return false;
}


void BT2Reader::begin() 
{
	if (deviceTableSize == 0) { setDeviceTableSize(1); }
	_pointerToBT2ReaderClass = this;
	registerDescriptionSize = sizeof(registerDescription) / sizeof(registerDescription[0]);
	registerValueSize = 0;
	for (int i = 0; i < registerDescriptionSize; i++) { registerValueSize += (registerDescription[i].bytesUsed / 2); }

	log("Register Description is %d entries, registerValue is %d entries\n", registerDescriptionSize, registerValueSize);

	for (int i = 0; i < deviceTableSize; i++) {
		DEVICE * device = &deviceTable[i];
		//device->registerValues = new REGISTER_VALUE[registerValueSize];
		//device->device = NULL;
		device->dataReceivedLength = 0;
		device->dataError = false;
		device->registerExpected = 0;
		device->newDataAvailable = false;

		for (int j = 0; j < registerDescriptionSize; j++) {
			int registerLength = registerDescription[j].bytesUsed / 2;
			int registerAddress = registerDescription[j].address;

			for (int k = 0; k < registerLength; k++) {
				device->registerValues[j+k].lastUpdateMillis = 0;
				device->registerValues[j+k].value = 0;
				device->registerValues[j+k].registerAddress = registerAddress;
				registerAddress++;
			}
		}
	}
	invalidRegister.lastUpdateMillis = 0;
	invalidRegister.value = 0;
}


boolean BT2Reader::scanCallback(BLEDevice peripheral) 
{
	//We're already connected to everything we care about, so ignore
	if (numberOfConnections == deviceTableSize) { return false; }

    // discovered a peripheral, print out address, local name, and advertised service
	log("Found device %s at %s with uuuid %s\n",peripheral.localName().c_str(),peripheral.address().c_str(),peripheral.advertisedServiceUuid().c_str());

	for (int i = 0; i < deviceTableSize; i++) 
	{
		if (peripheral.localName() == deviceTable[i].peerName
		 || (memcmp(peripheral.address().c_str(),deviceTable[i].peerAddress, 6) == 0))
		{
			if (deviceTable[i].slotNamed) 
			{
				log("Found targeted BT2 device, attempting connection\n");
				peripheral.connect();
				return true;
			} 
			else 
			{
				log("Found untargeted BT2 device %s entries long\n", peripheral.localName().c_str());
			}
		} 
	}						

	//If we got here, something went wrong
	return false;
}


boolean BT2Reader::connectCallback(BLEDevice myDevice) 
{
	numberOfConnections++;
	log("Connected to device %s, active connections = %d\n", myDevice.localName().c_str(), numberOfConnections);
	
	//stop scanning?
	if(numberOfConnections==deviceTableSize)
	{
		log("All targetting devices connected.  Stopping scanning\n");
		BLE.stopScan();
	}

	//Loop through our devices
	for (int i = 0; i < deviceTableSize; i++) 
	{
		DEVICE * device = &deviceTable[i];
		device->device = myDevice;
		if(myDevice.discoverService(device->TX_SERVICE_UUID))
		{
			log("Renogy Tx service %s discovered\n",device->TX_SERVICE_UUID);
			device->txDeviceCharateristic=myDevice.characteristic(device->TX_CHARACTERISTIC_UUID);
			if(device->txDeviceCharateristic)
			{
				Serial.println("Renogy Tx characteristic found!");
			}
			else
			{
				logerror("Renogy Tx characteristic not discovered, disconnecting\n");
				myDevice.disconnect();
				return true;
			}
		} 
		else 
		{
			logerror("Renogy Tx service not discovered, disconnecting\n");
			myDevice.disconnect();
			return true;
		}

		if(myDevice.discoverService(device->RX_SERVICE_UUID))
		{
			Serial.println("Renogy Rx service discovered");
			if(device->rxDeviceCharateristic=myDevice.characteristic(device->RX_CHARACTERISTIC_UUID))
			{
				if(device->rxDeviceCharateristic.canSubscribe() && device->rxDeviceCharateristic.subscribe())
				{
					device->rxDeviceCharateristic.setEventHandler(BLEWritten, mainNotifyCallback);
					Serial.println("Renogy Rx characteristic found and subscribed to!");
				} 
			}
		} 
		else 
		{
			logerror("Renogy Rx service or characteristic not discovered, disconnecting\n");
			myDevice.disconnect();
			return true;
		}
		return true;
	}
	return false;
}

boolean BT2Reader::disconnectCallback(BLEDevice myDevice) {
	
	for (int i = 0; i < deviceTableSize; i++) 
	{
		if (deviceTable[i].device == myDevice) 
		{
			//deviceTable[i].device = NULL;
			if (!deviceTable[i].slotNamed) 
			{
				memset(deviceTable[i].peerAddress, 0, 6);
				memset(deviceTable[i].peerName, 0, 20);
			}
		}
		numberOfConnections--;
		log("Disconnected, active connections = %d.  Starting scan again\n", numberOfConnections);
		BLE.scan();
		return true;
	}
	return false;
}


boolean BT2Reader::notifyCallback(BLEDevice myDevice, BLECharacteristic characteristic) {

	int index = getDeviceIndex(characteristic);
	if (index < 0) 
	{
		logerror("renogyNotifyCallback; characteristic not found in device table\n");
		return false;
	}

	DEVICE * device = &deviceTable[index];

	if (device->dataError) { return false; }									// don't append anything if there's already an error

	//Read data
	device->dataError = !appendRenogyPacket(device, characteristic);		// append second or greater packet

	if (!device->dataError && device->dataReceivedLength == getExpectedLength(device->dataReceived)) 
	{
		if (getIsReceivedDataValid(device->dataReceived)) 
		{
			//Serial.printf("Complete datagram of %d bytes, %d registers (%d packets) received:\n", 
			//	device->dataReceivedLength, device->dataReceived[2], device->dataReceivedLength % 20 + 1);
			//printHex(device->dataReceived, device->dataReceivedLength);
			processDataReceived(device);

			uint8_t bt2Response[21] = "main recv data[XX] [";
			for (int i = 0; i < device->dataError; i+= 20) 
			{
				bt2Response[15] = HEX_LOWER_CASE[(device->dataReceived[i] / 16) & 0x0F];
				bt2Response[16] = HEX_LOWER_CASE[(device->dataReceived[i]) & 0x0F];
				Serial.printf("Sending response #%d to BT2: %s\n", i, bt2Response);
				device->txDeviceCharateristic.writeValue(bt2Response, 20);
			}
		} 
		else 
		{
			Serial.printf("Checksum error: received is 0x%04X, calculated is 0x%04X\n", 
				getProvidedModbusChecksum(device->dataReceived), getCalculatedModbusChecksum(device->dataReceived));

			return false;
		}
	} 

	return true;
}


/** Appends received data.  Returns false if there's potential for buffer overrun, true otherwise
 */
boolean BT2Reader::appendRenogyPacket(DEVICE * device, BLECharacteristic characteristic) 
{
	int dataLen=characteristic.valueLength();
	if(dataLen<0)
		return true;

	if (dataLen + device->dataReceivedLength >= DEFAULT_DATA_BUFFER_LENGTH -1) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}

	characteristic.readValue(&device->dataReceived[device->dataReceivedLength],dataLen);
	device->dataReceivedLength += dataLen;
	if (getExpectedLength(device->dataReceived) < device->dataReceivedLength) 
	{
		logerror("Buffer overrun receiving data\n");
		return false;
	}
	return true;
}

void BT2Reader::processDataReceived(DEVICE * device) {

	int registerOffset = 0;
	int registersProvided = device->dataReceived[2] / 2;
	
	while (registerOffset < registersProvided) {
		int registerIndex = getRegisterValueIndex(device, device->registerExpected + registerOffset);
		if (registerIndex >= 0) {
			uint8_t msb = device->dataReceived[registerOffset * 2 + 3];
			uint8_t lsb = device->dataReceived[registerOffset * 2 + 4];
			device->registerValues[registerIndex].value = msb * 256 + lsb;
			device->registerValues[registerIndex].lastUpdateMillis = millis();
		}
		registerOffset++;
	}	
	device->newDataAvailable = true;
}


void BT2Reader::sendReadCommand(char * name, uint16_t startRegister, uint16_t numberOfRegisters) { sendReadCommand(getDeviceIndex(name), startRegister, numberOfRegisters); }
void BT2Reader::sendReadCommand(uint8_t * address, uint16_t startRegister, uint16_t numberOfRegisters) { sendReadCommand(getDeviceIndex(address), startRegister, numberOfRegisters); }
void BT2Reader::sendReadCommand(BLEDevice myDevice, uint16_t startRegister, uint16_t numberOfRegisters) { sendReadCommand(getDeviceIndex(myDevice), startRegister, numberOfRegisters); }
void BT2Reader::sendReadCommand(int index, uint16_t startRegister, uint16_t numberOfRegisters) 
{
	if (index == -1) {
		logerror("SendReadCommand: invalid name, mac address, or index provided\n");
		return;
	}

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

	DEVICE * device = &deviceTable[index];
	device->txDeviceCharateristic.writeValue(command, 8);
	device->registerExpected = startRegister;
	device->dataReceivedLength = 0;
	device->dataError = false;
	device->newDataAvailable = false;
}


int BT2Reader::getRegisterValueIndex(DEVICE * device, uint16_t registerAddress) {
	int left = 0;
	int right = registerValueSize - 1;
	while (left <= right) {
		int mid = (left + right) / 2;
		if (device->registerValues[mid].registerAddress == registerAddress) { return mid; }
		if (device->registerValues[mid].registerAddress < registerAddress) { 
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

int BT2Reader::getDeviceIndex(BLEDevice myDevice) {
	for (int i = 0; i < deviceTableSize; i++) {
			if (myDevice == deviceTable[i].device) { return i; }
		}
	return -1;
}

int BT2Reader::getDeviceIndex(char * name) {
	for (int i = 0; i < deviceTableSize; i++) {
			if (strcmp(name, deviceTable[i].peerName) == 0) { return i; }
		}
	return -1;
}

int BT2Reader::getDeviceIndex(uint8_t * address) {
	for (int i = 0; i < deviceTableSize; i++) {
		if (memcmp(address, deviceTable[i].peerAddress, 6) == 0) { return i; }
	}
	return -1;
}

int BT2Reader::getDeviceIndex(BLECharacteristic characteristic) {
	for (int i = 0; i < deviceTableSize; i++) {
		if (characteristic == deviceTable[i].txDeviceCharateristic) { return i; }
		if (characteristic == deviceTable[i].txDeviceCharateristic) { return i; }
	}
	return -1;
}

REGISTER_VALUE * BT2Reader::getRegister(char * name, uint16_t registerAddress) { return (getRegister(getDeviceIndex(name), registerAddress)); }
REGISTER_VALUE * BT2Reader::getRegister(uint8_t * address, uint16_t registerAddress) { return (getRegister(getDeviceIndex(address), registerAddress)); }
REGISTER_VALUE * BT2Reader::getRegister(BLEDevice myDevice, uint16_t registerAddress) { return (getRegister(getDeviceIndex(myDevice), registerAddress)); }
REGISTER_VALUE * BT2Reader::getRegister(int deviceIndex, uint16_t registerAddress) {
	if (deviceIndex < 0) { return &invalidRegister; }
	int registerValueIndex = getRegisterValueIndex(&deviceTable[deviceIndex], registerAddress);
	if (registerValueIndex < 0) { return &invalidRegister; }
	return (&deviceTable[deviceIndex].registerValues[registerValueIndex]);
}

boolean BT2Reader::getIsNewDataAvailable(char * name) { return (getIsNewDataAvailable(getDeviceIndex(name))); }
boolean BT2Reader::getIsNewDataAvailable(uint8_t * address) { return (getIsNewDataAvailable(getDeviceIndex(address))); }
boolean BT2Reader::getIsNewDataAvailable(BLEDevice myDevice) { return (getIsNewDataAvailable(getDeviceIndex(myDevice))); }
boolean BT2Reader::getIsNewDataAvailable(int index) {
	if (index == -1) { return false; }
	boolean isNewDataAvailable = deviceTable[index].newDataAvailable;
	deviceTable[index].newDataAvailable = false;
	return (isNewDataAvailable);
}

DEVICE * BT2Reader::getDevice(char * name) { return (getDevice(getDeviceIndex(name))); }
DEVICE * BT2Reader::getDevice(uint8_t * address) { return (getDevice(getDeviceIndex(address))); }
DEVICE * BT2Reader::getDevice(BLEDevice myDevice) { return (getDevice(getDeviceIndex(myDevice))); }
DEVICE * BT2Reader::getDevice(int index) { 
	if (index == -1) { return NULL; }
	return (&deviceTable[index]);
}

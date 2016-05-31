/** @file 
    @brief DYNAMIXEL Library Main file
   */ 
/*
  ax12.cpp - ArbotiX library for AX/RX control.
  Copyright (c) 2008-2011 Michael E. Ferguson.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "ax12.h"

/******************************************************************************
 * Hardware Serial Level, this uses the same stuff as Serial1, therefore 
 *  you should not use the Arduino Serial1 library.
 */

unsigned char ax_rx_buffer[AX12_BUFFER_SIZE];
unsigned char ax_tx_buffer[AX12_BUFFER_SIZE];
unsigned char ax_rx_int_buffer[AX12_BUFFER_SIZE];

// making these volatile keeps the compiler from optimizing loops of available()
volatile int ax_rx_Pointer;
volatile int ax_tx_Pointer;
volatile int ax_rx_int_Pointer;
#if defined(AX_RX_SWITCHED)
unsigned char dynamixel_bus_config[AX12_MAX_SERVOS];
#endif

/** helper functions to switch direction of comms */
void dxlSetTX(int id){
    bitClear(UCSR1B, RXEN1); 
  #if defined(AX_RX_SWITCHED)
    if(dynamixel_bus_config[id-1] > 0)
        SET_RX_WR;
    else
        SET_AX_WR;   
  #else
    // emulate half-duplex on ArbotiX, ArbotiX w/ RX Bridge
    #ifdef ARBOTIX_WITH_RX
      PORTD |= 0x10;
    #endif   
    bitSet(UCSR1B, TXEN1);
    bitClear(UCSR1B, RXCIE1);
  #endif
    ax_tx_Pointer = 0;
}
void dxlSetRX(int id){ 
  #if defined(AX_RX_SWITCHED)
    int i;
    // Need to wait for last byte to be sent before turning the bus around.
    // Check the Transmit complete flag
    while (bit_is_clear(UCSR1A, UDRE1));
    for(i=0; i<UBRR1L*15; i++)    
        asm("nop");
    if(dynamixel_bus_config[id-1] > 0)
        SET_RX_RD;
    else
        SET_AX_RD;
  #else
    // emulate half-duplex on ArbotiX, ArbotiX w/ RX Bridge
    #ifdef ARBOTIX_WITH_RX
      int i;
      // Need to wait for last byte to be sent before turning the bus around.
      // Check the Transmit complete flag
      while (bit_is_clear(UCSR1A, UDRE1));
      for(i=0; i<25; i++)    
          asm("nop");
      PORTD &= 0xEF;
    #endif 
    bitClear(UCSR1B, TXEN1);
    bitSet(UCSR1B, RXCIE1);
  #endif  
    bitSet(UCSR1B, RXEN1);
    ax_rx_int_Pointer = 0;
    ax_rx_Pointer = 0;
}
// for sync write
void dxlSetTXall(){
    bitClear(UCSR1B, RXEN1);    
  #if defined(AX_RX_SWITCHED)
    SET_RX_WR;
    SET_AX_WR;   
  #else
    #ifdef ARBOTIX_WITH_RX
      PORTD |= 0x10;
    #endif
    bitSet(UCSR1B, TXEN1);
    bitClear(UCSR1B, RXCIE1);
  #endif
    ax_tx_Pointer = 0;
}

/** Sends a character out the serial port. */
void dxlWrite(unsigned char data){
    while (bit_is_clear(UCSR1A, UDRE1));
    UDR1 = data;
}
/** Sends a character out the serial port, and puts it in the tx_buffer */
void dxlWriteB(unsigned char data){
    ax_tx_buffer[(ax_tx_Pointer++)] = data; 
    while (bit_is_clear(UCSR1A, UDRE1));
    UDR1 = data;
}
/** We have a one-way recieve buffer, which is reset after each packet is receieved.
    A wrap-around buffer does not appear to be fast enough to catch all bytes at 1Mbps. */
ISR(USART1_RX_vect){
    ax_rx_int_buffer[(ax_rx_int_Pointer++)] = UDR1;
}

/** read back the error code for our latest packet read */
int dxlError;
int dxlGetLastError(){ return dxlError; }
/** > 0 = success */
int dxlReadPacket(int length){
    unsigned long ulCounter;
    unsigned char offset, blength, checksum, timeout;
    unsigned char volatile bcount; 

    offset = 0;
    timeout = 0;
    bcount = 0;
    while(bcount < length){
        ulCounter = 0;
        while((bcount + offset) == ax_rx_int_Pointer){
            if(ulCounter++ > 1000L){ // was 3000
                timeout = 1;
                break;
            }
        }
        if(timeout) break;
        ax_rx_buffer[bcount] = ax_rx_int_buffer[bcount + offset];
        if((bcount == 0) && (ax_rx_buffer[0] != 0xff))
            offset++;
        else if((bcount == 2) && (ax_rx_buffer[2] == 0xff))
            offset++;
        else
            bcount++;
    }

    blength = bcount;
    checksum = 0;
    for(offset=2;offset<bcount;offset++)
        checksum += ax_rx_buffer[offset];
    if((checksum%256) != 255){
        return 0;
    }else{
        return 1;
    }
}

/** initializes serial1 transmit at baud, 8-N-1 */
void dxlInit(long baud){
    UBRR1H = (F_CPU / (8 * baud) - 1 ) >> 8;
    UBRR1L = (F_CPU / (8 * baud) - 1 );
    bitSet(UCSR1A, U2X1);
    ax_rx_int_Pointer = 0;
    ax_rx_Pointer = 0;
    ax_tx_Pointer = 0;
#if defined(AX_RX_SWITCHED)
    INIT_AX_RX;
    bitSet(UCSR1B, TXEN1);
    bitSet(UCSR1B, RXEN1);
    bitSet(UCSR1B, RXCIE1);
#else
  #ifdef ARBOTIX_WITH_RX
    DDRD |= 0x10;   // Servo B = output
    PORTD &= 0xEF;  // Servo B low
  #endif
    // set RX as pull up to hold bus to a known level
    PORTD |= (1<<2);
    // enable rx
    dxlSetRX(0);
#endif
}

/******************************************************************************
 * Packet Level
 */

/** Read register value(s) */
int dxlGetRegister(int id, int regstart, int length){  
    dxlSetTX(id);
    // 0xFF 0xFF ID LENGTH INSTRUCTION PARAM... CHECKSUM    
    int checksum = ~((id + 6 + regstart + length)%256);
    dxlWriteB(0xFF);
    dxlWriteB(0xFF);
    dxlWriteB(id);
    dxlWriteB(4);    // length
    dxlWriteB(AX_READ_DATA);
    dxlWriteB(regstart);
    dxlWriteB(length);
    dxlWriteB(checksum);  
    dxlSetRX(id);    
    if(dxlReadPacket(length + 6) > 0){
        dxlError = ax_rx_buffer[4];
        if(length == 1)
            return ax_rx_buffer[5];
        else
            return ax_rx_buffer[5] + (ax_rx_buffer[6]<<8);
    }else{
        return -1;
    }
}

/* Set the value of a single-byte register. */
void dxlSetRegister(int id, int regstart, int data){
    dxlSetTX(id);    
    int checksum = ~((id + 4 + AX_WRITE_DATA + regstart + (data&0xff)) % 256);
    dxlWriteB(0xFF);
    dxlWriteB(0xFF);
    dxlWriteB(id);
    dxlWriteB(4);    // length
    dxlWriteB(AX_WRITE_DATA);
    dxlWriteB(regstart);
    dxlWriteB(data&0xff);
    // checksum = 
    dxlWriteB(checksum);
    dxlSetRX(id);
    //dxlReadPacket();
}
/* Set the value of a double-byte register. */
void dxlSetRegister2(int id, int regstart, int data){
    dxlSetTX(id);    
    int checksum = ~((id + 5 + AX_WRITE_DATA + regstart + (data&0xFF) + ((data&0xFF00)>>8)) % 256);
    dxlWriteB(0xFF);
    dxlWriteB(0xFF);
    dxlWriteB(id);
    dxlWriteB(5);    // length
    dxlWriteB(AX_WRITE_DATA);
    dxlWriteB(regstart);
    dxlWriteB(data&0xff);
    dxlWriteB((data&0xff00)>>8);
    // checksum = 
    dxlWriteB(checksum);
    dxlSetRX(id);
    //dxlReadPacket();
}


void dxlRegWrite(int id, int regstart, int data)
{
    dxlSetTX(id);                  //
    int length = 4;             //parameter low byte + register # + id + inst
    int checksum = ~((id + length + AX_REG_WRITE + regstart + (data&0xff)) % 256);
    dxlWriteB(0xFF);           //header 1  
    dxlWriteB(0xFF);           //header 2
    dxlWriteB(id);             //id for command 
    dxlWriteB(length);         //packet length
    dxlWriteB(AX_REG_WRITE);   //instruction
    dxlWriteB(regstart);       //register to write byte at
    dxlWriteB(data&0xff);      //paramater data to set
    dxlWriteB(checksum);       //checksum byte of all non-header bytes
    dxlSetRX(id);                  //get ready to read return packet

}
void dxlRegWrite2(int id, int regstart, int data)
{

    dxlSetTX(id);    
    int length = 4;                 //parameter low byte + parameter high byte + register # + id + inst
    int checksum = ~((id + length + AX_REG_WRITE + regstart + (data&0xFF) + ((data&0xFF00)>>8)) % 256);
    dxlWriteB(0xFF);               //header 1
    dxlWriteB(0xFF);               //header 2
    dxlWriteB(id);                 //id for command
    dxlWriteB(length);             //packet length
    dxlWriteB(AX_REG_WRITE);       //instruction
    dxlWriteB(regstart);           //register to write byte at
    dxlWriteB(data&0xff);          //paramater to set (Low byte)       
    dxlWriteB((data&0xff00)>>8);   //parameter data (high byte)
    dxlWriteB(checksum);           //checksum byte of all non-header bytes
    dxlSetRX(id);                      //get ready to read return packet

}



void dxlAction(int id)
{
    dxlSetTX(id);    
    int length = 2;         //  id + inst
    int checksum = ~((id + length + AX_ACTION)  % 256);  //checksum is the inverse of all non header bytes
    dxlWriteB(0xFF);               //header 1
    dxlWriteB(0xFF);               //header 2
    dxlWriteB(id);                 //id for command
    dxlWriteB(length);             //packet length
    dxlWriteB(AX_ACTION);          //instruction
    dxlWriteB(checksum);           //checksum byte 
    dxlSetRX(0);     //don't need this, no return packet right?                 //get ready to read return packet
}




void dxlPing(int id)
{
    dxlSetTX(id);    
    int length = 2;                 //  id + inst
    int checksum = ~((id + length + AX_PING)  % 256);    //checksum is the inverse of all non header bytes
    dxlWriteB(0xFF);               //header 1
    dxlWriteB(0xFF);               //header 2
    dxlWriteB(id);                 //id for command
    dxlWriteB(length);             //packet length
    dxlWriteB(AX_PING);            //instruction
    dxlWriteB(checksum);           //checksum byte of all non-header bytes  
    dxlSetRX(id);             //get ready to read return packet
}



void dxlReset(int id)
{
    dxlSetTX(id);    
    int length = 2;                 //  id + inst
    int checksum = ~((id + length + AX_RESET)  % 256);    //checksum is the inverse of all non header bytes
    dxlWriteB(0xFF);               //header 1
    dxlWriteB(0xFF);               //header 2
    dxlWriteB(id);                 //id for command
    dxlWriteB(length);             //packet length
    dxlWriteB(AX_RESET);            //instruction
    dxlWriteB(checksum);           //checksum byte of all non-header bytes  
    dxlSetRX(id);             //get ready to read return packet
}




void dxlSycWrite()
{
    // int numServos;


    // int regData[];
    // int servoIds[];
    // int regStart;


    // dxlSetTXall();
    // int length = 4 + (2 * sizeof(regData[]));                 //  id + inst
    // int checksum = ~((id + length + AX_SYNC_WRITE  % 256);    //checksum is the inverse of all non header bytes
    // dxlWriteB(0xFF);               //header 1
    // dxlWriteB(0xFF);               //header 2
    // dxlWriteB(DXL_BROADCAST);      //id for command
    // dxlWriteB(length);             //packet length
    // dxlWriteB(AX_SYNC_WRITE);            //instruction
    // dxlWrite(AX_GOAL_POSITION_L);
    // dxlWrite(servoIds);
    // for(int i=0; i<sizeof(regData[]); i++)
    // {

    //     checksum += (regData[i]&0xff) + (regData[i]>>8) + servoIds[i];
    //     dxlWrite(servoIds[i]);
    //     dxlWrite(regData[i]&0xff);
    //     dxlWrite(regData[i]>>8);
    // } 
    // dxlWriteB(checksum);           //checksum byte of all non-header bytes  
    // //dxlSetRX(id);     //don't need this, no return packet right?                 //get ready to read return packet
//}


}


void mxBulkRead()
{

}


//writes with no read?


// general write?
// general sync write?



void centerServos()
{

}




float dxlGetSystemVoltage(int numberOfServos)
{

    int  servoList[numberOfServos];


    for(int i = 0; i < numberOfServos; i++)
    {
        servoList[i] = i + 1; //starting with servo number one in array slot 0, so everything is offset by 1
    }
    return(dxlGetSystemVoltage(numberOfServos, servoList));
}



float dxlGetSystemVoltage(int numberOfServos, int servoList[])
{
   int voltageSum = 0;  //temporary sum of all voltage readings for average
   int servosFound = 0;    //assume that we find all of the servos
   int tempVoltage = 0;                //temporary holder for the voltage
   int finalVoltage;

   for (int i = 0; i < numberOfServos; i++)
   {


      tempVoltage = dxlGetRegister (servoList[i], AX_PRESENT_VOLTAGE, 1);
      //if the voltage is greater or equal to zero, add it to the sum
      if(tempVoltage > -1)
      {  
         voltageSum = tempVoltage + voltageSum;
         servosFound = servosFound + 1;
      //Serial.println(voltageSum);
      }
      //anything below zero indicates a missing servo
      else
      {
        
      }
      delay(33);
   }


   if(servosFound < 1)
   {
     return(-1);

   }

   finalVoltage = (float(voltageSum) /float(servosFound)) / 10.0; //divide the voltage sum by the number of servos to get the raw average voltage. Divide by 10 to get votlage in volts
   


   return(finalVoltage);


}


void dxlVoltageReport(int numberOfServos)
{

    int  servoList[numberOfServos];

    for(int i = 0; i < numberOfServos; i++)
    {
        servoList[i] = i + 1; //starting with servo number one in array slot 0, so everything is offset by 1
    }
    dxlVoltageReport(numberOfServos, servoList);

    
}

void dxlVoltageReport(int numberOfServos, int servoList[])
{
   float voltage = dxlGetSystemVoltage(numberOfServos, servoList); //Check Power Supply Voltage
  
   Serial.print("Average System Voltage:");  
   Serial.println(voltage);   
   
   if( voltage < 10)
   {
     
     if (voltage < 0)
     {
       Serial.println("No Servos detected - voltage cannot be obtained");
     }
     else
     {
       Serial.println("System Voltage is under 10.0v, check your battery or power supply");
     }
   
     Serial.println("We reccomend you fix your problem before proceededing. Send any character to continue anyway");
     
     while(Serial.available() < 1)
     {
       //do nothing while no characters are detected
     }
     while(Serial.available() > 0)
     {
       Serial.read();//clear serial input
     }
     
   }
   
}


//future? return an array?
void dxlServoReport(int numberOfServos)
{

    int  servoList[numberOfServos];

    for(int i = 0; i < numberOfServos; i++)
    {
        servoList[i] = i + 1; //starting with servo number one in array slot 0, so everything is offset by 1
    }
    dxlServoReport(numberOfServos, servoList);

    
}


void dxlServoReport(int numberOfServos, int servoList[])
{
    int pos;            //holds the positional value of the servo
    int missingServos = 0;    //number of servos that could not be contacted

    Serial.println("######################################################");
    Serial.println("Starting Servo Scanning Test.");
    Serial.println("######################################################");

    for(int i = 0; i < numberOfServos ; i++)
    {
        pos =  dxlGetRegister(servoList[i], 36, 2);

        int errorBit = dxlGetLastError();


        //if there is no data, retry once
        if (pos <= 0)
        {
           Serial.println("###########################");
           Serial.print("Cannot connect to servo #");
           Serial.print(servoList[i]);
           Serial.println(", retrying");
           delay(500);  //short delay to clear the bus
           pos =  dxlGetRegister(servoList[i], 36, 2);
           int errorBit = dxlGetLastError();
        }

        //if there is still no data, print an error.
        if (pos <= 0)
        {
            Serial.print("ERROR! Servo ID: ");
            Serial.print(servoList[i]);
            Serial.println(" not found. Please check connection and verify correct ID is set.");
            Serial.println("###########################"); 
            missingServos = missingServos + 1;
        }

        else
        {

            Serial.print("Servo ID: ");
            Serial.println(servoList[i]);
            Serial.print("Servo Position: ");
            Serial.println(pos);

            if(ERR_NONE & errorBit)
            {
                Serial.println("          No Errors Found");
            }
            if(ERR_VOLTAGE & errorBit)
            {
                Serial.println("          Voltage Error");
            }
            if(ERR_ANGLE_LIMIT & errorBit)
            {
                Serial.println("          Angle Limit Error");
            }
            if(ERR_OVERHEATING & errorBit)
            {
                Serial.println("          Overheating Error");
            }
            if(ERR_RANGE & errorBit)
            {
                Serial.println("          Range Error");
            }
            if(ERR_CHECKSUM & errorBit)
            {
                Serial.println("          Checksum Error");
            }
            if(ERR_OVERLOAD & errorBit)
            {
                Serial.println("          Overload Error");
            }
            if(ERR_INSTRUCTION & errorBit)
            {
                Serial.println("          Instruction Error");
            }
      

            delay(100);
            
        }

    }
      

    if (missingServos > 0)
    {
        Serial.println("###########################");
        Serial.print("ERROR! ");
        Serial.print(missingServos);
        Serial.println(" servos ID(s) are missing from Scan. Please check connection and verify correct ID is set.");

        Serial.println("###########################");  
    }
    else
    {
        Serial.println("All servo IDs present.");
    }   
    Serial.println("######################################################");  

}



void dxlServoReport()
{
    int pos;            //holds the positional value of the servo
   
    Serial.println("######################################################");
    Serial.println("Starting Full Servo Scan.");
    Serial.println("######################################################");

    for(int i = 0; i < 255 ; i++)
    {
        pos =  dxlGetRegister(i, 36, 2);

        int errorBit = dxlGetLastError();


        //if there is no data, retry once
        if (pos <= 0)
        {
           delay(10);  //short delay to clear the bus
           pos =  dxlGetRegister(i, 36, 2);
           int errorBit = dxlGetLastError();
        }

        if (pos > -1)
        {


            Serial.print("Servo ID: ");
            Serial.println(i);
            Serial.print("Servo Position: ");
            Serial.println(pos);

            if(ERR_NONE & errorBit)
            {
                Serial.println("          No Errors Found");
            }
            if(ERR_VOLTAGE & errorBit)
            {
                Serial.println("          Voltage Error");
            }
            if(ERR_ANGLE_LIMIT & errorBit)
            {
                Serial.println("          Angle Limit Error");
            }
            if(ERR_OVERHEATING & errorBit)
            {
                Serial.println("          Overheating Error");
            }
            if(ERR_RANGE & errorBit)
            {
                Serial.println("          Range Error");
            }
            if(ERR_CHECKSUM & errorBit)
            {
                Serial.println("          Checksum Error");
            }
            if(ERR_OVERLOAD & errorBit)
            {
                Serial.println("          Overload Error");
            }
            if(ERR_INSTRUCTION & errorBit)
            {
                Serial.println("          Instruction Error");
            }
            delay(10);   
        }

    }
      

    Serial.println("Scan Finished");  
    Serial.println("######################################################");  

}







int dxlScanServos(int numberOfServos, int returnList[])
{

    int  servoList[numberOfServos];

    for(int i = 0; i < numberOfServos; i++)
    {
        servoList[i] = i + 1; //starting with servo number one in array slot 0, so everything is offset by 1
    }
    return(dxlScanServos(numberOfServos, servoList, returnList));

    
}


int dxlScanServos(int numberOfServos, int servoList[], int returnList[])
{

    int pos;            //holds the positional value of the servo. This is arbitrary as we'll be discarding this value - we're just using it to check if the servo is present

    int missingServos = 0;    //number of servos that could not be contacted
    int foundServos = 0;    //number of servos that could not be contacted

    for(int i = 0; i < numberOfServos ; i++)
    {
        pos =  dxlGetRegister(servoList[i], 36, 2);
        int errorBit = dxlGetLastError();


        //if there is no data, retry once
        if (pos <= 0)
        {
           delay(10);  //short delay to clear the bus
           pos =  dxlGetRegister(servoList[i], 36, 2);
           int errorBit = dxlGetLastError();
        }

        //if there is still no data add one to the missing servos.
        if (pos <= 0)
        {
            missingServos = missingServos + 1;
            returnList[i] = -1;
        }
        else
        {
            foundServos = foundServos + 1;
            returnList[i] = errorBit;
        }
    }
    
    return(foundServos);
}


void dxlRegisterReportSingle(int servoID)
{
    Serial.print("Register Table for Servo #");
    Serial.println(servoID);
    int regData;

    for(int i = 0; i< MAX_REGISTERS; i++)
    {
        regData = dxlGetRegister(servoID, i, 1); 
        Serial.print(i);
        Serial.print(",");
        delay(33);
        Serial.println(regData);
        delay(33);
    }
}







void axRegisterDump(int servoNumber)
{

}

void mxRegisterDump(int servoNumber)
{

}

void dxlLedTest(int numberOfServos, int ledTime)
{
 

    int  servoList[numberOfServos];

    for(int i = 0; i < numberOfServos; i++)
    {
        servoList[i] = i + 1; //starting with servo number one in array slot 0, so everything is offset by 1
    }

    dxlLedTest(numberOfServos, servoList, ledTime);

}

void dxlLedTest(int numberOfServos, int servoList[], int ledTime)
{

    for(int i = 0; i< numberOfServos; i++) 
    {
        dxlSetRegister(servoList[i], 25, 1);


        delay(ledTime);
        dxlSetRegister(servoList[i], 25, 0);  


        //delay(ledTime); //do we want time that the servo is off? I don't think so   

    }

}
   

    
//}

void dxlRegisterReportMultiple(int numberOfServos)
{

 

    int  servoList[numberOfServos];

    for(int i = 0; i < numberOfServos; i++)
    {
        servoList[i] = i + 1; //starting with servo number one in array slot 0, so everything is offset by 1
    }

    dxlRegisterReportMultiple( numberOfServos, servoList);
}



int dxlIsModelMX(int modelNumber)
{
  if((modelNumber == MX_12W_MODEL_NUMBER) || (modelNumber == MX_28_MODEL_NUMBER) || (modelNumber == MX_64_MODEL_NUMBER) || (modelNumber == MX_106_MODEL_NUMBER) )
  {
    return(1);
  }
  else
  {
    return(0);
  }
}

int dxlIsModelAX(int modelNumber)
{
  if((modelNumber == AX_12_MODEL_NUMBER) || (modelNumber == AX_18_MODEL_NUMBER) || (modelNumber == AX_12W_MODEL_NUMBER) )
  {
    return(1);
  }
  else
  {
    return(0);
  }
}



void mxSetMultiTurnMode(int servoId)
{
  dxlSetJointMode(servoId, MX_MAX_POSITION_VALUE, MX_MAX_POSITION_VALUE);
}


void mxSetJointMode(int servoId)
{
  dxlSetJointMode(servoId, 0, MX_MAX_POSITION_VALUE);
}


void axSetJointMode(int servoId)
{
  dxlSetJointMode(servoId, 0, AX_MAX_POSITION_VALUE);
}



void dxlSetJointMode(int servoId, int CWAngleLimit, int CCWAngleLimit)
{
  dxlSetCWAngleLimit(servoId, CWAngleLimit);
  dxlSetCCWAngleLimit(servoId, CCWAngleLimit);

}

void dxlSetWheelMode(int servoId)
{
  dxlSetCWAngleLimit(servoId, 0);
  dxlSetCCWAngleLimit(servoId, 0);

}






void dxlRegisterReportMultiple(int numberOfServos, int servoList[])
{
  int regData;
  int modelData[numberOfServos];

  Serial.println("Register Table for All Servos #");



  

    Serial.print("SERVO #,");
    for(int i = 0; i< numberOfServos; i++)
    {
        Serial.print(servoList[i]);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("MODEL,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetModel(servoList[i]); 

        modelData[i] = regData; //store register data forr later printout


        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");



    Serial.print("FIRMWARE,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetFirmware(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("ID,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetRegister(i, 3, 1); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("BAUD RATE,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetBaud(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("RETURN DELAY TIME,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetReturnDelayTime(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("CW ANGLE LIMIT,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetCWAngleLimit(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("CCW ANGLE LIMIT,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetCCWAngleLimit(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("TEMPERATURE LIMIT,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetTempLimit(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("LOW VOLTAGE LIMIT,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetLowVoltage(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("HIGH VOLTAGE LIMIT,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetHighVoltage(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("MAX TORQUE,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetStartupMaxTorque(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("STATUS RETURN LEVEL,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetStatusReturnLevel(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("ALARM LED,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetAlarmLED(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("ALARM SHUTDOWN,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetAlarmShutdown(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");



    Serial.print("Multi Turn Offset,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetMultiTurnOffset(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");


    Serial.print("Resolution Divider,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetResolutionDivider(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");









    Serial.print("TORQUE ENABLE,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetTorqueEnable(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("LED,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetLed(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");


    Serial.print("CW COMPLIANCE,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelAX(modelData[i]) == true)
      {
        regData = axGetCWComplianceMargin(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");




    Serial.print("CCW COMPLIANCE,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelAX(modelData[i]) == true)
      {
        regData = axGetCCWComplianceMargin(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");





    Serial.print("CW COMPLAINCE SLOPE,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelAX(modelData[i]) == true)
      {
        regData = axGetCWComplianceSlope(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");

    Serial.print("CCW COMPLAINCE SLOPE,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelAX(modelData[i]) == true)
      {
        regData = axGetCCWComplianceSlope(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");




    Serial.print("D,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetD(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");


    Serial.print("I,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetI(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");


    Serial.print("P,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetP(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");







    Serial.print("GOAL POSITION,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetGoalPosition(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("MOVING SPEED,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetGoalSpeed(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("TORQUE LIMIT,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetTorqueLimit(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("PRESENT POSITION,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetPosition(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("PRESENT SPEED,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetSpeed(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("PRESENT LOAD,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetTorque(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("PRESENT VOLTAGE,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetVoltage(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("PRESENT TEMPERATURE,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetTemperature(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("REGISTERED,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetRegistered(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("MOVING,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetMoving(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("LOCK,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetLock(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");

    Serial.print("PUNCH,");
    for(int i = 0; i< numberOfServos; i++)
    {

        regData = dxlGetPunch(servoList[i]);
        Serial.print(regData);
        Serial.print(",");
        delay(33);
    }
    Serial.println("");




    Serial.print("Current (MX 64/106 only),");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetCurrent(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");


    Serial.print("Torque Mode (MX 64/106 only),");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetTorqueMode(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");


    Serial.print("Goal Torque (MX 64/106 only),");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetGoalTorque(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");



    Serial.print("Goal acceleration,");
    for(int i = 0; i< numberOfServos; i++)
    {
      if(dxlIsModelMX(modelData[i]) == true)
      {
        regData = mxGetGoalAcceleration(servoList[i]); 
        Serial.print(regData);
        Serial.print(",");
        delay(33);
      }
      else
      {

        Serial.print("N/A");
        Serial.print(",");
      }
    }
    Serial.println("");





}

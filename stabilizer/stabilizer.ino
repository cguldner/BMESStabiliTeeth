// 2 servo planar stabilization system
// wp
// Jan 2016
//
// Based on Jeff Rowber's work found at
// https://github.com/jrowberg/i2cdevlib/blob/master/Arduino/MPU6050/MPU6050.cpp
//
// Use at your own risk.
//
// This code is placed under the MIT License (MIT)
//
// Copyright (c) 2016 woojay poynter

//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:

//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.

//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.
//

#include <Servo.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
//#include "MPU6050.h" // not necessary if using MotionApps include file
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
#include <Wire.h>
#endif

// Set to 1 if you want the Serial hookup 
#define DEBUG 0

#define LED_PIN 13
bool blinkState = true;

Servo Servo1;   // First Servo off the chassis
Servo Servo2;   // Second Servo off the chassis

int Servo1Pin = 9;
int Servo2Pin = 10;
int HeadPin   = 6;
int ButtonLEDPin = 7;

int Servo1Pos = 0;
int Servo2Pos = 0;

#define MOTOR_TIME 1000
#define BAUD_RATE 115200

// INPUT CALIBRATED OFFSETS HERE; SPECIFIC FOR EACH UNIT AND EACH MOUNTING CONFIGURATION!!!!
#define XGyroOffset -4
#define YGyroOffset -62
#define ZGyroOffset 4
#define XAccelOffset -5233
#define YAccelOffset -1087
#define ZAccelOffset 1082


float mpuPitch = 0;
float mpuRoll = 0;
float mpuYaw = 0;


// define MPU instance
MPU6050 mpu;                    // class default I2C address is 0x68; specific I2C addresses may be passed as a parameter here

// MPU control/status vars
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

// relative ypr[x] usage based on sensor orientation when mounted, e.g. ypr[PITCH]
#define PITCH 1     // defines the position within ypr[x] variable for PITCH; may vary due to sensor orientation when mounted
#define ROLL  2     // defines the position within ypr[x] variable for ROLL; may vary due to sensor orientation when mounted
#define YAW   0     // defines the position within ypr[x] variable for YAW; may vary due to sensor orientation when mounted


void setup() {
  /********* Stabilizer Setup *********/
  Servo1.attach(Servo1Pin);  // attaches the servo on D11 to the servo object
  Servo2.attach(Servo2Pin);  // Second servo on D11

    // join I2C bus (I2Cdev library doesn't do this automatically)
  #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    Wire.begin();
    TWBR = 24; // 400kHz I2C clock (200kHz if CPU is 8MHz)
  #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
    Fastwire::setup(400, true);
  #endif

  if(DEBUG) Serial.begin(BAUD_RATE);
  if(DEBUG) while (!Serial);      // wait for Leonardo enumeration, others continue immediately

  // initialize device
  if(DEBUG) Serial.println(F("Initializing I2C devices..."));
  mpu.initialize();

  // verify connection
  if(DEBUG) Serial.println(F("Testing device connections..."));
  if(DEBUG) Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

  // load and configure the DMP
  if(DEBUG) Serial.println(F("Initializing DMP"));
  devStatus = mpu.dmpInitialize();

  mpu.setXGyroOffset(XGyroOffset);
  mpu.setYGyroOffset(YGyroOffset);
  mpu.setZGyroOffset(ZGyroOffset);
  mpu.setXAccelOffset(XAccelOffset);
  mpu.setYAccelOffset(YAccelOffset);
  mpu.setZAccelOffset(ZAccelOffset);

  // make sure it worked (returns 0 if so)
  if (devStatus == 0) {
    // turn on the DMP, now that it's ready
    if(DEBUG) Serial.println(F("Enabling DMP"));
    mpu.setDMPEnabled(true);

    // enable Arduino interrupt detection
    if(DEBUG) Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)"));
    mpuIntStatus = mpu.getIntStatus();

    // get expected DMP packet size for later comparison
    packetSize = mpu.dmpGetFIFOPacketSize();
  }
  else {
    // ERROR!
    // 1 = initial memory load failed, 2 = DMP configuration updates failed (if it's going to break, usually the code will be 1)
    if(DEBUG) Serial.print(F("DMP Initialization failed code = "));
    if(DEBUG) Serial.println(devStatus);
  }

  // configure LED for output
  pinMode(LED_PIN, OUTPUT);
  
  pinMode(ButtonLEDPin, OUTPUT);
  
  /********* Motor Setup *********/
  // pinMode(HeadPin, OUTPUT);

}

void loop(void) {
//  digitalWrite(HeadPin, HIGH);
//  delay(MOTOR_TIME);
//  digitalWrite(HeadPin, LOW);
//  delay(MOTOR_TIME);

  digitalWrite(ButtonLEDPin, HIGH);
  
  processAccelGyro();
}



// ================================================================
// ===                    PROCESS ACCEL/GYRO IF AVAILABLE       ===
// ================================================================

void processAccelGyro() {
  // Get INT_STATUS byte
  mpuIntStatus = mpu.getIntStatus();

  // get current FIFO count
  fifoCount = mpu.getFIFOCount();

  // check for overflow (this should never happen unless our code is too inefficient)
  if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
    // reset so we can continue cleanly
    mpu.resetFIFO();
    if(DEBUG) Serial.println(F("FIFO overflow!"));
    return;
  }
  
  // otherwise continue processing
  if (mpuIntStatus & 0x02) {
    // check for correct available data length
    if (fifoCount < packetSize)
      return; //  fifoCount = mpu.getFIFOCount();

    // read a packet from FIFO
    mpu.getFIFOBytes(fifoBuffer, packetSize);

    // track FIFO count here in case there is > 1 packet available
    fifoCount -= packetSize;

    // flush buffer to prevent overflow
    mpu.resetFIFO();

    // display Euler angles in degrees
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    mpuPitch = ypr[PITCH] * 180 / M_PI;
    mpuRoll = ypr[ROLL] * 180 / M_PI;
    mpuYaw  = ypr[YAW] * 180 / M_PI;

    // flush buffer to prevent overflow
    mpu.resetFIFO();

    // blink LED to indicate activity
    blinkState = !blinkState;
    digitalWrite(LED_PIN, blinkState);

    // flush buffer to prevent overflow
    mpu.resetFIFO();

    Servo1.write(-mpuPitch + 90);
    Servo2.write(mpuRoll + 90);
    //delay(10);

    // flush buffer to prevent overflow
    mpu.resetFIFO();

  } // if (mpuIntStatus & 0x02)
}

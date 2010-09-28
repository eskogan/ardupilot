/*
	APM_Compass.cpp - Arduino Library for HMC5843 I2C Magnetometer
	Code by Jordi Mu�oz and Jose Julio. DIYDrones.com

	This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

	Sensor is conected to I2C port
	Sensor is initialized in Continuos mode (10Hz)
	
	Variables:
		Heading : Magnetic heading
		Heading_X : Magnetic heading X component
		Heading_Y : Magnetic heading Y component
		Mag_X : Raw X axis magnetometer data
		Mag_Y : Raw Y axis magnetometer data
		Mag_Z : Raw Z axis magnetometer data		
	
	Methods:
		Init() : Initialization of I2C and sensor
		Read() : Read Sensor data		

	To do : Calibration of the sensor, code optimization
	Mount position : UPDATED
                Big capacitor pointing backward, connector forward
		
*/
extern "C" {
  // AVR LibC Includes
  #include <math.h>
  #include "WConstants.h"
}

#include <Wire.h>
#include "APM_Compass.h"
#include "../AP_Math/AP_Math.h"

#define CompassAddress       0x1E
#define ConfigRegA           0x00
#define ConfigRegB           0x01
#define MagGain              0x20
#define PositiveBiasConfig   0x11
#define NormalOperation      0x10
#define ModeRegister         0x02
#define ContinuousConversion 0x00
#define SingleConversion     0x01

// constant rotation matrices
const Matrix3f rotation[16] = { 
	Matrix3f( 1, 0, 0, 0, 1, 0, 0 ,0, 1 ),   //  COMPONENTS_UP_PINS_BACK = no rotation
	Matrix3f( 0.70710678, 0.70710678, 0, -0.70710678, 0.70710678, 0, 0, 0, 1 ), //COMPONENTS_UP_PINS_BACK_LEFT = rotation_yaw_315
	Matrix3f( 0, 1, 0, -1, 0, 0, 0, 0, 1 ),  //  COMPONENTS_UP_PINS_LEFT = rotation_yaw_270
	Matrix3f( -0.70710678, 0.70710678, 0, -0.70710678, -0.70710678, 0, 0, 0, 1 ), // COMPONENTS_UP_PINS_FORWARD_LEFT = rotation_yaw_225
	Matrix3f( -1, 0, 0, 0, -1, 0, 0, 0, 1 ), //  COMPONENTS_UP_PINS_FORWARD = rotation_yaw_180
	Matrix3f( -0.70710678, -0.70710678, 0, 0.70710678, -0.70710678, 0, 0, 0, 1 ), // COMPONENTS_UP_PINS_FORWARD_RIGHT = rotation_yaw_135
	Matrix3f( 0, -1, 0, 1, 0, 0, 0, 0, 1 ),  //  COMPONENTS_UP_PINS_RIGHT = rotation_yaw_90
	Matrix3f( 0.70710678, -0.70710678, 0, 0.70710678, 0.70710678, 0, 0, 0, 1 ), // COMPONENTS_UP_PINS_BACK_RIGHT = rotation_yaw_45
	Matrix3f( 1, 0, 0, 0, -1, 0, 0, 0, -1 ), //  COMPONENTS_DOWN_PINS_BACK = rotation_roll_180
	Matrix3f( 0.70710678, -0.70710678, 0, -0.70710678, -0.70710678, 0, 0, 0, -1 ), // COMPONENTS_DOWN_PINS_BACK_LEFT = rotation_roll_180_yaw_315
	Matrix3f( 0, -1, 0, -1, 0, 0, 0, 0, -1 ),//  COMPONENTS_DOWN_PINS_LEFT = rotation_roll_180_yaw_270
	Matrix3f( -0.70710678, -0.70710678, 0, -0.70710678, 0.70710678, 0, 0, 0, -1 ), // COMPONENTS_DOWN_PINS_FORWARD_LEFT = rotation_roll_180_yaw_225
	Matrix3f( -1, 0, 0, 0, 1, 0, 0, 0, -1 ), //  COMPONENTS_DOWN_PINS_FORWARD = rotation_pitch_180
	Matrix3f( -0.70710678, 0.70710678, 0, 0.70710678, 0.70710678, 0, 0, 0, -1 ), // COMPONENTS_DOWN_PINS_FORWARD_RIGHT = rotation_roll_180_yaw_135
	Matrix3f( 0, 1, 0, 1, 0, 0, 0, 0, -1 ),  //  COMPONENTS_DOWN_PINS_RIGHT = rotation_roll_180_yaw_90
	Matrix3f( 0.70710678, 0.70710678, 0, 0.70710678, -0.70710678, 0, 0, 0, -1 ) // COMPONENTS_DOWN_PINS_BACK_RIGHT = rotation_roll_180_yaw_45
};

// Constructors ////////////////////////////////////////////////////////////////
APM_Compass_Class::APM_Compass_Class() : orientation(0)
{
}

// Public Methods //////////////////////////////////////////////////////////////
void APM_Compass_Class::Init(void)
{
  Wire.begin();
  
  delay(10);
  
  calibration[0] = 1.0;
  calibration[1] = 1.0;
  calibration[2] = 1.0;

  Wire.beginTransmission(CompassAddress);
  Wire.send(ConfigRegA);
  Wire.send(PositiveBiasConfig);
  Wire.endTransmission();
  delay(50);

  Wire.beginTransmission(CompassAddress);
  Wire.send(ConfigRegA);
  Wire.send(MagGain);
  Wire.endTransmission();
  delay(10);

  Wire.beginTransmission(CompassAddress);
  Wire.send(ModeRegister);
  Wire.send(SingleConversion);
  Wire.endTransmission();
  delay(10);

  Read();
  delay(10);

  calibration[0] = abs(715.0 / Mag_X);
  calibration[1] = abs(715.0 / Mag_Y);
  calibration[2] = abs(715.0 / Mag_Z);

  Wire.beginTransmission(CompassAddress);
  Wire.send(ConfigRegA);
  Wire.send(NormalOperation);
  Wire.endTransmission();
  delay(50);

  Wire.beginTransmission(CompassAddress);
  Wire.send(ModeRegister);
  Wire.send(ContinuousConversion);        // Set continuous mode (default to 10Hz)
  Wire.endTransmission();                 // End transmission
  
}

// Read Sensor data
void APM_Compass_Class::Read()
{
  int i = 0;
  byte buff[6];
 
  Wire.beginTransmission(CompassAddress); 
  Wire.send(0x03);        //sends address to read from
  Wire.endTransmission(); //end transmission
  
  //Wire.beginTransmission(CompassAddress); 
  Wire.requestFrom(CompassAddress, 6);    // request 6 bytes from device
  while(Wire.available())     
  { 
    buff[i] = Wire.receive();  // receive one byte
    i++;
  }
  Wire.endTransmission(); //end transmission
  
  if (i==6)  // All bytes received?
  {
    // MSB byte first, then LSB, X,Y,Z
    Mag_X = -((((int)buff[0]) << 8) | buff[1]) * calibration[0];    // X axis
    Mag_Y = ((((int)buff[2]) << 8) | buff[3]) * calibration[1];    // Y axis
    Mag_Z = -((((int)buff[4]) << 8) | buff[5]) * calibration[2];    // Z axis
  }
}

void APM_Compass_Class::Calculate(float roll, float pitch)
{
  float Head_X;
  float Head_Y;
  float cos_roll;
  float sin_roll;
  float cos_pitch;
  float sin_pitch;
  Vector3f rotMagVec;
  
  cos_roll = cos(roll);  // Optimizacion, se puede sacar esto de la matriz DCM?
  sin_roll = sin(roll);
  cos_pitch = cos(pitch);
  sin_pitch = sin(pitch);
  
  // rotate the magnetometer values depending upon orientation
  if( orientation == APM_COMPASS_COMPONENTS_UP_PINS_BACK)
      rotMagVec = Vector3f(Mag_X,Mag_Y,Mag_Z);  
  else
      rotMagVec = rotation[orientation]*Vector3f(Mag_X,Mag_Y,Mag_Z);  
  
  // Tilt compensated Magnetic field X component:
  Head_X = rotMagVec.x*cos_pitch+rotMagVec.y*sin_roll*sin_pitch+rotMagVec.z*cos_roll*sin_pitch;
  // Tilt compensated Magnetic field Y component:
  Head_Y = rotMagVec.y*cos_roll-rotMagVec.z*sin_roll;
  // Magnetic Heading
  Heading = atan2(-Head_Y,Head_X);
  // Optimization for external DCM use. Calculate normalized components
  Heading_X = cos(Heading);
  Heading_Y = sin(Heading);
}


void APM_Compass_Class::SetOrientation(int newOrientation)
{
    orientation = newOrientation;
}


// make one instance for the user to use
APM_Compass_Class APM_Compass;

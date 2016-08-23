/*
 Name:      NewerHotness.ino
 Created:   8/2/2016 7:40:04 PM
 Author:    Andrew Kaster
*/

#include <Adafruit_Sensor.h>
#include <Adafruit_LSM303_U.h>
#include <Adafruit_L3GD20_U.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_10DOF.h>
#include "Thermocouple_Max31855.h"
#include <string.h>
#include <SPI.h>
#include <SD.h>


#define NUM_BYTES_TO_SEND   (52)

#define XBEE_PORT           (Serial1)
#define XBEE_BAUD_RATE      (57600)
#define XBEE_CONFIG         (SERIAL_8N1)

#define STRAT_PORT          (Serial2)
//TODO strat config
#define GPS_PORT            (Serial3)
//TODO gps config

/** PIN DEFINES */
#define CS_TCA1             (4)
#define CS_TCA2             (5)
#define CS_TCA3             (6)
#define XBEE_CTS_PIN        (3)
#define RX_STRAT            (7) // RX_3
#define RX_GPS              (9) // RX_2
#define TX_GPS              (10)// TX_2
#define CS_SDCARD           (15)
#define Z_ADXL              (17)
#define Y_ADXL              (20)
#define X_ADXL              (22)

/** POLL FLAG DEFINES **/
#define TEMP1_BITMASK       (0x8000)
#define TEMP2_BITMASK       (0x4000)
#define TEMP3_BITMASK       (0x2000)
#define TEMP_10DOF_BITMASK  (0x1000)
#define ALT_PRESS_BITMASK   (0x0800)
#define ALT_STRAT_BITMASK   (0x0400)
#define ALT_GPS_BITMASK     (0x0200)
#define LAT_BITMASK         (0x0100)
#define LON_BITMASK         (0x0080)
#define ACCEL_X_BITMASK     (0x0040)
#define ACCEL_Y_BITMASK     (0x0020)
#define ACCEL_Z_BITMASK     (0x0010)

//TODO GOAL: 20Hz for both. Values will need tweaking for sure
#define MILIS_BTWN_SEND     (50)
#define MILIS_BTWN_WRITE    (50)

#define DEBUG_MODE

//OLD INFO below. Double check with diagram before trusting

// XBEE TX_1 = Pin 1        Purple 7 on logic analyzer
// XBEE RX_1 = Pin 0        Blue 6  on logic analyzer

//SPI Clk Pin 13
//SPI DataOut Pin 11
//SPI DataIn Pin 12


//SPI Clock speed max ~1-2 MHz (4 for yolo)

struct tenDOF_data_s
{
    float *pressure;
    sensors_vec_t *accel;
    sensors_vec_t *mag;
    sensors_vec_t *gyro;
    sensors_vec_t orientation;
};

typedef tenDOF_data_s tenDOF_data_t;

struct send_data_s {

    float temp1;
    float temp2;
    float temp3;
    float temp_10dof;
    float alt_press;
    float alt_strat;
    float alt_gps;
    float lat;
    float lon;
    float accel_x;
    float accel_y;
    float accel_z;
    short poll_flags;
    short padding;
};


typedef union {
    send_data_s component;
    byte        full[NUM_BYTES_TO_SEND];
}send_data_t;

enum karmanDataEnum
{
    TEMP1 = 0,
    TEMP2,
    TEMP3,
    TEMP_10DOF,
    ALT_PRESS,
    ALT_STRAT,
    ALT_GPS,
    LAT,
    LON,
    ACCEL_X,
    ACCEL_Y,
    ACCEL_Z,
    PRESSURE,
    ACCEL_10DOF_X,
    ACCEL_10DOF_Y,
    ACCEL_10DOF_Z,
    MAG_X,
    MAG_Y,
    MAG_Z,
    ROT_X,
    ROT_Y,
    ROT_Z,
    ROLL,
    PITCH,
    HEADING,
    NUM_KARMAN_DATA,
};

typedef struct
{
    tenDOF_data_t write_only;
    send_data_t write_send;
}write_data_t;

//Custom globals for Karman-specific stuff
write_data_t gWriteData;
send_data_t *gSendData = &(gWriteData.write_send);
String write_string;
elapsedMillis sinceSend;
elapsedMillis sinceWrite;
unsigned long currTime;
String dataFileName;
unsigned long ml_index;


SPISettings SPI_TCA1(1000000, MSBFIRST, SPI_MODE0);
SPISettings SPI_TCA2(1000000, MSBFIRST, SPI_MODE0);
SPISettings SPI_TCA3(1000000, MSBFIRST, SPI_MODE0);


// Call hardware spi constructor for TCA's
Thermocouple_Max31855 thermocouple1(CS_TCA1, SPI_TCA1);
Thermocouple_Max31855 thermocouple2(CS_TCA2, SPI_TCA2);
Thermocouple_Max31855 thermocouple3(CS_TCA3, SPI_TCA3);

//Call constructors for 10DOF chips
Adafruit_BMP085_Unified         bmp085(1);
Adafruit_L3GD20_Unified         l3gd20(2);
Adafruit_LSM303_Accel_Unified   lsm303_accel(3);
Adafruit_LSM303_Mag_Unified     lsm303_mag(4);
Adafruit_10DOF                  objTenDOF();

//Adafruit unified sensor library global sensor events
sensors_event_t                pressureBMP085;
sensors_event_t                gyroL3GD20;
sensors_event_t                accelLSM303;
sensors_event_t                magLSM303;


void setup() {

    //setup sensors and get ready for transmit loop
    //Configure Serial1
    XBEE_PORT.begin(XBEE_BAUD_RATE, XBEE_CONFIG);

   SPI.begin();

    // setup SD card recording.
    // Note begin() uses the SPI interface
    // to do setup communications with the sd card.
    if (!SD.begin(CS_SDCARD)) {
        Serial.println("Failed to Initialize SD card");
    }
    int dataNum = 0;
    dataFileName = "data" + String(dataNum);
    while (SD.exists(dataFileName.c_str()))
    {
        dataNum++;
        dataFileName = "data" + String(dataNum);
        if (dataNum > 1000)
        {
            break;
        }
    }


    File dummyClear = SD.open(dataFileName.c_str(), FILE_WRITE);
    if (dummyClear)
    {
        dummyClear.println("/****************************************/");
        dummyClear.println("Karman Summer Telemetry Newer Hotness Data");
        dummyClear.println("/****************************************/");
        dummyClear.close();
    }

    //Pressure --> Altitude
    //Also temperature
    bmp085.begin(BMP085_MODE_ULTRALOWPOWER);

    //Rotation rate in Rad/Sec U,V,W
    l3gd20.begin(GYRO_RANGE_250DPS);
    l3gd20.enableAutoRange(true); //This will auto scale our gryo if it's saturating

    //Acceleration X,Y,Z
    lsm303_accel.begin();
    lsm303_accel.enableAutoRange(true); //This will auto scale our accelerometer if it's saturating
   
    //Magnetic field strength X,Y,Z
    lsm303_mag.begin();
    lsm303_mag.enableAutoRange(true); //This will auto scale our magnetometer if it's saturating

    //TODO Set sealevel pressure
    /* pseudocode:
        wait a few seconds
        get pressure event
        float sealevelPressure = event.pressure;
    */

    //Assign global pointers into sensors events
    gWriteData.write_only.pressure  =    &(pressureBMP085.pressure);
    gWriteData.write_only.accel     =    &(accelLSM303.acceleration);
    gWriteData.write_only.mag       =    &(magLSM303.magnetic);
    gWriteData.write_only.gyro      =    &(gyroL3GD20.gyro);
}

void loop()
{
    //get data and try to send and write after each grab
    for (ml_index = 0; ml_index < NUM_KARMAN_DATA; ml_index++)
    {
        //TODO use this return value
        (void)get_karman_data(ml_index);
        send_check();
        write_check();
    }
}


void send_check()
{
    if (sinceSend > MILIS_BTWN_SEND && digitalRead(XBEE_CTS_PIN) == LOW)
    {
        int numBytes = sizeof(send_data_t) / sizeof(byte);
        XBEE_PORT.write((gSendData->full), NUM_BYTES_TO_SEND);
#ifdef DEBUG_MODE
        float debugFloat;
        for (int i = 0; i < numBytes / 4; i++)
        {
            memcpy(&debugFloat, (&(gSendData->full[4 * i])), 4);
            Serial.print(debugFloat);
            Serial.print('\n');
        }
        Serial.print(gSendData->component.poll_flags);
        Serial.print('\n');
        sinceSend = 0;
#endif
        gSendData->component.poll_flags &= 0; //Clear flags
    }
    return;

}

//TODO get all data, only getting temperature atm
bool get_karman_data(byte data_number)
{
    bool retVal = false;
    switch (data_number)
    {
    case TEMP1:
        thermocouple1.getTemperature(gSendData->component.temp1);
        gSendData->component.poll_flags |= TEMP1_BITMASK;
        retVal = true;
        break;
    case TEMP2:
        thermocouple2.getTemperature(gSendData->component.temp2);
        gSendData->component.poll_flags |= TEMP2_BITMASK;
        retVal = true;
        break;
    case TEMP3:
        thermocouple3.getTemperature(gSendData->component.temp3);
        gSendData->component.poll_flags |= TEMP3_BITMASK;
        retVal = true;
        break;
    case TEMP_10DOF:
        gSendData->component.temp_10dof = 25.0;
        gSendData->component.poll_flags |= TEMP_10DOF_BITMASK;
        retVal = true;
        break;
    case ALT_PRESS:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= ALT_PRESS_BITMASK;
        retVal = true;
        break;
    case ALT_STRAT:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= ALT_STRAT_BITMASK;
        retVal = true;
        break;
    case ALT_GPS:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= ALT_GPS_BITMASK;
        retVal = true;
        break;
    case LAT:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= LAT_BITMASK;
        retVal = true;
        break;
    case LON:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= LON_BITMASK;
        retVal = true;
        break;
    case ACCEL_X:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= TEMP2_BITMASK;
        retVal = true;
        break;
    case ACCEL_Y:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= TEMP2_BITMASK;
        retVal = true;
        break;
    case ACCEL_Z:
        gSendData->component.temp2 = 6.28;
        gSendData->component.poll_flags |= TEMP2_BITMASK;
        retVal = true;
        break;
    default:
        //TODO add extra 10dof items combine some cases as they access the same chip
        break;
    }
    return retVal;
}



// Records all data currently in the global data structure to the SD card
// With a timestamp instead of poll flags
void write_check()
{
    if (sinceWrite > MILIS_BTWN_WRITE)
    {
        //TODO get write-only 10DOF data
        File data_file = SD.open(dataFileName.c_str(), FILE_WRITE);
        if (data_file)
        {
            currTime = millis();
            //Fill write_string with data from gWriteData
            getFormattedWriteString(&write_string);
#ifdef DEBUG_MODE
            Serial.println(String("SD card Data: \n" + write_string));
#endif
            data_file.println(write_string);
            data_file.close();
        }
        else
        {
            Serial.println("Error opening data file");
        }
        sinceWrite = 0;
    }
}

void getFormattedWriteString(String *pWriteString)
{
    *pWriteString += String(currTime) + ',';
    *pWriteString += String(gWriteData.write_send.component.temp1) + ',';
    *pWriteString += String(gWriteData.write_send.component.temp2) + ',';
    *pWriteString += String(gWriteData.write_send.component.temp3) + ',';
    *pWriteString += String(gWriteData.write_send.component.temp_10dof) + ',';
    *pWriteString += String(gWriteData.write_send.component.alt_press) + ',';
    *pWriteString += String(gWriteData.write_send.component.alt_strat) + ',';
    *pWriteString += String(gWriteData.write_send.component.alt_gps) + ',';
    *pWriteString += String(gWriteData.write_send.component.lat) + ',';
    *pWriteString += String(gWriteData.write_send.component.lat) + ',';
    *pWriteString += String(gWriteData.write_send.component.accel_x) + ',';
    *pWriteString += String(gWriteData.write_send.component.accel_y) + ',';
    *pWriteString += String(gWriteData.write_send.component.accel_z) + ',';
    *pWriteString += String(gWriteData.write_send.component.poll_flags, HEX) + ',';
    *pWriteString += String(*(gWriteData.write_only.pressure)) + ',';
    *pWriteString += String(gWriteData.write_only.orientation.roll) + ',';
    *pWriteString += String(gWriteData.write_only.orientation.pitch) + ',';
    *pWriteString += String(gWriteData.write_only.orientation.heading) + ',';
    *pWriteString += String(gWriteData.write_only.accel->x) + ',';
    *pWriteString += String(gWriteData.write_only.accel->y) + ',';
    *pWriteString += String(gWriteData.write_only.accel->z) + ',';
    *pWriteString += String(gWriteData.write_only.mag->x) + ',';
    *pWriteString += String(gWriteData.write_only.mag->y) + ',';
    *pWriteString += String(gWriteData.write_only.mag->z) + ',';
    *pWriteString += String(gWriteData.write_only.gyro->x) + ',';
    *pWriteString += String(gWriteData.write_only.gyro->y) + ',';
    *pWriteString += String(gWriteData.write_only.gyro->z) + ',';
    return;
}


#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

//*-- IoT Information
//#define SSID "NOKIA Lumia 530_1580"
//#define PASS "#514B72j"
#define IP "soilgate.com" // ThingSpeak IP Address: 184.106.153.149ini

//
#define DEBUG FALSE //comment out to remove debug msgs

// ESP via SoftSerial
#define _rxpin      6
#define _txpin      7
SoftwareSerial ESPSerial(_rxpin, _txpin); // RX, TX

// GP207 via SoftSerial
SoftwareSerial altser(3, 4); // RX, TX

// ESP ON/PFF MOS control pin
#define ESPPWR 2

// Sensor ON/PFF MOS control pin
#define SENPWR A3

// GPS ON/PFF MOS control pin
#define GPUPWR 8

// Mode Setting Pin
#define MODE A0

// Adc pins definition
#define BATTVOLT A1
#define SOLARVOLT A2
#define HUMIVOLT A4
#define TEMPVOLT A5

#define BUZZERPIN 5

volatile int f_wdt = 1;

// Setting Value variables
static unsigned int nIdleTime = 20; // Second Unit : EEPROM Addr : 0x10, 0x11
static String ThingsparkKey = ""; // IOT Device key : EEPROM Addr : 0x20 ~ 0x3F : Maximum : 32Byte
static String WIFI_SSID = ""; // WIFI SSID String : EEPROM Addr : 0x40 ~ 0x5F : Maximum : 32Byte
static String WIFI_PASS = ""; // WIFI Password String : EEPROM Addr : 0x60 ~ 0x7F : Maximum : 32Byte
unsigned char tmpChar[32];

// Process Value Variables
static float fTemper = 0.0F;
static float fHumi = 0.0F;
static float fBattVolt = 0.0F;
static float fSolarVolt = 0.0F;
static float fLati = 0.0F;
static float fLong = 0.0F;

volatile unsigned int nElapseTime = 0;
volatile unsigned int nCommTime = 0;

bool connectWiFi();
String GetResponse();
int Contains(String s, String search);

struct tonblock
{
    unsigned IN: 1; // IN option
    unsigned Q: 1; // Output
    unsigned long PT; // Set Timeout
    unsigned long ET; // Elapsed time
};
typedef struct tonblock TON;

struct RisingTrg
{
    unsigned IN : 1;
    unsigned PRE : 1;
    unsigned Q : 1;
    unsigned : 5;
};
typedef struct RisingTrg Rtrg;

void TONFunc(TON *pTP)
{
    if(pTP->IN)
    {
        if((pTP->ET + pTP->PT) <= millis())
            pTP->Q = 1;  
    }
    else
    {
        pTP->ET = millis();
        pTP->Q = 0;
    }
}

void RTrgFunc(Rtrg *pTrg)
{
    pTrg->Q = 0;
    if(pTrg->IN != pTrg->PRE)
    {
        pTrg->PRE = pTrg->IN;
        if(pTrg->PRE == 1)
        {
            pTrg->Q = 1;
        }    
    }
}


void WDT_Start()
{ // 
    /*** Setup the WDT ***/
    wdt_reset();
    /* Clear the reset flag. */
    MCUSR &= ~(1<<WDRF);
    /* In order to change WDE or the prescaler, we need to
     * set WDCE (This will allow updates for 4 clock cycles).
     */
    WDTCSR |= (1<<WDCE) | (1<<WDE);
    /* set new watchdog timeout prescaler value */
    WDTCSR = (1<<WDP0 | 1<<WDP1) | 1<<WDP2; /* 2.0 seconds */
    /* Enable the WD interrupt (note no reset). */
    WDTCSR |= _BV(WDIE);
}

/***************************************************
 *  Name:        ISR(WDT_vect)
 *
 *  Returns:     Nothing.
 *
 *  Parameters:  None.
 *
 *  Description: Watchdog Interrupt Service. This
 *               is executed when watchdog timed out.
 *
 ***************************************************/
ISR(WDT_vect)
{
    nCommTime += 2;
    nElapseTime += 2;
  
    if(f_wdt == 0)
    {
        f_wdt=1;
    }
    else
    {
        Serial.println("WDT Overrun!!!");
    }
}

/*
 * 
*/
void GetSensorValues()
{
    Serial.println(F("Sensor Values Display"));

    // Battery Voltage
    fBattVolt = analogRead(BATTVOLT) * 5.0F / 1024.0F;
    
    Serial.println(F("Battery Voltage"));
    Serial.print(String(fBattVolt, 2));
    Serial.println(F("V."));

    // Solar Voltage
    fSolarVolt = analogRead(SOLARVOLT)*5.0F/1024.0F;
    
    Serial.println(F("Solar Voltage"));
    Serial.print(F("ADC:"));
    Serial.print(String(fSolarVolt, 3));
    Serial.print(F("V, "));

    fSolarVolt *= 6.729F;
    
    Serial.print(F("Real:"));
    Serial.print(String(fSolarVolt, 3));
    Serial.println(F("V"));

    // Humidity Voltage
    fHumi = analogRead(HUMIVOLT)*5.0F/1024.0F;
    Serial.println(F("Humidity Percent"));
    Serial.print(F("ADC:"));
    Serial.print(String(fHumi, 3));
    Serial.print(F("V, "));

    if(fHumi < 1.1F)
        fHumi = 10.27 * fHumi - 1.305;
    else
        fHumi = ((95.50F * fHumi) - 15.88F * (fHumi * fHumi)) - 83.21;        
    
    Serial.print(F("Real:"));
    Serial.print(String(fHumi, 2));
    Serial.println(F("%"));
    
    // Temperature Voltage
    fTemper = analogRead(TEMPVOLT)*5.0F/1024.0F;
    Serial.println(F("Termperaure Degree"));
    Serial.print(F("ADC:"));
    Serial.print(String(fTemper, 3));
    Serial.print(F("V, "));

    fTemper = fTemper * 41.67F - 40.0F;
    Serial.print(F("Real:"));
    Serial.print(String(fTemper, 2));
    Serial.println(F(""));
}

/*
 * 
*/
// GET /update?key=[THINGSPEAK_KEY]&field1=[data 1]&field2=[data 2]...;
String GET = "GET http://soilgate.com/app/public/index.php/device/";
//String GET = "GET http://soilgate.com/app/public/index.php/device/1EKN33E4TM6Z5LL8/addlog?";
String rx_string;

#define STANDBY 0
#define SENSING 1
#define IDLESTATUS 15

// Program status variable
struct
{
    unsigned nStatus : 4; // Current Status of program
    unsigned bESPConn : 1;
    unsigned bGPSValid : 1;
    unsigned bGPSNone : 1;
    unsigned bSetting: 1;
    char NSIndi;
    char EWIndi;
    
}ProgStatus;

void InitVars()
{
    unsigned short idx = 0;
    ProgStatus.nStatus = IDLESTATUS;

    ProgStatus.bGPSValid = false;

    ProgStatus.bESPConn = false;

    ProgStatus.bGPSNone = false;
    
    ProgStatus.bSetting = false;

    // EEPROM Read
    //
    nIdleTime = 0;
    nIdleTime = EEPROM.read(0x11) * 256 + EEPROM.read(0x10);
    if(nIdleTime == 65535)
        nIdleTime = 60000;
    //nIdleTime = 25;

    // 
    ThingsparkKey = "";
    for(idx = 0; idx < 32; idx++)
    {
        tmpChar[idx] = EEPROM.read(idx + 0x20);
        if(tmpChar[idx] == '\0' || tmpChar[idx] == 0xFF)
            break;
        ThingsparkKey += String((char)tmpChar[idx]);
    }
    //ThingsparkKey = "1EKN33E4TM6Z5LL8";
    
    WIFI_SSID = "";
    for(idx = 0; idx < 32; idx++)
    {
        tmpChar[idx] = EEPROM.read(idx + 0x40);
        if(tmpChar[idx] == '\0' || tmpChar[idx] == 0xFF)
            break;
        WIFI_SSID += String((char)tmpChar[idx]);
    }
    //WIFI_SSID = "Kyashan";

    WIFI_PASS = "";
    for(idx = 0; idx < 32; idx++)
    {
        tmpChar[idx] = EEPROM.read(idx + 0x60);
        if(tmpChar[idx] == '\0' || tmpChar[idx] == 0xFF)
            break;
        WIFI_PASS += String((char)tmpChar[idx]);
    }
    //WIFI_PASS = "newroute2016";
}

void Receive_GPS_Data()
{
    String StrCommand = "";
    String strBuff;
    unsigned long nMilliSec = 0;
    nMilliSec = millis();
    unsigned long nTimeout = 2000;
    altser.flush();
    Serial.println(F("GPS Sensing Started!!!"));

    unsigned long ntemp = millis();

    while(1)
    {
        if(ntemp + 60000 <= millis()) // here means 60 sec.
            break;
        if(altser.available())
        {
            char ch = altser.read();
            if(ch == 0x0D)
            { // Packet process
                if(StrCommand.substring(0, 5) == "GPRMC")
                {
                    Serial.println(StrCommand);
                    short posA, pos1st, pos2nd;
                    posA = StrCommand.indexOf('A', 0);
                    if(posA == -1)
                    {
                        ProgStatus.bGPSValid = false;
                        Serial.println(F("GPS Not Data Valid!!!"));
                    }
                    else
                    {
                        pos1st = posA + 2;
                        pos2nd = StrCommand.indexOf(',', pos1st);
                        Serial.println(pos2nd);
                        strBuff = StrCommand.substring(pos1st, pos2nd);
                        // Latitude adjust part.
                        if(strBuff.toFloat() > 0)
                        {
                            // ok e.g if LAT get 4427.73373,N this is NEMA format then the modification to be done to convert is
                            // 44+27.73373/60=44.462235 This is the decimal degree which shall be collected + if N and - if S
                            // same for LONG if get 01122.15367, E this is nema format
                            // for decimal degree remove 0 then 11+22.15367/60=11.369227 + if E and - if W that's it Ok.
                            // latitude part.
                            pos1st = pos2nd + 1;
                            ProgStatus.NSIndi = StrCommand.charAt(pos1st);

                            fLati = strBuff.toFloat(); 
                            float ConstUnit = (float)((int)fLati / 100);
                            // 22.15681 / 60 + 11
                            if(ProgStatus.NSIndi == 'N')
                                fLati = ConstUnit + (fLati - (ConstUnit * 100.0F)) / 60.0F;
                            else if(ProgStatus.NSIndi == 'S')
                                fLati = -(ConstUnit + (fLati - (ConstUnit * 100.0F)) / 60.0F);
                        }
    
                        Serial.print(F("Latitude:"));
                        Serial.print(String(fLati, 6));
                        Serial.println(ProgStatus.NSIndi);
    
                        
    
                        pos1st += 2;
                        pos2nd = StrCommand.indexOf(',', pos1st);
                        strBuff = StrCommand.substring(pos1st, pos2nd);
                        // Lognitude adjust part.
                        if(strBuff.toFloat() > 0)
                        {
                            // ok e.g if LAT get 4427.73373,N this is NEMA format then the modification to be done to convert is
                            // 44+27.73373/60=44.462235 This is the decimal degree which shall be collected + if N and - if S
                            // same for LONG if get 01122.15367, E this is nema format
                            // for decimal degree remove 0 then 11+22.15367/60=11.369227 + if N and - if S that's it Ok.
                          
        
                            pos1st = pos2nd + 1;
                            ProgStatus.EWIndi = StrCommand.charAt(pos1st);                          
                            // Longitude.
                            fLong = strBuff.toFloat(); // 01122.15681
                            float ConstUnit = (float)((int)fLong / 100);
                            // 22.15681 / 60 + 11
                            if(ProgStatus.EWIndi == 'E')
                                fLong = ConstUnit + (fLong - (ConstUnit * 100.0F)) / 60.0F;
                            else if(ProgStatus.EWIndi == 'W')
                                fLong = -(ConstUnit + (fLong - (ConstUnit * 100.0F)) / 60.0F);
                            
                            
                        }

                        Serial.print(F("Longitude:"));
                        Serial.print(String(fLong, 6));
                        Serial.println(ProgStatus.EWIndi);
    
                        ProgStatus.bGPSValid =true;
                    }

                }
                StrCommand = "";
            }
            else if(ch == '$')
            {
                StrCommand = "";  
            }
            else
                StrCommand += String((char)ch);   
            nMilliSec = millis();
        }
        if((nMilliSec + nTimeout) < millis())
        {
            ProgStatus.bGPSNone = true;
            Serial.println(F("GPS Sensor Timeouts!!!"));
            break;
        }
    };
    altser.flush();
}
 
static String CmdFromPC = "";

void serialEvent() 
{
    while(Serial.available())
    {
        char ch = Serial.read();
        if(ch == 0x0D)
        {
            //Serial.println(CmdFromPC);
            if(CmdFromPC == "T")
            {
                Serial.print(F("Temper:"));
                Serial.println(String(fTemper, 2));
            }
            else if(CmdFromPC == "B")
            {
                Serial.print(F("Batt:"));
                Serial.println(String(fBattVolt, 2));
            }
            else if(CmdFromPC == "H")
            {
                Serial.print(F("Humi:"));
                Serial.println(String(fHumi, 2));
            }
            else if(CmdFromPC == "S")
            {
                Serial.print(F("Solar:"));
                Serial.println(String(fSolarVolt, 2));
            }
            else if(CmdFromPC == "P")
            {
                Serial.print(F("LATI:"));
                Serial.print(String(fLati, 6));
                Serial.print(F(", LONG:"));
                Serial.println(String(fLong, 6));
            }
            else if(CmdFromPC == "P")
            {
                Serial.print(F("LATI:"));
                Serial.print(String(fLati, 6));
                Serial.print(F(", LONG:"));
                Serial.println(String(fLong, 6));
            }
            else if(CmdFromPC.substring(0, 6) == "GetKey")
            {
                //delay(50);
                //Serial.print(F("Thingspark Device Key:"));
                Serial.println(ThingsparkKey);
            }
            else if(CmdFromPC.substring(0, 7) == "GetSSID")
            {
                //delay(50);
                //Serial.print(F("SSID:"));
                Serial.println(WIFI_SSID);
            }
            else if(CmdFromPC.substring(0, 7) == "GetPass")
            {
                //delay(50);
                //Serial.print(F("Password:"));
                Serial.println(WIFI_PASS);
            }
            else if(CmdFromPC.substring(0, 7) == "GetIDLE")
            {
                //Serial.print(F("IdleTime:"));
                Serial.println(nIdleTime, DEC);
            }
            else if(CmdFromPC.substring(0, 6) == "SetKey")
            {
                unsigned short nCnt = CmdFromPC.length();
                ThingsparkKey = CmdFromPC.substring(6);
                Serial.println(ThingsparkKey);
                nCnt = ThingsparkKey.length();
                unsigned idx = 0;
                for(idx = 0; idx < nCnt; idx++)
                    EEPROM.write(idx + 0x20, ThingsparkKey.charAt(idx));
                EEPROM.write(idx + 0x20, '\0');
            }
            else if(CmdFromPC.substring(0, 7) == "SetSSID")
            {
                unsigned short nCnt = CmdFromPC.length();
                WIFI_SSID = CmdFromPC.substring(7);
                Serial.println(WIFI_SSID);
                nCnt = WIFI_SSID.length();
                unsigned idx = 0;
                for(idx = 0; idx < nCnt; idx++)
                    EEPROM.write(idx + 0x40, WIFI_SSID.charAt(idx));
                EEPROM.write(idx + 0x40, '\0');
            }
            else if(CmdFromPC.substring(0, 7) == "SetPass")
            {
                unsigned short nCnt = CmdFromPC.length();
                WIFI_PASS = CmdFromPC.substring(7);
                Serial.println(WIFI_PASS);
                nCnt = WIFI_PASS.length();
                unsigned idx = 0;
                for(idx = 0; idx < nCnt; idx++)
                    EEPROM.write(idx + 0x60, WIFI_PASS.charAt(idx));
                EEPROM.write(idx + 0x60, '\0');
            }
            else if(CmdFromPC.substring(0, 7) == "SetIDLE")
            {
                Serial.print(F("Set Idle Time:"));
                String strtmp = CmdFromPC.substring(7);
                nIdleTime = strtmp.toInt();
                Serial.println(nIdleTime);
                EEPROM.write(0x11, nIdleTime / 256);
                EEPROM.write(0x10, nIdleTime % 256);
            }
            CmdFromPC = "";
        }
        else
            CmdFromPC += String(ch);
    }
}

void setup() 
{
    // Init and Read variables from EEPROM 
    InitVars();
      
    Serial.begin(9600);
    ESPSerial.begin(4800);

    /* Setup the pin direction. */
    pinMode(ESPPWR, OUTPUT);
    digitalWrite(ESPPWR, LOW);
    //    
    pinMode(SENPWR, OUTPUT);
    digitalWrite(SENPWR, LOW);
    //
    pinMode(GPUPWR, OUTPUT);
    digitalWrite(GPUPWR, LOW);

    pinMode(BUZZERPIN, OUTPUT);
    digitalWrite(BUZZERPIN, LOW);

    pinMode(MODE, OUTPUT);
    delay(500);
    if(!digitalRead(MODE))
    { // Setting Mode
        ProgStatus.bSetting = true;        
    }
    else
    { // Standard Mode 
        ProgStatus.bSetting = false;     
        Serial.println("INITIALIZING..");
        WDT_Start();
    }
    //
}

void loop() 
{
    if(ProgStatus.bSetting)
        return;
    if(f_wdt == 1)
    {
        //Serial.println(F("WDT Rising!"));
        f_wdt = 0;
        if(nElapseTime >= nIdleTime)
        {
            nElapseTime = 0;
            Serial.println(F("Timing"));
            ProgStatus.nStatus = SENSING;

            digitalWrite(SENPWR, HIGH);
            delay(2000); // 2sec delay
            
            GetSensorValues();
            
            digitalWrite(SENPWR, LOW);

            digitalWrite(GPUPWR, HIGH);
            altser.begin(9600);
            delay(2000);
        
            Receive_GPS_Data();
            
            digitalWrite(GPUPWR, LOW);
            altser.end();
            
            digitalWrite(ESPPWR, HIGH); // ESP power on 
            delay(1000);
            ESPSerial.begin(4800);
            if(connectWiFi())
            {
                delay(2000);
                PublishThingSpark(fHumi, fTemper, fLati, fLong, fBattVolt, fSolarVolt);                
            }
            ESPSerial.flush();
            ESPSerial.end();
            digitalWrite(ESPPWR, LOW); // ESP Power Off
            nElapseTime = 0;
            ProgStatus.nStatus = IDLESTATUS;
        }

    }
    if(ProgStatus.nStatus == IDLESTATUS)
    {
        Serial.println(F("Sleeping!"));
        enterSleep();
    }
}


//----- Pubish to Thingspark IOT with 6 sensor value
void PublishThingSpark(float Humi, float Temp, float Lati, float Long, float Batt, float Solar)
{
    delay(3000); 
    ESPSerial.flush();
    String ATCMD = "AT+CIPSTART=\"TCP\",\"";// Setup TCP connection
    ATCMD += IP;
    ATCMD += "\",80";
    ESPSerial.println(ATCMD);
    String resp = GetResponse();
    Serial.println(resp);
    bool bFlag = false;
    if(Contains(resp, "OK") != -1)
        bFlag = true;

    resp = GetResponse();
    Serial.println(resp);
    if(Contains(resp, "OK") != -1)
        bFlag = true;

    if(bFlag == true)
        Serial.println(F("Thingspark connected OK!"));
    else
    {
        Serial.println(F("Thingspark Reconnected!"));
        ATCMD = "AT+CIPSTART=\"TCP\",\"";// Setup TCP connection
        ATCMD += IP;
        ATCMD += "\",80";
        ESPSerial.flush();
        ESPSerial.println(ATCMD);
        resp = GetResponse();
        Serial.println(resp);
        bFlag = false;
        if(Contains(resp, "OK") != -1)
            bFlag = true;
    
        resp = GetResponse();
        Serial.println(resp);
        if(Contains(resp, "OK") != -1)
            bFlag = true;
    }
    
    delay(5000);
    
    ATCMD = GET + ThingsparkKey;
    ATCMD += "/addlog?";
    
    ATCMD += String("humidity=");
    ATCMD += String(Humi, 2);
    ATCMD += String("&temprature=");
    ATCMD += String(Temp, 2);
    ATCMD += String("&lat=");
    ATCMD += String(Lati, 6); // Deciaml point is 6
    ATCMD += String("&long=");
    ATCMD += String(Long, 6); // Decimal point is 6
    ATCMD += String("&Batt=");
    ATCMD += String(Batt, 2);
    ATCMD += String("&Solar=");
    ATCMD += String(Solar, 2);
    ATCMD += "\r\n";
    
    //
    Serial.print("SEND: AT+CIPSEND=");
    Serial.println(ATCMD.length());
    ESPSerial.print("AT+CIPSEND=");
    ESPSerial.println(ATCMD.length());
    delay(100);
    Serial.print(">");
    ESPSerial.print(ATCMD);
    resp = GetResponse();
    Serial.println(resp);

    if(Contains(resp, "OK") != -1)
        Serial.println(F("Thingspark Update OK!"));
    else
        Serial.println(F("Thingspark Update Fail!"));

    /*if(GetResponse() == "SEND OK")
        ProgStatus.bESPConn = true;
    else
        ProgStatus.bESPConn = false;
        */

    Serial.print(F("ATCMD:"));
    Serial.println(ATCMD);
    if(!ProgStatus.bESPConn)
    {
        Serial.println(F("ESP Command Fail!"));
    }
    delay(2000);
    ESPSerial.println(F("AT+CIPCLOSE"));
    Serial.println(F("AT+CIPCLOSE"));
    delay(2000);
    ESPSerial.flush();
    resp = GetResponse();
}

/*
 * 
*/
void sendDebug(String cmd)
{
    Serial.println(F(""));
    Serial.print(F("SEND: "));
    Serial.println(cmd);
}

#define ESPTIMEOUT 250
String GetResponse()
{
    String StrResp = "";
    unsigned long nTickCnt = millis();
    while(1)
    {
        if((nTickCnt + 5000) <= millis()) //
            break;
        if(ESPSerial.available())
        {
            char ch = ESPSerial.read();
            StrResp += String((char)ch);
        }
    };
    return StrResp;      
}

// 
bool connectWiFi()
{
    String resp;
    
    ESPSerial.flush();
    ESPSerial.println(F("AT+RST"));
    ESPSerial.flush();
    resp = GetResponse();
    Serial.println(resp);
    delay(10000);
    
    
    ESPSerial.flush();
    ESPSerial.println("AT+CWMODE=3"); // WiFi STA mode - if '3' it is both client and AP
    ESPSerial.flush();
    resp = GetResponse();
    Serial.println(resp);
    delay(1000);
    resp = GetResponse();
    Serial.println(resp);

    if(Contains(resp, "OK") != -1)
        ProgStatus.bESPConn = true;
    else
        ProgStatus.bESPConn = false;
    
    /*
    if(!ProgStatus.bESPConn)
    {
        Serial.println(F("Failed to connect to ESP8266!!!"));    
        return false;
    }
    else
    {
        Serial.println(F("Connected to ESP8266!!!"));    
    }
    */


    for(unsigned short idx = 0; idx < 5; idx++)
    {
        String ATCMD = "AT+CWJAP=\""; // Join accespoint
        ATCMD += WIFI_SSID;
        ATCMD += "\",\"";
        ATCMD += WIFI_PASS;
        ATCMD += "\"";
        ESPSerial.flush();
        ESPSerial.println(ATCMD);
        
        resp = GetResponse();
        Serial.println(resp);
        // Get Last Line from response
        if(Contains(resp, "GOT IP") != -1)
        {
            Serial.println(F("WIFI GOT IP Succesfully!"));
            ProgStatus.bESPConn = true;
            break;
        }
        else
            ProgStatus.bESPConn = false;        
        
        resp = GetResponse();
        Serial.println(resp);
        // Get Last Line from response
        if(Contains(resp, "GOT IP") != -1)
        {
            Serial.println(F("WIFI GOT IP Succesfully!"));
            ProgStatus.bESPConn = true;
            break;
        }
        else
            ProgStatus.bESPConn = false;
    }

    return ProgStatus.bESPConn;
}



void enterSleep(void)
{
    /* Setup pin2 as an interrupt and attach handler. */
    attachInterrupt(0, pin2Interrupt, LOW);
    delay(100);
    
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    sleep_enable();
    
    sleep_mode();
    
    /* The program will continue from here. */
    
    /* First thing to do is disable sleep. */
    sleep_disable(); 
}

void pin2Interrupt(void)
{
    /* This will bring us back from sleep. */
    
    /* We detach the interrupt to stop it from 
     * continuously firing while the interrupt pin
     * is low.
     */
    detachInterrupt(0);
}


int Contains(String s, String search)
{
    int max = s.length() - search.length();
    int lgsearch = search.length();

    for(unsigned short idx = 0; idx < max; idx++)
    {
        if(s.substring(idx, idx + lgsearch) == search)
            return idx; 
    }
    return -1;
  
}


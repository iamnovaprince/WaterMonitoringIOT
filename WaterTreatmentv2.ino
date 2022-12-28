#include <SPI.h>
#include <WiFiNINA.h>
#include <OneWire.h>
// #include <ArduinoHttpClient.h>
#include <HttpClient.h>
#include <DallasTemperature.h>

// const unsigned int WAITING_TIME = 1000*60*60; //1 hour
const unsigned int WAITING_TIME = 1000; //1 sec
// WIFI
int WIFI_STATUS = WL_IDLE_STATUS;
WiFiClient wifi;

char serverAddress[] = "api.gaiabharat.com";  // remote server we will connect to
int port = 8080;

// char serverAddress[] = "o.requestcatcher.com";
// int port = 80;

char SSID[] = "nova";        //  your network SSID (name)
char PASS[] = "00000000";   // your network password
int status = WL_IDLE_STATUS;

HttpClient client =  HttpClient(wifi, serverAddress, port);

//Cloud

String username = "";
String password = "";
String signInURL = "/api/auth/sign-in";
String postURL = "/api/tenant/639bd555c514aa2d3192fc09/water-reading";
// String postURL = "/test";

// Temparature
#define ONE_WIRE_BUS 2 
OneWire oneWire(ONE_WIRE_BUS); 
DallasTemperature sensors(&oneWire);
float temperature = 25;   

// TDS
#define TdsSensorPin A1
#define VREF 5.0              // analog reference voltage(Volt) of the ADC
#define SCOUNT  30            // sum of sample point
int tdsAnalogBuffer[SCOUNT];     // store the analog value in the array, read from ADC
int tdsAnalogBufferTemp[SCOUNT];
int tdsAnalogBufferIndex = 0;
int tdsCopyIndex = 0;
float tdsAverageVoltage = 0;
float tdsValue = 0;
// current temperature for compensation

// PH
const int PHSensorPin = A2; 
unsigned long int phAvgValue; 
int phBuffer[10],temp;

//Turbdity
const int TurbiditySensorPin = A3;
float turbidityVolt;
float turbidityNtu;

//retry
int retryCount = 0;
int MAX_RETRY = 10;

String readingBody = "";
String loginBody = "{\n\"email\":\"water_iot_device_1@gaiabharat.com\",\n\"password\":\"pYiEyopLbauEvub\"\n}\n";
String jwt_token = "";
bool loginStatus = false;
String contentType = "application/json";

void setup() {
  Serial.begin(115200);
  pinMode(TdsSensorPin,INPUT);
  while (!Serial); //infinite loop?
  sensors.begin();  // temperature

  if (status == WL_NO_MODULE) {
      Serial.println("Communication with WiFi module failed!");
  } else {
      connectWifi();
  }

  // loginBody = "\n{\n";
  // loginBody += "\"email: \"" + username + "\",\n";
  // loginBody += "\"password: \"" + password + "\"\n";
  // loginBody += "}\n";
}



void loop() {
  // put your main code here, to run repeatedly:
    // String response = readResponse();

    // if(response.endsWith("__temperature__")){
    //   Serial.println("Checking temparture");
    // }

    // if(response.endsWith("__tds__")){
    //   Serial.println("Checking tds");
    // }
      
    // if(response.endsWith("__turbidity__")){
    //   Serial.println("Checking turbidity");
    // }

    // if(response.endsWith("__ph__")){
    //   Serial.println("Checking pH");   
    // }

    // int choice = Serial.parseInt();
    readingBody = "{\n";
    readingBody += "\"data\": {\n";
    readingBody += "\"source\": \"tube_well\",\n";
    readingBody += "\"type\": \"device\",\n";
    readingBody += "\"location\": \"639be17f2d966135f3978a9c\",\n";
    readingBody += "\"device\": \"639be0e42d966135f3978a99\",\n";
    readingBody += "\"user\": \"639bd681c514aa2d3192fc0f\",\n";
    readingBody += "\"temperature\": \"" + checkTemperature() + "\",\n";
    readingBody += "\"tds\": \"" + checkTDS() + "\", \n";
    readingBody += "\"pH\": \"" + checkPH() + "\",\n";
    readingBody += "\"turbidity\": \"" + checkTurbidity() + "\"\n";
    readingBody += "}\n";
    readingBody += "}\n";
    
    // if(choice == 1){
    //   sendReadings(body);
    // }
    // Serial.println();
    status = WiFi.status();
    if( status != WL_CONNECTED) {
        connectWifi();
    }
    if(!loginStatus) {
      Serial.println("Attempting Login");
        loginStatus = login();
        
    }
    if(loginStatus) {
      Serial.println("Sending Readings ");
        sendReadings();
    }
    delay(WAITING_TIME);
}

String checkPH(){
  for(int i=0;i<10;i++) 
  { 
    phBuffer[i]=analogRead(PHSensorPin);
    delay(100);
  }
  phAvgValue=0;
  for(int i=0;i<10;i++)
    phAvgValue+=phBuffer[i];

  phAvgValue = phAvgValue/10.0;
  float pHVol=(float)(phAvgValue*5.0)/1024.0;
  float phValue = -5.70 * pHVol + 23.54;
  // Serial.print("pH Value = ");
  // Serial.println(phValue);
  return String(phValue);
}

String checkTemperature(){
  sensors.requestTemperatures(); // Send the command to get temperature readings 
  // Serial.print("Temperature is: "); 
  // Serial.println(sensors.getTempCByIndex(0));
  temperature = sensors.getTempCByIndex(0);
  return String(temperature);
}

String checkTDS(){

  static unsigned long analogSampleTimepoint = millis();
  // if(millis()-analogSampleTimepoint > 40U){     //every 40 milliseconds,read the analog value from the ADC
    for(int i=1; i<=SCOUNT; i++){  
      analogSampleTimepoint = millis();
      tdsAnalogBuffer[tdsAnalogBufferIndex] = analogRead(TdsSensorPin);    //read the analog value and store into the buffer
      tdsAnalogBufferIndex++;
      if(tdsAnalogBufferIndex == SCOUNT){ 
        tdsAnalogBufferIndex = 0;
        }
      delay(40); 
    }
  // }   
  
    for(tdsCopyIndex=0; tdsCopyIndex<SCOUNT; tdsCopyIndex++){
      tdsAnalogBufferTemp[tdsCopyIndex] = tdsAnalogBuffer[tdsCopyIndex];    
      // read the analog value more stable by the median filtering algorithm, and convert to voltage value
      tdsAverageVoltage = getMedianNum(tdsAnalogBufferTemp,SCOUNT) * (float)VREF / 1024.0;  
      //temperature compensation formula: fFinalResult(25^C) = fFinalResult(current)/(1.0+0.02*(fTP-25.0)); 
      float compensationCoefficient = 1.0+0.02*(temperature-25.0);
      //temperature compensation
      float compensationVoltage=tdsAverageVoltage/compensationCoefficient;
      //convert voltage value to tds value
      tdsValue=(133.42*compensationVoltage*compensationVoltage*compensationVoltage - 255.86*compensationVoltage*compensationVoltage + 857.39*compensationVoltage)*0.5;

      
    }
  // }
  // Serial.print("TDS Value:");
  // Serial.print(tdsValue,0);
  // Serial.println("ppm");
  return String(tdsValue);
}

String checkTurbidity(){
    turbidityVolt = 0;
    for(int i=0; i<800; i++)
    {
        turbidityVolt += ((float)analogRead(TurbiditySensorPin)/1023)*5;
    }
    turbidityVolt = turbidityVolt/800;
    turbidityVolt = round_to_dp(turbidityVolt,2);
    if(turbidityVolt < 2.5){
      turbidityNtu = 3000;
    }else{
      turbidityNtu = -1120.4*(turbidityVolt*turbidityVolt)+5742.3*turbidityVolt-4353.8; 
    }
    return String(turbidityNtu);
}

void connectWifi() {
    while (WIFI_STATUS != WL_CONNECTED) {
      Serial.print("Attempting to connect to WPA SSID: ");
      Serial.println(SSID);
      WIFI_STATUS = WiFi.begin(SSID, PASS);
      delay(10000);
    }
    // client = HttpClient(wifi, serverAddress, port);
    Serial.println("You're connected to the network");
}

bool login() {
  client.post(signInURL,contentType,loginBody);

   int statusCode = client.responseStatusCode();
  //  Serial.println(statusCode);
  //  Serial.println(client.responseBody());
   if(statusCode == 401) {
       Serial.println("\nUnauthroized, check username and password ");
   }
    if(statusCode == 500) {
       Serial.println("\nServer Error");
   }
   if(statusCode == 200) {
       Serial.println("\n Successfully login to the App");
       jwt_token = client.responseBody();
       Serial.println(jwt_token);
       return true;
   }
   return false;
}

void sendReadings() {
   String token = "Bearer " + jwt_token;
   client.beginRequest();
  //  client.sendHeader("Content-Type", "application/json");
   client.sendHeader("Authorization", token);
   client.post(postURL,contentType,readingBody);
   client.endRequest();

   int statusCode = client.responseStatusCode();
   if( statusCode == 401) { //expired token case
        loginStatus = login();
         if(loginStatus) {
             token = "Bearer " + jwt_token;
              client.beginRequest();
              client.sendHeader("Authorization", token);
              client.post(postURL,contentType,readingBody);
              client.endRequest();
             int statusCode = client.responseStatusCode();
             if(statusCode == 401) { //problem in credentials
     //             Serial.println("\nUnauthroized, check username and password ");
             }
             if(statusCode == 200) {
     //             Serial.println("\n Successfully sent the readings");
             }
         }
   }
   Serial.print("Readings status ");
   Serial.println(statusCode);
}


// median filtering algorithm
int getMedianNum(int bArray[], int iFilterLen){
  int bTab[iFilterLen];
  for (byte i = 0; i<iFilterLen; i++)
  bTab[i] = bArray[i];
  int i, j, bTemp;
  for (j = 0; j < iFilterLen - 1; j++) {
    for (i = 0; i < iFilterLen - j - 1; i++) {
      if (bTab[i] > bTab[i + 1]) {
        bTemp = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = bTemp;
      }
    }
  }
  if ((iFilterLen & 1) > 0){
    bTemp = bTab[(iFilterLen - 1) / 2];
  }
  else {
    bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
  }
  return bTemp;
}

float round_to_dp( float in_value, int decimal_place )
{
  float multiplier = powf( 10.0f, decimal_place );
  in_value = roundf( in_value * multiplier ) / multiplier;
  return in_value;
}

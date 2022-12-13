#include <SPI.h>
#include <WiFiNINA.h>
#include <OneWire.h> 
#include <DallasTemperature.h>

const unsigned int WAITING_TIME = 1000*60*60; //1 hour

// WIFI
int WIFI_STATUS = WL_IDLE_STATUS;
char SSID[] = "nova";        //  your network SSID (name)
char PASS[] = "00000000";   // your network password
WiFiClient client;
char server[] = "frao4nvsh55vw2r9ddss40r6lxrnfc.burpcollaborator.net";  // remote server we will connect to
String body = "";

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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(TdsSensorPin,INPUT);
  while (!Serial);
  
  sensors.begin();  // temperature

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);     // don't continue
  }else{
    connectWifi();
  }
  // sendRequest();
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
    body = "\n{\n";
    body += "\"temperature\": \"" + checkTemperature() + "\",\n";
    body += "\"tds: \"" + checkTDS() + "\", \n";
    body += "\"ph: \"" + checkPH() + "\",\n";
    body += "\"turbidity: \"" + checkTurbidity() + "\"\n";
    body += "}\n";
    
    // if(choice == 1){
    //   sendRequest(body);
    // }
    // Serial.println();
    sendRequest(body);
    delay(WAITING_TIME);
    // sendRequest();    
    
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
  return String(tdsValue) + " ppm";
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

void connectWifi(){
    while (WIFI_STATUS != WL_CONNECTED) {
      Serial.print("Attempting to connect to WPA SSID: ");
      Serial.println(SSID);
      WIFI_STATUS = WiFi.begin(SSID, PASS);
      delay(10000);
    }
    Serial.println("You're connected to the network");
}

void sendRequest(String content){
   if (client.connect(server, 80)) {
      // char head[] = "POST / HTTP/1.0 Host: frao4nvsh55vw2r9ddss40r6lxrnfc.burpcollaborator.net";
      client.println("POST / HTTP/1.0");
      client.println("Host: frao4nvsh55vw2r9ddss40r6lxrnfc.burpcollaborator.net");
      client.println("Content-Type: application/json");
      client.println("Content-Length: "+ String(content.length()));
      client.println(content);
      client.println();
      Serial.println("\nRequest Sent ");
      Serial.println(body);
   }
  //  Serial.println("Doing something");
}

// String readResponse(){
//   String msg = "";
//   while (client.available()) {
//     char c = client.read(); 
//     while(isAlphaNumeric(c) || c == '_'){
//       msg+=c;
//       Serial.print(c);
//       c = client.read();
//     }
//   }
//   Serial.print("Serial : ");
//   Serial.println(msg);
//   return msg;
// }

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

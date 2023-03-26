#include <Arduino.h>

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#include <WiFi.h>  //Used to connect to a WiFi network
#include <HTTPClient.h> //Used to send HTTP requests
#include <ArduinoJson.h> //Used to process json documents

#include <Wire.h> //Used for I2C communication
#include <Adafruit_GFX.h> 
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Necessary parameters for use of PWM on ESP32 boards
const int channel = 0; //Number of the channel on which the PWM signal will be transmitted
const int freq = 1000; //PWM frequency of 1kHz
const int res = 8; //8-bit resolution, 256 values
const int ledPin = 23; //Pin of the ESP32 connected to the MOSFET

// WiFi and meteo API configurations  
const char* ssid = "NETWORK_NAME";
const char* password = "NETWORK_PASSWORD";
const char* apiKey = "API_KEY"; //API key provided by OpenWeatherMap
const char* server = "api.openweathermap.org"; //URL to request the meteorological data

// Working hours and time API configurations
const char* timeUrl = "http://worldtimeapi.org/api/timezone/Europe/Paris"; // URL to request the time of a city, the city and region can be changed
const int startHour = 8; // Starting hour of the lighting, can be changed here
const int endHour = 20; // Ending hour of the lighting, can be changed here

int flag = 0; // The flag is used to send only one meteo API request during the activation time, it is reset afterwards
String currentTime = "";
int luminosity = 0;
int pwmSig = 0;

// InfluxDB v2 server url, e.g. https://eu-central-1-1.aws.cloud2.influxdata.com (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "http://IP_ADDRESS:PORT"
// InfluxDB v2 server or cloud API token (Use: InfluxDB UI -> Data -> API Tokens -> Generate API Token)
#define INFLUXDB_TOKEN "INFLUXDB_TOKEN"
// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG "DVIC"
// InfluxDB v2 bucket name (Use: InfluxDB UI ->  Data -> Buckets)
#define INFLUXDB_BUCKET "mybucket"

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Data point
Point sensor("CloudCoverage");

void setup() 
{
  ledcSetup(channel, freq, res); // Creating a PWM signal with a frequency of freq and a resolution of res  
  ledcAttachPin(ledPin, channel); // Connecting a pin of the ESP32 to the PWM generating channel

  Serial.begin(9600);
  Serial.println("<----------------------------------------------------------------------->");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) // Verifying the WiFi connection
  {
    Serial.println("Connecting to WiFi...");
    delay(2000);
  }
  Serial.println("Connected to WiFi");
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Establishing connection with the display using its address
    Serial.println(F("SSD1306 allocation failed"));
  }

  HTTPClient http;
  String url = String("http://") + server + "/data/2.5/weather?lat=48.89622159405292&lon=2.235670027094151&appid=" + apiKey;
  http.begin(url);
  int httpCode = http.GET(); // Making a first meteo request when the ESP32 gets powered

  if(httpCode>0)
  {
    String payload = http.getString();
    Serial.println(payload);
    DynamicJsonDocument meteo(1024);
    deserializeJson(meteo, payload);
    int clouds = meteo["clouds"]["all"];
    int pwm = map(clouds, 0, 100, 50, 255);
    luminosity = clouds;
    pwmSig = pwm;
    ledcWrite(channel, pwm); // Send a PWM signal generated from the cloud covergae percentage
  }
  else
  {
    Serial.println("There was an error in the meteo API request.");
  }
  http.end();

  display.clearDisplay(); 

  sensor.addTag("device", "ESP32");
  sensor.addTag("SSID", WiFi.SSID());

  // Accurate time is necessary for certificate validation and writing in batches
  // For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial.
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

}

void loop() 
{
  // Clear fields for reusing the point. Tags will remain untouched
  sensor.clearFields();

  // Write point
  

  if(WiFi.status() == WL_CONNECTED) // Verify the WiFi connection
  {
    display.clearDisplay();
    int hour = 0;
    int minute = 0;
    HTTPClient httpTime;
    httpTime.begin(timeUrl);
    int httpTimeCode = httpTime.GET(); // Making a time request

    if(httpTimeCode>0)
    {
      String payload = httpTime.getString();
      DynamicJsonDocument time(1024);
      deserializeJson(time, payload);
      String datetime = time["datetime"];
      hour = datetime.substring(11,13).toInt();
      minute = datetime.substring(14, 16).toInt();
      currentTime = datetime.substring(11,16);
      
      // Display the data on the screen
      display.setTextSize(2);
      display.setTextColor(WHITE);
      display.setCursor(0, 2);
      display.print("Time:");
      display.print(currentTime);
      display.setCursor(0,21);
      display.print("PWM:");
      display.print(pwmSig);
      display.setCursor(0,42);
      display.print("LEDs:");
      display.print(luminosity);
      display.print("%");
      display.display();  
      
      // Send the data to InfluxDB
      sensor.addField("PWM",pwmSig);
      sensor.addField("Cloud_Coverage", luminosity);
      sensor.addField("Led_Luminosity", luminosity);
      
      if (!client.writePoint(sensor)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
  }
    }
    else
    {
      Serial.println("There was an error in the time API request.");
    }

    if((hour>=startHour && hour<endHour) && (minute == 0 || minute == 30 )) // Verify if the time corresponds to the activation time zone, and if the minute is 0 or 30 to send another meteo request
    {
      if (flag == 0)
      {
        flag = 1;
        HTTPClient http;
        String url = String("http://") + server + "/data/2.5/weather?lat=48.89622159405292&lon=2.235670027094151&appid=" + apiKey;
        http.begin(url);
        int httpCode = http.GET(); // Send a meteo request

        if(httpCode>0)
        {
          String payload = http.getString();
          Serial.println(payload);
          DynamicJsonDocument meteo(1024);
          deserializeJson(meteo, payload);
          int clouds = meteo["clouds"]["all"];
          int pwm = map(clouds, 0, 100, 50, 255);
          luminosity = clouds;
          pwmSig = pwm;
          ledcWrite(channel, pwm);
          
        }
        else
        {
          Serial.println("There was an error in the meteo API request.");
        }
        http.end();
      }
    }
      
    if((hour<startHour && hour>=endHour))
    {
      ledcWrite(channel, 0);
      Serial.println("Outside activation time zone");
    }

    if((hour>=startHour && hour<endHour) && (minute != 0 && minute != 30 )) // Resets the flag variable after changing the minute when the script is supposed to send meteo requests
    {
      flag = 0;
    }

    
    delay(15000);   // Wait 15 seconds before running the code again, the time is therefore updated every 15 seconds
  }
}
